// Threaded VM inspired by wasm3 -- https://github.com/wasm3/wasm3

#include "gavm.h"
#include "util.h"
#include "gaobj.h"
#include "gc.h"
#include "runtime.h"

struct DFunc;


// This is exposed to the C API.
// The idea is that this removes the need for manual
// index tracking, making each designated stack loction
// easily accessible via stackref[0..n)
struct StackCRef
{
    VM * const vm;
    const size_t offs;

    FORCEINLINE StackCRef(VM *vm, Val *where)
        : vm(vm), offs(where - vm->stack_base())
    {
    }

    FORCEINLINE Val *ptr()
    {
        return &vm->stk[offs];
    }

    FORCEINLINE const Val *ptr() const
    {
        return &vm->stk[offs];
    }

    FORCEINLINE Val& operator[](size_t idx)
    {
        return vm->stk[offs + idx];
    }

    FORCEINLINE const Val& operator[](size_t idx) const
    {
        return vm->stk[offs + idx];
    }
};

struct StackRef : public StackCRef
{
    FORCEINLINE StackRef(VM *vm, Val *where) : StackCRef(vm, where)
    {
    }

    FORCEINLINE Val *push(Val v)
    {
        return vm->stack_push(v);
    }

    FORCEINLINE Val *alloc(size_t n)
    {
        return vm->stack_alloc(n);
    }

    FORCEINLINE Val pop()
    {
        return vm->stk.pop_back();
    }

    FORCEINLINE void popn(size_t n)
    {
        return vm->stk.pop_n(n);
    }
};


#define RETVAL(idx) (*(vals+(retidx)))


struct Imm_LeafCall
{
    LeafFunc f;
    u32 spmod;
};

struct Imm_CCall
{
    CFunc f;
    byte nargs;
    byte nret;
    byte flags;
};

struct Imm_GCall
{
    Inst *entry;
    u32 nrets;   // return slots
    u32 nlocals; // prealloc this much stack for args + locals
};


// Call stack rollback helper. Set ins, then CHAIN(rer) to return all the way and resume at ins.
VMFUNC_DEF(rer)
{
    // sbase is only changed by function call/return
    vm->cur.sp = sp;
    return ins;
}

// Error handler helper. Set vm->err, then CHAIN(onerror) to handle error
VMFUNC(onerror)
{
    assert(vm->err);
    // TODO: use ins to figure out where exactly we are
    return NULL;
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

locals[] = sbase + f.numargs + f.numrets

R return values are assigned to sbase[0...R) before returning.

So we end up with this generic layout:

[ ReturnSlots | Args | (Varargs) | Locals | VarRets ]


1) A function with #args and #rets known:

    x, y, z = f(a, b)

    [x, y, z| a, b| ...locals...]
    ^sbase    ^sp

2) A function with #args known, and variadic returns:

    x, y, z, ... = f(a, b)

    [x, y, z| a, b| ...local...|..vreturns...]
            >~~~~~~~cut~~~~~~~~< (This is a known constant RETV)
    ^sbase    ^sp
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
- assign return values to sbase[0..]
- args start at sp[0..]
*/


// Call a nonvariadic C leaf function -- no stack fixup necessary
// Assumes params are already on top of the stack and the stack is large enough
VMFUNC_IMM(leafcall, Imm_LeafCall)
{
    RTError err = imm->f(vm, sp + imm->spmod); // this writes to return slots
    if(LIKELY(!err))
        NEXT();
    FAIL(err);
}

// Specialization without error check for functions that are known to never cause an error
VMFUNC_IMM(leafcall_noerr, Imm_LeafCall)
{
    RTError err = imm->f(vm, sp + imm->spmod); // this writes to return slots
    assert(!err && "Function was marked to never cause an error, but it did");
    NEXT();
}

// The imm param here is just for type inference and to get the correct number of slots
template<typename Imm>
static FORCEINLINE void pushret(VMPARAMS, const Imm *imm)
{
    VMCallFrame f;
    f.ins = ins + immslots(imm);
    f.sp = sp;
    f.sbase = sbase;
    vm->callstack.push_back(f);
}

static const Inst *fullCcall(VMPARAMS, CFunc f, size_t nargs, size_t nret, u32 flags, size_t skipslots)
{
    const tsize totalstack = MINSTACK + nargs + nret;
    const size_t stackBaseOffs = sp - sbase;
    sp = vm->stack_alloc(totalstack);
    // TODO: when we realloc, fixup all stack frames in the VM??
    StackCRef rets(vm, sp);
    StackCRef args(vm, sp + nret); // args follow after return slots
    StackRef stk(vm, sp + nargs + nret);
    int r = f(vm, nargs, &rets, &args, &stk); // this writes to return slots and may invalidate sp, sbase
    if(r < 0)
    {
        assert(false); // TODO: throw error
        return NULL;
    }

    assert(r == nret
        || ((flags & FuncInfo::VarRets) && r >= nret));

    sbase = rets.ptr();
    sp = sbase + nret;

    const Val *pend = vm->stk.pend();
    if(flags & FuncInfo::VarRets)
    {
        // Move extra return values after the regular return slots

        if(const size_t nv = size_t(r) - nret)
        {
            size_t availAtEnd = pend - sp;
            assert(nv < availAtEnd);
            const Val *lastfew = pend - availAtEnd;
            memmove(sp, lastfew, nv);
            sp += nv;
        }
    }

    const size_t topop = pend - sp;
    stk.popn(topop);

    ins += skipslots + 1;
    CHAIN(rer);
}

