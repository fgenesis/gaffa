#pragma once

#include "defs.h"


class Lexer
{
public:
    enum TokenType
    {
        TOK_E_ERROR,
        TOK_E_EOF,

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
        TOK_ASSIGN, // =
        TOK_MASSIGN,// :=
        TOK_EQ,     // ==
        TOK_NEQ,    // !=
        TOK_LT,     // <
        TOK_GT,     // >
        TOK_LTE,    // <=
        TOK_GTE,    // >=
        TOK_FATARROW,// =>
        TOK_QM,     // ?
        TOK_EXCL,   // !
        TOK_DOT,    // .
        TOK_COLON,  // :
        TOK_COMMA,  // ,
        TOK_ARROW,  // ->
        TOK_DBLCOLON,// ::
        TOK_DOTDOT, // ..
        TOK_SEMICOLON,// ;
        TOK_HAT,    // ^
        TOK_TILDE,  // ~
        TOK_SHL,    // <<
        TOK_SHR,    // >>
        TOK_SINK,   // _
        TOK_HASH,   // #
        TOK_BITAND, // &
        TOK_BITOR,  // |
        TOK_LOGAND, // &&
        TOK_LOGOR,  // ||
        TOK_DOLLAR, // $
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
        TOK_CONST,  // const
        TOK_BREAK,  // break
        TOK_RETURN, // return
        TOK_CONTINUE,// continue
        TOK_FALLTHROUGH,//fallthrough
        TOK_AND,    // and
        TOK_OR,     // or
        // rest
        TOK_LITSTR, // "...", '...'
        TOK_LITNUM, // 123, 123.45
        TOK_IDENT,  // identifier

    };

    struct Token
    {
        TokenType tt;
        const char *begin;
        union
        {
            unsigned len;
            const char *err;
        } u;
        unsigned line;
    };

    Lexer(const char *text);
    Token next();
    bool done() const { return !*_p; }

    static const char *GetTokenText(TokenType tt);

private:
    const char *skipws();
    Token tok(TokenType tt, const char *where, const char *end);
    Token errtok(const char *msg);
    Token ident(const char *where);
    Token litstr(const char *where, unsigned char term);
    Token litnum(const char *where);

    const char *_p;
    const char *_linebegin;
    unsigned _line;
};
