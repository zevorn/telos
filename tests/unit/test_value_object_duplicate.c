#include <stdio.h>

#include <telos/value.h>

int main(void)
{
    struct telos_value *first = telos_value_new_integer(1);
    struct telos_value *second = telos_value_new_integer(2);
    const char *keys[] = {"duplicate", "duplicate"};
    const struct telos_value *values[] = {first, second};
    struct telos_value *object = telos_value_new_object(keys, values, 2);

    telos_value_release(second);
    telos_value_release(first);

    if (object != NULL) {
        fputs("object accepted duplicate keys\n", stderr);
        telos_value_release(object);
        return 1;
    }

    return 0;
}