VMFUNC_IMM(ccall, Imm_CCall)
{
    return fullCcall(VMARGS, imm->f, imm->nargs, imm->nret, imm->flags, immslots(imm));
}

VMFUNC_IMM(gcall, Imm_GCall)
{
    pushret(VMARGS, imm);
    sbase = sp + imm->nrets;
    sp = sbase + imm->nlocals;
    ins = imm->entry;
    CHAIN(curop); // Can't CHAIN(rer) here since that doesn't save sbase
}

VMFUNC_IMM(anycall, Imm_2xu32)
{
    Val *f = LOCAL(imm->a);
    const u32 nargs = imm->b;
    DFunc *df = f->asFunc();
    if(LIKELY(df))
    {
        switch(df->info.flags & FuncInfo::FuncTypeMask)
        {
            case FuncInfo::LFunc:
            {
                RTError err = df->u.lfunc(vm, sp);
                if(UNLIKELY(err))
                    FAIL(err);
                NEXT();
            }

            case FuncInfo::GFunc:
                pushret(VMARGS, imm);
                sbase = sp + df->info.nrets;
                sp = sbase + df->info.nlocals;
                ins = df->u.gfunc.chunk->begin();
                CHAIN(rer);

            case FuncInfo::CFunc:
                return fullCcall(VMARGS, df->u.cfunc, df->info.nargs, df->info.nrets, df->info.flags, immslots(imm));
        }
    }

    // TODO: if it's an object with overloaded call op, call that

    FAIL(RTE_NOT_CALLABLE);
}


size_t emitMovesToSlots(void *dst, const u32 *argslots)
{
    
}


// argslots[]: indices of locals where the parameters for this function are stored
size_t emitCall(void *dst, const DFunc *df, const u32 *argslots)
{
    if(df->opdef) // Best case: Function can be implemented with a VM op
        return df->opdef->gen(dst, argslots);

    // TODO: emit move args into param slots

    switch(df->info.flags & FuncInfo::FuncTypeMask)
    {
        case FuncInfo::LFunc:
        {
            Imm_LeafCall imm { df->u.lfunc, spmod };
            // Use slightly more efficient forwarder in case the function is known to not throw errors
            VMFunc op = (df->info.flags & FuncInfo::NoError) ? op_leafcall_noerr : op_leafcall;
            return writeInst(dst, op, imm);
        }
        
        case FuncInfo::CFunc:
        case FuncInfo::GFunc:
    
        default: unreachable();
    }

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
    sbase[0] = *LOCAL(imm->a);
    return doreturn(VMARGS, 1);
}

VMFUNC_IMM(ret2, Imm_2xu32)
{
    sbase[0] = *LOCAL(imm->a);
    sbase[1] = *LOCAL(imm->b);
    return doreturn(VMARGS, 2);
}

VMFUNC_IMM(ret3, Imm_3xu32)
{
    sbase[0] = *LOCAL(imm->a);
    sbase[1] = *LOCAL(imm->b);
    sbase[2] = *LOCAL(imm->c);
    return doreturn(VMARGS, 3);
}

VMFUNC_IMM(ret4, Imm_4xu32)
{
    sbase[0] = *LOCAL(imm->a);
    sbase[1] = *LOCAL(imm->b);
    sbase[2] = *LOCAL(imm->c);
    sbase[3] = *LOCAL(imm->d);
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
        sp[i] = *LOCAL(p[i]);

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
        sp[i] = *LOCAL(p[i]);

    return vreturn(VMARGS, n, imm->b);
}



VMFUNC_IMM(loadkui32, Imm_2xu32)
{
    Val *slot = LOCAL(imm->a);
    *slot = Val(imm->b);
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


#if 0
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

#endif


VM* VM::GCNew(Runtime* rt, SymTable* env)
{
    VM *vm = (VM*)gc_new(rt->gc, sizeof(VM), PRIMTYPE_OPAQUE);
    if(!vm)
        return NULL;
    vm->rt = rt;
    vm->env = env;
    vm->err = 0;
    vm->cur = {};
    return vm;
}

inline Val *VM::stack_push(Val v)
{
    return stk.push_back(rt->gc, v);
}

inline Val *VM::stack_alloc(size_t n)
{
    return stk.alloc_n(rt->gc, n);
}

inline Val *VM::stack_allocz(size_t n)
{
    Val *a = stack_alloc(n);
    if(a)
        memset(a, 0, sizeof(Val) * n);
    return a;
}
