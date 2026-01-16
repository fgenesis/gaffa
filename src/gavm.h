#pragma once

#include "defs.h"
#include "typing.h"
#include <vector>

struct VmIter;
struct DType;
struct VM;
struct VMP;
struct Inst;
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

struct VMCallFrame
{
    Val *sp;
    Val *sbase;
    const Inst *ins;
};

struct VmStackAlloc
{
    Val *p;
    ptrdiff_t diff;
};


struct VM
{
    VM *GCNew(Runtime *rt, SymTable *env);

    Runtime *rt;
    SymTable *env; // Updated whenever a function is called that has an env
    int err;
    VMCallFrame cur;
    Val *_stkbase, *_stkend;
    std::vector<VMCallFrame> callstack;
    std::vector<VmIter> iterstack;

    const DFunc *currentFunc();

    // Add the returned .diff to all pointers to stack memory to move the pointer in case the stack reallocated.
    // If .p is valid, p[0..slots) is valid to access.
    VmStackAlloc stack_ensure(Val *sp, size_t slots);


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

#define VMPARAMS const Inst *ins, VM * const vm, Val *sbase, Val *sp
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

#define VMFUNC_MTH_IMM(name, T) \
    VMFUNC_DEF(name) { return xop_ ## name(VMARGS, _imm<T>(ins)); } \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm)

#define VMFUNC_IMM(name, T) \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm); \
    VMFUNC_MTH_IMM(name, T)

#define VMFUNC(name) VMFUNC_IMM(name, Imm_None)


// Fetch next instruction and jump to it
// Very very important that this is force-inlined!
// This is the hottest piece of code that is inlined into every single VM opcode.
static FORCEINLINE VMFUNC_DEF(nextop)
{
    ++ins;
    TAIL_RETURN(ins->f(VMARGS));
}

static FORCEINLINE VMFUNC_DEF(curop)
{
    TAIL_RETURN(ins->f(VMARGS));
}

VMFUNC_DEF(rer);
VMFUNC_DEF(onerror);



#define CHAIN(name) TAIL_RETURN(op_ ## name(VMARGS))
#define NEXT() do { ins += immslots(imm); CHAIN(nextop); } while(0)

#define TAILFWD(nx) do { ins = nx; TAIL_RETURN(ins->f(VMARGS)); } while(0)
#define FAIL(e) do { vm->err = (e); CHAIN(onerror); } while(0)
#define FORWARD(a) do { imm += (a); CHAIN(nextop); } while(0)

#define LOCAL(i) (&sbase[i])


typedef size_t (*OpGenFunc)(void *dst, const u32 *argslots);


struct OpDef
{
    OpGenFunc gen;
    u32 immbytes;
};

template<typename T>
static size_t writeInst(void *p, VMFunc f, const T& imm)
{
    assert(!((uintptr_t)p % sizeof(Inst)));
    // Packing this into a struct, ensures correct alignment and size
    struct S
    {
        Inst ins;
        T imm;
    };
    // Note: This can leave a hole in case an immediate doesn't occupy the full size of a pointer.
    // It'll stay uninited but since it's never used this shouldn't be a problem.
    ((S*)p)->ins.f = f;
    ((S*)p)->imm = imm;
    return sizeof(S);
}

enum RTError
{
    RTE_OK = 0,
    RTE_OVERFLOW = -1,
    RTE_DIV_BY_ZERO = -2,
    RTE_VALUE_CAST = -3,
    RTE_NOT_CALLABLE = -4,
    RTE_ALLOC_FAIL = -5,
};


struct LocalTracker
{
    // Alloc one slot for a local variable
    u32 allocSlot();
    void freeSlot(u32);

    // Alloc contiguous array of slots, returns first index
    u32 allocSlots(u32 n);
    void freeSlots(u32 first, u32 n);
};
