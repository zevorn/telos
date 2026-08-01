#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/value.h>

int main(void)
{
    static const char secret[] = "never-write-this-token";
    struct telos_value *sensitive = telos_value_new_sensitive(secret);
    const size_t size = telos_value_json_size(sensitive);
    char *json = malloc(size);

    if (sensitive == NULL ||
        telos_value_type(sensitive) != TELOS_VALUE_SENSITIVE || json == NULL ||
        !telos_value_write_json(sensitive, json, size, NULL, NULL) ||
        strstr(json, secret) != NULL ||
        strcmp(json, "{\"$redacted\":true}") != 0) {
        fputs("sensitive Value was not redacted\n", stderr);
        free(json);
        telos_value_release(sensitive);
        return 1;
    }

    free(json);
    telos_value_release(sensitive);
    return 0;
}
