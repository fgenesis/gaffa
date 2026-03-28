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
    const Inst *ins;
    Val *sbase;
    Val *sp;
    u32 vargs;
    // If sp == NULL, it's an error handler frame.
    // -> On error, write error value to sbase, then continue at ins
};

struct VmStackAlloc
{
    Val *p;
    ptrdiff_t diff;
};

// Runtime error and control codes. Must be negative.
enum RTError
{
    // Yield signals and temporaries that exit the VM loop. Can just resume afterward.
    RTE_OK_MINIYIELD          = -1, // Yield once to get out of the VM loop. No value.
    RTE_OK_LINE_REACHED       = -2, // line reached debug op. Line is in VM::debug.val.
    RTE_OK_BREAK              = -3, // debug marker. VM::debug.val is a user-defined value
    RTE_OK_RECOVERED          = -4, // Recovered from a runtime error. Original error is in VM::debug.val
    // Actual errors
    RTE_FIRST_ERROR        = -100,
    RTE_OVERFLOW           = RTE_FIRST_ERROR - 0,
    RTE_DIV_BY_ZERO        = RTE_FIRST_ERROR - 1,
    RTE_VALUE_CAST         = RTE_FIRST_ERROR - 2,
    RTE_NOT_CALLABLE       = RTE_FIRST_ERROR - 3,
    RTE_ALLOC_FAIL         = RTE_FIRST_ERROR - 4,
    RTE_DEAD_VM            = RTE_FIRST_ERROR - 5, // VM ran to completion and terminated without error
    RTE_NOT_ENOUGH_PARAMS  = RTE_FIRST_ERROR - 6,
    RTE_TOO_MANY_PARAMS    = RTE_FIRST_ERROR - 7,
    RTE_NOT_YIELDABLE      = RTE_FIRST_ERROR - 8,
};

static FORCEINLINE bool RTIsError(int e)
{
    return e <= RTE_FIRST_ERROR;
}


struct VM
{
    void init(Runtime* rt, const Inst *entry);

    Runtime *rt;
    int state; // RTError if negative, otherwise # of return values on the stack
    VMCallFrame cur; // Saved call frame when yielding
    Val *_stkbase, *_stkend;
    std::vector<VMCallFrame> callstack;
    std::vector<VmIter> iterstack;

    struct
    {
        int val; // Some RTError place a value here, eg. RTE_OK_LINE_REACHED: line number.
        const Inst *errinst;
    } debug;


    // TODO? DFunc *panic;

    // Add the returned .diff to all pointers to stack memory to move the pointer in case the stack reallocated.
    // If .p is valid, p[0..slots) is valid to access.
    VmStackAlloc stack_ensure(Val *sp, size_t slots);

    // Continue running from any interrupted state
    int run();

    // True when the VM is in a regular yield state, ie. adjusting any values in [sbase..sp]
    // or reallocating the stack is allowed.
    inline bool isYielded() const { return cur.ins && state >= 0; }

    // '... = yield(...)' is a function that takes values and returns values.
    // The body of a file is a function f(...).
    // -> Both accept any number of parameters that are made available to the called code.
    // To set the parameters passed to the script function, call prepareArgs(n) with n equal to
    // the number of parameters you want to set.
    // The returned pointer is an array where [0..n) should be set to your values. Then call run().
    Val *prepareArgs(size_t n);

    // Get a pointer to the start of the return values from a previous script run. The VM may be dead
    // (ie. ran to completion, or died due to an error) or yielded. The number of valid elements is equal to
    // the last return value of run() (equal to VM::state), if it is >= 0.
    // Otherwise this returns NULL
    const Val *getReturns();

};

struct Imm_None
{
};

struct Imm_s32
{
    s32 a;
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
struct Imm_uint
{
    uint a; // 32 or 64 bit depending on arch
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
VMFUNC_DEF(_onerror);
VMFUNC_DEF(callany);



#define CHAIN(name) TAIL_RETURN(op_ ## name(VMARGS))
#define NEXT() do { ins += immslots(imm); CHAIN(nextop); } while(0)

#define TAILFWD(nx) do { ins = nx; TAIL_RETURN(ins->f(VMARGS)); } while(0)
#define FAIL(e) do { vm->state = (e); CHAIN(_onerror); } while(0)
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

struct LocalTracker
{
    // Alloc one slot for a local variable
    u32 allocSlot();
    void freeSlot(u32);

    // Alloc contiguous array of slots, returns first index
    u32 allocSlots(u32 n);
    void freeSlots(u32 first, u32 n);

private:
    Heap<u32> _h;
    u32 _max;
    GC& gc;

    void _shorten();
};
