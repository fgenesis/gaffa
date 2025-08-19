// Threaded VM inspired by wasm3 -- https://github.com/wasm3/wasm3

#include "gavm.h"
#include "util.h"

#include <vector>

enum
{
    MINSTACK = 16
};


struct Inst;

struct VMCallFrame
{
    Val *sp;
    Val *sbase;
    const Inst *ins;
};

class Stack : public PodArray<Val>
{
public:
    GC& gc;

    inline Val *root() { return this->data(); }

    Stack(GC& gc) : gc(gc) {}
    ~Stack() { this->dealloc(gc); }

    inline Val *push(Val v)
    {
        return this->push_back(gc, v);
    }

    inline Val *alloc(size_t n)
    {
        return this->alloc_n(gc, n);
    }

    inline Val *allocz(size_t n)
    {
        Val *a = alloc(n);
        if(a)
            memset(a, 0, sizeof(Val) * n);
        return a;
    }
};

struct VM
{
    VM(GC& gc) : stk(gc), cur({}) {}
    Stack stk;
    VMCallFrame cur;
    int err;
    std::vector<VMCallFrame> callstack;
    std::vector<VmIter> iterstack;
};

// This is exposed to the C API.
// The idea is that this removes the need for manual
// index tracking, making each designated stack loction
// easily accessible via stackref[0..n)
struct StackCRef
{
    Stack * const stk;
    const size_t offs;

    FORCEINLINE StackCRef(Stack *stk, Val *where)
        : stk(stk), offs(where - stk->data())
    {
    }

    FORCEINLINE Val *operator[](size_t idx)
    {
        return &(*stk)[offs + idx];
    }

    FORCEINLINE const Val *operator[](size_t idx) const
    {
        return &(*stk)[offs + idx];
    }
};

struct StackRef : public StackCRef
{
    FORCEINLINE StackRef(Stack *stk, Val *where) : StackCRef(stk, where)
    {
    }

    FORCEINLINE Val *push(Val v)
    {
        return stk->push(v);
    }

    FORCEINLINE Val *alloc(size_t n)
    {
        return stk->alloc(n);
    }

    FORCEINLINE Val pop()
    {
        return stk->pop_back();
    }

    FORCEINLINE void popn(size_t n)
    {
        return stk->pop_n(n);
    }
};

// C leaf function; fastest to call but has some restrictions:
// - Non-variadic, max(#parameters, #retvals) must be <= MINSTACK
// - Read args from inout[0..], write return values to inout[0..]
// - Must NOT call back into the VM (no call frame is pushed)
// - Can not reallocate the VM stack
// - You need to know the number of parameters and return values,
//   and the function must be registered correctly so the VM
//   and type system know this too.
// - Stack space is limited; up to MINSTACK usable slots total
// - Can't throw errors (but can return error values)
typedef void (*LeafFunc)(VM *vm, Val inout[MINSTACK]);

// Full-fledged C function; slower to call
// - May or may not be variadic (params, return values, or both)
// - Calling back into the VM is allowed
// - Can grow the stack
// ---- C function call protocol: ----
// - Parameters are in args[0..nargs)
// - If normal return: Write return values to ret[0..n), then return n
// - If variadic return: Write regular return values to ret[0..n),
//   push extra variadic ones onto the stack, then return (n + #variadic)
// - To throw an error, write error to ret[0], then return -1
typedef int (*CFunc)(VM *vm, size_t nargs, StackCRef *ret,
    const StackCRef *args, StackRef *stack);



struct FuncInfo
{
    u32 nargs; // Minimal number. If variadic, there may be more.
    u32 nrets;
    enum Flags
    {
        VarArgs = 1 << 0,
        VarRets = 1 << 1,
    };
    u32 flags;
};

struct DebugInfo
{
    u32 linestart;
    u32 lineend;
    sref name;
     // TODO
};

struct DFunc
{
    FuncInfo info;
    // TODO: (DType: Func(Args, Ret))
    union
    {
        CFunc native;
        struct
        {
            void *vmcode; // TODO
            DebugInfo *dbg; // this is part of the vmcode but forwarded here for easier reference
        }
        gfunc;
    } u;
};


#define RETVAL(idx) (*(vals+(retidx)))


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

struct Imm_DCall
{
    Inst *ins;
    unsigned nargs;
    unsigned localidx;
};

struct Imm_LeafCall
{
    LeafFunc f;
};

