#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <telos/task_scope.h>

struct telos_task_scope_item {
    char *name;
    telos_task_scope_dispose_fn dispose;
    void *context;
};

struct telos_task_scope {
    pthread_mutex_t mutex;
    enum telos_task_scope_state state;
    char *name;
    struct telos_task_scope *parent;
    struct telos_task_scope *next_sibling;
    struct telos_task_scope *first_child;
    struct telos_task_scope *last_child;
    struct telos_task_scope_item *items;
    size_t count;
    size_t capacity;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static void clear_items(struct telos_task_scope *scope, bool run_dispose)
{
    /*
     * LIFO: dispose from the newest item to the oldest.  Each callback
     * runs exactly once because dispose() transitions to DISPOSED
     * before unwinding, and commit() never calls dispose.
     */
    for (size_t index = scope->count; index > 0; --index) {
        struct telos_task_scope_item *item = &scope->items[index - 1];

        if (run_dispose && item->dispose != NULL) {
            item->dispose(item->context);
        }
        free(item->name);
    }
    free(scope->items);
    scope->items = NULL;
    scope->count = 0;
    scope->capacity = 0;
}

static void detach_from_parent(struct telos_task_scope *scope)
{
    struct telos_task_scope *parent = scope->parent;
    struct telos_task_scope *cursor;

    if (parent == NULL) {
        return;
    }
    /*
     * Caller holds scope->mutex; lock order is always child -> parent,
     * so this cannot deadlock with open() (which holds only parent).
     */
    pthread_mutex_lock(&parent->mutex);
    if (parent->first_child == scope) {
        parent->first_child = scope->next_sibling;
    } else {
        cursor = parent->first_child;
        while (cursor != NULL && cursor->next_sibling != scope) {
            cursor = cursor->next_sibling;
        }
        if (cursor != NULL) {
            cursor->next_sibling = scope->next_sibling;
        }
    }
    if (parent->last_child == scope) {
        /* Recompute the tail: a predecessor may still be linked. */
        if (parent->first_child == NULL) {
            parent->last_child = NULL;
        } else {
            cursor = parent->first_child;
            while (cursor->next_sibling != NULL) {
                cursor = cursor->next_sibling;
            }
            parent->last_child = cursor;
        }
    }
    scope->parent = NULL;
    scope->next_sibling = NULL;
    pthread_mutex_unlock(&parent->mutex);
}

static void attach_to_parent(struct telos_task_scope *scope,
                             struct telos_task_scope *parent)
{
    scope->parent = parent;
    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = scope;
    } else {
        parent->first_child = scope;
    }
    parent->last_child = scope;
}

struct telos_task_scope *
telos_task_scope_open(struct telos_task_scope *parent,
                      const char *name,
                      struct telos_error **error)
{
    struct telos_task_scope *scope;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    if (name == NULL || name[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Task Scope name is required");
        return NULL;
    }
    scope = calloc(1, sizeof(*scope));
    if (scope == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Task Scope allocation failed");
        return NULL;
    }
    scope->name = copy_string(name);
    if (scope->name == NULL) {
        free(scope);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Task Scope name allocation failed");
        return NULL;
    }
    result = pthread_mutex_init(&scope->mutex, NULL);
    if (result != 0) {
        free(scope->name);
        free(scope);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Task Scope mutex initialization failed");
        return NULL;
    }
    scope->state = TELOS_TASK_SCOPE_OPEN;
    if (parent != NULL) {
        pthread_mutex_lock(&parent->mutex);
        if (parent->state == TELOS_TASK_SCOPE_DISPOSED) {
            pthread_mutex_unlock(&parent->mutex);
            free(scope->name);
            pthread_mutex_destroy(&scope->mutex);
            free(scope);
            set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                      "Parent Task Scope is disposed");
            return NULL;
        }
        attach_to_parent(scope, parent);
        pthread_mutex_unlock(&parent->mutex);
    }
    return scope;
}

