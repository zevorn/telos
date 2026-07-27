#include <stdio.h>
#include <string.h>

#include <telos/value.h>

int main(void)
{
    char source[] = "immutable";
    struct telos_value *value = telos_value_new_string(source);
    struct telos_value *observer;

    if (value == NULL) {
        fputs("failed to create string value\n", stderr);
        return 1;
    }

    observer = telos_value_retain(value);
    source[0] = 'X';
    telos_value_release(value);

    if (
        observer == NULL
        || telos_value_type(observer) != TELOS_VALUE_STRING
        || strcmp(telos_value_string(observer), "immutable") != 0
    ) {
        fputs("string value did not retain immutable content\n", stderr);
        telos_value_release(observer);
        return 1;
    }

    telos_value_release(observer);
    return 0;
}
