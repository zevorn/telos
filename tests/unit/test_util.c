#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <telos/align.h>
#include <telos/array.h>
#include <telos/bitfield.h>
#include <telos/bitops.h>
#include <telos/checked_math.h>
#include <telos/list.h>
#include <telos/static_buffer.h>

struct item {
    int value;
    struct telos_list link;
};

static unsigned int evaluations;

static uint64_t evaluated(uint64_t value)
{
    ++evaluations;
    return value;
}

static void test_checked_math_and_alignment(void)
{
    size_t result = 0;

    assert(telos_size_add(4, 5, &result) && result == 9);
    assert(!telos_size_add(SIZE_MAX, 1, &result));
    assert(!telos_size_add(1, 1, NULL));
    assert(telos_size_multiply(7, 6, &result) && result == 42);
    assert(telos_size_multiply(0, SIZE_MAX, &result) && result == 0);
    assert(!telos_size_multiply(SIZE_MAX, 2, &result));
    assert(!telos_size_multiply(1, 1, NULL));

    assert(telos_is_power_of_two(1));
    assert(telos_is_power_of_two(64));
    assert(!telos_is_power_of_two(0));
    assert(!telos_is_power_of_two(3));
    assert(telos_align_down(15, 8, &result) && result == 8);
    assert(telos_align_up(15, 8, &result) && result == 16);
    assert(telos_align_up(16, 8, &result) && result == 16);
    assert(!telos_align_down(1, 3, &result));
    assert(!telos_align_down(1, 1, NULL));
    assert(!telos_align_up(SIZE_MAX, 8, &result));
}

static void test_bitmaps(void)
{
    TELOS_DECLARE_BITMAP(bitmap, 65) = {0};
    bool previous = true;

    assert(TELOS_ARRAY_SIZE(bitmap) == 3);
    assert(TELOS_BIT_U32(5) == UINT32_C(0x20));
    assert(TELOS_BIT_U64(40) == UINT64_C(0x10000000000));
    assert(!telos_bitmap_test(bitmap, 65, 0));
    assert(telos_bitmap_set(bitmap, 65, 0));
    assert(telos_bitmap_set(bitmap, 65, 31));
    assert(telos_bitmap_set(bitmap, 65, 32));
    assert(telos_bitmap_set(bitmap, 65, 64));
    assert(!telos_bitmap_set(bitmap, 65, 65));
    assert(!telos_bitmap_set(NULL, 65, 0));
    assert(telos_bitmap_test(bitmap, 65, 31));
    assert(telos_bitmap_test(bitmap, 65, 64));
    assert(!telos_bitmap_test(bitmap, 65, 65));
    assert(telos_bitmap_toggle(bitmap, 65, 31));
    assert(!telos_bitmap_test(bitmap, 65, 31));
    assert(!telos_bitmap_toggle(bitmap, 65, 65));
    assert(telos_bitmap_test_and_set(bitmap, 65, 31, &previous));
    assert(!previous && telos_bitmap_test(bitmap, 65, 31));
    assert(telos_bitmap_test_and_clear(bitmap, 65, 31, &previous));
    assert(previous && !telos_bitmap_test(bitmap, 65, 31));
    assert(!telos_bitmap_test_and_set(bitmap, 65, 0, NULL));
    assert(!telos_bitmap_test_and_clear(NULL, 65, 0, &previous));
    assert(telos_bitmap_clear(bitmap, 65, 64));
    assert(!telos_bitmap_clear(bitmap, 65, 65));
}

