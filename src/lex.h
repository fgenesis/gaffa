#pragma once

#include "defs.h"

enum TokenType
{
    TOK_E_ERROR,
    TOK_E_EOF,
    TOK_E_WAIT,

    TOK_LPAREN, // (
    TOK_RPAREN, // )
    TOK_LSQ,    // [
    TOK_RSQ,    // ]
    TOK_LCUR,   // {
    TOK_RCUR,   // }
    TOK_PLUS,   // +
    TOK_MINUS,  // -
    TOK_STAR,   // *
    TOK_SLASH,  // /
    TOK_EQ,     // =
    TOK_EQEQ,   // ==
    TOK_LT,     // <
    TOK_GT,     // >
    TOK_LTE,    // <=
    TOK_GTE,    // >=
    TOK_FATARROW,// =>
    TOK_QM,     // ?
    TOK_DOT,    // .
    TOK_COLON,  // :
    TOK_ARROW,  // ->
    TOK_DBLCOLON,// ::
    TOK_DOTDOT, // ..
    TOK_SEMICOLON,// ;
    // keywords
    TOK_NIL,    // nil
    TOK_TRUE,   // true
    TOK_FALSE,  // false
    TOK_IF,     // if
    TOK_ELSE,   // else
    TOK_FUNC,   // func
    TOK_VAR,    // var
    TOK_FOR,    // for
    TOK_WHILE,  // while
    // rest
    TOK_LITVAL, // "...", 123, 123.45,
    TOK_IDENT,

};

struct Token
{
    TokenType tt;
    const char *begin;
    const char *end;
    unsigned line;
    unsigned col;
};


class Lexer
{
public:
    Lexer(const char *text);
    Token next();
    Token prev();
    bool done() const { return !*_p; }
    Token tok(TokenType tt, const char *cur);
    Token errtok(const char *msg);
    const char *skipws();


private:
    const char *_p;
    const char *_linebegin;
    unsigned _line;
};
