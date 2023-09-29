#pragma once

#include <stddef.h>
#include <stdint.h>
#include <limits.h> // CHAR_BIT

typedef size_t usize;
typedef int64_t sint;
typedef uint64_t uint;
typedef float real;


enum Constants
{
    INT_BITS =  sizeof(uint) * CHAR_BIT,
};

enum TypeBits
{
    TB_OPTION     = 1u << (sizeof(unsigned) - 1u), // is option type
    TB_VEC        = 1u << (sizeof(unsigned) - 2u), // is vector type
    PRIMTYPE_MASK = (~0u >> 2u)
};

enum PrimType
{
    PRIMTYPE_POISON = -1,   // if this ever reaches codegen, abort
    PRIMTYPE_UNK    = 0,    // unresolved type; used for type inference

    PRIMTYPE_TYPE,          // types have this type

    PRIMTYPE_ANY,           // can hold any value
    PRIMTYPE_NIL,
    PRIMTYPE_BOOL,
    PRIMTYPE_SINT,
    PRIMTYPE_UINT,
    PRIMTYPE_FLOAT,
    PRIMTYPE_STRING,
    PRIMTYPE_TABLE,
};

struct _Any {};
struct _Nil {};
struct _Type {};
struct _Poison {};
struct _Str
{
    _Str(unsigned id) : id(id) {}
    const unsigned id;
};

struct Val
{
    inline Val(): type(PRIMTYPE_NIL) { u.ui = 0; }
    inline Val(bool b): type(PRIMTYPE_BOOL) { u.ui = b; }
    inline Val(uint i): type(PRIMTYPE_UINT) { u.ui = i; }
    inline Val(sint i): type(PRIMTYPE_SINT) { u.si = i; }
    inline Val(real f): type(PRIMTYPE_FLOAT) { u.f = f; }
    inline Val(_Any): type(PRIMTYPE_ANY) { u.ui = 0; }
    inline Val(_Type): type(PRIMTYPE_TYPE) { u.ui = 0; }
    inline Val(_Poison): type(PRIMTYPE_POISON) { u.ui = 0; }
    inline Val(_Nil): type(PRIMTYPE_NIL) { u.ui = 0; }
    inline Val(_Str s): type(PRIMTYPE_STRING) { u.strid = s.id; }


    unsigned type;

    union
    {
        sint si;
        uint ui;
        real f;
        void *p;
        unsigned strid;
    } u;
};
