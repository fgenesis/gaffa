#pragma once

#include <assert.h>

#include "defs.h"

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef FORCEINLINE
#  ifdef _MSC_VER
#    define FORCEINLINE __forceinline
#  elif defined(__GNUC__)
#    define FORCEINLINE __inline__ __attribute__((__always_inline__))
#  else
#    define FORCEINLINE inline
#  endif
#endif

#ifndef NOINLINE
#  ifdef _MSV_VER
#    define NOINLINE __declspec(noinline)
#  elif defined(__GNUC__)
#    define NOINLINE __attribute__((noinline))
#  else
#    define NOINLINE
#  endif
#endif

#ifndef TAIL_RETURN
#  ifdef __clang__
#    if __clang_major__+0 >= 13
#      define TAIL_RETURN(x) __attribute__((musttail)) return(x)
#    endif
#  endif
#endif

#ifndef TAIL_RETURN
#define TAIL_RETURN(x) return(x) // Hope the compiler is smart enough
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
