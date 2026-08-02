#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/registry.h>

struct registry_entry {
    struct telos_extension_descriptor descriptor;
    char *id;
    char *plugin_id;
    char **capabilities;
};

struct telos_registry_generation {
    atomic_uint references;
    uint64_t number;
    size_t count;
    struct registry_entry *entries;
};

struct telos_registry {
    pthread_mutex_t mutex;
    struct telos_registry_generation *current;
    size_t capability_count;
    char **capabilities;
};

struct telos_registry_transaction {
    struct telos_registry *registry;
    char *plugin_id;
    size_t count;
    size_t capacity;
    struct registry_entry *entries;
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
    size_t size;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    size = strlen(value) + 1;
    copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static void entry_clear(struct registry_entry *entry)
{
    for (size_t index = 0; index < entry->descriptor.required_capability_count;
         ++index) {
        free(entry->capabilities[index]);
    }
    free(entry->capabilities);
    free(entry->plugin_id);
    free(entry->id);
    memset(entry, 0, sizeof(*entry));
}

static bool entry_copy(struct registry_entry *target,
                       const struct telos_extension_descriptor *source,
                       const char *plugin_id)
{
    memset(target, 0, sizeof(*target));
    target->id = copy_string(source->id);
    target->plugin_id = copy_string(plugin_id);
    if (target->id == NULL || target->plugin_id == NULL) {
        entry_clear(target);
        return false;
    }

    if (source->required_capability_count > 0) {
        if (source->required_capabilities == NULL ||
            source->required_capability_count >
                SIZE_MAX / sizeof(*target->capabilities)) {
            entry_clear(target);
            return false;
        }
        target->capabilities = calloc(source->required_capability_count,
                                      sizeof(*target->capabilities));
        if (target->capabilities == NULL) {
            entry_clear(target);
            return false;
        }
        for (size_t index = 0; index < source->required_capability_count;
             ++index) {
            target->capabilities[index] =
                copy_string(source->required_capabilities[index]);
            if (target->capabilities[index] == NULL) {
                target->descriptor.required_capability_count = index;
                entry_clear(target);
                return false;
            }
            target->descriptor.required_capability_count = index + 1;
        }
    }

    target->descriptor.id = target->id;
    target->descriptor.plugin_id = target->plugin_id;
    target->descriptor.kind = source->kind;
    target->descriptor.required_capabilities =
        (const char *const *)target->capabilities;
    target->descriptor.required_capability_count =
        source->required_capability_count;
    target->descriptor.implementation = source->implementation;
    return true;
}

static struct telos_registry_generation *generation_create(uint64_t number,
                                                           size_t count)
{
    struct telos_registry_generation *generation;

    if (count > SIZE_MAX / sizeof(*generation->entries)) {
        return NULL;
    }
    generation = calloc(1, sizeof(*generation));
    if (generation == NULL) {
        return NULL;
    }
    if (count > 0) {
        generation->entries = calloc(count, sizeof(*generation->entries));
        if (generation->entries == NULL) {
            free(generation);
            return NULL;
        }
    }
    atomic_init(&generation->references, 1);
    generation->number = number;
    generation->count = count;
    return generation;
}

static void generation_destroy(struct telos_registry_generation *generation)
{
    for (size_t index = 0; index < generation->count; ++index) {
        entry_clear(&generation->entries[index]);
    }
    free(generation->entries);
    free(generation);
}

struct telos_registry *telos_registry_create(const char *const *capabilities,
                                             size_t capability_count,
                                             struct telos_error **error)
{
    struct telos_registry *registry;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    if (capability_count > 0 && capabilities == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Registry capabilities are invalid");
        return NULL;
    }
    if (capability_count > SIZE_MAX / sizeof(*registry->capabilities)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry capability count is too large");
        return NULL;
    }
    for (size_t index = 0; index < capability_count; ++index) {
        if (capabilities[index] == NULL || capabilities[index][0] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Registry capability names must not be empty");
            return NULL;
        }
    }
    registry = calloc(1, sizeof(*registry));
    if (registry == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry allocation failed");
        return NULL;
    }
    result = pthread_mutex_init(&registry->mutex, NULL);
    if (result != 0) {
        free(registry);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Registry mutex initialization failed");
        return NULL;
    }

    if (capability_count > 0) {
        registry->capabilities =
            calloc(capability_count, sizeof(*registry->capabilities));
        if (registry->capabilities == NULL) {
            pthread_mutex_destroy(&registry->mutex);
            free(registry);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Registry capability allocation failed");
            return NULL;
        }
        for (size_t index = 0; index < capability_count; ++index) {
            registry->capabilities[index] = copy_string(capabilities[index]);
            if (registry->capabilities[index] == NULL) {
                registry->capability_count = index;
                telos_registry_destroy(registry);
                set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                          "Registry capability copy failed");
                return NULL;
            }
            registry->capability_count = index + 1;
        }
    }

