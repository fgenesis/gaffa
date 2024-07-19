#pragma once

#include "compiler-fixup.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h> // CHAR_BIT
#include <assert.h>

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
    TB_ARRAY        = 1u << (sizeof(unsigned) - 2u), // is array of type
    PRIMTYPE_MASK = 0xff
};

enum PrimType
{
    PRIMTYPE_NIL,
    PRIMTYPE_BOOL,
    PRIMTYPE_UINT,
    PRIMTYPE_SINT,
    PRIMTYPE_FLOAT,
    PRIMTYPE_STRING,
    PRIMTYPE_TYPE,          // types have this type
    PRIMTYPE_TABLE,
    PRIMTYPE_STRUCTTYPE,
    PRIMTYPE_ANY,           // can hold any value
};

struct _Nil {};

struct Str
{
    size_t id;
    size_t len;
};

struct Type
{
    unsigned pt;        // Primtype | TypeBits
    unsigned nameStrId; // if the type has a known name, it goes here
    Type *list;         // if the type is PRIMTYPE_STRUCTTYPE, this has the subtypes
};

template<typename T>
struct Range
{
    T begin, end, step;
};

// dumb type, no ctors
struct ValU
{
    union
    {
        sint si;
        uint ui;
        real f;
        void *p;
        Str str;
        Range<uint> urange;
        Range<sint> srange;
        Range<real> frange;
        Type t;
    } u;

    // This must not be PRIMTYPE_ANY.
    unsigned type; // PrimType | TypeBits
};

struct Val : public ValU
{
    inline Val(const ValU& v)           { *this = v; }
    inline Val()                        { type = PRIMTYPE_NIL;    u.ui = 0; }
    inline Val(bool b)                  { type = PRIMTYPE_BOOL;   u.ui = b; }
    inline Val(unsigned int i)          { type = PRIMTYPE_UINT;   u.ui = i; }
    inline Val(int i)                   { type = PRIMTYPE_SINT;   u.si = i; }
    inline Val(uint i, _Nil _ = _Nil()) { type = PRIMTYPE_UINT;   u.ui = i; }
    inline Val(sint i, _Nil _ = _Nil()) { type = PRIMTYPE_SINT;   u.si = i; }
    inline Val(real f)                  { type = PRIMTYPE_FLOAT;  u.f = f; }
    inline Val(_Nil)                    { type = PRIMTYPE_NIL;    u.ui = 0; }
    inline Val(Str s)                   { type = PRIMTYPE_STRING; u.str = s; }
    inline Val(Type t)                  { type = PRIMTYPE_TYPE;   u.t = t; }
};

// Dynamic value, has a runtime type attached
struct DVal
{
    Val v;
    Type *t;
};
