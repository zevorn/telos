#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <telos/id.h>

static atomic_uint_fast64_t next_id = 1;

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

    return (struct telos_id){
        .high = UINT64_C(0x54454c4f53000000),
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
