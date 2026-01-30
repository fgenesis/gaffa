// Threaded VM inspired by wasm3 -- https://github.com/wasm3/wasm3

#include "gavm.h"
#include "util.h"
#include "gaobj.h"
#include "gc.h"
#include "runtime.h"

struct DFunc;

#define RETVAL(idx) (*(vals+(retidx)))


struct Imm_LeafCall
{
    LeafFunc f;
    u32 baseoffs;
};

struct Imm_CCall
{
    CFunc f;
    u32 baseoffs; // HMM: This would fit in ~16 bits
    u32 nargs; // and this in 8
};

struct Imm_CCallv
{
    CFunc f;
    u32 baseoffs;
};

struct Imm_GCall
{
    Inst *entry;
    u32 baseoffs;
    u32 maxstack; // prealloc this much stack for args + locals
};


// Call stack rollback helper. Set ins, then CHAIN(rer) to return all the way and resume at ins.
// Do NOT use this as a regular op! If this appears in the instruction stream it's an endless loop with no escape.
VMFUNC_DEF(rer)
{
    // sbase is only changed by function call/return
    vm->cur.sp = sp;
    return ins;
}

// TODO/IDEA: in debug codegen, insert a "line reached" opcode after each line and forward to breakpoint handler

VMFUNC(yield)
{
    vm->cur.sbase = sbase;
    vm->cur.sp = sp;
    vm->cur.ins = ins + 1; // store for resuming to next instruction
    return NULL;
}

// Error handler helper. Set vm->err, then CHAIN(onerror) to handle error
// Do NOT use this as a regular op!
VMFUNC_DEF(onerror)
{
    assert(vm->err);
    --ins;
    CHAIN(yield);
}

