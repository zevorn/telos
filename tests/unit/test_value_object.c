#include <stdio.h>
#include <string.h>

#include <telos/value.h>

int main(void)
{
    char key[] = "status";
    struct telos_value *child = telos_value_new_string("ready");
    const char *keys[] = {key};
    const struct telos_value *values[] = {child};
    struct telos_value *object;
    const struct telos_value *stored;

    if (child == NULL) {
        fputs("failed to create object child\n", stderr);
        return 1;
    }

    object = telos_value_new_object(keys, values, 1);
    key[0] = 'X';
    telos_value_release(child);
    stored = telos_value_get(object, "status");

    if (object == NULL || telos_value_type(object) != TELOS_VALUE_OBJECT ||
        telos_value_count(object) != 1 ||
        strcmp(telos_value_key_at(object, 0), "status") != 0 ||
        stored == NULL || strcmp(telos_value_string(stored), "ready") != 0 ||
        telos_value_get(object, "missing") != NULL) {
        fputs("object did not retain immutable keys and values\n", stderr);
        telos_value_release(object);
        return 1;
    }

    telos_value_release(object);
    return 0;
}
