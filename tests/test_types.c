#include <assert.h>

#include <telos/types.h>

int main(void)
{
    const telos_size maximum_size = TELOS_SIZE_MAX;
    const telos_ssize minimum_signed_size = TELOS_SSIZE_MIN;
    const telos_ssize maximum_signed_size = TELOS_SSIZE_MAX;
    const telos_offset minimum_offset = TELOS_OFFSET_MIN;
    const telos_offset maximum_offset = TELOS_OFFSET_MAX;
    telos_byte byte = 0;

    assert(_Alignof(telos_max_align) >= _Alignof(void *));
    assert(sizeof(telos_i8) == 1);
    assert(sizeof(telos_u8) == 1);
    assert(sizeof(telos_i16) == 2);
    assert(sizeof(telos_u16) == 2);
    assert(sizeof(telos_i32) == 4);
    assert(sizeof(telos_u32) == 4);
    assert(sizeof(telos_i64) == 8);
    assert(sizeof(telos_u64) == 8);
    assert(sizeof(telos_iptr) == sizeof(void *));
    assert(sizeof(telos_uptr) == sizeof(void *));
    assert(maximum_size > 0);
    assert(minimum_signed_size < 0);
    assert(maximum_signed_size > 0);
    assert(minimum_offset < 0);
    assert(maximum_offset > 0);
    assert(byte == 0);
    return 0;
}
