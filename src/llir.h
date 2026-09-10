#pragma once

#include "defs.h"
#include "array.h"
#include "valstore.h"

class Runtime;

union MLNode;

// In the low-level representation, as much as possible is lowered to function calls.
// Not intended for execution in this stage, even though it would be possible to implement an interpreter for this.
// This adopts some design decisions of the actual VM to make code generation easier, but is probably not optimal
// for JITting. This is particularly obvious in the selection of jump opcodes, and the presence of the C register.
// (Then again, most architectures have processor flags in some form, which is kinda the same)
//
// In the MLIR -> LLIR step, the following gets lost:
// - Type infos
//   - Types are used to select functions to be called
//   - If needed, LL_TYPECHECK is emitted
// - AST & structure
//   - Loops become jumps
//   - Upvalues are tracked as such
//   - Locals are allocated as slots
// Notes:
// - This is call-heavy. Most operations are implemented as calls.
//   - Math ops
//   - Indexing, lookup
//   - Actual function calls
//   - ... see enum OperatorId
// - A "call" here is not an actual function call, but a call to a DFunc object
//   that will resolve to a VM opcode or a regular function call in the next codegen stage.
// - LLIR is generated per-funtion, and does not cross function boundaries


// K: constant
// R: register
// U: upvalue
// L: label
// C: special temporary comparison result register
// S: code section

enum LLCmd
{
    LL_MARKER,      // debug marker
    LL_LABEL,       // L.id  (label & landing pad; jumps can only target labels)
    LL_CALL,        // K.idx R.dstbase R.argbase  (call constant)
    LL_CALLREG,     // R.idx R.dstbase R.argbase  (call indirect)
    LL_CALLUPVAL,   // U.idx R.dstbase R.argbase  (call upvalue)
    LL_LOADK,       // R.dst K.idx  (reg = load constant)
    LL_MOV,         // R.dst R.src  (move between regs)
    LL_SETUPVAL,    // U.dst R.src
    LL_GETUPVAL,    // R.dst U.src
    LL_MOVUPVAL,    // U.dst U.src
    LL_CLOSEUPVAL,  // U.idx
    LL_TYPECHECK,   // R K.typeidx
    LL_RET,         //
    LL_FORLOOP,     // L  (label to continue at)
    LL_FORBREAK,    // L  (break out of a for loop)
    LL_UNWRAP,      // R.dst R.src   (dst = *src)
    LL_TRYUNWRAP,   // R.dst R.src L.fail  (if(src => dst))
    LL_TEST,        // R (C = 1 if valid, 0 if not)
    LL_J,           // L
    LL_JZ,          // L.zero, L.nonzero  (simple jump on C, don't use for loops)
    LL_JLEG,        // L<  L=  L>  (3-way jump based on C; ok to use to implement loops)
    LL_SETC,        // R.src  (C = Sign(reg) )
    LL_GETC,        // R.dst  (reg = result of previous comparison as an int)
    LL_GETEQ,       // R.dst  (reg = C == 0)
    LL_GETNE,       // R.dst  (reg = C != 0)
    LL_GETLT,       // R.dst  (reg = C < 0)
    LL_GETGT,       // R.dst  (reg = C > 0)
    LL_GETLE,       // R.dst  (reg = C <= 0)
    LL_GETGE,       // R.dst  (reg = C >= 0)
    LL_CLOSURE,     // R.dst S.idx R.upvalbase (load section as function)
    _LL_MAX
};

struct LLIns
{
    u16 cmd;
    u16 p[3];
};

// A section of code is one function, either in its fully compiled form,
// or as a link to the original MLNode tree if full compilation wasn't possible.
// If some types are referenced as upvalues, we can only compile the
// section once the upvalues are known and have a value.
struct LLSection
{
    // If delay-expanded, this is non-NULL.
    MLIR *mlir;
    size_t startnode;

    // If everything is pre-compiled, mlir==NULL, and this is filled
    PodArray<LLIns> code;

    tsize nupvals;
    tsize nparams;
    tsize nrets;
};

class LLModule
{
    PodArray<LLSection*> sections;
};

struct LLVar
{
    enum Flags
    {
        MUTABLE       = 0x01,
        USED_AS_UPVAL = 0x02
    };
    u32 flags;
    u32 localslot;
    int upvalslot; // Default -1. If used as upval, this is >= 0
    const DType *dtype;
    const MLVar *mlvar;
};

struct LLVarRef
{
    tsize slot;
    bool upval;
};

struct LLIters
{
    void emitAdvanceAndLoop(u32 labelid); // advance iters, than loop back to labelid if the loop continues
};


// A low-level codegen operates on a single function. Any closure inside of a function spawns a new LLCodegen.
// TODO: store locals as MLVar*[]? upvals too?
//

class LLCodegen
{
public:
    LLCodegen(Runtime& rt, const MLIR& mlir);

    // Attempt to compile a function node into LLIR.
    // This will return an incomplete section if that isn't possible
    // due to upvalues that will only be present at runtime.
    // (This is only for static compilation)
    LLSection *generate(const MLNode& ml);
    
