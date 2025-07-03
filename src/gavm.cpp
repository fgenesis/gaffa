// Threaded VM inspired by wasm3 -- https://github.com/wasm3/wasm3

#include "gavm.h"
#include "util.h"

#include <vector>


struct StackSlot
{
    ValU v;
};

struct Inst;

struct VMReent
{
    StackSlot *sp;
    const Inst *ins;
    size_t jc;
    size_t r0;
};

struct VM
{
    VMReent cur;
    int err;
    int yield;
    std::vector<const Inst*> returns;
    std::vector<VMReent> reent;
    std::vector<VmIter> iterstack;
};

typedef int (*CFunc)(unsigned nargs, StackSlot *sp, VM *vm);


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

struct Imm_Cfunc
{
    CFunc cfunc;
    unsigned nargs;
};

struct Imm_Call
{
    Inst *ins;
    unsigned nargs;
};

struct Imm_DCall
{
    Inst *ins;
    unsigned nargs;
    unsigned localidx;
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

template<typename T> static FORCEINLINE const T *_imm(const Inst *ins)
{
    return reinterpret_cast<const T*>(ins + 1);
}

// Invariant: Each Inst array ends with an entry that has func=NULL,
// and gfunc holds the object that contains this instruction array.

#define VMPARAMS const Inst *ins, VM *vm, StackSlot *sp, size_t jc, size_t r0
#define VMARGS ins, vm, sp, jc, r0

// Inst and OpFunc are kinda the same, but a C function typedef can't use
// itself as a function parameter
typedef const Inst* (*OpFunc)(VMPARAMS);
struct Inst
{
    OpFunc f;
};

#define VMFUNC_DEF(name) const Inst * op_ ## name(VMPARAMS)
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

#define LOCAL(i) (&sp[i]) // FIXME


// Call stack rollback helper. Set ins, then CHAIN(rer) to return all the way and resume at ins.
NOINLINE static VMFUNC_DEF(rer)
{
    vm->cur.sp = sp;
    vm->cur.jc = jc;
    vm->cur.r0 = r0;
    return ins;
}

// TODO/IDEA: in debug codegen, insert a "line reached" opcode after each line and forward to breakpoint handler

VMFUNC(yield)
{
    vm->yield = 1;
    CHAIN(rer);
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

// ALL CALLS ARE TAILCALLS WTF?
// NO THIS MUST FIX STACK

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

/*
VMFUNC_IMM(call, Imm_Call)
{
    vm->returns.push_back(ins + 1);
    sp -= imm->nargs;
    ins = imm->
    CHAIN(rer);
}

VMFUNC_IMM(dcall, Imm_DCall)
{
    vm->returns.push_back(ins + 1);
    sp -= imm->nargs;
    StackSlot *f = LOCAL(imm->localidx);
    ins = imm.gfunc->ins;
    CHAIN(rer);
}

VMFUNC(ret)
{
    sp += ins->imm.ui;
    ins = vm->returns.back();
    vm->returns.pop_back();
    NEXT();
}
*/

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
        it.u.numeric.step.si = -it.u.numeric.step.si;
        it.next = iter_adv_ui_forward;
        return v.u.ui < it.u.numeric.end.ui;
    }

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
static VmIter *setupiter(VM *vm, const StackSlot *sp, const Imm_3xu32 *imm)
{
    VmIter *it = newiter(vm);
    it->u.numeric.start = sp[imm->a].v;
    it->u.numeric.end = sp[imm->b].v.u;
    it->u.numeric.step = sp[imm->c].v.u;
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
    VmIter * const iters = &vm->iterstack.back() - niters;
    for(u32 i = 0; i < niters; ++i)
        if(!iters[i].next(sp[firstlocal + i].v, iters[i]))
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


#if 0

VMFUNC(popn)
{
    sp -= ins->imm.ui;
    NEXT();
}

VMFUNC(int_addc_pop)
{
    r0 += ins->imm.ui + sp->ui;
    --sp;
    NEXT();
}

VMFUNC(jskip_any)
{
    if(jc & ins->imm.ui)
        ++ins;
    NEXT();
}

VMFUNC(jskip1)
{
    ins += (jc & 1);
    NEXT();
}

VMFUNC(pushjc)
{
    sp->ui = jc;
    ++sp;
    NEXT();
}

VMFUNC(lda)
{
    r0 = sp[ins->imm.si].ui;
    NEXT();
}

VMFUNCT(uint, add)
{
    
    NEXT();
}

#endif


static void runloop(VM *vm)
{
    const Inst *ins = vm->cur.ins;
    for(;;)
    {
        StackSlot * const sp = vm->cur.sp;
        const size_t jc = vm->cur.jc;
        const size_t r0 = vm->cur.r0;
        ins = op_nextop(VMARGS);
        if(!ins)
            break;
    }
    vm->cur.ins = ins;
}
