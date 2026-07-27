#include <math.h>
#include <stdio.h>
#include <string.h>

#include <telos/value.h>

static bool rejected(const char *json)
{
    struct telos_error *error = NULL;
    struct telos_value *value = telos_value_parse_json(
        json,
        strlen(json),
        &error
    );
    const bool result = value == NULL
        && error != NULL
        && telos_error_domain(error) == TELOS_ERROR_DOMAIN_PROTOCOL;

    telos_value_release(value);
    telos_error_release(error);
    return result;
}

int main(void)
{
    static const char *invalid[] = {
        "",
        "{\"a\":1",
        "{\"a\":1,\"a\":2}",
        "[1,]",
        "\"bad\\xescape\"",
        "01",
        "1 trailing",
        "1e9999",
    };
    struct telos_value *non_finite = telos_value_new_real(NAN);
    char buffer[8];
    struct telos_error *error = NULL;

    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        if (!rejected(invalid[index])) {
            fprintf(stderr, "accepted invalid JSON: %s\n", invalid[index]);
            telos_value_release(non_finite);
            return 1;
        }
    }

    if (
        telos_value_json_size(non_finite) != 0
        || telos_value_write_json(
            non_finite,
            buffer,
            sizeof(buffer),
            NULL,
            &error
        )
        || error == NULL
    ) {
        fputs("serialized a non-finite JSON number\n", stderr);
        telos_error_release(error);
        telos_value_release(non_finite);
        return 1;
    }

    telos_error_release(error);
    telos_value_release(non_finite);
    return 0;
}
