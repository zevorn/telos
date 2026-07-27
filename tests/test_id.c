#include <stdio.h>

#include <telos/id.h>

int main(void)
{
    const struct telos_id original = telos_id_generate();
    struct telos_id parsed = {0};
    char text[TELOS_ID_TEXT_SIZE];

    if (!telos_id_format(original, text, sizeof(text))) {
        fputs("failed to format generated ID\n", stderr);
        return 1;
    }

    if (!telos_id_parse(text, &parsed)) {
        fputs("failed to parse formatted ID\n", stderr);
        return 1;
    }

    if (!telos_id_equal(original, parsed)) {
        fputs("ID changed during text round trip\n", stderr);
        return 1;
    }

    return 0;
}
