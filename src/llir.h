#pragma once

#include "defs.h"
#include "array.h"

// In the low-level representation, as much as possible is lowered to function calls, including:
// - The usual operators
// - Unwrapping an optional
// - Try inwrap optional


// C: constant
// R: register
// U: upvalue
// L: label

enum LLCmd
{
    LL_LABEL,       //
    LL_LOADK,       // R.dst C.idx
    LL_MOV,         // R.dst R.src
    LL_RET,         //
    LL_SETUPVAL,    // U.dst R.src
    LL_GETUPVAL,    // R.dst U.src
    LL_CLOSEUPVAL,  // U.idx
    LL_FOR,         // slots L.loopstart L.loopend
    LL_FORN2,       // R.base L.loopstart L.loopend (base+0: start, base+1: end)
    LL_FORN3,       // R.base L.loopstart L.loopend (base+0: start, base+1: end, base+2: step)
    LL_FORBREAK,    // L
    LL_J,           // L
    LL_JZ,          // L
    LL_JLEG,        // L<  L=  L>
    LL_GETC,        // R.dst
    LL_GETEQ,       // R.dst
    LL_GETNE,       // R.dst
    LL_GETLT,       // R.dst
    LL_GETGT,       // R.dst
    LL_GETLE,       // R.dst
    LL_GETGE,       // R.dst
    LL_CALLC,       // C.idx R.dstbase R.argbase
    LL_CALLR,       // R.idx R.dstbase R.argbase
    LL_CALLMC,      // C.idx R.dstbase R.argbase
    _LL_MAX
};

struct LLIns
{
    u16 cmd;
    u16 p[3];
};

class LLCodegen
{
public:
    typedef u32 Reg;
    typedef u32 Const;
    typedef u32 Upv;
    typedef u32 Label;
    inline void emit(LLCmd cmd)                      { return emit(cmd, 0, 0, 0); }
    inline void emit(LLCmd cmd, u32 a)               { return emit(cmd, a, 0, 0); }
    inline void emit(LLCmd cmd, u32 a, u32 b)        { return emit(cmd, a, b, 0); }
    inline void emit(LLCmd cmd, u32 a, u32 b, u32 c) { return emit(cmd, a, b, c); }

    unsigned label();
    void loadk(Reg dst, Const c);
    void mov(Reg dst, Reg reg);
    void ret();
    void setupval(Upv u, Reg src);
    void getupval(Reg dst, Upv u);
    void closeupval(Upv u);
    void j(Label dst);
    void jz(Label dst);
    void jleg(Label less, Label eq, Label gt);
    void getc(Reg dst);



private:

    unsigned nextlabel;
    PodArray<LLIns> ins;
    PodArray<DFunc*> funcs;

};

class LLIR
{
public:


private:

};

