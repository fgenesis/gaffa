#pragma once

#include <assert.h>

#include "defs.h"

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

namespace detail
{
    template <typename T, size_t N>
    char(&_ArraySizeHelper(T(&a)[N]))[N];
}
#define Countof(a) (sizeof(detail::_ArraySizeHelper(a)))


#ifdef __GNUC__
static inline __attribute__((always_inline, noreturn)) void unreachable() { __builtin_unreachable(); }
#elif defined(_MSC_VER)
static __forceinline __declspec(noreturn) void unreachable() { __assume(false); }
#else
static inline void unreachable() { assert(false && "unreachable"); }
#endif


template<typename T>
inline bool add_check_overflow(T* res, T a, T b)
{
#if __has_builtin(__builtin_add_overflow)
    return __builtin_add_overflow(a, b, res);
#else
    return ((*res = a + b)) < a;
#endif
}

template<typename T>
inline bool mul_check_overflow(T* res, T a, T b)
{
#if __has_builtin(__builtin_mul_overflow)
    return __builtin_mul_overflow(a, b, res);
#else
    T tmp = a * b;
    *res = tmp;
    return a && tmp / a != b;
#endif
}


struct MaybeNum
{
    union
    {
        uint ui;
        sint si;
    } val;

    size_t used;
    bool overflow;

    inline operator bool() const { return used && !overflow; }
};
MaybeNum strtouint(const char* s, size_t maxlen = -1);

MaybeNum strtouint_dec(const char* s, size_t maxlen = -1);
// TODO: hex, oct, bin
