#pragma once

#include <stddef.h>

typedef void *(*GaffaAllocFunc)(void *ud, void *ptr, size_t osize, size_t nsize);

