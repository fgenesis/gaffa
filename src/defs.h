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

struct Type
{
    size_t id;
};

// The highest 2 bits of the type id are used as tags
enum TypeBits
{
    TB_OPTION  = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 1u), // is option type
    TB_ARRAY   = size_t(1u) << size_t(sizeof(Type) * CHAR_BIT - 2u), // is array of type
    TB_TYPEMASK = ~(TB_OPTION | TB_ARRAY)
};

enum PrimType
{
    PRIMTYPE_NIL, // must be 0
    PRIMTYPE_BOOL,
    PRIMTYPE_UINT,
    PRIMTYPE_SINT,
    PRIMTYPE_FLOAT,
    PRIMTYPE_STRING,
    PRIMTYPE_TYPE,          // types have this type
    PRIMTYPE_TABLE,
    PRIMTYPE_ANY,           // can hold any value
    // Ranges can only be made from the 3 primitive numeric types,
    // so there' no reason to allocate extra TypeBits for a range type
    PRIMTYPE_URANGE,
    PRIMTYPE_SRANGE,
    PRIMTYPE_FRANGE,
    // These are the engine-level types. Runtime-created types are any IDs after this.
    PRIMTYPE_MAX
};

struct _Nil {};

struct Str
{
    size_t id;
    size_t len;
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
    Type type; // Runtime type of this value (PrimType | TypeBits)

    void _init(unsigned tyid);
    bool operator==(const ValU& o) const;
};

struct Val : public ValU
{
    inline Val(const ValU& v)           { *this = v; }
    inline Val()                        { _init(PRIMTYPE_NIL);    u.ui = 0; }
    inline Val(bool b)                  { _init(PRIMTYPE_BOOL);   u.ui = b; }
    inline Val(unsigned int i)          { _init(PRIMTYPE_UINT);   u.ui = i; }
    inline Val(int i)                   { _init(PRIMTYPE_SINT);   u.si = i; }
    inline Val(uint i, _Nil _ = _Nil()) { _init(PRIMTYPE_UINT);   u.ui = i; }
    inline Val(sint i, _Nil _ = _Nil()) { _init(PRIMTYPE_SINT);   u.si = i; }
    inline Val(real f)                  { _init(PRIMTYPE_FLOAT);  u.f = f; }
    inline Val(_Nil)                    { _init(PRIMTYPE_NIL);    u.ui = 0; }
    inline Val(Str s)                   { _init(PRIMTYPE_STRING); u.str = s; }
    inline Val(Type t)                  { _init(PRIMTYPE_TYPE);   u.t = t; }
};

enum UnOpType
{
    UOP_INVALID,
    UOP_NOT,
    UOP_POS,
    UOP_NEG,
    UOP_BIN_COMPL,
    UOP_TRY,
    UOP_UNWRAP,
};

enum BinOpType
{
    OP_INVALID,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_INTDIV,
    OP_MOD,
    OP_BIN_AND,
    OP_BIN_OR,
    OP_BIN_XOR,
    OP_SHL,
    OP_SHR,
    OP_C_EQ,
    OP_C_NEQ,
    OP_C_LT,
    OP_C_GT,
    OP_C_LTE,
    OP_C_GTE,
    OP_C_AND,
    OP_C_OR,
    OP_EVAL_AND,
    OP_EVAL_OR,
    OP_CONCAT,
};

BinOpType BinOp_TokenToOp(unsigned tok);
UnOpType UnOp_TokenToOp(unsigned tok);
