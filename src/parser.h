#pragma once

#include "lex.h"

class HLIRBuilder;
struct HLNode;

class Parser
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
        PREC_ADD,        // + -
        PREC_MUL,      // * /
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
    HLNode *parsePrecedence(Prec p);
    HLNode *litnum();
    HLNode *litstr();
    HLNode *ident();


private:
    void advance();
    void eat(Lexer::TokenType tt);
    void errorAt(const Lexer::Token& tok, const char *msg);
    void error(const char *msg);
    void errorAtCurrent(const char *msg);
    HLNode *emitConstant(const Val& v);
    void outOfMemory();

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;

    typedef HLNode* (Parser::*ParseMth)(void);
    //static const ParseMth NoMth;

    struct ParseRule
    {
        Lexer::TokenType tok;
        ParseMth prefix;
        ParseMth infix;
        Prec precedence;
        unsigned param;
    };

    static const ParseRule Rules[];

    static const ParseRule *GetRule(Lexer::TokenType tok);
};
