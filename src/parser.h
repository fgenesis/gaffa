#pragma once

#include "lex.h"
#include <vector>

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
    bool parse();
    std::vector<Val> constants;

protected:
    void grouping(); // after (
    void unary(); // after op
    void binary(); // after expr
    void value();
    void expr();
    void parsePrecedence(Prec p);


private:
    void advance();
    void eat(Lexer::TokenType tt);
    void report(const Lexer::Token& tok);
    void errorAtCurrent(const char *msg);
    void emitConstant(const Val& v);

    Lexer::Token curtok;
    Lexer::Token prevtok;
    Lexer *_lex;
    const char *_fn;
    bool hadError;
    bool panic;
};