    typedef u32 Reg;
    typedef u32 Const;
    typedef u32 Upv;
    typedef u32 LabelId;
    inline void emit(LLCmd cmd)                      { emit(cmd, 0, 0, 0); }
    inline void emit(LLCmd cmd, u32 a)               { emit(cmd, a, 0, 0); }
    inline void emit(LLCmd cmd, u32 a, u32 b)        { emit(cmd, a, b, 0); }
    void emit(LLCmd cmd, u32 a, u32 b, u32 c);

    struct Label
    {
        Label(LLCodegen *gen, LabelId id) : codegen(gen), id(id) {}
        LLCodegen *codegen;
        const LabelId id;
        void here() { codegen->_label(id); codegen = NULL; }; // If this crashes, the label was already placed, dumbass
        inline operator LabelId() const { return id; }
    };

    struct FuncScopeData
    {
        u32 nextLocalSlot;
        u32 nextUpvalSlot;
    };

    inline Label label()                                  { return Label(this, nextlabel++); }
    inline LabelId labelhere()                            { return _label(nextlabel++); }
    inline LabelId _label(LabelId id)                     { emit(LL_LABEL, id); return id; }
    inline void loadk(Reg dst, Const c)                   { emit(LL_LOADK, c); }
    inline void mov(Reg dst, Reg src)                     { emit(LL_MOV, dst, src); }
    inline void movupval(Upv dst, Upv src)                { emit(LL_MOVUPVAL, dst, src); }
    inline void setupval(Upv u, Reg src)                  { emit(LL_SETUPVAL, u, src); }
    inline void getupval(Reg dst, Upv u)                  { emit(LL_GETUPVAL, dst, u); }
    inline void closeupval(Upv u)                         { emit(LL_CLOSEUPVAL, u); }
    inline void ret()                                     { emit(LL_RET); }
    inline void forbreak(LabelId to)                      { emit(LL_FORBREAK, to); }
    inline void unwrap(Reg dst, Reg src)                  { emit(LL_UNWRAP, dst, src); }
    inline void tryunwrap(Reg dst, Reg src, LabelId fail) { emit(LL_TRYUNWRAP, dst, src, fail); }
    inline void j(LabelId dst)                            { emit(LL_J, dst); }
    inline void jz(LabelId zero, LabelId nonzero)         { emit(LL_JZ, zero, nonzero); }
    inline void jleg(LabelId less, LabelId eq, LabelId gt){ emit(LL_JLEG, less, eq, gt); }
    inline void setc(Reg src)                             { emit(LL_SETC, src); }
    inline void getc(Reg dst)                             { emit(LL_GETC, dst); }
    inline void geteq(Reg dst)                            { emit(LL_GETEQ, dst); }
    inline void getne(Reg dst)                            { emit(LL_GETNE, dst); }
    inline void getlt(Reg dst)                            { emit(LL_GETLT, dst); }
    inline void getgt(Reg dst)                            { emit(LL_GETGT, dst); }
    inline void getle(Reg dst)                            { emit(LL_GETLE, dst); }
    inline void getge(Reg dst)                            { emit(LL_GETGE, dst); }

    // helpers
    void load(Reg dst, const Val& v); // put in constant table if necessary, emit loadk()
    void conditionAndJumpOnFail(const MLNode& ml, LabelId fail);
    //void conditionAndJumpOnSuccess(const MLNode& ml, LabelId success);

    LLVar *allocVars(size_t n);

    // TODO: assignment helper that emits typecheck if necessary (and picks correct mov, getupval, setupval, etc)

    struct Scope
    {
        Scope(LLCodegen *gen, u32 reason)
            : prevTotalLocals(gen->vars.size()), reason(reason), endlabel(0), prevscope(gen->funcscope)
        {}

        void close(LLCodegen *gen);
        LabelId getEndLabel(LLCodegen *gen) { if(!endlabel) endlabel = gen->nextlabel++; return endlabel; }

        //const tsize prevNumLocals;
        const u32 reason; // MLCmd
        LabelId endlabel; // 0 if unused
        tsize prevTotalLocals;
        FuncScopeData prevscope;
    };


private:

    Scope *pushScope(u32 reason);
    void popScope();
    void closeUpvalsUntil(tsize idx);

    void emitAssignment(tsize dst, tsize src); // does the correct sequence for locals/upvals or mixed
    LLVarRef getVarRef(tsize id) const;
    void loadConstant(u32 idx, Val c);
    u32 getVarIdxFromMLIdx(u32 mlvar) const;

    int _initUpvalues(const Val *upvals, size_t nupvals);
    int _lower_scopedExplicit(const MLNode& ml, u32 reason);
    int _lower_scoped(const MLNode& ml);
    int _lower_inner(const MLNode& ml);
    int _lower_expr(const MLNode& ml, size_t dstidx);
    LLIters _lower_iters(const MLNode& ml);
    GC& gc();

    LabelId nextlabel;

    // These are set back to 0 whenever we enter a function, and restored when the function scope is left
    FuncScopeData funcscope;

    PodArray<LLIns> code;
    PodArray<LLVar> vars;
    PodArray<LLVar> upvals;
    PodArray<Scope> scopes;
    PodArray<u32> mlvar2idx;
    ValStore consts;
    const MLIR& mlir;

    Runtime& _rt;
};
