#pragma once

#include "defs.h"
#include "typing.h"

struct VmIter;
struct DType;
struct VM;
struct VMP;
struct Runtime;

enum
{
    MINSTACK = 16
};

// advance iterator; old value is in val and updated to new value
// continue iteration until this returns 0
typedef uint (*IterAdv)(ValU& val, VmIter& it);

struct VmIter
{
    IterAdv next;

    union
    {
        struct
        {
            _AnyValU end, step;
            ValU start;
        } numeric;

        struct
        {
            tsize i;
            GCobj *obj;
        } index;
    } u;
};

struct VM
{
    Runtime *rt;
};

struct Imm_None
{
};

struct Imm_u32
{
    u32 a;
};
struct Imm_2xu32
{
    u32 a, b;
};

struct Imm_3xu32
{
    u32 a, b, c;
};

struct Imm_4xu32
{
    u32 a, b, c, d;
};

template<typename T>
struct _ImmSlotHelper
{
    enum { Slots = (sizeof(T) + (sizeof(Inst*) - 1)) / sizeof(Inst*) };
};
template<>
struct _ImmSlotHelper<Imm_None>
{
    enum { Slots = 0 };
};

template<typename T>
static FORCEINLINE size_t immslots(const T *imm)
{
    return _ImmSlotHelper<T>::Slots;
}

#define IMMSLOTS(T) (_ImmSlotHelper<T>::Slots)

// Invariant: Each Inst array ends with an entry that has func=NULL,
// and gfunc holds the object that contains this instruction array.

struct Inst;

#define VMPARAMS const Inst *ins, VMP * const vm, Val *sbase, Val *sp
#define VMARGS ins, vm, sbase, sp

// Inst and OpFunc are kinda the same, but a C function typedef can't use
// itself as a function parameter
typedef const Inst* (*VMFunc)(VMPARAMS);
struct Inst
{
    VMFunc f;
};

template<typename T> static FORCEINLINE const T *_imm(const Inst *ins)
{
    return reinterpret_cast<const T*>(ins + 1);
}

#define VMFUNC_DEF(name) NOINLINE const Inst * op_ ## name(VMPARAMS)
#define VMFUNC_IMM(name, T) \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm); \
    VMFUNC_DEF(name) { return xop_ ## name(VMARGS, _imm<T>(ins)); } \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm)

#define VMFUNC(name) VMFUNC_IMM(name, Imm_None)


// Fetch next instruction and jump to it
// Very very important that this is force-inlined!
// This is the hottest piece of code that is inlined into every single VM opcode.
static FORCEINLINE VMFUNC_DEF(nextop)
{
    ++ins;
    TAIL_RETURN(ins->f(VMARGS));
}


#define CHAIN(name) TAIL_RETURN(op_ ## name(VMARGS))
#define NEXT() do { ins += immslots(imm); CHAIN(nextop); } while(0)

#define TAILFWD(nx) do { ins = nx; TAIL_RETURN(ins->f(VMARGS)); } while(0)
#define FAIL(e) do { vm->err = (e); CHAIN(rer); } while(0)
#define FORWARD(a) do { imm += (a); CHAIN(nextop); } while(0)

#define LOCAL(i) (&sbase[i])

