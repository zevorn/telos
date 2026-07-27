#ifndef TELOS_INSTALL_SHA256_H
#define TELOS_INSTALL_SHA256_H

#include <stdbool.h>

bool telos_sha256_file(const char *path, char output[65]);

bool telos_sha256_directory(const char *path, char output[65]);

bool telos_sha256_source_directory(const char *path, char output[65]);

#endif
