#pragma once

#include "gainternal.h"
#include "lex.h"
#include "symstore.h"

class HLIRBuilder;
struct HLNode;
class StringPool;
struct GC;

class Parser
{
public:
    enum Prec
    {
        // weakest binding
        PREC_NONE,
        PREC_SPECIAL,     // => ..  (only valid in if() / for(); elsewhere they're an error)
        PREC_OR,          // or     (parse from here to stop before PREC_SPECIAL)
        PREC_AND,         // and
        PREC_LOGIC_OR,    // ||
        PREC_LOGIC_AND,   // &&
        PREC_EQUALITY,    // == !=
        PREC_COMPARISON,  // < > <= >=
        PREC_CONCAT,      // ++
        PREC_BIT_OR,      // |
        PREC_BIT_XOR,     // ^
        PREC_BIT_AND,     // &
        PREC_BIT_SHIFT,   // << >>
        PREC_ADD,         // + -
        PREC_MUL,         // * /
        PREC_UNARY,       // ! - # ?? *
        PREC_CALL,        // () . [] :: :mth() T{}
        PREC_PRIMARY
        // strongest binding
    };

    enum OpFlags
    {
        OPF_NONE,
        OPF_OVL, // Overloadable
    };

    enum Context
    {
        CTX_DEFAULT = 0x00,
        // TODO: inside of value block
    };

    enum IdentUsage // how an identifier is used
    {
        IDENT_USAGE_UNTRACKED, // just parse identifier but don't track it as a local variable
        IDENT_USAGE_DECL, // ident/variable is being declared (but not yet usable)
        IDENT_USAGE_USE, // ident/variable is used in an expression (ie. evaluated/referenced)
    };

    Parser(Lexer *lex, const char *fn, GC& gc, StringPool& strpool);
    HLNode *parse();
    //std::vector<Val> constants;

    HLIRBuilder *hlir;
    StringPool& strpool;

private:
    struct ParseRule;

protected:
    HLNode *grouping(Context ctx); // after (
    HLNode *unary(Context ctx); // after op
    HLNode *binary(Context ctx, const ParseRule *rule, HLNode *lhs); // after expr
    HLNode *expr();
    HLNode *stmt();

    HLNode *valblock(Context ctx); // after $

    // control flow
    HLNode *conditional(); // after if
    HLNode *forloop();
    HLNode *whileloop();


    // functions
    HLNode *_functiondef(HLNode **pname, HLNode **pnamespac);
    HLNode *_funcparams();
    HLNode *namedfunction();
    HLNode *closurecons(Context ctx);
    HLNode *functionbody();
    void _funcattribs(unsigned *pfuncflags);
    HLNode *_funcreturns();
    HLNode *_methodname();

    HLNode *_assignment(HLNode *firstLhs, const Lexer::Token& lhsTok); // a, b.c, d[e] = EXPRLIST
    HLNode *_assignTarget();
    HLNode *_sink(const Lexer::Token& tok);
    HLNode *_decllist(SymbolRefContext symref, bool allowVariadic);
    void _finishDecl(HLNode *decls, bool mut, bool deferred);

    HLNode *_fncall(HLNode *callee);
    HLNode *_methodcall(HLNode *obj);
    HLNode *_endcall(HLNode *call);
    HLNode *_exprlist();
    HLNode *_namespace(HLNode *ns);
    HLNode *trydecl();
    HLNode *decl();
    HLNode *declOrStmt();
    HLNode *block();

    HLNode *fncall(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *mthcall(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *dotindex(Context ctx, const ParseRule *rule, HLNode *lhs); // a.name
    HLNode *sqindex(Context ctx, const ParseRule *rule, HLNode *lhs);  // a[expr]
    HLNode *nsindex(Context ctx, const ParseRule *rule, HLNode *lhs);  // a::name
    HLNode *structcons(Context ctx, const ParseRule *rule, HLNode *lhs); // T{...}
    HLNode *andor(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *badTypeMod(Context ctx, const ParseRule *rule, HLNode *lhs); // '?' or '...' not after a type name
    HLNode *badUnwrap(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *badRangeInfix(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *badRange(Context ctx);
    HLNode *badSink(Context ctx);
    HLNode *varargs(Context ctx);

    HLNode *_export();

    HLNode *_paramlist(); // (a, b, c)

    // literal values
    HLNode *litnum(Context ctx);
    HLNode *litstr(Context ctx);
    HLNode *name(const char *whatfor);
    HLNode *nameAsLitstr(const char *whatfor);
    HLNode *ident(const char *whatfor, IdentUsage usage, SymbolRefContext symref);
    HLNode *typeident(bool allowVariadic);
    unsigned _typemods(bool allowVariadic);
    HLNode *_identInExpr(Context ctx);
    HLNode *tablecons(Context ctx);
    HLNode *arraycons(Context ctx);
    HLNode *_ident(const Lexer::Token& tok, const char *whatfor, IdentUsage usage, SymbolRefContext symref);

    HLNode *_iterdecl(); // var a,b = EXPR or RANGE
    HLNode *_iterexpr(); // EXPR or RANGE

    HLNode *stmtlist(Lexer::TokenType endtok);

private:
    HLNode *ensure(HLNode *node);
    HLNode *ensure(HLNode *node, const Lexer::Token& tok);
    void advance();
    void lookAhead();
    HLNode *parsePrecedence(Prec p);
    void eat(Lexer::TokenType tt);
    bool tryeat(Lexer::TokenType tt);
    bool match(Lexer::TokenType tt) const;
    void eatmatching(Lexer::TokenType tt, const Lexer::Token& begintok);
    void errorAt(const Lexer::Token& tok, const char *msg, const char *hint = NULL);
    void error(const char *msg, const char *hint = NULL);
    void errorAtCurrent(const char *msg, const char *hint = NULL);
    HLNode *emitConstant(const Lexer::Token& tok, const Val& v); // anything but nil or opaque
    void outOfMemory();
    Val makestr(const char *s, const char *end);
    Str _tokenStr(const Lexer::Token& tok);
    Str _identStr(const Lexer::Token& tok);
    bool _checkname(const Lexer::Token& tok, const char *whatfor);
    void _applyUsage(const Lexer::Token& tok, HLNode *node, IdentUsage usage, SymbolRefContext symref);
    void _checkAssignTarget(HLNode *node, const Lexer::Token& nodetok);
    const char *symbolname(const HLNode *node) const;
    const char *symbolname(const Symstore::Sym *sym) const;
    void _beginFunction(ScopeType scope = SCOPE_FUNCTION);
    void _endFunction();
    void _beginScope(ScopeType scope);
    void _endScope();
    void _checkClosedScope(const Symstore::Frame& f);
    void _sync(Lexer::TokenType endtok);
    HLNode *_errnode();

    // What's allowed at the current position; reset when entering a function
    struct FuncCtx
    {
        unsigned loops;     // break, continue
        unsigned valblocks; // emit
        bool variadic;      // '...'
        bool infunc;        // false at file level
    };

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer::Token lookahead;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;
    unsigned _nerrors;
    FuncCtx _fctx;
    GC& gc;
public:
    Symstore syms;
private:

    typedef HLNode* (Parser::*UnaryMth)(Context ctx);
    typedef HLNode* (Parser::*InfixMth)(Context ctx, const ParseRule *rule, HLNode *prefix);

    struct ParseRule
    {
        Lexer::TokenType tok;
        UnaryMth prefix;
        InfixMth infix;
        InfixMth postfix;
        Prec precedence;
        OpFlags opflags;
    };

    static const ParseRule Rules[];

    static const ParseRule *GetRule(Lexer::TokenType tok);
};
