#include <stdio.h>
#include <string.h>

#include <telos/value.h>

int main(void)
{
    struct telos_value *child = telos_value_new_string("retained");
    const struct telos_value *items[] = {child};
    struct telos_value *array;
    const struct telos_value *stored;

    if (child == NULL) {
        fputs("failed to create array child\n", stderr);
        return 1;
    }

    array = telos_value_new_array(items, 1);
    telos_value_release(child);

    stored = telos_value_at(array, 0);
    if (array == NULL || telos_value_type(array) != TELOS_VALUE_ARRAY ||
        telos_value_count(array) != 1 || stored == NULL ||
        strcmp(telos_value_string(stored), "retained") != 0 ||
        telos_value_at(array, 1) != NULL) {
        fputs("array did not retain immutable children\n", stderr);
        telos_value_release(array);
        return 1;
    }

    telos_value_release(array);
    return 0;
}
