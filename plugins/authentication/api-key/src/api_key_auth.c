#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/plugins/api_key_auth.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define API_KEY_AUTH_PATH_SIZE 4096U
#define API_KEY_AUTH_PROVIDER_SIZE 32U
#define API_KEY_AUTH_TARGET_SIZE 64U
#define API_KEY_AUTH_ENVIRONMENT_SIZE 64U
#define API_KEY_AUTH_CACHE_SIZE 64U
#define API_KEY_AUTH_SECRET_SIZE (16U * 1024U)

struct api_key_profile {
    const char *provider;
    const char *target;
    const char *environment;
    const char *cache_name;
};

struct telos_authentication {
    char state_directory[API_KEY_AUTH_PATH_SIZE];
    const struct api_key_profile *profile;
    char key[API_KEY_AUTH_SECRET_SIZE];
    enum telos_authentication_state state;
};

static const struct api_key_profile deepseek_profile = {
    .provider = "deepseek",
    .target = "provider.deepseek",
    .environment = "DEEPSEEK_API_KEY",
    .cache_name = "deepseek-api-key",
};

static const struct api_key_profile zai_profile = {
    .provider = "zai",
    .target = "provider.zai",
    .environment = "ZAI_API_KEY",
    .cache_name = "zai-api-key",
};

static const struct api_key_profile anthropic_profile = {
    .provider = "anthropic",
    .target = "provider.anthropic",
    .environment = "ANTHROPIC_API_KEY",
    .cache_name = "anthropic-api-key",
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static void secure_wipe(void *data, size_t size)
{
    /* Volatile keeps secret erasure observable to the compiler. */
    volatile unsigned char *cursor = data;

    while (size > 0) {
        *cursor++ = 0;
        --size;
    }
}

static bool copy_text(char *target, size_t capacity, const char *source)
{
    size_t size;

    if (target == NULL || capacity == 0 || source == NULL) {
        return false;
    }
    size = strlen(source);
    if (size >= capacity) {
        return false;
    }
    memcpy(target, source, size + 1);
    return true;
}

static bool cancellation_requested(const struct telos_cancel *cancel,
                                   struct telos_error **error)
{
    if (cancel != NULL && telos_cancel_requested(cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "API-key authentication was cancelled");
        return true;
    }
    return false;
}

static bool state_directory_private(const char *path, bool create,
                                    struct telos_error **error)
{
    struct stat status;

    if (stat(path, &status) != 0) {
        if (!create || errno != ENOENT || mkdir(path, 0700) != 0) {
            if (create || errno != ENOENT) {
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "API-key authentication state directory is "
                          "unavailable");
                return false;
            }
            return true;
        }
        return true;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != getuid() ||
        (status.st_mode & 0077) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "API-key authentication state directory is not private");
        return false;
    }
    return true;
}

static bool cache_path(const struct telos_authentication *authentication,
                       char path[API_KEY_AUTH_PATH_SIZE],
                       struct telos_error **error)
{
    if (snprintf(path, API_KEY_AUTH_PATH_SIZE, "%s/%s.key",
                 authentication->state_directory,
                 authentication->profile->cache_name) >=
        (int)API_KEY_AUTH_PATH_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "API-key authentication state path is too long");
        return false;
    }
    return true;
}

static bool write_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(descriptor, data, size);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        data += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

static bool persist_key(struct telos_authentication *authentication,
                        struct telos_error **error)
{
    char path[API_KEY_AUTH_PATH_SIZE];
    int descriptor;
    size_t size = strlen(authentication->key);
    bool result;

    if (!state_directory_private(authentication->state_directory, true,
                                 error) ||
        !cache_path(authentication, path, error)) {
        return false;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "API-key authentication state could not be opened");
        return false;
    }
    result = fchmod(descriptor, 0600) == 0 &&
             write_all(descriptor, authentication->key, size) &&
             write_all(descriptor, "\n", 1);
    if (!result) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "API-key authentication state could not be written");
        return false;
    }
    if (close(descriptor) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "API-key authentication state could not be closed");
        return false;
    }
    return true;
}

