#pragma once

#include "defs.h"
#include "array.h"
#include "valstore.h"

union MLNode;

// In the low-level representation, as much as possible is lowered to function calls.
// Not intended for execution in this stage, even though it would be possible to implement an interpreter for this.
// This adopts some design decisions of the actual VM to make code generation easier, but is probably not optimal
// for JITting. This is particularly obvious in the selection of jump opcodes, and the presence of C.
//
// In the MLIR -> LLIR step, the following gets lost:
// - Type infos
//   - Types are used to select functions to be called
//   - If needed, LL_TYPECHECK is emitted
// - AST & structure
//   - Loops become jumps
//   - Upvalues are tracked as such
//   - Locals are allocated as slots
//

// K: constant
// R: register
// U: upvalue
// L: label
// C: special temporary comparison result register

enum LLCmd
{
    LL_MARKER,      // debug marker
    LL_LABEL,       // L.id  (label & landing pad; jumps can only target labels)
    LL_LOADK,       // R.dst K.idx  (reg = load constant)
    LL_MOV,         // R.dst R.src  (move between regs)
    LL_SETUPVAL,    // U.dst R.src
    LL_GETUPVAL,    // R.dst U.src
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
    LL_CALL,        // K.idx R.dstbase R.argbase  (call constant)
    LL_CALLREG,     // R.idx R.dstbase R.argbase  (call indirect)
    _LL_MAX
};

struct LLIns
{
    u16 cmd;
    u16 p[3];
};

/*struct LLIns_aB
{
    u16 cmd;
    u16 a;
    u32 B;
};*/

class LLCodegen
{
public:
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

    inline Label label()                                  { return Label(this, nextlabel++); }
    inline LabelId labelhere()                            { return _label(nextlabel++); }
    inline LabelId _label(LabelId id)                     { emit(LL_LABEL, id); return id; }
    inline void loadk(Reg dst, Const c)                   { emit(LL_LOADK, c); }
    inline void mov(Reg dst, Reg src)                     { emit(LL_MOV, dst, src); }
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
    void lower(const MLNode& ml);
    void conditionAndJumpOnFail(const MLNode& ml, LabelId fail);
    void conditionAndJumpOnSuccess(const MLNode& ml, LabelId success);

    // TODO: assignment helper that emits typecheck if necessary (and picks correct mov, getupval, setupval, etc)



private:

    unsigned nextlabel;
    PodArray<LLIns> code;
    ValStore consts;
    GC& gc;

};

class LLIR
{
public:


private:

};

