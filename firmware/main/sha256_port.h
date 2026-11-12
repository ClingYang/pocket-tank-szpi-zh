#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool sha256_port(const void *data, size_t length, uint8_t digest[32]);