static bool load_key(struct telos_authentication *authentication,
                     struct telos_error **error)
{
    char path[API_KEY_AUTH_PATH_SIZE];
    char buffer[API_KEY_AUTH_SECRET_SIZE];
    struct stat status;
    ssize_t received;
    int descriptor;

    if (!cache_path(authentication, path, error)) {
        return false;
    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            return true;
        }
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "API-key authentication state could not be opened");
        return false;
    }
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != getuid() || (status.st_mode & 0077) != 0) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "API-key authentication state file is not private");
        return false;
    }
    received = read(descriptor, buffer, sizeof(buffer) - 1);
    if (received < 0) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "API-key authentication state could not be read");
        return false;
    }
    if (close(descriptor) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "API-key authentication state could not be closed");
        return false;
    }
    if (received == (ssize_t)(sizeof(buffer) - 1)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EFBIG,
                  "API-key authentication state is too large");
        secure_wipe(buffer, sizeof(buffer));
        return false;
    }
    buffer[received] = '\0';
    if (received > 0 && buffer[received - 1] == '\n') {
        buffer[received - 1] = '\0';
    }
    if (buffer[0] == '\0' || !copy_text(authentication->key,
                                        sizeof(authentication->key), buffer)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "API-key authentication state is invalid");
        secure_wipe(buffer, sizeof(buffer));
        return false;
    }
    authentication->state = TELOS_AUTHENTICATION_SIGNED_IN;
    secure_wipe(buffer, sizeof(buffer));
    return true;
}

static struct telos_authentication *
create_authentication(const struct telos_authentication_config *config,
                      const struct api_key_profile *profile,
                      struct telos_error **error)
{
    struct telos_authentication *authentication;

    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || config->state_directory == NULL ||
        config->state_directory[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "API-key authentication state directory is required");
        return NULL;
    }
    if (!state_directory_private(config->state_directory, false, error)) {
        return NULL;
    }
    authentication = calloc(1, sizeof(*authentication));
    if (authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "API-key authentication allocation failed");
        return NULL;
    }
    if (!copy_text(authentication->state_directory,
                   sizeof(authentication->state_directory),
                   config->state_directory)) {
        free(authentication);
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "API-key authentication state directory is too long");
        return NULL;
    }
    authentication->profile = profile;
    authentication->state = TELOS_AUTHENTICATION_SIGNED_OUT;
    if (!load_key(authentication, error)) {
        secure_wipe(authentication->key, sizeof(authentication->key));
        free(authentication);
        return NULL;
    }
    return authentication;
}

static void destroy_authentication(telos_authentication *authentication)
{
    if (authentication == NULL) {
        return;
    }
    secure_wipe(authentication->key, sizeof(authentication->key));
    free(authentication);
}

static bool emit_completed(telos_authentication_event_fn emit,
                           void *emit_context,
                           struct telos_error **error)
{
    const struct telos_authentication_event event = {
        .kind = TELOS_AUTHENTICATION_COMPLETED,
    };

    if (emit == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "API-key authentication event callback is required");
        return false;
    }
    return emit(&event, emit_context, error);
}

static bool login_authentication(telos_authentication *authentication,
                                 const struct telos_cancel *cancel,
                                 telos_authentication_event_fn emit,
                                 void *emit_context,
                                 struct telos_error **error)
{
    const char *value;

    if (cancellation_requested(cancel, error)) {
        return false;
    }
    value = getenv(authentication->profile->environment);
    if ((value == NULL || value[0] == '\0') &&
        strcmp(authentication->profile->provider, "zai") == 0) {
        value = getenv("Z_AI_API_KEY");
    }
    if (value == NULL || value[0] == '\0') {
        if (authentication->state == TELOS_AUTHENTICATION_SIGNED_IN) {
            return emit_completed(emit, emit_context, error);
        }
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Set the Provider API-key environment variable before "
                  "login");
        return false;
    }
    if (!copy_text(authentication->key, sizeof(authentication->key), value)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Provider API-key is too long");
        return false;
    }
    if (!persist_key(authentication, error)) {
        secure_wipe(authentication->key, sizeof(authentication->key));
        return false;
    }
    authentication->state = TELOS_AUTHENTICATION_SIGNED_IN;
    return emit_completed(emit, emit_context, error);
}

