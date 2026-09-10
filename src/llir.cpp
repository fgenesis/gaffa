#include "llir.h"
#include <assert.h>
#include "mlir.h"
#include "runtime.h"

LLCodegen::LLCodegen(Runtime& rt, const MLIR& mlir)
    : nextlabel(0), consts(rt.gc), mlir(mlir), _rt(rt)
{
}

enum ScopeExt // Values are anything outside the MLCmd range
{
    SCOPE_ROOTBLOCK = 0x10000,
    SCOPE_IFBODY,
    SCOPE_ELSEBODY,
    SCOPE_WHILEBODY,
    SCOPE_FORBODY,
};

void LLCodegen::emit(LLCmd cmd, u32 a, u32 b, u32 c)
{
    LLIns *z = code.alloc_n(gc(), 1); // TODO: handle OOM
    z->cmd = cmd;
    z->p[0] = a;
    z->p[1] = b;
    z->p[2] = c;
    // TODO: handle overflow (refuse codegen?)
    assert(z->p[0] == a);
    assert(z->p[1] == b);
    assert(z->p[2] == c);
}

void LLCodegen::load(Reg dst, const Val& v)
{
    tsize a = consts.put(v);
    loadk(dst, a);
}

LLSection *LLCodegen::generate(const MLNode& ml)
{
    assert(ml.m.cmd == ML_FUNC);
    code.clear();

    int err = 0;

    labelhere(); // Initial unused label so that we can simply check for LabelId != 0
    pushScope(SCOPE_ROOTBLOCK);
    err = _lower_inner(ml);
    popScope();

    LLSection *sec = gc_new_unmanaged_T<LLSection>(gc());
    GA_PLACEMENT_NEW(sec) LLSection;
    sec->code.move(this->code);

    return sec;
}

LLCodegen::Scope* LLCodegen::pushScope(u32 reason)
{
    Scope *s = scopes.alloc_n(gc(), 1);
    if(s)
        GA_PLACEMENT_NEW(s) Scope(this, reason);
    return s;
}

void LLCodegen::popScope()
{
    scopes.pop_back().close(this);
}

LLVarRef LLCodegen::getVarRef(tsize id) const
{
    LLVarRef ret = { vars[id].localslot, false };

    for(size_t i = scopes.size(); i --> 0; )
    {
        const Scope& s = scopes[i];
        if(s.reason == ML_FUNC)
        {
            ret.upval = id < s.prevNumLocals;
            ret.slot = vars[id].upvalslot;
            assert(vars[id].upvalslot >= 0);
            break;
        }
    }

    return ret;
}

void LLCodegen::emitAssignment(tsize dst, tsize src)
{
    const LLVarRef dstref = getVarRef(dst);
    const LLVarRef srcref = getVarRef(src);
    if(dstref.upval && srcref.upval)
        movupval(dstref.slot, srcref.slot);
}

void LLCodegen::closeUpvalsUntil(tsize idx)
{
    const size_t n = vars.size();
    assert(idx <= n);

    if(idx < n)
    {
        // Close upvals, backwards
        for(size_t i = n; i --> idx; )
            if(vars[i].flags & LLVar::USED_AS_UPVAL)
            {
                assert(vars[i].upvalslot >= 0);
                closeupval(vars[i].upvalslot);
            }

        vars.resize(gc, idx);
    }
}

void LLCodegen::_lower_scoped(const MLNode& ml)
{
    pushScope(ml.m.cmd);
    _lower_inner(ml);
    popScope();
}

int LLCodegen::_initUpvalues(const Val* upvals, size_t nupvals)
{

}

void LLCodegen::_lower_scopedExplicit(const MLNode& ml, u32 reason)
{
    pushScope(reason);
    _lower_inner(ml);
    popScope();
}

