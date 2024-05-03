#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "hlir.h"


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

static Val makestr(const char *s, const char *end)
{
    return Val(_Str(0)); // TODO
}


const Parser::ParseRule Parser::Rules[] =
{
    { Lexer::TOK_LPAREN, &Parser::grouping, NULL,            Parser::PREC_NONE,  0          },
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, Parser::PREC_ADD,   HLOP_ADD   },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, Parser::PREC_ADD,   HLOP_SUB   },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, Parser::PREC_MUL,   HLOP_MUL   },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, Parser::PREC_MUL,   HLOP_DIV   },
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            Parser::PREC_NONE,  0          },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            Parser::PREC_NONE,  0          },
    { Lexer::TOK_IDENT , &Parser::ident,    NULL,            Parser::PREC_NONE,  0          },

    { Lexer::TOK_E_ERROR,NULL,              NULL,            Parser::PREC_NONE,  0 }
};

const Parser::ParseRule * Parser::GetRule(Lexer::TokenType tok)
{
    for(size_t i = 0; Rules[i].tok != Lexer::TOK_E_ERROR; ++i)
        if(Rules[i].tok == tok)
            return &Rules[i];

    return NULL;
}

Parser::Parser(Lexer* lex, const char *fn)
    : hlir(NULL), _lex(lex), _fn(fn), hadError(false), panic(false)
{
    curtok.tt = Lexer::TOK_E_ERROR;

}

HLNode *Parser::parse()
{
    advance();
    HLNode *p = expr();
    eat(Lexer::TOK_E_EOF);

    return !hadError ? p : NULL;
}

HLNode *Parser::expr()
{
    return parsePrecedence(PREC_ASSIGNMENT);
}

HLNode *Parser::parsePrecedence(Prec p)
{
    advance();
    const ParseRule *rule = GetRule(prevtok.tt);
    if(!rule->prefix)
    {
        error("Expected expression");
        return NULL;
    }

    HLNode *u = (this->*(rule->prefix))();

    for(;;)
    {
        rule = GetRule(curtok.tt);
        if(p > rule->precedence)
            break;

        advance();

        HLNode *infixnode = (this->*(rule->infix))();
        if(!infixnode)
            return NULL;

        ((HLBinary*)infixnode)->lhs = u;
        u = infixnode;
    }
    return u;

}

HLNode* Parser::litnum()
{
    return emitConstant(makenum(curtok.begin, curtok.begin + curtok.u.len));
}

HLNode* Parser::litstr()
{
    return emitConstant(makestr(curtok.begin, curtok.begin + curtok.u.len));
}

HLNode* Parser::ident()
{
    return NULL; // TODO
}

HLNode *Parser::grouping()
{
    // ( was eaten
    HLNode *u = expr();
    eat(Lexer::TOK_RPAREN);
    return u;
}

HLNode *Parser::unary()
{
    const ParseRule *rule = GetRule(prevtok.tt);
    HLNode *rhs = parsePrecedence(PREC_UNARY);

    HLUnary *u = hlir->unary();
    if(u)
    {
        u->op = (HLOp)rule->param;
        u->rhs = rhs;
    }
    else
        outOfMemory();

    return u;
}

HLNode * Parser::binary()
{
    const ParseRule *rule = GetRule(prevtok.tt);
    HLNode *rhs = parsePrecedence((Prec)(rule->precedence + 1));

    HLBinary *u = hlir->binary();
    if(u)
    {
        u->op = (HLOp)rule->param;
        u->rhs = rhs;
    }
    else
        outOfMemory();

    // lhs is assigned by caller
    return u;
}

void Parser::advance()
{
    prevtok = curtok;

    for(;;)
    {
        curtok = _lex->next();
        if(curtok.tt != Lexer::TOK_E_ERROR)
            break;
        errorAt(curtok, curtok.u.err);
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

void Parser::errorAt(const Lexer::Token& tok, const char *msg)
{
    if(panic)
        return;

    printf("(%s:%u): %s\n", _fn, tok.line, msg);

    hadError = true;
    panic = true;
}

void Parser::error(const char* msg)
{
    errorAt(prevtok, msg);
}

void Parser::errorAtCurrent(const char* msg)
{
    errorAt(curtok, msg);
}

HLNode *Parser::emitConstant(const Val& v)
{
    HLConstantValue *u = hlir->constantValue();
    if(u)
        u->val = v;
    else
        outOfMemory();
    return u;
}

void Parser::outOfMemory()
{
    errorAtCurrent("out of memory");
}
