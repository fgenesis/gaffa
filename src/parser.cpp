#include "parser.h"
#include <stdlib.h>
#include <stdio.h>


static Val parsefloat(uint intpart, const char *s, const char *end)
{
    char *pend;
    real f = strtod(s, &pend); // FIXME: use grisu2
    if(pend != end)
        return Val();
    f += real(intpart);
    return f;
}

static Val parsehex(const char *s, const char *end)
{
    uint u = 0;
    for(unsigned char c; s < end && (c = *s++);)
    {
        u *= 16u;
        if(c >= '0' && c <= '9')
            u += c - '0';
        else if(c >= 'a' && c <= 'f')
            u += 10 + c - 'a';
        else if(c >= 'A' && c <= 'F')
            u += 10 + c - 'A';
        else
            return Val();
    }
    return u;
}

static Val makenum(const char *s, const char *end)
{
    size_t len = end - s;
    bool hex = false;
    if(len > 2)
    {
        if(s[0] == '0')
        {
            if(s[1] == 'x' || s[1] == 'X')
                return parsehex(&s[2], end);
        }
    }

    uint u = 0;
    do
    {
        const unsigned char c = *s;
        if(c >= '0' && c <= '9')
            u *= 10u;
        else if(c == '.')
            return parsefloat(u, s, end);
        else
            return Val();
        ++s;
    }
    while(s < end);

    return u;
}

Parser::Parser(Lexer* lex, const char *fn)
    : _lex(lex), _fn(fn), hadError(false), panic(false)
{
    curtok.tt = Lexer::TOK_E_ERROR;

}

bool Parser::parse()
{
    for(;;)
    {

    }
    return !hadError;
}

void Parser::value()
{
    switch(curtok.tt)
    {
        case Lexer::TOK_LITNUM:
        {
            Val num = makenum(curtok.begin, curtok.begin + curtok.u.len);
            emitConstant(num);
            return;
        }

        case Lexer::TOK_IDENT:
        {

        }
    }
}

void Parser::expr()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

void Parser::parsePrecedence(Prec p)
{
}

void Parser::grouping()
{
    // ( was eaten
    expr();
    eat(Lexer::TOK_RPAREN);
}

void Parser::unary()
{
    const Lexer::TokenType prev = prevtok.tt;
    parsePrecedence(PREC_UNARY);
    switch(prev)
    {
        case Lexer::TOK_MINUS: // TODO
        case Lexer::TOK_TILDE:
        case Lexer::TOK_EXCL:
        case Lexer::TOK_PLUS:
    }
}

void Parser::binary()
{
}

void Parser::advance()
{
    prevtok = curtok;

    for(;;)
    {
        curtok = _lex->next();
        if(curtok.tt != Lexer::TOK_E_ERROR)
            break;
        report(curtok);
    }
}

void Parser::eat(Lexer::TokenType tt)
{
    if(curtok.tt == tt)
    {
        advance();
        return;
    }

    char buf[64];
    sprintf(&buf[0], "Expected '%s', got '%s", Lexer::GetTokenText(tt), Lexer::GetTokenText(curtok.tt));
    errorAtCurrent(&buf[0]);
}

void Parser::report(const Lexer::Token& tok)
{
    if(panic)
        return;

    hadError = true;
    panic = true;
}

void Parser::errorAtCurrent(const char* msg)
{
    printf("(%s:%u): %s\n", _fn, curtok.line, msg);
    // TODO
}

void Parser::emitConstant(const Val& v)
{
}