bool telos_task_scope_register(struct telos_task_scope *scope,
                               const char *name,
                               telos_task_scope_dispose_fn dispose,
                               void *context,
                               struct telos_error **error)
{
    struct telos_task_scope_item *items;
    size_t capacity;
    bool ok = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (scope == NULL || name == NULL || name[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Task Scope register arguments are invalid");
        return false;
    }
    pthread_mutex_lock(&scope->mutex);
    if (scope->state != TELOS_TASK_SCOPE_OPEN) {
        pthread_mutex_unlock(&scope->mutex);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                  "Task Scope is not open for registration");
        return false;
    }
    if (scope->count == scope->capacity) {
        capacity = scope->capacity == 0 ? 4 : scope->capacity * 2;
        if (capacity < scope->capacity ||
            capacity > SIZE_MAX / sizeof(*scope->items)) {
            pthread_mutex_unlock(&scope->mutex);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Task Scope capacity overflow");
            return false;
        }
        items = realloc(scope->items, capacity * sizeof(*scope->items));
        if (items == NULL) {
            pthread_mutex_unlock(&scope->mutex);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Task Scope item allocation failed");
            return false;
        }
        scope->items = items;
        scope->capacity = capacity;
    }
    scope->items[scope->count].name = copy_string(name);
    if (scope->items[scope->count].name == NULL) {
        pthread_mutex_unlock(&scope->mutex);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Task Scope item name allocation failed");
        return false;
    }
    scope->items[scope->count].dispose = dispose;
    scope->items[scope->count].context = context;
    scope->count += 1;
    ok = true;
    pthread_mutex_unlock(&scope->mutex);
    return ok;
}

bool telos_task_scope_commit(struct telos_task_scope *scope,
                             struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (scope == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Task Scope is required");
        return false;
    }
    pthread_mutex_lock(&scope->mutex);
    if (scope->state != TELOS_TASK_SCOPE_OPEN) {
        pthread_mutex_unlock(&scope->mutex);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                  "Task Scope is not open");
        return false;
    }
    scope->state = TELOS_TASK_SCOPE_COMMITTED;
    pthread_mutex_unlock(&scope->mutex);

    /* Commit keeps the effects; only the bookkeeping is released. */
    pthread_mutex_lock(&scope->mutex);
    detach_from_parent(scope);
    pthread_mutex_unlock(&scope->mutex);
    clear_items(scope, false);
    free(scope->name);
    pthread_mutex_destroy(&scope->mutex);
    free(scope);
    return true;
}

void telos_task_scope_dispose(struct telos_task_scope *scope)
{
    struct telos_task_scope *child;

    if (scope == NULL) {
        return;
    }
    pthread_mutex_lock(&scope->mutex);
    if (scope->state == TELOS_TASK_SCOPE_DISPOSED) {
        pthread_mutex_unlock(&scope->mutex);
        return;
    }
    scope->state = TELOS_TASK_SCOPE_DISPOSED;
    child = scope->first_child;
    scope->first_child = NULL;
    scope->last_child = NULL;
    pthread_mutex_unlock(&scope->mutex);

    /* Cascade: dispose all children first, then our own items. */
    while (child != NULL) {
        struct telos_task_scope *next = child->next_sibling;

        telos_task_scope_dispose(child);
        child = next;
    }
    clear_items(scope, true);

    pthread_mutex_lock(&scope->mutex);
    detach_from_parent(scope);
    pthread_mutex_unlock(&scope->mutex);
    free(scope->name);
    pthread_mutex_destroy(&scope->mutex);
    free(scope);
}

void telos_task_scope_destroy(struct telos_task_scope *scope)
{
    if (scope == NULL) {
        return;
    }
    pthread_mutex_lock(&scope->mutex);
    detach_from_parent(scope);
    pthread_mutex_unlock(&scope->mutex);
    clear_items(scope, false);
    free(scope->name);
    pthread_mutex_destroy(&scope->mutex);
    free(scope);
}

enum telos_task_scope_state
telos_task_scope_state(const struct telos_task_scope *scope)
{
    enum telos_task_scope_state state;

    if (scope == NULL) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t *)&scope->mutex);
    state = scope->state;
    pthread_mutex_unlock((pthread_mutex_t *)&scope->mutex);
    return state;
}
