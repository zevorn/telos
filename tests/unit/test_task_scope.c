#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/task_scope.h>

#define CONCURRENT_THREADS 8

static int disposed_order[8];
static size_t disposed_count;

static void record_dispose(void *context)
{
    disposed_order[disposed_count++] = (int)(intptr_t)context;
}

static void *concurrent_open_child(void *arg)
{
    struct telos_task_scope *parent = arg;
    struct telos_task_scope *child;
    struct telos_error *error = NULL;

    child = telos_task_scope_open(parent, "c", &error);
    assert(child != NULL);
    assert(telos_task_scope_register(child, "item", record_dispose,
                                     (void *)(intptr_t)7, &error));
    return NULL;
}

static void test_basic_dispose(void)
{
    struct telos_task_scope *scope;
    struct telos_error *error = NULL;

    disposed_count = 0;
    scope = telos_task_scope_open(NULL, "basic", &error);
    assert(scope != NULL);
    assert(error == NULL);
    assert(telos_task_scope_register(scope, "a", record_dispose,
                                     (void *)(intptr_t)1, &error));
    assert(telos_task_scope_register(scope, "b", record_dispose,
                                     (void *)(intptr_t)2, &error));
    assert(telos_task_scope_register(scope, "c", record_dispose,
                                     (void *)(intptr_t)3, &error));
    assert(error == NULL);

    telos_task_scope_dispose(scope);
    /* LIFO: c, b, a */
    assert(disposed_count == 3);
    assert(disposed_order[0] == 3);
    assert(disposed_order[1] == 2);
    assert(disposed_order[2] == 1);
}

static void test_commit_keeps_effects(void)
{
    struct telos_task_scope *scope;
    struct telos_error *error = NULL;

    disposed_count = 0;
    scope = telos_task_scope_open(NULL, "commit", &error);
    assert(scope != NULL);
    assert(telos_task_scope_register(scope, "a", record_dispose,
                                     (void *)(intptr_t)1, &error));
    assert(telos_task_scope_commit(scope, &error));
    assert(error == NULL);
    /* Commit must not run dispose callbacks. */
    assert(disposed_count == 0);
}

static void test_dispose_calls_callback_once(void)
{
    struct telos_task_scope *scope;
    struct telos_error *error = NULL;

    disposed_count = 0;
    scope = telos_task_scope_open(NULL, "idem", &error);
    assert(scope != NULL);
    assert(telos_task_scope_register(scope, "a", record_dispose,
                                     (void *)(intptr_t)1, &error));
    telos_task_scope_dispose(scope);
    /* Each dispose callback fires exactly once. */
    assert(disposed_count == 1);
}

static void test_nested_cascade(void)
{
    struct telos_task_scope *parent;
    struct telos_task_scope *child;
    struct telos_error *error = NULL;

    disposed_count = 0;
    parent = telos_task_scope_open(NULL, "parent", &error);
    assert(parent != NULL);
    child = telos_task_scope_open(parent, "child", &error);
    assert(child != NULL);
    assert(telos_task_scope_register(parent, "p", record_dispose,
                                     (void *)(intptr_t)10, &error));
    assert(telos_task_scope_register(child, "c", record_dispose,
                                     (void *)(intptr_t)20, &error));

    telos_task_scope_dispose(parent);
    /* Child disposed before parent's own items. */
    assert(disposed_count == 2);
    assert(disposed_order[0] == 20);
    assert(disposed_order[1] == 10);
}

static void test_nested_commit_detaches(void)
{
    struct telos_task_scope *parent;
    struct telos_task_scope *child;
    struct telos_error *error = NULL;

    disposed_count = 0;
    parent = telos_task_scope_open(NULL, "parent", &error);
    assert(parent != NULL);
    child = telos_task_scope_open(parent, "child", &error);
    assert(child != NULL);
    assert(telos_task_scope_register(child, "c", record_dispose,
                                     (void *)(intptr_t)1, &error));
    assert(telos_task_scope_commit(child, &error));
    assert(error == NULL);
    assert(disposed_count == 0);

    /* Parent dispose must not see the committed child. */
    telos_task_scope_dispose(parent);
    assert(disposed_count == 0);
}

static void test_dispose_middle_child_keeps_list(void)
{
    struct telos_task_scope *parent;
    struct telos_task_scope *child_b;
    struct telos_task_scope *child_c;
    struct telos_error *error = NULL;

    disposed_count = 0;
    parent = telos_task_scope_open(NULL, "parent", &error);
    assert(parent != NULL);
    assert(telos_task_scope_open(parent, "a", &error) != NULL);
    child_b = telos_task_scope_open(parent, "b", &error);
    assert(child_b != NULL);
    child_c = telos_task_scope_open(parent, "c", &error);
    assert(child_c != NULL);
    assert(telos_task_scope_register(child_c, "c", record_dispose,
                                     (void *)(intptr_t)1, &error));

    /* Dispose the middle child: the tail pointer must be repaired. */
    telos_task_scope_dispose(child_b);
    assert(disposed_count == 0);

    /* A new child must still append to the surviving tail. */
    assert(telos_task_scope_open(parent, "d", &error) != NULL);
    assert(telos_task_scope_register(child_c, "c2", record_dispose,
                                     (void *)(intptr_t)2, &error));

    telos_task_scope_dispose(parent);
    assert(disposed_count == 2);
    assert(disposed_order[0] == 2);
    assert(disposed_order[1] == 1);
}

static void test_concurrent_attach_and_cascade(void)
{
    struct telos_task_scope *parent;
    pthread_t threads[CONCURRENT_THREADS];
    struct telos_error *error = NULL;
    size_t index;

    disposed_count = 0;
    parent = telos_task_scope_open(NULL, "parent", &error);
    assert(parent != NULL);
    for (index = 0; index < CONCURRENT_THREADS; ++index) {
        assert(pthread_create(&threads[index], NULL, concurrent_open_child,
                              parent) == 0);
    }
    for (index = 0; index < CONCURRENT_THREADS; ++index) {
        assert(pthread_join(threads[index], NULL) == 0);
    }
    /* All children cascade on parent dispose. */
    telos_task_scope_dispose(parent);
    assert(disposed_count == CONCURRENT_THREADS);
}

static void test_invalid_arguments(void)
{
    struct telos_error *error = NULL;

    assert(telos_task_scope_open(NULL, "", &error) == NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_task_scope_open(NULL, NULL, &error) == NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(!telos_task_scope_register(NULL, "x", record_dispose, NULL,
                                      &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(!telos_task_scope_commit(NULL, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_task_scope_state(NULL) == 0);
}

int main(void)
{
    test_basic_dispose();
    test_commit_keeps_effects();
    test_dispose_calls_callback_once();
    test_nested_cascade();
    test_nested_commit_detaches();
    test_dispose_middle_child_keeps_list();
    test_concurrent_attach_and_cascade();
    test_invalid_arguments();
    printf("task_scope: all tests passed\n");
    return 0;
}
