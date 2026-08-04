#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <telos/id.h>

static atomic_uint_fast64_t next_id = 1;
static atomic_uint_fast64_t process_tag;

static uint64_t current_process_tag(void)
{
    uint64_t tag = atomic_load_explicit(&process_tag, memory_order_relaxed);
    struct timespec timestamp;
    uint64_t candidate;
    uint64_t expected = 0;

    if (tag != 0) {
        return tag;
    }
    if (clock_gettime(CLOCK_REALTIME, &timestamp) == 0) {
        candidate = ((uint64_t)timestamp.tv_sec << 32) ^
                    (uint64_t)timestamp.tv_nsec;
    } else {
        candidate = (uint64_t)time(NULL);
    }
    candidate ^= (uint64_t)(unsigned long)getpid() << 16;
    candidate ^= (uint64_t)(uintptr_t)&timestamp;
    if (candidate == 0) {
        candidate = 1;
    }
    if (!atomic_compare_exchange_strong_explicit(
            &process_tag, &expected, candidate, memory_order_relaxed,
            memory_order_relaxed)) {
        candidate = expected;
    }
    return candidate;
}

static bool parse_half(const char *text, uint64_t *value)
{
    uint64_t result = 0;

    for (size_t index = 0; index < 16; ++index) {
        const char character = text[index];
        uint8_t digit;

        if (character >= '0' && character <= '9') {
            digit = (uint8_t)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (uint8_t)(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = (uint8_t)(character - 'A' + 10);
        } else {
            return false;
        }

        result = (result << 4) | digit;
    }

    *value = result;
    return true;
}

struct telos_id telos_id_generate(void)
{
    const uint64_t sequence =
        atomic_fetch_add_explicit(&next_id, 1, memory_order_relaxed);
    const uint64_t process = current_process_tag() ^
                             ((uint64_t)(unsigned long)getpid() << 32);

    return (struct telos_id){
        .high = UINT64_C(0x54454c4f53000000) ^ process,
        .low = sequence,
    };
}

bool telos_id_equal(struct telos_id lhs, struct telos_id rhs)
{
    return lhs.high == rhs.high && lhs.low == rhs.low;
}

bool telos_id_format(struct telos_id id, char *buffer, size_t buffer_size)
{
    int written;

    if (buffer == NULL || buffer_size < TELOS_ID_TEXT_SIZE) {
        return false;
    }

    written = snprintf(buffer, buffer_size, "%016" PRIx64 "%016" PRIx64,
                       id.high, id.low);

    return written == TELOS_ID_TEXT_SIZE - 1;
}

bool telos_id_parse(const char *text, struct telos_id *id)
{
    struct telos_id parsed;

    if (text == NULL || id == NULL || strlen(text) != TELOS_ID_TEXT_SIZE - 1) {
        return false;
    }

    if (!parse_half(text, &parsed.high) ||
        !parse_half(text + 16, &parsed.low)) {
        return false;
    }

    *id = parsed;
    return true;
}
