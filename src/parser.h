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
        PREC_NONE,
        PREC_ASSIGNMENT,  // =
        PREC_OR,          // or
        PREC_AND,         // and
        PREC_EQUALITY,    // == !=
        PREC_COMPARISON,  // < > <= >=
        PREC_RANGE,       // ..a  a..b  b..
        PREC_ADD,         // + -
        PREC_MUL,         // * /
        PREC_UNARY,       // ! - #
        PREC_CALL,        // . ()
        PREC_PRIMARY
    };

    Parser(Lexer *lex, const char *fn, const GaAlloc& ga, StringPool& strpool);
    HLNode *parse();
    //std::vector<Val> constants;

    HLIRBuilder *hlir;
    StringPool& strpool;

private:
    struct ParseRule;

protected:
    HLNode *grouping(); // after (
    HLNode *unary(); // after op
    HLNode *binary(const ParseRule *rule, HLNode *lhs); // after expr
    HLNode *expr();
    HLNode *stmt();

    HLNode *valblock(); // after $

    // control flow
    HLNode *conditional(); // after if
    HLNode *forloop();
    HLNode *whileloop();
    HLNode *_assignment(bool isconst);
    HLNode *_assignmentWithPrefix();
    HLNode *_decllist();


    HLNode *_exprlist();
    HLNode *trydecl();
    HLNode *decl();
    HLNode *declOrStmt();
    HLNode *block();

    HLNode *prefixexpr(); // ident | (expr)
    HLNode *primexpr(); // prefixexpr { .ident | [expr] | :ident paramlist | paramlist }

    // functions
    HLNode *_paramlist(); // (a, b, c)

    // literal values
    HLNode *litnum();
    HLNode *litstr();
    HLNode *btrue();
    HLNode *bfalse();
    HLNode *ident();
    HLNode *_identPrev();
    HLNode *nil();
    HLNode *tablecons();
    HLNode *arraycons();
    HLNode *_ident(const Lexer::Token& tok);

    HLNode *unaryRange();
    HLNode *binaryRange(const ParseRule *rule, HLNode *lhs);
    HLNode *postfixRange(const ParseRule *rule, HLNode* lhs);
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
    bool match(Lexer::TokenType tt);
    void eatmatching(Lexer::TokenType tt, char opening, unsigned linebegin);
    void errorAt(const Lexer::Token& tok, const char *msg);
    void error(const char *msg);
    void errorAtCurrent(const char *msg);
    HLNode *emitConstant(const Val& v); // anything but nil
    void outOfMemory();
    Val makestr(const char *s, const char *end);

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer::Token lookahead;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;

    typedef HLNode* (Parser::*UnaryMth)();
    typedef HLNode* (Parser::*InfixMth)(const ParseRule *rule, HLNode *prefix);

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
