#pragma once

#include <stddef.h>

typedef void *(*GaffaAllocFunc)(void *ud, void *ptr, size_t osize, size_t nsize);

enum GaffaError
{
    GAFFA_E_OK,
    GAFFA_E_OUT_OF_MEMORY,
    GAFFA_E_INVALID_INPUT
};
typedef enum GaffaError GaffaError;

struct ga_RT;

