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


#ifndef PACKED_STRUCT
  #ifdef _MSC_VER
  #  define PACKED_STRUCT(name) __pragma(pack(push, 1)) struct name __pragma(pack(pop))
  #elif defined(__GNUC__)
  #  define PACKED_STRUCT(name) struct __attribute__((packed)) name
  #endif
#endif

#ifndef ALIGN
  #ifdef _MSC_VER
  #  define ALIGN(x) __declspec(align(x))
  #elif defined(__GNUC__)
  #  define ALIGN(x) __attribute__((aligned(x)))
  #endif
#endif

#ifndef ASSUME
  #ifdef _MSC_VER
  #  define ASSUME(x) __assume(x)
  #elif __has_builtin(__builtin__assume)
  #  define ALIGN(x) __builtin__assume(x)
  #endif
  #ifndef ASSUME
  #  define ASSUME(x)
  #endif
#endif

#ifndef UNLIKELY
#  if __has_builtin(__builtin_expect)
#    define UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#  else
#    define UNLIKELY(expr) (!!(expr))
#  endif
#endif

#ifndef LIKELY
#  if __has_builtin(__builtin_expect)
#    define LIKELY(expr) __builtin_expect(!!(expr), 1)
#  else
#    define LIKELY(expr) (!!(expr))
#  endif
#endif

#define STATIC_ASSERT(cond) switch((int)!!(cond)){case 0:;case(!!(cond)):;}




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


template<typename T>
static inline T roundUpToPowerOfTwo(T v)
{
    STATIC_ASSERT(sizeof(v) <= 8); // This supports up to 64bit size_t
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    if(sizeof(v) > 4) // Need a check here. ARM produces garbage with too large shifts.
        v |= v >> 32u;
    v++;
    return v;
}

template<typename T>
static inline T alignTo(T val, T aln)
{
    return ((val + (aln - 1)) / aln) * aln;
}

template <typename T, T v>
struct IntegralConstant
{
    typedef T value_type;
    typedef IntegralConstant<T,v> type;
    enum { value = v };
};

typedef IntegralConstant<bool, true>  CompileTrue;
typedef IntegralConstant<bool, false> CompileFalse;
template<bool V> struct CompileCheck : IntegralConstant<bool, V>{};
