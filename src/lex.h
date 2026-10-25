#pragma once

#include "defs.h"
#include "gaalloc.h"



class Lexer
{
public:
    enum TokenType
    {
        TOK_E_ERROR,
        TOK_E_EOF,
        TOK_E_UNDEF,
        TOK_SYNTHETIC,

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
        TOK_SLASH2X,// //
        TOK_CASSIGN,// =
        TOK_MASSIGN,// :=
        TOK_EQ,     // ==
        TOK_NEQ,    // !=
        TOK_LT,     // <
        TOK_GT,     // >
        TOK_LTE,    // <=
        TOK_GTE,    // >=
        TOK_CMP,    // <=>
        TOK_FATARROW,// =>
        TOK_QQM,    // ??
        TOK_QM,     // ?
        TOK_EXCL,   // !
        TOK_TRIDOT, // ...
        TOK_DOTDOT, // ..
        TOK_DOT,    // .
        TOK_DBLCOLON,// ::
        TOK_COLON,  // :
        TOK_COMMA,  // ,
        TOK_RARROW, // ->
        TOK_SEMICOLON,// ;
        TOK_HAT,    // ^
        TOK_TILDE,  // ~
        TOK_SHL,    // <<
        TOK_SHR,    // >>
        TOK_SINK,   // _   (exactly one value)
        TOK_WSINK,  // ___ (any number of values)
        TOK_HASH,   // #
        TOK_BITAND, // &
        TOK_BITOR,  // |
        TOK_LOGAND, // &&
        TOK_LOGOR,  // ||
        TOK_DOLLAR, // $
        TOK_CONCAT, // ++
        TOK_MOD,    // %
        // keywords
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
        TOK_AND,    // and
        TOK_OR,     // or
        TOK_LIKE,   // like
        TOK_EXPORT, // export
        TOK_EMIT,   // emit
        TOK_DEFER,  // defer
        TOK_DO,     // do
        TOK_TRY,    // try
        TOK_CATCH,  // catch
        TOK_ORELSE, // orelse
        TOK_ITERATOR,// iterator
        TOK_PRAGMA, // pragma
        // rest
        TOK_LITSTR, // "...", '...'
        TOK_LITNUM, // 123, 123.45
        TOK_IDENT,  // identifier, including _

    };

    struct Token
    {
        TokenType tt;
        unsigned line;
        const char *begin;
        const char *linebegin;
        union
        {
            unsigned len;
            const char *err;
        } u;
        unsigned column() const { return begin - linebegin; }
        bool operator==(const Token& o) const { return tt == o.tt && begin == o.begin && line == o.line; }
        inline bool operator!=(const Token& o) const { return !(*this == o); }
    };

    Lexer(GC& gc, const char *text);
    Token next();
    bool done() const { return !*_p; }
    const char *getLineBegin() const { return _linebegin; }

    static const char *GetTokenText(TokenType tt);
    static bool IsKeyword(TokenType tt);
    static bool IsReserved(TokenType tt); // keyword that is not implemented yet

    static OperatorId TokenToBinOp(TokenType tok);
    static OperatorId TokenToUnOp(TokenType tok);

    // Operator function names: "+", "prefix-", "[]=", "()", ...
    static const char *GetOperatorName(OperatorId op); // NULL if op can't be overloaded
    static OperatorId GetOperatorFromName(const char *s, size_t len); // OP_ERROR if not an operator name
    static bool IsOperatorNamePrefix(const char *s, size_t len); // true if s is the start of an operator name

    struct UnescResult
    {
        char *s; // NULL if there was nothing to unescape
        size_t len;
        const char *err; // NULL on success
    };

    UnescResult unescape(const char *s, const char *end);

private:
    const char *skipws();
    Token tok(TokenType tt, const char *where, const char *end);
    Token errtok(const char *msg, const char *skipto); // continue lexing at skipto
    Token ident(const char *where);
    Token atident(const char *where);
    Token litstr(const char *where, unsigned char term);
    Token litnum(const char *where);

    const char *_p;
    const char *_linebegin;
    unsigned _line;

    BlockListAllocator _strs; // Temporary created strings; tokens point here, so elements must not move
};