VMFUNC(resume)
{
    sp = vm->cur.sp;
    sbase = vm->cur.sbase;
    TAILFWD(vm->cur.ins);
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

// Unwind is a bit of a hack -- it unwinds the stack but preserves sbase.
// (Like rer, but is always safe to use, but also slower.
// Normally sbase is not restored, but the thunked resume takes care of that.)
// Should not be needed outside of debugging.
static const Inst unwind_thunk { op_resume };
VMFUNC_DEF(unwind)
{
    vm->cur.sbase = sbase;
    vm->cur.sp = sp;
    vm->cur.ins = ins + 1; // store for resuming to next instruction
    return &unwind_thunk;
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
    const Val *oldstack = vm->_stkend;
    RTError err = imm->f(vm, sbase + imm->baseoffs); // this writes to return slots
    assert(oldstack == vm->_stkend && "Leafcalls are not supposed to change the stack in any way, this will crash");
    if(LIKELY(!err))
        NEXT();
    FAIL(err);
}

// Specialization without error check for functions that are known to never cause an error
VMFUNC_IMM(leafcall_noerr, Imm_LeafCall)
{
    const Val *oldstack = vm->_stkend;
    RTError err = imm->f(vm, sp + imm->baseoffs); // this writes to return slots
    assert(oldstack == vm->_stkend && "Leafcalls are not supposed to change the stack in any way, this will crash");
    assert(!err && "Function was marked to never cause an error, but it did");
    NEXT();
}

static FORCEINLINE void pushEH(VMPARAMS, Inst *errh, Val *errvalGoesHere)
{
    assert(sbase <= errvalGoesHere);
    VMCallFrame f;
    f.ins = errh;
    f.sp = NULL;
    f.sbase = errvalGoesHere;
    vm->callstack.push_back(f);
}

static FORCEINLINE void pushret(VMPARAMS, size_t skipslots)
{
    assert(sbase <= sp);
    VMCallFrame f;
    f.ins = ins + skipslots;
    f.sp = sp;
    f.sbase = sbase;
    vm->callstack.push_back(f);
}

// The imm param here is just for type inference and to get the correct number of slots
template<typename Imm>
static FORCEINLINE void pushret(VMPARAMS, const Imm *imm)
{
    pushret(VMARGS, immslots(imm));
}

static FORCEINLINE const Inst *doreturn(VMPARAMS, tsize nret)
{
    // Precondition: sbase[0..nret) contains return values

    sp = sbase + nret; // The function left some things on the stack
    // New sp is one past the return values

    // restore previous call frame
    VMCallFrame f;
    do
    {
        f = vm->callstack.back();
        vm->callstack.pop_back();
    }
    while(!f.sp); // Drop any error handler frames (when returning from inside try-block)

    sbase = f.sbase;
    ins = f.ins;

#ifdef _DEBUG
    CHAIN(unwind); // unwind the stack
#else
    CHAIN(curop); // A return directly continues with the loaded ins
#endif
}

static FORCEINLINE const Inst *finishCCall(VMPARAMS, CFunc f, size_t nargs)
{
    // Preconditions:
    // - original stack frame was pushed
    // - sbase was adjusted so that sbase[0] is the first parameter to the function

    int r = f(vm, nargs, sbase); // this writes to return slots and may invalidate sp, sbase
    if(r < 0)
        FAIL(r);

    // sbase[0..] now contains return values

    assert(!vm->err && "VM error is set but was not indicated by the C function's return value");

    // Restore valid sp, sbase (the call frame popped here was fixed up if stack was realloc'd in f())
    return doreturn(VMARGS, r);
}

VMFUNC_IMM(callc, Imm_CCall)
{
    pushret(VMARGS, imm);
    sbase += imm->baseoffs;
    return finishCCall(VMARGS, imm->f, imm->nargs);
}

VMFUNC_IMM(callcv, Imm_CCallv)
{
    pushret(VMARGS, imm);
    sbase += imm->baseoffs;
    assert(sbase <= sp);
    size_t nargs = sp - sbase;
    return finishCCall(VMARGS, imm->f, nargs);
}

// Notes for Gcalls:
// These are the only ones that need to reserve stack before a new function is entered.
// (leafcalls can't grow the stack, Ccalls realloc stack themselves if they need it)
// nargs is only used when makegap is > 0
static FORCEINLINE const Inst *finishGCall(VMPARAMS, size_t maxstack, size_t makegap, size_t nargs)
{
    // Preconditions:
    // - original stack frame was pushed
    // - sbase was adjusted so that sbase[0] is the first parameter to the function
    // - ins is set to the first instruction of the function

    assert(sbase <= sp);

    // Ensure that the function has enough stack space
    VmStackAlloc sa = vm->stack_ensure(sp, maxstack);
    if(UNLIKELY(!sa.p))
        CHAIN(onerror);
    sp = sa.p;
    sbase += sa.diff;

    if(makegap)
    {
        // Make a gap for the regular return values.
        // This must be done when parameters are not adjusted to account for return slots.
        memmove(sbase + makegap, sbase, nargs * sizeof(*sbase));
        sbase += makegap;
        sp += makegap;
    }

    // sbase[0..] is now return slots, sp[0..] are args


#ifdef _DEBUG
    CHAIN(unwind); // unwind the stack
#else
    CHAIN(curop); // continue directly
#endif
}

VMFUNC_IMM(callg, Imm_GCall)
{
    pushret(VMARGS, imm);
    sbase += imm->baseoffs;
    ins = imm->entry;
    return finishGCall(VMARGS, imm->maxstack, 0, 0);
}

// Slower, dynamic call that can call anything that is callable
VMFUNC_IMM(callany, Imm_3xu32)
{
    Val *f = LOCAL(imm->a);
    Val *fbase = sbase + imm->b;
    assert(fbase <= sp);
    const u32 nargs = imm->c ? imm->c - 1 : sp - fbase;

    DFunc *df = f->asFunc();
    if(LIKELY(df))
    {
        // TODO: Could keep track of the minimal required number of params (excl. optionals at the end),
        // and fill those with nil values if not passed
        if(nargs < df->info.nargs)
            FAIL(RTE_NOT_ENOUGH_PARAMS);

        switch(df->info.flags & FuncInfo::FuncTypeMask)
        {
            case FuncInfo::LFunc:
            {
                RTError err = df->u.lfunc(vm, fbase);
                if(UNLIKELY(err))
                    FAIL(err);
                NEXT();
            }

            case FuncInfo::GFunc:
                pushret(VMARGS, imm);
                sbase = fbase;
                ins = df->u.gfunc.chunk->begin();
                return finishGCall(VMARGS, df->u.gfunc.maxstack, df->info.nrets, nargs);

            case FuncInfo::CFunc:
                pushret(VMARGS, imm);
                sbase = fbase;
                return finishCCall(VMARGS, df->u.cfunc, nargs);

            default:
                unreachable();
        }
    }

    // TODO: if it's an object with overloaded call op, call that

    FAIL(RTE_NOT_CALLABLE);
}


size_t emitMovesToSlots(void *dst, const u32 *argslots)
{
    return 0;
}


// argslots[]: indices of locals where the parameters for this function are stored
size_t emitFuncCall(void *dst, const DFunc *df, const u32 *argslots, u32 nargs, bool variadicArgs)
{
    if(df->opdef) // Best case: Function can be implemented with a VM op
        return df->opdef->gen(dst, argslots);

    // TODO: emit move args into param slots
    u32 baseoffs = 0; // FIXME: ??

    switch(df->info.flags & FuncInfo::FuncTypeMask)
    {
        case FuncInfo::LFunc:
        {
            Imm_LeafCall imm { df->u.lfunc, baseoffs };
            // Use slightly more efficient forwarder in case the function is known to not throw errors
            VMFunc op = (df->info.flags & FuncInfo::NoError) ? op_leafcall_noerr : op_leafcall;
            return writeInst(dst, op, imm);
        }

        case FuncInfo::CFunc:
            if(!variadicArgs)
            {
                Imm_CCall imm { df->u.cfunc, baseoffs, nargs };
                return writeInst(dst, op_callc, imm);
            }
            else
            {
                Imm_CCallv imm { df->u.cfunc, baseoffs };
                return writeInst(dst, op_callcv, imm);
            }

        case FuncInfo::GFunc:
        {
            Imm_GCall imm { df->u.gfunc.chunk->begin(), baseoffs, df->u.gfunc.maxstack };
            return writeInst(dst, op_callg, imm);
        }

        default: unreachable();
    }
}

size_t emitCall(void *dst, const Val *obj, const u32 *argslots, u32 nargs, bool variadicArgs)
{
    if(const DFunc *df = obj->asFunc())
        return emitFuncCall(dst, df, argslots, nargs, variadicArgs);

    // TODO: if it's an object with overloaded call op, try that

    assert(false);
    return NULL;
}

// Fixed number of return values, which have already been moved into return slots
VMFUNC_IMM(ret, Imm_u32)
{
    return doreturn(VMARGS, imm->a);
}

// single return without return value
/*
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
*/

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

// Variadic return, imm->a are in regular slots (sbase[0..]), the rest is left of sp
VMFUNC_IMM(retv, Imm_2xu32)
{
    return vreturn(VMARGS, imm->a, imm->b);
}

/*
VMFUNC_IMM(retnv, Imm_2xu32)
{
    const u32 n = imm->a; // regular return values
    const u32 * const p = &imm->b + 1;

    // Put locals into return slots
    for(u32 i = 0; i < n; ++i)
        sp[i] = *LOCAL(p[i]);

    return vreturn(VMARGS, n, imm->b);
}
*/


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

static const char *vmerrstr(int rte)
{
    switch(rte)
    {
        case RTE_OVERFLOW:            return "over/underflow";
        case RTE_DIV_BY_ZERO:         return "division by zero";
        case RTE_VALUE_CAST:          return "incompatible value cast";
        case RTE_NOT_CALLABLE:        return "not callable";
        case RTE_ALLOC_FAIL:          return "allocation failure";
        case RTE_DEAD_VM:             return "dead VM";
        case RTE_NOT_ENOUGH_PARAMS:   return "not enough parameters";
        case RTE_TOO_MANY_PARAMS:     return "too many parameters";
    }

    return "unknown error";
}

static Val vmerrval(VM *vm, int err)
{
    if(err >= RTE_OK)
        return Val();

    Str s = vm->rt->sp.put(vmerrstr(err));
    Val ret(s);
    ret.type = PRIMTYPE_ERROR;
    return ret;
}

static int runloop(VM *vm)
{
    const Inst *ins = vm->cur.ins;
    if(!ins)
        return RTE_DEAD_VM;

run:
    vm->err = 0;
    {
        do
        {
            // sp is saved by rer or yield; since it's passed along as
            // funcparam upon return it needs to be saved there and restored here.
            Val * const sbase = vm->cur.sbase; // This is changed by calls, return, and unwind
            Val * const sp = vm->cur.sp;
            ins = op_curop(VMARGS);
            // We end up here on rer, yield, or error. Most likely a rer.
            // In that case just loop around since we got the next instruction
            // returned. More expensive handling is only necessary for ins == NULL.
        }
        while(ins);
    }

    // Yield or error.
    int err = vm->err;

    if(err)
    {
        // Error. Try to recover...
        VMCallFrame f = vm->callstack.back();
        if(!f.sp) // error handler frame? (Don't go up the callstack. Exceptions don't cross function boundaries!)
        {
            vm->callstack.pop_back();
            ins = f.ins;

            if(f.sbase) // Should produce error value?
            {
                assert(vm->cur.sbase <= f.sbase);
                f.sbase[0] = vmerrval(vm, err);
            }
            goto run; // Recovered! Continue with error handler
        }
    }

    // This VM is now either yielded, or failed.
    // - vm->cur.ins:
    //   - If failed: points to the vm op that failed (Trying to continue running the VM will result in the exact same error)
    //   - If yielded, points to the instruction to continue
    // - The callstack is intact
    // - Local variables are in their slots on the stack
    return err;
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
    vm->_stkbase = NULL;
    vm->_stkend = NULL;
    if(!vm->stack_ensure(NULL, 64).p)
        vm = NULL;
    return vm;
}

VmStackAlloc VM::stack_ensure(Val *sp, size_t n)
{
    assert(_stkbase <= sp && sp <= _stkend);
    size_t remain = _stkend - sp;
    n += MINSTACK; // Always ensure minimum extra stack size
    if(LIKELY(remain >= n))
    {
        VmStackAlloc ret { sp, 0 };
        return ret;
    }

    // Need to reallocate. This is slow and will hopefully not happen often...

    const size_t oldcap = _stkend - _stkbase;
    const size_t newcap = oldcap + (n > oldcap ? n : oldcap);
    Val * const nextbase = gc_alloc_unmanaged_T<Val>(rt->gc, _stkbase, oldcap, newcap);
    if(!nextbase)
    {
        this->err = RTE_ALLOC_FAIL;
        VmStackAlloc ret { NULL, 0 };
        return ret;
    }

    memset(nextbase + oldcap, 0, oldcap * sizeof(Val));

    // Fixup pointers in the call stack
    const ptrdiff_t d = nextbase - _stkbase;
    for(size_t i = 0; i < callstack.size(); ++i)
    {
        VMCallFrame &f = callstack[i];
        // For error handler frames, sbase may be NULL, and sp is NULL
        if(f.sbase)
            f.sbase += d;
        if(f.sp)
            f.sp += d;
    }
    _stkbase = nextbase;
    _stkend = nextbase + newcap;

    VmStackAlloc ret { sp + d, d };
    return ret;
}

