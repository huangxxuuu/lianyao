#pragma once

#include <stddef.h>
#include <stdint.h>

void potion_sha256(const void *data, size_t len, uint8_t out[32]);
uint64_t potion_fingerprint(const char *domain, const void *data, size_t len);
