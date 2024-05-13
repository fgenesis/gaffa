#pragma once

#include "lex.h"
#include "gainternal.h"

class HLIRBuilder;
struct HLNode;

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
        PREC_ADD,         // + -
        PREC_MUL,         // * /
        PREC_UNARY,       // ! - #
        PREC_CALL,        // . ()
        PREC_PRIMARY
    };

    Parser(Lexer *lex, const char *fn);
    HLNode *parse();
    //std::vector<Val> constants;

    HLIRBuilder *hlir;

protected:
    HLNode *grouping(); // after (
    HLNode *unary(); // after op
    HLNode *binary(); // after expr
    HLNode *expr();
    HLNode *stmt();
    HLNode *_stmtNoAdvance();

    HLNode *valblock(); // after $

    // control flow
    HLNode *conditional(); // after if
    HLNode *forloop();
    HLNode *whileloop();
    HLNode *_assignment(bool isconst);
    HLNode *_assignmentWithPrefix();
    HLNode *_vardecllist();
    HLNode *_cdecllist();
    HLNode *_paramlist(); // after ident

    HLNode *_exprlist();
    HLNode *declOrStmt();
    HLNode *block();

    // literal values
    HLNode *litnum();
    HLNode *litstr();
    HLNode *btrue();
    HLNode *bfalse();
    HLNode *ident();
    HLNode *nil();
    HLNode *tablecons();
    HLNode *_ident(const Lexer::Token& tok);

    HLNode *iterexpr();

    HLNode *stmtlist(Lexer::TokenType endtok);

private:
    void advance();
    HLNode *parsePrecedence(Prec p);
    void eat(Lexer::TokenType tt);
    bool tryeat(Lexer::TokenType tt);
    bool match(Lexer::TokenType tt);
    void errorAt(const Lexer::Token& tok, const char *msg);
    void error(const char *msg);
    void errorAtCurrent(const char *msg);
    HLNode *emitConstant(const Val& v); // anything but nil
    void outOfMemory();

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;

    typedef HLNode* (Parser::*ParseMth)(void);

    struct ParseRule
    {
        Lexer::TokenType tok;
        ParseMth prefix;
        ParseMth infix;
        Prec precedence;
    };

    static const ParseRule Rules[];

    static const ParseRule *GetRule(Lexer::TokenType tok);
};