    registry->current = generation_create(0, 0);
    if (registry->current == NULL) {
        telos_registry_destroy(registry);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry Generation allocation failed");
        return NULL;
    }
    return registry;
}

void telos_registry_destroy(struct telos_registry *registry)
{
    if (registry == NULL) {
        return;
    }
    telos_registry_generation_release(registry->current);
    for (size_t index = 0; index < registry->capability_count; ++index) {
        free(registry->capabilities[index]);
    }
    free(registry->capabilities);
    pthread_mutex_destroy(&registry->mutex);
    free(registry);
}

struct telos_registry_generation *
telos_registry_acquire(struct telos_registry *registry)
{
    struct telos_registry_generation *generation;

    if (registry == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&registry->mutex);
    generation = telos_registry_generation_retain(registry->current);
    pthread_mutex_unlock(&registry->mutex);
    return generation;
}

telos_registry_generation *
telos_registry_generation_retain(const telos_registry_generation *generation)
{
    struct telos_registry_generation *mutable_generation =
        (struct telos_registry_generation *)generation;

    if (mutable_generation != NULL) {
        atomic_fetch_add_explicit(&mutable_generation->references, 1,
                                  memory_order_relaxed);
    }
    return mutable_generation;
}

void
telos_registry_generation_release(const telos_registry_generation *generation)
{
    struct telos_registry_generation *mutable_generation =
        (struct telos_registry_generation *)generation;

    if (mutable_generation != NULL &&
        atomic_fetch_sub_explicit(&mutable_generation->references, 1,
                                  memory_order_acq_rel) == 1) {
        generation_destroy(mutable_generation);
    }
}

uint64_t
telos_registry_generation_number(const telos_registry_generation *generation)
{
    return generation == NULL ? 0 : generation->number;
}

size_t
telos_registry_generation_count(const telos_registry_generation *generation)
{
    return generation == NULL ? 0 : generation->count;
}

const telos_extension_descriptor *
telos_registry_generation_at(const telos_registry_generation *generation,
                             size_t index)
{
    if (generation == NULL || index >= generation->count) {
        return NULL;
    }
    return &generation->entries[index].descriptor;
}

const telos_extension_descriptor *
telos_registry_generation_find(const telos_registry_generation *generation,
                               enum telos_extension_kind kind,
                               const char *id)
{
    if (generation == NULL || id == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < generation->count; ++index) {
        const struct telos_extension_descriptor *descriptor =
            &generation->entries[index].descriptor;

        if (descriptor->kind == kind && strcmp(descriptor->id, id) == 0) {
            return descriptor;
        }
    }
    return NULL;
}

struct telos_registry_transaction *
telos_registry_transaction_begin(struct telos_registry *registry,
                                 const char *plugin_id,
                                 struct telos_error **error)
{
    struct telos_registry_transaction *transaction;

    if (error != NULL) {
        *error = NULL;
    }
    if (registry == NULL || plugin_id == NULL || plugin_id[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Registry transaction Plugin ID is invalid");
        return NULL;
    }
    transaction = calloc(1, sizeof(*transaction));
    if (transaction == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry transaction allocation failed");
        return NULL;
    }
    transaction->plugin_id = copy_string(plugin_id);
    if (transaction->plugin_id == NULL) {
        free(transaction);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry Plugin ID allocation failed");
        return NULL;
    }
    transaction->registry = registry;
    return transaction;
}

bool
telos_registry_transaction_add(telos_registry_transaction *transaction,
                               const telos_extension_descriptor *descriptor,
                               struct telos_error **error)
{
    struct registry_entry *entries;
    size_t capacity;

    if (error != NULL) {
        *error = NULL;
    }
    if (transaction == NULL || descriptor == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Registry descriptor is required");
        return false;
    }
    if (transaction->count == transaction->capacity) {
        capacity = transaction->capacity == 0 ? 4 : transaction->capacity * 2;
        if (capacity < transaction->capacity ||
            capacity > SIZE_MAX / sizeof(*entries)) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Registry transaction capacity overflow");
            return false;
        }
        entries = realloc(transaction->entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Registry transaction allocation failed");
            return false;
        }
        transaction->entries = entries;
        transaction->capacity = capacity;
    }

    if (!entry_copy(&transaction->entries[transaction->count], descriptor,
                    transaction->plugin_id)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry descriptor copy failed");
        return false;
    }
    transaction->count += 1;
    return true;
}

static bool registry_has_capability(const struct telos_registry *registry,
                                    const char *capability)
{
    for (size_t index = 0; index < registry->capability_count; ++index) {
        if (strcmp(registry->capabilities[index], capability) == 0) {
            return true;
        }
    }
    return false;
}