int LLCodegen::_lower_inner(const MLNode& ml)
{
    const size_t nch = ml.m.nch;
    const MLNode *ch = nch ? ml.firstChild() : NULL;
    switch((MLCmd)ml.m.cmd)
    {
        case ML_IFELSE: assert(nch == 3);
        {
            Label Lelse = label();
            Label Lend = label();
            conditionAndJumpOnFail(ch[0], Lelse);
            _lower_scopedExplicit(ch[1], SCOPE_IFBODY);
            j(Lend);
            Lelse.here();
            _lower_scopedExplicit(ch[2], SCOPE_ELSEBODY);
            Lend.here();
        }
        break;

        case ML_WHILE: assert(nch == 2);
        {
            Label check = label();
            Label after = label();
            check.here();
            conditionAndJumpOnFail(ch[0], after);
            _lower_scopedExplicit(ch[1], SCOPE_WHILEBODY);
            j(check);
            after.here();
        }
        break;

        // TODO? DOWHILE
        /*{
            LabelId Lbegin = labelhere();
            lower(ch[0]);
            conditionAndJumpOnSuccess(ch[1], Lbegin);
        }
        break;*/

        case ML_FOR: assert(nch == 2);
        {
            Label fornext = label();
            Label forbody = label();

            Scope *bodyscope = pushScope(SCOPE_FORBODY);

            // iterator decls -> this creates locals to store the iterator(s)
            LLIters iters = _lower_iters(ch[0]);
            // (Since one iterator can occupy multiple locals but they are unnamed and there's no way to access them,
            // they won't be flagged as upvalues. So a close over them does nothing.)

            j(fornext);
            forbody.here();
            _lower_inner(ch[1]); // body. likely to allocate more local slots.

            // At the end of the loop, all locals used as upvalues must be closed.
            // The next loop iteration "recreates" loop variables. If a loop variable was captured as an upvalue,
            // the closure holding it will retain the value it had in the loop when it was captured.
            // NB: Loop variables are not mutable, but they may be referenced as const upvals.
            popScope();

            // FIXME: make sure the vars are still acessible in the iterators!

            fornext.here();
            iters.emitAdvanceAndLoop(forbody);
        }
        break;

        case ML_DECL: assert(nch == 2);
        {
            if(scopes.back().reason == SCOPE_FORBODY)
            {
                // Declaring iterator
                assert(false); // TODO
            }
            else // Declaring normal local variables
            {
                const size_t n = ch[0].numchildren();
                const MLNode *types = ch[0].firstChild();
                const MLNode *exprs = ch[1].firstChild();
                const size_t firstMLVar = ml.m.p[0];
                const MLVar *mv = &mlir.vars[firstMLVar];

                size_t varStartIdx = vars.size();
                LLVar * const v = allocVars(n);

                if(mlvar2idx.size() < firstMLVar + n)
                    mlvar2idx.resize(gc, firstMLVar + n);

                for(size_t i = 0; i < n; ++i)
                {
                    if(mv[i].isMutable()) // Not sure if we actually need this; copy flag to be sure
                        v[i].flags |= LLVar::MUTABLE;

                    mlvar2idx[firstMLVar + i] = varStartIdx + i;
                }

                const size_t nexpr = ch[1].numchildren();
                for(size_t i = 0; i < nexpr; ++i)
                {
                    size_t used = _lower_expr(ch[1], varStartIdx);
                    // TODO: insert typechecks
                    assert(used);
                    varStartIdx += used;
                }
                assert(varStartIdx <= vars.size());
            }
        }
        break;

        case ML_ASSIGN: assert(nch == 2);
        {
            // TODO: first assign to temporaries, then move to dst slots
            //emitAssignment(
        }
        break;



    }

    return 0;
}

void LLCodegen::loadConstant(u32 idx, Val c)
{
    tsize cid = consts.put(c);
    loadk(vars[idx].localslot, cid);
}

u32 LLCodegen::getVarIdxFromMLIdx(u32 mlvar) const
{
    return mlvar2idx[mlvar];
}

int LLCodegen::_lower_expr(const MLNode & ml, size_t dstidx)
{
    assert(false); // TODO

    switch((MLCmd)ml.m.cmd)
    {
        case _ML_VAL: loadConstant(dstidx, ml.val); return 1;
        case ML_VAR: emitAssignment(dstidx, getVarIdxFromMLIdx(ml.m.p[0]));
    }

    return 0;
}

LLIters LLCodegen::_lower_iters(const MLNode& ml)
{
    return LLIters();
}

void LLCodegen::conditionAndJumpOnFail(const MLNode& ml, LabelId fail)
{
}

/*void LLCodegen::conditionAndJumpOnSuccess(const MLNode& ml, LabelId success)
{
}*/

LLVar* LLCodegen::allocVars(size_t n)
{
    LLVar *v = vars.alloc_n(gc, n); // TODO: handle alloc fail
    for(size_t i = 0; i < n; ++i)
    {
        v[i].flags = 0;
        v[i].localslot = funcscope.nextLocalSlot + i;
        v[i].upvalslot = -1;
        v[i].dtype = NULL;
    }
    funcscope.nextLocalSlot += n;
    return v;

}

void LLCodegen::Scope::close(LLCodegen *gen)
{
    if(endlabel)
        gen->_label(endlabel);

    gen->closeUpvalsUntil(prevTotalLocals);

    if(reason == ML_FUNC)
        gen->funcscope = prevscope;
}

void LLIters::emitAdvanceAndLoop(u32 labelid)
{



}