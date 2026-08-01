#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <telos/value.h>

static bool rejected(const char *json)
{
    struct telos_error *error = NULL;
    struct telos_value *value =
        telos_value_parse_json(json, strlen(json), &error);
    const bool result =
        value == NULL && error != NULL &&
        telos_error_domain(error) == TELOS_ERROR_DOMAIN_PROTOCOL;

    telos_value_release(value);
    telos_error_release(error);
    return result;
}

static bool accepted(const char *json)
{
    struct telos_error *error = NULL;
    struct telos_value *value =
        telos_value_parse_json(json, strlen(json), &error);
    bool result = value != NULL && error == NULL;

    telos_value_release(value);
    telos_error_release(error);
    return result;
}

int main(void)
{
    static const char *invalid[] = {
        "",
        "tru",
        "falsee",
        "nul",
        "+1",
        "-",
        ".1",
        "{\"a\":1",
        "{\"a\":1,\"a\":2}",
        "{a:1}",
        "{\"a\" 1}",
        "{\"a\":}",
        "{\"a\":1,}",
        "{\"a\":1 \"b\":2}",
        "[1,]",
        "[,1]",
        "[1 2]",
        "[",
        "\"unterminated",
        "\"raw\nnewline\"",
        "\"bad\\xescape\"",
        "\"bad\\u12x4\"",
        "\"bad\\ud800\"",
        "\"bad\\ud800xxxxxx\"",
        "\"bad\\ud800\\x0041\"",
        "\"bad\\ud800\\u0041\"",
        "\"bad\\ud800\\ue000\"",
        "\"bad\\udc00\"",
        "\"bad\\u0000\"",
        "\"bad\\",
        "01",
        "-01",
        "9223372036854775808",
        "-9223372036854775809",
        "1.",
        "1e",
        "1e+",
        "1 trailing",
        "1e9999",
    };
    static const char *valid[] = {
        "null",
        "true",
        "false",
        "0",
        "-0",
        "-9223372036854775808",
        "9223372036854775807",
        "1.25",
        "-1.5e+2",
        "1E-2",
        "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"",
        "\"\\u0041\\u00e9\\u20ac\\ud83d\\ude00\"",
        "\"abcdefghijklmnopqrstuvwxyz0123456789\"",
        "[0,1,2,3,4]",
        "{\"a\":0,\"b\":1,\"c\":2,\"d\":3,\"e\":4}",
        " [ null, true, false, {\"nested\":[1,2,3]} ] ",
        "{\"empty_object\":{},\"empty_array\":[],\"text\":\"ok\"}",
    };
    struct telos_value *non_finite = telos_value_new_real(NAN);
    char buffer[8];
    char large_buffer[256];
    struct telos_error *error = NULL;
    char nested[300];
    size_t nested_size = 0;

    assert(telos_value_parse_json(NULL, 1, &error) == NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_value_json_size(NULL) == 0);
    assert(!telos_value_write_json(NULL, buffer, sizeof(buffer), NULL, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    {
        struct telos_value *value = telos_value_new_null();

        assert(
            !telos_value_write_json(value, NULL, sizeof(buffer), NULL, &error));
        assert(error != NULL);
        telos_error_release(error);
        error = NULL;
        assert(!telos_value_write_json(value, buffer, 0, NULL, &error));
        assert(error != NULL);
        telos_error_release(error);
        error = NULL;
        assert(!telos_value_write_json(value, buffer, 2, NULL, &error));
        assert(error != NULL);
        telos_error_release(error);
        error = NULL;
        telos_value_release(value);
    }

    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        if (!rejected(invalid[index])) {
            fprintf(stderr, "accepted invalid JSON: %s\n", invalid[index]);
            telos_value_release(non_finite);
            return 1;
        }
    }
    for (size_t index = 0; index < sizeof(valid) / sizeof(valid[0]); ++index) {
        if (!accepted(valid[index])) {
            fprintf(stderr, "rejected valid JSON: %s\n", valid[index]);
            telos_value_release(non_finite);
            return 1;
        }
    }
    for (size_t index = 0; index < 140; ++index) {
        nested[nested_size++] = '[';
    }
    nested[nested_size++] = '0';
    for (size_t index = 0; index < 140; ++index) {
        nested[nested_size++] = ']';
    }
    nested[nested_size] = '\0';
    if (!rejected(nested)) {
        fputs("accepted JSON beyond the nesting limit\n", stderr);
        telos_value_release(non_finite);
        return 1;
    }

    if (telos_value_json_size(non_finite) != 0 ||
        telos_value_write_json(non_finite, buffer, sizeof(buffer), NULL,
                               &error) ||
        error == NULL) {
        fputs("serialized a non-finite JSON number\n", stderr);
        telos_error_release(error);
        telos_value_release(non_finite);
        return 1;
    }
    telos_error_release(error);
    error = NULL;

    {
        const char escaped[] = {
            '"', '\\', '\b', '\f', '\n', '\r', '\t', 1, '\0',
        };
        struct telos_value *string = telos_value_new_string(escaped);
        struct telos_value *false_value = telos_value_new_boolean(false);
        struct telos_value *integer = telos_value_new_integer(INT64_MIN);
        struct telos_value *real = telos_value_new_real(2.0);
        struct telos_value *sensitive =
            telos_value_new_sensitive("do-not-serialize");
        const struct telos_value *items[] = {
            string, false_value, integer, real, sensitive,
        };
        struct telos_value *array = telos_value_new_array(items, 5);
        size_t written = 0;

        assert(telos_value_write_json(array, large_buffer, sizeof(large_buffer),
                                      &written, &error));
        assert(error == NULL);
        assert(written > 0);
        assert(strstr(large_buffer, "\\\"") != NULL);
        assert(strstr(large_buffer, "\\\\") != NULL);
        assert(strstr(large_buffer, "\\b") != NULL);
        assert(strstr(large_buffer, "\\f") != NULL);
        assert(strstr(large_buffer, "\\n") != NULL);
        assert(strstr(large_buffer, "\\r") != NULL);
        assert(strstr(large_buffer, "\\t") != NULL);
        assert(strstr(large_buffer, "\\u0001") != NULL);
        assert(strstr(large_buffer, "false") != NULL);
        assert(strstr(large_buffer, "-9223372036854775808") != NULL);
        assert(strstr(large_buffer, "2.0") != NULL);
        assert(strstr(large_buffer, "{\"$redacted\":true}") != NULL);
        telos_value_release(array);
        telos_value_release(sensitive);
        telos_value_release(real);
        telos_value_release(integer);
        telos_value_release(false_value);
        telos_value_release(string);
    }

    telos_error_release(error);
    telos_value_release(non_finite);
    return 0;
}