static void test_bitfields(void)
{
    const uint64_t mask = UINT64_C(0x0000ff00);
    uint64_t value = 0;

    assert(telos_field_mask_valid_u64(mask));
    assert(telos_field_mask_valid_u64(UINT64_MAX));
    assert(!telos_field_mask_valid_u64(0));
    assert(!telos_field_mask_valid_u64(UINT64_C(0x5)));
    assert(TELOS_FIELD_GET(mask, UINT64_C(0x12345678)) == UINT64_C(0x56));
    assert(TELOS_FIELD_FITS(mask, UINT64_C(0xff)));
    assert(!TELOS_FIELD_FITS(mask, UINT64_C(0x100)));
    assert(TELOS_FIELD_PREP(mask, UINT64_C(0xab), &value));
    assert(value == UINT64_C(0xab00));
    assert(!TELOS_FIELD_PREP(mask, UINT64_C(0x100), &value));
    assert(!TELOS_FIELD_PREP(mask, 1, NULL));
    assert(TELOS_FIELD_REPLACE(UINT64_C(0xffff0000), mask, UINT64_C(0x12),
                               &value));
    assert(value == UINT64_C(0xffff1200));
    assert(!TELOS_FIELD_REPLACE(0, UINT64_C(0x5), 1, &value));

    evaluations = 0;
    assert(TELOS_FIELD_GET(evaluated(mask), evaluated(UINT64_C(0x1200))) ==
           UINT64_C(0x12));
    assert(evaluations == 2);
}

static void test_lists(void)
{
    TELOS_LIST_HEAD(head);
    TELOS_LIST_HEAD(other);
    struct item first = {.value = 1};
    struct item second = {.value = 2};
    struct item third = {.value = 3};
    struct item replacement = {.value = 4};
    struct telos_list *position;
    struct telos_list *next;
    int expected = 1;

    telos_list_initialize(&first.link);
    telos_list_initialize(&second.link);
    telos_list_initialize(&third.link);
    telos_list_initialize(&replacement.link);
    assert(telos_list_empty(&head));
    telos_list_add(&head, &second.link);
    telos_list_add(&head, &first.link);
    telos_list_add_tail(&head, &third.link);
    assert(!telos_list_empty(&head));
    assert(!telos_list_singular(&head));
    assert(TELOS_LIST_FIRST_ENTRY(&head, struct item, link) == &first);
    assert(TELOS_LIST_LAST_ENTRY(&head, struct item, link) == &third);

    TELOS_LIST_FOR_EACH(position, &head)
    {
        assert(TELOS_LIST_ENTRY(position, struct item, link)->value ==
               expected++);
    }
    assert(expected == 4);

    telos_list_move(&head, &third.link);
    assert(TELOS_LIST_FIRST_ENTRY(&head, struct item, link) == &third);
    telos_list_move_tail(&head, &third.link);
    assert(TELOS_LIST_LAST_ENTRY(&head, struct item, link) == &third);
    telos_list_replace(&second.link, &replacement.link);
    assert(TELOS_LIST_ENTRY_CONST(head.next->next, struct item, link) ==
           &replacement);
    assert(telos_list_empty(&second.link));

    telos_list_add_tail(&other, &second.link);
    assert(telos_list_singular(&other));
    telos_list_splice_tail(&head, &other);
    assert(telos_list_empty(&other));
    assert(TELOS_LIST_LAST_ENTRY(&head, struct item, link) == &second);

    telos_list_move(&other, &second.link);
    telos_list_splice(&head, &other);
    assert(telos_list_empty(&other));
    assert(TELOS_LIST_FIRST_ENTRY(&head, struct item, link) == &second);

    TELOS_LIST_FOR_EACH_SAFE(position, next, &head)
    {
        telos_list_remove(position);
    }
    assert(telos_list_empty(&head));
}

static void test_static_buffer(void)
{
    char storage[8];
    struct telos_static_buffer buffer;

    assert(telos_static_buffer_initialize(&buffer, storage, sizeof(storage)));
    assert(telos_static_buffer_append(&buffer, "abc"));
    assert(telos_static_buffer_append_n(&buffer, "def", 3));
    assert(strcmp(storage, "abcdef") == 0);
    assert(telos_static_buffer_remaining(&buffer) == 1);
    assert(!telos_static_buffer_append(&buffer, "xy"));
    assert(strcmp(storage, "abcdef") == 0);
    assert(!telos_static_buffer_append(&buffer, NULL));
    assert(!telos_static_buffer_initialize(NULL, storage, sizeof(storage)));
    assert(!telos_static_buffer_initialize(&buffer, NULL, sizeof(storage)));
    assert(!telos_static_buffer_initialize(&buffer, storage, 0));
    assert(telos_static_buffer_remaining(NULL) == 0);
}

int main(void)
{
    test_checked_math_and_alignment();
    test_bitmaps();
    test_bitfields();
    test_lists();
    test_static_buffer();
    return 0;
}
