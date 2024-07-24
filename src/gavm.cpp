// Threaded VM inspired by wasm3 -- https://github.com/wasm3/wasm3

#include "gavm.h"
#include "util.h"

#include <vector>


struct StackSlot
{
    uint ui;
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
};


typedef int (*CFunc)(unsigned nargs, StackSlot *sp, VM *vm);

struct CFuncDescriptor
{
    CFunc f;
    unsigned nargs;
    unsigned nret;
};

struct GFunc
{
    Inst *ins;
    unsigned nins;
    unsigned nargs;
    unsigned nret;
};


typedef const Inst * (*OpFunc)(VM *vm, StackSlot *sp, const Inst *ins, size_t jc, size_t r0);

// Invariant: Each Inst array ends with an entry that has func=NULL,
// and gfunc holds the object that contains this instruction array.
struct Inst
{
    OpFunc func;
    union
    {
        void *ptr;
        uint ui;
        sint si;
        const CFuncDescriptor *cfunc;
        const GFunc *gfunc;
    } imm;
};

#define VMFUNC(name) const Inst * op_ ## name(VM *vm, StackSlot *sp, const Inst *ins, size_t jc, size_t r0)

static FORCEINLINE VMFUNC(nextop)
{
    ++ins;
    TAIL_RETURN(ins->func(vm, sp, ins, jc, r0));
}


#define CHAIN(f) TAIL_RETURN(op_ ## f(vm, sp, ins, jc, r0))
#define NEXT() CHAIN(nextop)
#define FAIL(e) do { vm->err = (e); CHAIN(rer); } while(0)


// Call stack rollback helper. Set ins, then CHAIN(rer) to return all the way and resume at ins.
NOINLINE static VMFUNC(rer)
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

VMFUNC(jf)
{
    ins += ins->imm.si;
    NEXT();
}

VMFUNC(jb)
{
    // Backwards jumps roll back the call stack
    ins -= ins->imm.si;
    CHAIN(rer);
}

VMFUNC(rccopy)
{
    const size_t k = ins->imm.ui;
    const size_t sh = k & 0x1f;
    jc <<= sh;
    jc |= k >> 5u;
    NEXT();
}

VMFUNC(ccall)
{
    const unsigned nargs = ins->imm.cfunc->nargs;
    // TODO: check there's enough stack space for nret things
    sp -= nargs;
    const int nret = ins->imm.cfunc->f(nargs, sp, vm);
    if(nret < 0)
        FAIL(nret);
    sp += nret;
    NEXT();
}

VMFUNC(tailcall)
{
    ins = ins->imm.gfunc->ins;
    NEXT();
}

VMFUNC(gcall)
{
    vm->returns.push_back(ins + 1);
    sp -= ins->imm.gfunc->nargs;
    ins = ins->imm.gfunc->ins;
    CHAIN(rer);
}

VMFUNC(ret)
{
    sp += ins->imm.ui;
    ins = vm->returns.back();
    vm->returns.pop_back();
    NEXT();
}

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


static void runloop(VM *vm)
{
    const Inst *ins = vm->cur.ins;
    for(;;)
    {
        ins = op_nextop(vm, vm->cur.sp, ins, vm->cur.jc, vm->cur.r0);
        if(!ins)
            break;
    }
    vm->cur.ins = ins;
}
