#include <stdio.h>

#include <telos/value.h>

int main(void)
{
    struct telos_value *null_value = telos_value_new_null();
    struct telos_value *boolean_value = telos_value_new_boolean(true);
    struct telos_value *integer_value = telos_value_new_integer(INT64_C(-42));
    struct telos_value *real_value = telos_value_new_real(3.25);
    bool boolean_result = false;
    int64_t integer_result = 0;
    double real_result = 0.0;
    int failed = 0;

    if (
        null_value == NULL
        || telos_value_type(null_value) != TELOS_VALUE_NULL
        || boolean_value == NULL
        || !telos_value_boolean(boolean_value, &boolean_result)
        || !boolean_result
        || integer_value == NULL
        || !telos_value_integer(integer_value, &integer_result)
        || integer_result != INT64_C(-42)
        || real_value == NULL
        || !telos_value_real(real_value, &real_result)
        || real_result != 3.25
    ) {
        fputs("scalar value changed type or content\n", stderr);
        failed = 1;
    }

    telos_value_release(real_value);
    telos_value_release(integer_value);
    telos_value_release(boolean_value);
    telos_value_release(null_value);
    return failed;
}