static bool logout_authentication(telos_authentication *authentication,
                                  struct telos_error **error)
{
    char path[API_KEY_AUTH_PATH_SIZE];

    if (!cache_path(authentication, path, error)) {
        return false;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "API-key authentication state could not be removed");
        return false;
    }
    secure_wipe(authentication->key, sizeof(authentication->key));
    authentication->state = TELOS_AUTHENTICATION_SIGNED_OUT;
    return true;
}

static bool status_authentication(const telos_authentication *authentication,
                                  telos_authentication_status *status,
                                  struct telos_error **error)
{
    if (status == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "API-key authentication status is required");
        return false;
    }
    *status = (telos_authentication_status){
        .state = authentication == NULL
                     ? TELOS_AUTHENTICATION_SIGNED_OUT
                     : authentication->state,
        .provider = authentication == NULL || authentication->profile == NULL
                        ? NULL
                        : authentication->profile->provider,
        .account_id = authentication == NULL || authentication->profile == NULL
                          ? NULL
                          : authentication->profile->provider,
    };
    return true;
}

static char *resolve_authentication(telos_authentication *authentication,
                                    const char *target,
                                    struct telos_error **error)
{
    char *result;
    size_t size;

    if (authentication == NULL || target == NULL ||
        strcmp(target, authentication->profile->target) != 0 ||
        authentication->state != TELOS_AUTHENTICATION_SIGNED_IN) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Provider API-key is not available");
        return NULL;
    }
    size = strlen(authentication->key) + 1;
    result = malloc(size);
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Provider API-key allocation failed");
        return NULL;
    }
    memcpy(result, authentication->key, size);
    return result;
}

static telos_authentication *create_deepseek(
    const struct telos_authentication_config *config,
    struct telos_error **error)
{
    return create_authentication(config, &deepseek_profile, error);
}

static telos_authentication *create_zai(
    const struct telos_authentication_config *config,
    struct telos_error **error)
{
    return create_authentication(config, &zai_profile, error);
}

static telos_authentication *create_anthropic(
    const struct telos_authentication_config *config,
    struct telos_error **error)
{
    return create_authentication(config, &anthropic_profile, error);
}

static const struct telos_authentication_definition_v1 deepseek_definition = {
    .struct_size = sizeof(deepseek_definition),
    .id = "dev.zevorn.deepseek-api-key-auth",
    .create = create_deepseek,
    .destroy = destroy_authentication,
    .login = login_authentication,
    .logout = logout_authentication,
    .status = status_authentication,
    .resolve = resolve_authentication,
};

static const struct telos_authentication_definition_v1 zai_definition = {
    .struct_size = sizeof(zai_definition),
    .id = "dev.zevorn.zai-api-key-auth",
    .create = create_zai,
    .destroy = destroy_authentication,
    .login = login_authentication,
    .logout = logout_authentication,
    .status = status_authentication,
    .resolve = resolve_authentication,
};

static const struct telos_authentication_definition_v1 anthropic_definition = {
    .struct_size = sizeof(anthropic_definition),
    .id = "dev.zevorn.anthropic-api-key-auth",
    .create = create_anthropic,
    .destroy = destroy_authentication,
    .login = login_authentication,
    .logout = logout_authentication,
    .status = status_authentication,
    .resolve = resolve_authentication,
};

const struct telos_authentication_definition_v1 *
telos_deepseek_api_key_authentication_definition(void)
{
    return &deepseek_definition;
}

const struct telos_authentication_definition_v1 *
telos_zai_api_key_authentication_definition(void)
{
    return &zai_definition;
}

const struct telos_authentication_definition_v1 *
telos_anthropic_api_key_authentication_definition(void)
{
    return &anthropic_definition;
}
