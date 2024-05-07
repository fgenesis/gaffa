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
            // TODO: bin, oct
        }
    }

    uint u = 0;
    do
    {
        const unsigned char c = *s;
        if(c >= '0' && c <= '9')
            u = u * 10u + (c - '0');
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
    // operators
    { Lexer::TOK_LPAREN, &Parser::grouping, NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, Parser::PREC_ADD   },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, Parser::PREC_ADD   },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, Parser::PREC_MUL   },
    { Lexer::TOK_EXCL  , &Parser::unary,    &Parser::binary, Parser::PREC_MUL   },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, Parser::PREC_MUL   },

    // control structures
    { Lexer::TOK_IF,     &Parser::conditional,NULL,          Parser::PREC_NONE  },
    { Lexer::TOK_FOR,    &Parser::forloop,  NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_WHILE,  &Parser::whileloop,NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_RETURN, &Parser::returnstmt,NULL,           Parser::PREC_NONE  },
    { Lexer::TOK_FOR,    &Parser::forloop,  NULL,            Parser::PREC_NONE  },

    // values
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_IDENT,  &Parser::ident,    NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_NIL,    &Parser::nil,      NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_TRUE ,  &Parser::btrue,    NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_FALSE , &Parser::bfalse,   NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_DOLLAR, &Parser::valblock, NULL,            Parser::PREC_NONE  },
    { Lexer::TOK_LCUR,   &Parser::tabcons,  NULL,            Parser::PREC_NONE  },

    { Lexer::TOK_E_ERROR,NULL,              NULL,            Parser::PREC_NONE, }
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

    HLNode *root = stmtlist(Lexer::TOK_E_EOF);

    return !hadError ? root : NULL;
}

HLNode *Parser::expr()
{
    return parsePrecedence(PREC_ASSIGNMENT);
}

// does not include declarations: if(x) stmt (without {})
HLNode* Parser::stmt()
{
    return nullptr;
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

    HLNode *node = (this->*(rule->prefix))();

    for(;;)
    {
        rule = GetRule(curtok.tt);

        // No rule? Stop here, it's probably the end of the expr
        if(!rule || p > rule->precedence)
            break;

        advance();

        HLNode *infixnode = (this->*(rule->infix))();
        if(!infixnode)
            return NULL;

        assert(infixnode->type == HLNODE_BINARY);
        infixnode->u.binary.lhs = node;
        node = infixnode;
    }
    return node;

}

HLNode* Parser::valblock()
{
    eat(Lexer::TOK_LCUR);
    return block();
}

HLNode* Parser::conditional()
{
    return nullptr;
}

HLNode* Parser::forloop()
{
    return nullptr;
}

HLNode* Parser::whileloop()
{
    return nullptr;
}

HLNode* Parser::assignment()
{
    return nullptr;
}

HLNode* Parser::returnstmt()
{
    return nullptr;
}

HLNode* Parser::declOrStmt()
{
    stmt();
}

HLNode* Parser::block()
{
    for(;;)
    {
        if(prevtok.tt == Lexer::TOK_E_ERROR)
            break;

        if(match(Lexer::TOK_RCUR))
            break;


        declOrStmt();
    }
}

HLNode* Parser::litnum()
{
    return emitConstant(makenum(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::litstr()
{
    return emitConstant(makestr(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::btrue()
{
    return emitConstant(true);
}

HLNode* Parser::bfalse()
{
    return emitConstant(false);
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

    HLNode *node = hlir->unary();
    if(node)
    {
        node->u.unary.tok = rule->tok;
        node->u.unary.rhs = rhs;
    }
    else
        outOfMemory();

    return node;
}

HLNode * Parser::binary()
{
    const ParseRule *rule = GetRule(prevtok.tt);
    HLNode *rhs = parsePrecedence((Prec)(rule->precedence + 1));

    HLNode *node = hlir->binary();
    if(node)
    {
        node->u.binary.tok = rule->tok;
        node->u.binary.rhs = rhs;
    }
    else
        outOfMemory();

    // lhs is assigned by caller
    return node;
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

bool Parser::tryeat(Lexer::TokenType tt)
{
  if(!match(tt))
      return false;
  advance();
  return true;
}
bool Parser::match(Lexer::TokenType tt)
{
    return curtok.tt == tt;
}

void Parser::errorAt(const Lexer::Token& tok, const char *msg)
{
    if(panic)
        return;

    printf("(%s:%u): %s (Token: %s)\n", _fn, tok.line, msg, Lexer::GetTokenText(tok.tt));

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

HLNode *Parser::nil()
{
    HLNode *node = hlir->constantValue();
    node->u.constant.val.type = PRIMTYPE_NIL;
    return node;
}

HLNode* Parser::tablecons()
{
    return nullptr;
}

HLNode* Parser::stmtlist(Lexer::TokenType endtok)
{
    HLNode *sl = hlir->stmtlist();
    if(sl)
        while(!match(endtok))
        {
            HLNode *p = declOrStmt();
            if(p)
                sl->u.stmtlist.add(p, *this);
            else
                break;
        }
    return sl;
}

HLNode *Parser::emitConstant(const Val& v)
{
    if(v.type == PRIMTYPE_NIL)
        return NULL;

    HLNode *node = hlir->constantValue();
    if(node)
        node->u.constant.val = v;
    else
        outOfMemory();
    return node;
}

void Parser::outOfMemory()
{
    errorAtCurrent("out of memory");
}
