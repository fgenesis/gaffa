#include "lex.h"


Lexer::Lexer(const char* text)
    : _p(text)
{
}

Token Lexer::next()
{
    
}

Token Lexer::tok(TokenType tt, const char *end)
{
    Token t;
    t.tt = tt;
    t.begin = _p;
    t.end = end;
    t.line = _line;
    t.col = _p - _linebegin;
    return t;
}

Token Lexer::errtok(const char *msg)
{
    Token t;
    t.tt = TOK_E_ERROR;
    t.begin = msg;
    t.end = NULL;
    t.col = _p - _linebegin;
    t.line = _line;
    return t;
}

const char* Lexer::skipws()
{
    return nullptr;
}

