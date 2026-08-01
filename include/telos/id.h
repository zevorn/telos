#ifndef TELOS_ID_H
#define TELOS_ID_H

#include <telos/types.h>

#define TELOS_ID_TEXT_SIZE 33

struct telos_id {
    uint64_t high;
    uint64_t low;
};

struct telos_id telos_id_generate(void);

bool telos_id_equal(struct telos_id lhs, struct telos_id rhs);

bool telos_id_format(struct telos_id id, char *buffer, size_t buffer_size);

bool telos_id_parse(const char *text, struct telos_id *id);

#endif
