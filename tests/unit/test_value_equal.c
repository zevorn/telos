#include <assert.h>

#include <telos/value.h>

int main(void)
{
    const char *keys[] = {"count", "label"};
    struct telos_value *one = telos_value_new_integer(1);
    struct telos_value *label = telos_value_new_string("one");
    const struct telos_value *members[] = {one, label};
    struct telos_value *lhs = telos_value_new_object(keys, members, 2);
    struct telos_value *rhs = telos_value_new_object(keys, members, 2);
    struct telos_value *different = telos_value_new_integer(2);

    assert(telos_value_equal(lhs, rhs));
    assert(!telos_value_equal(lhs, different));
    assert(!telos_value_equal(lhs, NULL));
    assert(telos_value_equal(NULL, NULL));

    telos_value_release(different);
    telos_value_release(rhs);
    telos_value_release(lhs);
    telos_value_release(label);
    telos_value_release(one);
    return 0;
}
