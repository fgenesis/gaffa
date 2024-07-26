#pragma once

#include "gainternal.h"
#include "lex.h"

class HLIRBuilder;
struct HLNode;
class StringPool;

class Parser : public GaAlloc
{
public:
    enum Prec
    {
        // weakest binding
        PREC_NONE,
        PREC_UNWRAP,      // =>
        PREC_OR,          // or
        PREC_AND,         // and
        PREC_LOGIC_AND,   // &&
        PREC_LOGIC_OR,    // ||
        PREC_EQUALITY,    // == !=
        PREC_COMPARISON,  // < > <= >=
        PREC_RANGE,       // ..a  a..b  b..
        PREC_CONCAT,      // ++
        PREC_BIT_OR,      // |
        PREC_BIT_XOR,     // ^
        PREC_BIT_AND,     // &
        PREC_BIT_SHIFT,   // << >>
        PREC_ADD,         // + -
        PREC_MUL,         // * /
        PREC_UNARY,       // ! - # ??
        PREC_CALL,        // . ()
        PREC_PRIMARY
        // strongest binding
    };

    enum Context
    {
        CTX_DEFAULT = 0x00,
        // TODO: inside of value block
    };

    Parser(Lexer *lex, const char *fn, const GaAlloc& ga, StringPool& strpool);
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

    HLNode *valblock(); // after $

    // control flow
    HLNode *conditional(); // after if
    HLNode *forloop();
    HLNode *whileloop();


    // functions
    HLNode *_functiondef(HLNode **pname);
    HLNode *_funcparams();
    HLNode *namedfunction();
    HLNode *closurecons(Context ctx);
    HLNode *functionbody();
    void _funcattribs(unsigned *pfuncflags);
    HLNode *_funcreturns(unsigned *pfuncflags);

    HLNode *_assignmentWithPrefix(HLNode *lhs); // = EXPR or := EXPR
    HLNode *_restassign(HLNode *firstLhs); // returns list
    HLNode *_decllist();

    HLNode *_fncall(HLNode *callee);
    HLNode *_methodcall(HLNode *obj);
    HLNode *_exprlist();
    HLNode *trydecl();
    HLNode *decl();
    HLNode *declOrStmt();
    HLNode *block();

    HLNode *fncall(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *mthcall(Context ctx, const ParseRule *rule, HLNode *lhs);


    //HLNode *prefixexpr(Context ctx); // ident | (expr)

    HLNode *primaryexpr(); // the start of any expr-as-statement
    HLNode *suffixedexpr();
    HLNode *_suffixed(HLNode *prefix);

    // prefixexpr { .ident | [expr] | :ident paramlist | paramlist }

    //HLNode *holder(Context ctx);
    // functions
    HLNode *_paramlist(); // (a, b, c)

    // literal values
    HLNode *litnum(Context ctx);
    HLNode *litstr(Context ctx);
    HLNode *btrue(Context ctx);
    HLNode *bfalse(Context ctx);
    HLNode *name(const char *whatfor);
    HLNode *ident(const char *whatfor);
    HLNode *typeident();
    HLNode *_identInExpr(Context ctx);
    HLNode *nil(Context ctx);
    HLNode *tablecons(Context ctx);
    HLNode *arraycons(Context ctx);
    HLNode *_ident(const Lexer::Token& tok, const char *whatfor);

    HLNode *unaryRange(Context ctx);
    HLNode *binaryRange(Context ctx, const ParseRule *rule, HLNode *lhs);
    HLNode *postfixRange(Context ctx, const ParseRule *rule, HLNode* lhs);
    HLNode *_rangeStep();

    HLNode *iterdecls(); // for(var a,b = ...; var c = 0..)
    HLNode *iterexprs(); // iterator(..., 0..)

    HLNode *stmtlist(Lexer::TokenType endtok);

private:
    HLNode *ensure(HLNode *node);
    void advance();
    void lookAhead();
    HLNode *parsePrecedence(Prec p);
    void eat(Lexer::TokenType tt);
    bool tryeat(Lexer::TokenType tt);
    bool match(Lexer::TokenType tt) const;
    void eatmatching(Lexer::TokenType tt, char opening, unsigned linebegin);
    void errorAt(const Lexer::Token& tok, const char *msg, const char *hint = NULL);
    void error(const char *msg, const char *hint = NULL);
    void errorAtCurrent(const char *msg, const char *hint = NULL);
    HLNode *emitConstant(const Val& v); // anything but nil
    void outOfMemory();
    Val makestr(const char *s, const char *end);
    Str _tokenStr(const Lexer::Token& tok);
    Str _identStr(const Lexer::Token& tok);
    bool _checkname(const Lexer::Token& tok, const char *whatfor);

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer::Token lookahead;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;

    typedef HLNode* (Parser::*UnaryMth)(Context ctx);
    typedef HLNode* (Parser::*InfixMth)(Context ctx, const ParseRule *rule, HLNode *prefix);

    struct ParseRule
    {
        Lexer::TokenType tok;
        UnaryMth prefix;
        InfixMth infix;
        InfixMth postfix;
        Prec precedence;
    };

    static const ParseRule Rules[];

    static const ParseRule *GetRule(Lexer::TokenType tok);
};