static bool descriptor_valid(const struct telos_registry *registry,
                             const struct registry_entry *entry)
{
    const struct telos_extension_descriptor *descriptor = &entry->descriptor;

    if (descriptor->id == NULL || descriptor->id[0] == '\0' ||
        descriptor->plugin_id == NULL || descriptor->plugin_id[0] == '\0' ||
        descriptor->kind < TELOS_EXTENSION_PROVIDER ||
        descriptor->kind > TELOS_EXTENSION_MODEL_CATALOG) {
        return false;
    }
    for (size_t index = 0; index < descriptor->required_capability_count;
         ++index) {
        if (descriptor->required_capabilities[index] == NULL ||
            !registry_has_capability(
                registry, descriptor->required_capabilities[index])) {
            return false;
        }
    }
    return true;
}

static bool descriptor_conflicts(const struct registry_entry *entries,
                                 size_t count,
                                 const struct registry_entry *candidate)
{
    for (size_t index = 0; index < count; ++index) {
        if (entries[index].descriptor.kind == candidate->descriptor.kind &&
            strcmp(entries[index].descriptor.id, candidate->descriptor.id) ==
                0) {
            return true;
        }
    }
    return false;
}

static size_t survivor_count(const struct telos_registry_generation *generation,
                             const char *plugin_id)
{
    size_t count = 0;

    for (size_t index = 0; index < generation->count; ++index) {
        if (strcmp(generation->entries[index].descriptor.plugin_id,
                   plugin_id) != 0) {
            count += 1;
        }
    }
    return count;
}

bool telos_registry_transaction_commit(telos_registry_transaction *transaction,
                                       struct telos_error **error)
{
    struct telos_registry *registry;
    struct telos_registry_generation *current;
    struct telos_registry_generation *next;
    size_t survivors;
    size_t output = 0;

    if (error != NULL) {
        *error = NULL;
    }
    if (transaction == NULL || transaction->count == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Registry transaction is empty");
        return false;
    }
    registry = transaction->registry;
    pthread_mutex_lock(&registry->mutex);
    current = registry->current;
    survivors = survivor_count(current, transaction->plugin_id);
    if (transaction->count > SIZE_MAX - survivors) {
        pthread_mutex_unlock(&registry->mutex);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry Generation size overflow");
        return false;
    }

    for (size_t index = 0; index < transaction->count; ++index) {
        if (!descriptor_valid(registry, &transaction->entries[index]) ||
            descriptor_conflicts(transaction->entries, index,
                                 &transaction->entries[index])) {
            pthread_mutex_unlock(&registry->mutex);
            set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EINVAL,
                      "Registry transaction descriptor validation failed");
            return false;
        }
        for (size_t prior = 0; prior < current->count; ++prior) {
            if (strcmp(current->entries[prior].descriptor.plugin_id,
                       transaction->plugin_id) != 0 &&
                current->entries[prior].descriptor.kind ==
                    transaction->entries[index].descriptor.kind &&
                strcmp(current->entries[prior].descriptor.id,
                       transaction->entries[index].descriptor.id) == 0) {
                pthread_mutex_unlock(&registry->mutex);
                set_error(error, TELOS_ERROR_DOMAIN_STATE, EEXIST,
                          "Registry extension ID already exists");
                return false;
            }
        }
    }

    next =
        generation_create(current->number + 1, survivors + transaction->count);
    if (next == NULL) {
        pthread_mutex_unlock(&registry->mutex);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Registry Generation allocation failed");
        return false;
    }
    for (size_t index = 0; index < current->count; ++index) {
        if (strcmp(current->entries[index].descriptor.plugin_id,
                   transaction->plugin_id) != 0 &&
            !entry_copy(&next->entries[output++],
                        &current->entries[index].descriptor,
                        current->entries[index].descriptor.plugin_id)) {
            goto copy_failure;
        }
    }
    for (size_t index = 0; index < transaction->count; ++index) {
        if (!entry_copy(&next->entries[output++],
                        &transaction->entries[index].descriptor,
                        transaction->plugin_id)) {
            goto copy_failure;
        }
    }

    registry->current = next;
    pthread_mutex_unlock(&registry->mutex);
    telos_registry_generation_release(current);
    telos_registry_transaction_abort(transaction);
    return true;

copy_failure:
    next->count = output;
    telos_registry_generation_release(next);
    pthread_mutex_unlock(&registry->mutex);
    set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
              "Registry Generation copy failed");
    return false;
}

void telos_registry_transaction_abort(telos_registry_transaction *transaction)
{
    if (transaction == NULL) {
        return;
    }
    for (size_t index = 0; index < transaction->count; ++index) {
        entry_clear(&transaction->entries[index]);
    }
    free(transaction->entries);
    free(transaction->plugin_id);
    free(transaction);
}