struct Imm_CCall
{
    CFunc f;
    byte nargs;
    byte nret;
    byte flags;
};

static u32 slotsForU32(u32 n)
{
    return (n * sizeof(u32)) / sizeof(Inst*);
}

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

template<typename T> static FORCEINLINE const T *_imm(const Inst *ins)
{
    return reinterpret_cast<const T*>(ins + 1);
}

// Invariant: Each Inst array ends with an entry that has func=NULL,
// and gfunc holds the object that contains this instruction array.

#define VMPARAMS const Inst *ins, VM * const vm, Val *sbase, Val *sp
#define VMARGS ins, vm, sbase, sp

// Inst and OpFunc are kinda the same, but a C function typedef can't use
// itself as a function parameter
typedef const Inst* (*OpFunc)(VMPARAMS);
struct Inst
{
    OpFunc f;
};

#define VMFUNC_DEF(name) NOINLINE const Inst * op_ ## name(VMPARAMS)
#define VMFUNC_IMM(name, T) \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm); \
    VMFUNC_DEF(name) { return xop_ ## name(VMARGS, _imm<T>(ins)); } \
    static FORCEINLINE const Inst * xop_ ## name(VMPARAMS, const T *imm)

#define VMFUNC(name) VMFUNC_IMM(name, Imm_None)

static FORCEINLINE VMFUNC_DEF(curop)
{
    TAIL_RETURN(ins->f(VMARGS));
}

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

// Call stack rollback helper. Set ins, then CHAIN(rer) to return all the way and resume at ins.
static VMFUNC_DEF(rer)
{
    // sbase is only changed by function call/return
    vm->cur.sp = sp;
    return ins;
}

// TODO/IDEA: in debug codegen, insert a "line reached" opcode after each line and forward to breakpoint handler

VMFUNC(yield)
{
    vm->cur.sp = sp;
    vm->cur.ins = ins + 1; // store for resuming to next instruction
    return NULL;
}

VMFUNC_IMM(jf, Imm_u32)
{
    ins += imm->a;
    NEXT();
}

VMFUNC_IMM(jb, Imm_u32)
{
    // Backwards jumps roll back the call stack
    ins -= imm->a;
    CHAIN(rer);
}


/*
VMFUNC_IMM(rccopy, Imm_u32)
{
    const size_t k = imm->a;
    const size_t sh = k & 0x1f;
    jc <<= sh;
    jc |= k >> 5u;
    NEXT();
}

VMFUNC_IMM(ccall, Imm_Cfunc)
{
    const unsigned nargs = imm->nargs;
    sp -= nargs;
    const int nret = imm->cfunc(nargs, sp, vm);
    if(nret < 0)
        FAIL(nret);
    sp += nret;
    NEXT();
}

VMFUNC_IMM(tailcall, Imm_Call)
{
    vm->returns.push_back(ins + 1);
    sp -= imm->nargs;
    return imm->ins;
}

VMFUNC_IMM(call, Imm_Call)
{
    vm->returns.push_back(ins + 1);
    sp -= imm->nargs;
    ins = imm->
    CHAIN(rer);
}


*/


/* ------

Function call semantics and layout:

Rules:

sbase[] is  the stack base of the function's call frame (const)
sp[] is the current stack top (moved as the function allocates stack)

Given a function f():

locals[] = sbase + f.numargs + f.

R return values are assigned to sbase[0...R) before returning.

So we end up with this generic layout:

[ ReturnSlots | Args | (Varargs) | Locals | VarRets ]


1) A function with #args and #rets known:

    x, y, z = f(a, b)

    [x, y, z| a, b| ...locals...]

2) A function with #args known, and variadic returns:

    x, y, z, ... = f(a, b)

    [x, y, z| a, b| ...local...|..vreturns...]
            >~~~~~~~cut~~~~~~~~< (This is a known constant)
    After shifting by RETV:

    [x, y, z, ...vreturns...]

3) A function with varargs, and known #rets:

    x, y, z = f(a, b, ...)

    [x, y, z|...varargs...|...locals...]

4) A function with both varargs and varrets:

    x, y, z, ... = f(a, b, ...)

    [x, y, z|...varargs...|...locals...|...vreturns...]
            >~~~~~~~~~~~cut~~~~~~~~~~~~< (#locals is known, #varargs is not!)
    After shifting by RETV:
    [x, y, z, ...vreturns...]

Observations:
- Args is always a contiguous array
- Return values must be shifted if variadic
- After shifting, return values are contiguous
- 

*/

// Call a nonvariadic C leaf function -- no stack fixup necessary
// Assumes params are already on top of the stack and the stack is large enough
VMFUNC_IMM(leafcall, Imm_LeafCall)
{
    imm->f(vm, sp); // this writes to return slots
    NEXT();
}

VMFUNC_IMM(ccall, Imm_CCall)
{
    const tsize totalstack = MINSTACK + imm->nargs + imm->nret;
    const size_t stackBaseOffs = sp - sbase;
    sp = vm->stk.alloc(totalstack);
    // TODO: when we realloc, fixup all stack frames in the VM??
    StackCRef rets(&vm->stk, sp);
    StackCRef args(&vm->stk, sp + imm->nret); // args follow after return slots
    StackRef stk(&vm->stk, sp + imm->nargs + imm->nret);
    int r = imm->f(vm, imm->nargs, &rets, &args, &stk); // this writes to return slots
    if(r < 0)
    {
        assert(false); // TODO: throw error
        return NULL;
    }

    assert(r == imm->nret
        || ((imm->flags & FuncInfo::VarRets) && r >= imm->nret));

    stk.popn(totalret);
    NEXT();
}

// The imm param here is just for type inference and to get the correst number of slots
template<typename Imm>
static FORCEINLINE void pushret(VMPARAMS, const Imm *imm)
{
    VMCallFrame f;
    f.ins = ins + immslots(imm)
    f.sp = sp;
    f.sbase = sbase;
    vm->returns.push_back(f);
}

static FORCEINLINE const Inst *doreturn(VMPARAMS, tsize nret)
{
    VMCallFrame f = vm->callstack.back();
    vm->callstack.pop_back();
    sp = f.sp + nret; // The function left some things on the stack
    ins = f.ins;
    sbase = f.sbase;
    CHAIN(curop); // A return directly continues with the loaded ins
}

// single return without return value
VMFUNC(ret0)
{
    return doreturn(VMARGS, 0);
}

VMFUNC_IMM(ret1, Imm_u32)
{
    sbase[0] = LOCAL(imm->a);
    return doreturn(VMARGS, 1);
}

VMFUNC_IMM(ret2, Imm_2xu32)
{
    sbase[0] = LOCAL(imm->a);
    sbase[1] = LOCAL(imm->b);
    return doreturn(VMARGS, 2);
}

VMFUNC_IMM(ret3, Imm_3xu32)
{
    sbase[0] = LOCAL(imm->a);
    sbase[1] = LOCAL(imm->b);
    sbase[2] = LOCAL(imm->c);
    return doreturn(VMARGS, 3);
}

VMFUNC_IMM(ret4, Imm_4xu32)
{
    sbase[0] = LOCAL(imm->a);
    sbase[1] = LOCAL(imm->b);
    sbase[2] = LOCAL(imm->c);
    sbase[3] = LOCAL(imm->d);
    return doreturn(VMARGS, 4);
}


// Variable size! The good news is that we don't need to know how long it is
// since a return is a return and there's no NEXT().
// The first immediate value is the count, and may be 0. Then that many u32 follow.
VMFUNC_IMM(retn, Imm_u32)
{
    const u32 n = imm->a; // how many return slots to fill
    const u32 * const p = &imm->a + 1; // locals indices array start
    for(u32 i = 0; i < n; ++i)
        sp[i] = LOCAL(p[i]);

    return doreturn(VMARGS, n);
}

// Variadic return helper
// sbase[] layout:
// example for n = 4, m = the number of locals in the function (which is known)
// <---- n ---><--- gap ------------><--- vn ------------------------------------------------------>
// [0][1][2][3][.params..][.locals..][extra stuff on the stack to be used as variadic return values]
// Is moved to that it looks like:
// <----- n + vn -------->
// [0][1][2][3][extra....]
static FORCEINLINE const Inst *vreturn(VMPARAMS, tsize n, tsize gap)
{
    // Number of values on the stack (those that are returned variadically)
    const u32 vn = (sp - sbase) - n + gap; // total size of stack, minus return value slots

    // Close the gap
    memmove(&sbase[n], &sbase[n + gap], vn * sizeof(*sp));

    // n regular return values plus some extra ones
    return doreturn(VMARGS, n + vn);
}


VMFUNC_IMM(retv, Imm_2xu32)
{
    const u32 n = imm->a; // regular return values
    const u32 * const p = &imm->b + 1;

    // Put locals into return slots
    for(u32 i = 0; i < n; ++i)
        sp[i] = LOCAL(p[i]);

    return vreturn(VMARGS, n, imm->b);
}



VMFUNC_IMM(loadkui32, Imm_2xu32)
{
    Val *slot = LOCAL(imm->a);
    *slot = imm->b;
    NEXT();
}

static VmIter *newiter(VM *vm)
{
    vm->iterstack.emplace_back();
    return &vm->iterstack.back();
}

static uint iter_adv_ui_forward(ValU& v, VmIter& it)
{
    const uint i = v.u.ui + it.u.numeric.step.ui;
    v.u.ui = i;
    return i < it.u.numeric.end.ui; // FIXME
}
static uint iter_adv_ui_backward(ValU& v, VmIter& it)
{
    const uint i = v.u.ui - it.u.numeric.step.ui;
    v.u.ui = i;
    return i > it.u.numeric.end.ui; // FIXME
}
static uint iter_init_ui(ValU& v, VmIter& it)
{
    v = it.u.numeric.start;
    if(it.u.numeric.step.si >= 0)
    {
        it.next = iter_adv_ui_forward;
        return v.u.ui < it.u.numeric.end.ui;
    }

    it.u.numeric.step.si = -it.u.numeric.step.si;
    it.next = iter_adv_ui_backward;
    return v.u.ui > it.u.numeric.end.ui;
}

static uint iter_adv_f_forward(ValU& v, VmIter& it)
{
    const real i = v.u.f + it.u.numeric.step.f;
    v.u.f = i;
    return i < it.u.numeric.end.f;
}
static uint iter_adv_f_backward(ValU& v, VmIter& it)
{
    const real i = v.u.f + it.u.numeric.step.f;
    v.u.f = i;
    return i > it.u.numeric.end.f; // FIXME
}
static uint iter_init_f(ValU& v, VmIter& it)
{
    v = it.u.numeric.start;
    if(it.u.numeric.step.f >= 0)
    {
        it.next = iter_adv_f_forward;
        return v.u.f < it.u.numeric.end.f;
    }

    it.next = iter_adv_f_backward;
    return v.u.f > it.u.numeric.end.f;
}

// This is for when we know all values have the correct type
static VmIter *setupiter(VM *vm, const Val *sp, const Imm_3xu32 *imm)
{
    VmIter *it = newiter(vm);
    it->u.numeric.start = sp[imm->a];
    it->u.numeric.end = sp[imm->b].u;
    it->u.numeric.step = sp[imm->c].u;
    return it;
}

VMFUNC_IMM(iter1_ui, Imm_3xu32)
{
    VmIter *it = setupiter(vm, sp, imm);
    it->next = iter_init_ui;
    NEXT();
}

VMFUNC_IMM(iter1_f, Imm_3xu32)
{
    VmIter *it = setupiter(vm, sp, imm);
    it->next = iter_init_f;
    NEXT();
}

static Inst *error()
{
    assert(false);
    return NULL;
}

static void typeerror(tsize is, tsize shouldbe)
{
    assert(false);
}

static void checktype(tsize is, tsize shouldbe)
{
    if(is != shouldbe)
        typeerror(is, shouldbe);
}

/*
// Unsafe variant where we get 3 values and need to typecheck
VMFUNC_IMM(iter1_any, Imm_3xu32)
{
    VmIter *it = setupiter(vm, sp, imm);
    const tsize t = it->u.numeric.cur.type.id;
    const tsize t2 = sp[imm->b].v.type.id;
    const tsize t3 = sp[imm->c].v.type.id;
    switch(t)
    {
        case PRIMTYPE_UINT:
        case PRIMTYPE_SINT:
        case PRIMTYPE_FLOAT:
            checktype(t2, t);
            if(t == PRIMTYPE_FLOAT)
            {
                it->next = it->u.numeric.cur.u.f >= 0 ? iter_adv_f_incr : iter_adv_f_decr;
                checktype(t3, t);
            }
            else // must be some int
            {
                it->next = iter_adv_i;
                if(!(t3 == PRIMTYPE_UINT || t3 == PRIMTYPE_SINT))
                    typeerror(t3, t);
            }
            break;
        default:
            error(); // TODO NOT NUMERIC
    }
    NEXT();
}
*/

VMFUNC_IMM(iterpack, Imm_u32)
{
    // TODO: pack some iters into object

    // pop iters
    vm->iterstack.resize(vm->iterstack.size() - imm->a);
    NEXT();
}

VMFUNC_IMM(iterpop, Imm_u32)
{
    vm->iterstack.resize(vm->iterstack.size() - imm->a);
    NEXT();
}

VMFUNC_IMM(iternext, Imm_3xu32)
{
    const u32 niters = imm->a;
    const u32 firstlocal = imm->b;
    VmIter * const iters = &vm->iterstack.back() - niters + 1;
    for(u32 i = 0; i < niters; ++i)
        if(!iters[i].next(sp[firstlocal + i], iters[i]))
            NEXT();

    // Jump back
    ins -= imm->c;
    CHAIN(rer);
}

/* Iteration protocol:
for(int x = 1..5; Thing t = ...) {}

    // Push 3 iterators
    iter x
    iter y
    iter z
    jf next
loop:
    ... LOOP BODY HERE ...
    on break, goto end
next:
    iternext 3 xx ->loop
end:
    iterpop 3
*/

VMFUNC_IMM(addui, Imm_2xu32)
{
    LOCAL(imm->a)->u.ui += LOCAL(imm->b)->u.ui;
    NEXT();
}

// Simple, integer-only loop
VMFUNC_IMM(simplenext, Imm_3xu32)
{
    Val *ctr = LOCAL(imm->a);
    const uint limit = LOCAL(imm->b)->u.ui;
    if(++ctr->u.ui < limit)
    {
        ins -= imm->c;
        CHAIN(rer);
    }

    NEXT();
}

VMFUNC(halt)
{
    vm->cur.ins = NULL;
    return NULL;
}

struct Testcode
{
    Inst init0;
    Imm_2xu32 init0p;
    Inst init1;
    Imm_2xu32 init1p;
    //Inst loopinit;
    //Imm_3xu32 loopinitp;

    Inst loopjump;
    //Imm_u32 loopjumpp;
    Imm_2xu32 loopjumpp;

    Inst i0;
    Imm_2xu32 i0p;

    Inst loopnext;
    Imm_3xu32 loopnextp;

    //Inst iterpop;
    //Imm_u32 iterpopp;

    Inst halt;
};

static void runloop(VM *vm)
{
    if(const Inst *ins = vm->cur.ins)
    {
        Val * const sbase = vm->cur.sbase;
        do
        {
            // sp is saved by rer or yield; since it's passed along as
            // funcparam upon return it needs to be saved there and restored here.
            // sbase doesn't need to be changed here because it's only
            // changed by function call & return; those handle that internally.
            Val * const sp = vm->cur.sp;
            ins = op_curop(VMARGS);
            // We end up here on rer, yield, or error. Most likely a rer.
            // In that case just loop around since we got the next instruction
            // returned. More expensive handling is only necessary for ins == NULL.
        }
        while(ins);
    }

    assert(!vm->err);
}


Testcode tc;

void vmtest(GC& gc)
{
    tc.init0 = { op_loadkui32 };
    tc.init0p = { 0, 0 };
    tc.init1 = { op_loadkui32 };
    tc.init1p = { 1, 0 };
    //tc.loopinit = { op_iter1_ui };
    //tc.loopinitp = { 0, 10, 11 };

    tc.loopjump = { op_loadkui32 };
    tc.loopjumpp = { 2, 0 };
    //tc.loopjump = { op_jf };
    //tc.loopjumpp = { 2 }; // FIXME

    tc.i0 = { op_addui };
    tc.i0p = { 0, 2 };
    
    //tc.loopnext = { op_iternext };
    //tc.loopnextp = { 1, 2, 2 };

    tc.loopnext = { op_simplenext };
    tc.loopnextp = { 2, 10, 2 };
    
    //tc.iterpop = { op_iterpop };
    //tc.iterpopp = { 1 };
    tc.halt = { op_halt };

    VM vm(gc);
    vm.stk.alloc(16);
    vm.err = 0;
    vm.stk[10] = Val(uint(500) * uint(1000000) + 1);
    vm.stk[11] = Val(1u);
    vm.cur.ins = &tc.init0;
    vm.cur.sp = vm.stk.data();
    vm.cur.sbase = vm.cur.sp;

    runloop(&vm);

    printf("r0 = %zu\n", vm.stk[0].u.ui);

};