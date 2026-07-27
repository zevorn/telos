#define _XOPEN_SOURCE 700

#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sha256.h"

struct sha256_context {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[64];
    size_t block_size;
};

static const uint32_t constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491),
    UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
    UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
    UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
    UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
    UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
    UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
    UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624),
    UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
    UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
    UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
    UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static void transform(
    struct sha256_context *context,
    const unsigned char block[64]
)
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    for (size_t index = 0; index < 16; ++index) {
        words[index] = ((uint32_t)block[index * 4] << 24)
            | ((uint32_t)block[index * 4 + 1] << 16)
            | ((uint32_t)block[index * 4 + 2] << 8)
            | (uint32_t)block[index * 4 + 3];
    }
    for (size_t index = 16; index < 64; ++index) {
        uint32_t s0 = rotate_right(words[index - 15], 7)
            ^ rotate_right(words[index - 15], 18)
            ^ (words[index - 15] >> 3);
        uint32_t s1 = rotate_right(words[index - 2], 17)
            ^ rotate_right(words[index - 2], 19)
            ^ (words[index - 2] >> 10);

        words[index] = words[index - 16] + s0
            + words[index - 7] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (size_t index = 0; index < 64; ++index) {
        uint32_t sum1 = rotate_right(e, 6)
            ^ rotate_right(e, 11)
            ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choice
            + constants[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2)
            ^ rotate_right(a, 13)
            ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void initialize(struct sha256_context *context)
{
    *context = (struct sha256_context) {
        .state = {
            UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
            UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
            UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
            UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
        },
    };
}

static void update(
    struct sha256_context *context,
    const unsigned char *data,
    size_t size
)
{
    context->bit_count += (uint64_t)size * UINT64_C(8);
    while (size > 0) {
        size_t available = sizeof(context->block) - context->block_size;
        size_t amount = size < available ? size : available;

        memcpy(context->block + context->block_size, data, amount);
        context->block_size += amount;
        data += amount;
        size -= amount;
        if (context->block_size == sizeof(context->block)) {
            transform(context, context->block);
            context->block_size = 0;
        }
    }
}

static void finish(
    struct sha256_context *context,
    unsigned char digest[32]
)
{
    uint64_t bit_count = context->bit_count;
    unsigned char padding[72] = {0x80};
    size_t padding_size = context->block_size < 56
        ? 56 - context->block_size
        : 120 - context->block_size;

    update(context, padding, padding_size);
    for (size_t index = 0; index < 8; ++index) {
        context->block[63 - index] = (unsigned char)(
            bit_count >> (index * 8)
        );
    }
    transform(context, context->block);
    for (size_t index = 0; index < 8; ++index) {
        digest[index * 4] = (unsigned char)(context->state[index] >> 24);
        digest[index * 4 + 1] = (unsigned char)(
            context->state[index] >> 16
        );
        digest[index * 4 + 2] = (unsigned char)(
            context->state[index] >> 8
        );
        digest[index * 4 + 3] = (unsigned char)context->state[index];
    }
}

bool telos_sha256_file(const char *path, char output[65])
{
    static const char digits[] = "0123456789abcdef";
    struct sha256_context context;
    unsigned char buffer[8192];
    unsigned char digest[32];
    FILE *stream;
    size_t received;

    if (path == NULL || output == NULL) {
        return false;
    }
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    initialize(&context);
    while ((received = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        update(&context, buffer, received);
    }
    if (ferror(stream) || fclose(stream) != 0) {
        return false;
    }
    finish(&context, digest);
    for (size_t index = 0; index < sizeof(digest); ++index) {
        output[index * 2] = digits[digest[index] >> 4];
        output[index * 2 + 1] = digits[digest[index] & 0x0f];
    }
    output[64] = '\0';
    return true;
}

static int compare_names(const void *lhs, const void *rhs)
{
    const char *const *left = lhs;
    const char *const *right = rhs;

    return strcmp(*left, *right);
}

static bool hash_file_content(
    struct sha256_context *context,
    const char *path
)
{
    unsigned char buffer[8192];
    FILE *stream = fopen(path, "rb");
    size_t received;

    if (stream == NULL) {
        return false;
    }
    while ((received = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        update(context, buffer, received);
    }
    if (ferror(stream) || fclose(stream) != 0) {
        return false;
    }
    return true;
}

static void free_names(char **names, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        free(names[index]);
    }
    free(names);
}

static bool hash_directory_recursive(
    struct sha256_context *context,
    const char *root,
    const char *relative
)
{
    char directory_path[4096];
    DIR *directory;
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0;
    bool result = true;

    if (
        snprintf(
            directory_path,
            sizeof(directory_path),
            "%s%s%s",
            root,
            relative[0] == '\0' ? "" : "/",
            relative
        ) >= (int)sizeof(directory_path)
    ) {
        return false;
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        return false;
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        char **next;
        size_t size;

        if (
            strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0
            || strcmp(entry->d_name, ".git") == 0
            || strcmp(entry->d_name, "build") == 0
        ) {
            continue;
        }
        if (count == SIZE_MAX / sizeof(*next)) {
            result = false;
            break;
        }
        next = realloc(names, (count + 1) * sizeof(*next));
        if (next == NULL) {
            result = false;
            break;
        }
        names = next;
        size = strlen(entry->d_name) + 1;
        names[count] = malloc(size);
        if (names[count] == NULL) {
            result = false;
            break;
        }
        memcpy(names[count], entry->d_name, size);
        count += 1;
    }
    if (errno != 0) {
        result = false;
    }
    closedir(directory);
    if (!result) {
        free_names(names, count);
        return false;
    }
    qsort(names, count, sizeof(*names), compare_names);

    for (size_t index = 0; index < count; ++index) {
        char child_relative[4096];
        char child_path[4096];
        struct stat status;
        unsigned char separator = 0;

        if (
            snprintf(
                child_relative,
                sizeof(child_relative),
                "%s%s%s",
                relative,
                relative[0] == '\0' ? "" : "/",
                names[index]
            ) >= (int)sizeof(child_relative)
            || snprintf(
                child_path,
                sizeof(child_path),
                "%s/%s",
                root,
                child_relative
            ) >= (int)sizeof(child_path)
            || lstat(child_path, &status) != 0
        ) {
            result = false;
            break;
        }
        update(
            context,
            (const unsigned char *)child_relative,
            strlen(child_relative)
        );
        update(context, &separator, 1);
        if (S_ISDIR(status.st_mode)) {
            const unsigned char kind = 'd';

            update(context, &kind, 1);
            if (!hash_directory_recursive(context, root, child_relative)) {
                result = false;
                break;
            }
        } else if (S_ISREG(status.st_mode)) {
            const unsigned char kind = 'f';

            update(context, &kind, 1);
            if (!hash_file_content(context, child_path)) {
                result = false;
                break;
            }
        } else {
            result = false;
            break;
        }
    }
    free_names(names, count);
    return result;
}

bool telos_sha256_directory(const char *path, char output[65])
{
    static const char digits[] = "0123456789abcdef";
    struct sha256_context context;
    unsigned char digest[32];
    struct stat status;

    if (
        path == NULL
        || output == NULL
        || stat(path, &status) != 0
        || !S_ISDIR(status.st_mode)
    ) {
        return false;
    }
    initialize(&context);
    if (!hash_directory_recursive(&context, path, "")) {
        return false;
    }
    finish(&context, digest);
    for (size_t index = 0; index < sizeof(digest); ++index) {
        output[index * 2] = digits[digest[index] >> 4];
        output[index * 2 + 1] = digits[digest[index] & 0x0f];
    }
    output[64] = '\0';
    return true;
}
