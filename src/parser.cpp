#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "hlir.h"
#include "strings.h"


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

Val Parser::makestr(const char *s, const char *end)
{
    Str q = strpool.put(s, end - s + 1);
    return Val(q);
}


const Parser::ParseRule Parser::Rules[] =
{
    // operators
    { Lexer::TOK_LPAREN, &Parser::grouping, NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_EXCL  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_QQM,    &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY  },
    { Lexer::TOK_DOTDOT, &Parser::unaryRange,&Parser::binaryRange,&Parser::postfixRange, Parser::PREC_UNARY  },

    // control structures
    //{ Lexer::TOK_IF,     &Parser::conditional,NULL,          NULL,   Parser::PREC_NONE  },
    //{ Lexer::TOK_FOR,    &Parser::forloop,  NULL,            NULL,   Parser::PREC_NONE  },
    //{ Lexer::TOK_WHILE,  &Parser::whileloop,NULL,            NULL,   Parser::PREC_NONE  },

    // values
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_IDENT,  &Parser::_identPrev,NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_NIL,    &Parser::nil,      NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_TRUE ,  &Parser::btrue,    NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_FALSE , &Parser::bfalse,   NULL,            NULL,             Parser::PREC_NONE  },
    //{ Lexer::TOK_DOLLAR, &Parser::valblock, NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_LCUR,   &Parser::tablecons,NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_LSQ,    &Parser::arraycons,NULL,            NULL,             Parser::PREC_NONE  },

    { Lexer::TOK_E_ERROR,NULL,              NULL,            NULL,             Parser::PREC_NONE, }
};

const Parser::ParseRule * Parser::GetRule(Lexer::TokenType tok)
{
    for(size_t i = 0; Rules[i].tok != Lexer::TOK_E_ERROR; ++i)
        if(Rules[i].tok == tok)
            return &Rules[i];

    return NULL;
}

Parser::Parser(Lexer* lex, const char *fn, const GaAlloc& ga, StringPool& strpool)
    : GaAlloc(ga)
    , hlir(NULL), strpool(strpool), _lex(lex), _fn(fn), hadError(false), panic(false)
{
    curtok.tt = Lexer::TOK_E_ERROR;
    prevtok.tt = Lexer::TOK_E_ERROR;
    lookahead.tt = Lexer::TOK_E_UNDEF;

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
    HLNode *ret = NULL;
    HLNode *exp;
    switch(curtok.tt)
    {
        case Lexer::TOK_CONTINUE:
            advance();
            ret = hlir->continu();
            break;

        case Lexer::TOK_BREAK:
            advance();
            ret = hlir->brk();
            break;

        case Lexer::TOK_FOR:
            advance();
            ret = forloop();
            break;

        case Lexer::TOK_IF:
            advance();
            ret = conditional();
            break;

        case Lexer::TOK_RETURN:
            advance();
            ret = hlir->retn();
            ret->u.retn.what = _exprlist();
            break;

        case Lexer::TOK_LCUR:
            advance();
            ret = block();
            break;

        case Lexer::TOK_LPAREN: // expression as statement -> same as single identifier
            advance();
            exp = expr();
            goto asexpr;

        case Lexer::TOK_IDENT:
        {
            exp = ident();
            switch(curtok.tt)
            {
                case Lexer::TOK_CASSIGN: // x =
                case Lexer::TOK_MASSIGN: // x :=
                {
                    ret = hlir->assignment();
                    HLNode *idents = hlir->list();
                    idents->u.list.add(exp, *this);
                    ret->u.assignment.dstlist = idents;
                    ret->u.assignment.vallist = _assignmentWithPrefix();
                }
                break;

                case Lexer::TOK_COMMA: // x, y =
                {
                    ret = hlir->assignment();
                    HLNode *idents = hlir->list();
                    idents->u.list.add(exp, *this);
                    advance();
                    do
                        idents->u.list.add(ident(), *this);
                    while(tryeat(Lexer::TOK_COMMA));
                    ret->u.assignment.dstlist = idents;
                    ret->u.assignment.vallist = _assignmentWithPrefix();
                }
                break;

                default: // single identifier, probably followed by ( to make a function call
                asexpr:
                {
                    ret = hlir->fncall();
                    ret->u.fncall.func = exp;
                    ret->u.fncall.paramlist = _paramlist();
                }
            }
        }
        break;

        default:
            errorAtCurrent("can't parse as statement");
    }

    tryeat(Lexer::TOK_SEMICOLON);

    return ret;
}

HLNode *Parser::parsePrecedence(Prec p)
{
    const ParseRule *rule = GetRule(curtok.tt);
    if(!rule)
        return NULL;

    if(!rule->prefix)
    {
        errorAt(curtok, "Expected expression");
        return NULL;
    }

    advance();

    const Context ctx = p <= PREC_ASSIGNMENT ? CTX_ASSIGN : CTX_DEFAULT;
    HLNode *node = (this->*(rule->prefix))(ctx);

    for(;;)
    {
        rule = GetRule(curtok.tt);

        // No rule? Stop here, it's probably the end of the expr
        if(!rule || p > rule->precedence)
            break;

        advance();

        node = (this->*(rule->infix))(ctx, rule, node);
        assert(node);
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
    // 'if' was consumed
    HLNode *node = hlir->conditional();
    eat(Lexer::TOK_LPAREN);
    node->u.conditional.condition = expr();
    eat(Lexer::TOK_RPAREN);
    node->u.conditional.ifblock = stmt();
    node->u.conditional.elseblock = tryeat(Lexer::TOK_ELSE) ? stmt() : NULL;
    return node;
}

HLNode* Parser::forloop()
{
    // 'for' was consumed
    HLNode *node = hlir->forloop();
    eat(Lexer::TOK_LPAREN);
    node->u.forloop.iter = decl(); // must decl new var(s) for the loop
    eat(Lexer::TOK_RPAREN);
    node->u.forloop.body = stmt();
    return node;
}

HLNode* Parser::whileloop()
{
    // 'while' was consumed
    HLNode *node = hlir->whileloop();
    eat(Lexer::TOK_LPAREN);
    node->u.whileloop.cond = expr();
    eat(Lexer::TOK_RPAREN);
    node->u.whileloop.body = stmt();
    return node;
}

HLNode* Parser::_assignmentTarget()
{
    if(match(Lexer::TOK_IDENT))
        return ident();

    //HLNode *prefix = prefixexpr();
    assert(0);
    return NULL; // FIXME

}

HLNode* Parser::_assignment(bool isconst)
{
    // '='´or ':=' was consumed
    HLNode *list = hlir->list();
    do
        list->u.list.add(expr(), *this);
    while(tryeat(Lexer::TOK_COMMA));
    return list;
}

HLNode* Parser::_assignmentWithPrefix()
{
    bool mut = tryeat(Lexer::TOK_MASSIGN);
    if(!mut)
        eat(Lexer::TOK_CASSIGN);
    return _assignment(!mut);
}

// var x, y
// var x:int, y:float
// int x, blah y, T z
// int a, float z
// T a, b, Q c, d, e
// int x, y, var z,q
HLNode* Parser::_decllist()
{
    // curtok is ident or var
    HLNode * const node = hlir->list();

    bool isvar = curtok.tt == Lexer::TOK_VAR;
    HLNode *lasttype = NULL;
    HLNode *first = NULL;
    HLNode *second = NULL;

    switch(curtok.tt)
    {
        case Lexer::TOK_IDENT:
            first = ident();
            lasttype = first;
            break;
        case Lexer::TOK_VAR:
            advance();
            break;
        default:
            errorAtCurrent("expected type identifier or 'var'");
            return NULL;
    }
    goto begin;

    for(;;)
    {
        second = NULL;

        // one decl done, need a comma to start another
        if(!tryeat(Lexer::TOK_COMMA))
            break;

        // either 'var' or a type name. This switches context.
        // In 'var' context, types are deduced, in 'C' context, they need to be known.
        if(tryeat(Lexer::TOK_VAR))
        {
            isvar = true;
            lasttype = NULL;
            first = NULL;
        }
        else
        {
            first = ident(); // type name, if present
            if(!match(Lexer::TOK_IDENT))
            {
                second = first;
                first = NULL;
            }
        }
begin:

        HLNode * const def = hlir->vardef();

        if(!second)
            second = ident(); // var name

        if(isvar) // 'var x'
        {
            lasttype = NULL;
            assert(!first);
            def->u.vardef.ident = second; // always var name
            if(tryeat(Lexer::TOK_COLON))
                def->u.vardef.type = expr();
        }
        else // C-style decl
        {
            if(first)
            {
                def->u.vardef.type = first;
                def->u.vardef.ident = second;
                lasttype = first;
            }
            else
            {
                def->u.vardef.type = lasttype;
                def->u.vardef.ident = second;
            }
        }

        node->u.list.add(def, *this);
    }

    return node;
}

HLNode* Parser::_paramlist()
{
    eat(Lexer::TOK_LPAREN);
    HLNode *ret = _exprlist();
    eat(Lexer::TOK_RPAREN);
    return ret;
}

HLNode* Parser::_exprlist()
{
    HLNode *node = hlir->list();
    do
        node->u.list.add(expr(), *this);
    while(tryeat(Lexer::TOK_COMMA));
    return node;
}

// var i ('var' + ident...)
// int i (2 identifiers)
HLNode* Parser::trydecl()
{
    if(match(Lexer::TOK_VAR) || match(Lexer::TOK_IDENT))
    {
        lookAhead();
        if(lookahead.tt == Lexer::TOK_IDENT)
        {
            HLNode *node = ensure(hlir->vardecllist());
            if(node)
            {
                node->u.vardecllist.decllist = _decllist();
                node->u.vardecllist.vallist = _assignmentWithPrefix();
            }
            return node;
        }
    }

    return NULL;
}

HLNode* Parser::decl()
{
    HLNode *node = trydecl();
    if(!node)
        error("Expected declaration");
    return node;
}

HLNode* Parser::declOrStmt()
{
    if(HLNode *decl = trydecl())
        return decl;

    return stmt();
}

HLNode* Parser::block()
{
    /*HLNode *node = hlir->list();
    for(;;)
    {
        if(prevtok.tt == Lexer::TOK_E_ERROR)
            break;

        if(tryeat(Lexer::TOK_RCUR))
            break;

        HLNode *stmt = declOrStmt();
        node->u.list.add(stmt, *this);
    }
    return node;*/

    HLNode *node = stmtlist(Lexer::TOK_RCUR);
    eat(Lexer::TOK_RCUR);
    return node;
}

HLNode* Parser::prefixexpr(Context ctx)
{
    return tryeat(Lexer::TOK_LPAREN)
        ? grouping(ctx)
        : ident();
}

HLNode* Parser::primexpr(Context ctx)
{
    HLNode *prefix = prefixexpr(ctx);


    return prefix;
}

HLNode* Parser::litnum(Context ctx)
{
    return emitConstant(makenum(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::litstr(Context ctx)
{
    return emitConstant(makestr(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::btrue(Context ctx)
{
    return emitConstant(true);
}

HLNode* Parser::bfalse(Context ctx)
{
    return emitConstant(false);
}


HLNode* Parser::ident()
{
    HLNode *node = _ident(curtok);
    advance();
    return node;
}

HLNode* Parser::_identPrev(Context ctx)
{
    return _ident(prevtok);
}

HLNode *Parser::grouping(Context ctx)
{
    // ( was eaten
    HLNode *u = expr();
    eat(Lexer::TOK_RPAREN);
    return u;
}

HLNode *Parser::unary(Context ctx)
{
    const ParseRule *rule = GetRule(prevtok.tt);
    HLNode *rhs = parsePrecedence(PREC_UNARY);

    HLNode *node = ensure(hlir->unary());
    if(node)
    {
        node->u.unary.tok = rule->tok;
        node->u.unary.rhs = rhs;
    }

    return node;
}

HLNode * Parser::binary(Context ctx, const Parser::ParseRule *rule, HLNode *lhs)
{
    HLNode *rhs = parsePrecedence((Prec)(rule->precedence + 1));

    if(!rhs && rule->postfix)
        return (this->*(rule->postfix))(ctx, rule, lhs);

    HLNode *node = ensure(hlir->binary());
    if(node)
    {
        node->u.binary.tok = rule->tok;
        node->u.binary.rhs = rhs;
        node->u.binary.lhs = lhs;
    }
    return node;
}

HLNode* Parser::ensure(HLNode* node)
{
    if(!node)
        outOfMemory();
    return node;
}

void Parser::advance()
{
    prevtok = curtok;

    if(lookahead.tt != Lexer::TOK_E_UNDEF)
    {
        curtok = lookahead;
        lookahead.tt = Lexer::TOK_E_UNDEF;
        return;
    }

    for(;;)
    {
        curtok = _lex->next();
        if(curtok.tt != Lexer::TOK_E_ERROR)
            break;
        errorAt(curtok, curtok.u.err);
    }
}

void Parser::lookAhead()
{
    assert(lookahead.tt == Lexer::TOK_E_UNDEF);
    lookahead = _lex->next();
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

void Parser::eatmatching(Lexer::TokenType tt, char opening, unsigned linebegin)
{
    if(curtok.tt == tt)
    {
        advance();
        return;
    }

    char buf[64];
    sprintf(&buf[0], "Expected to close %c in line %u", opening, linebegin);
    errorAtCurrent(&buf[0]);
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

HLNode *Parser::nil(Context ctx)
{
    HLNode *node = hlir->constantValue();
    node->u.constant.val.type = PRIMTYPE_NIL;
    return node;
}

HLNode* Parser::tablecons(Context ctx)
{
    // { was just eaten while parsing an expr
    const unsigned beginline = prevtok.line;

    HLNode *node = hlir->list();
    node->type = HLNODE_TABLECONS;

    do
    {
        if(match(Lexer::TOK_RCUR))
            break;

        HLNode *key = NULL;

        if(tryeat(Lexer::TOK_LSQ)) // [expr] = ...
        {
            key = expr();
            eat(Lexer::TOK_RSQ);
            eat(Lexer::TOK_CASSIGN);
        }
        else if(curtok.tt == Lexer::TOK_IDENT)
        {
            // either key=...
            // or single var lookup as expr
            lookAhead();

            if(lookahead.tt == Lexer::TOK_CASSIGN)
            {
                key = ident(); // key=...
                eat(Lexer::TOK_CASSIGN);
            }
            // else it's a single var lookup; handle as expr
        }

        node->u.list.add(key, *this);
        node->u.list.add(expr(), *this);
    }
    while(tryeat(Lexer::TOK_COMMA) || tryeat(Lexer::TOK_SEMICOLON));

    eatmatching(Lexer::TOK_RCUR, '{', beginline);

    return node;
}

HLNode* Parser::arraycons(Context ctx)
{
    // [ was just eaten while parsing an expr
    const unsigned beginline = prevtok.line;

    HLNode *node = hlir->list();
    node->type = HLNODE_ARRAYCONS;

    do
    {
        if(match(Lexer::TOK_RSQ))
            break;

        node->u.list.add(expr(), *this);
    }
    while(tryeat(Lexer::TOK_COMMA) || tryeat(Lexer::TOK_SEMICOLON));

    eatmatching(Lexer::TOK_RSQ, '[', beginline);

    return node;
}

HLNode* Parser::_ident(const Lexer::Token& tok)
{
    if(tok.tt != Lexer::TOK_IDENT)
    {
        errorAt(tok, "expected identifier");
        return NULL;
    }

    HLNode *node = hlir->ident();
    Str s = strpool.put(tok.begin, tok.u.len);
    node->u.ident.nameStrId = s.id;
    node->u.ident.len = s.len;
    return node;
}

// ..n
HLNode* Parser::unaryRange(Context ctx)
{
    HLNode *node = unary(ctx);
    if(node)
    {
        assert(node->type == HLNODE_UNARY);
        node->u.ternary.b = node->u.unary.rhs;
        node->u.ternary.a = NULL;
        node->u.ternary.c = _rangeStep();
    }
    return node;
}

// 0..
HLNode* Parser::postfixRange(Context ctx, const ParseRule *rule, HLNode* lhs)
{
    HLNode *node = ensure(hlir->ternary());
    if(node)
    {
        node->u.ternary.a = lhs;
        node->u.ternary.c = _rangeStep();
    }
    return node;
}

// 0..n
HLNode* Parser::binaryRange(Context ctx, const ParseRule *rule, HLNode* lhs)
{
    HLNode *node = binary(ctx, rule, lhs);
    if(node)
    {
        node->type = HLNODE_TERNARY;
        // a, b are the original lhs and rhs, which stay
        node->u.ternary.c = _rangeStep();
    }
    return node;
}

HLNode* Parser::_rangeStep()
{
    return tryeat(Lexer::TOK_EXCL) // TODO: not happy with this
        ? expr()
        : NULL;
}

HLNode* Parser::iterdecls()
{
    HLNode *node = hlir->list();
    do
        node->u.list.add(decl(), *this);
    while(tryeat(Lexer::TOK_SEMICOLON));

    node->type = HLNODE_ITER_DECLLIST;
    return node;
}

HLNode* Parser::iterexprs()
{
    HLNode *node = hlir->list();
    do
        node->u.list.add(expr(), *this);
    while(tryeat(Lexer::TOK_SEMICOLON));

    node->type = HLNODE_ITER_EXPRLIST;
    return node;
}

HLNode* Parser::stmtlist(Lexer::TokenType endtok)
{
    HLNode *sl = ensure(hlir->list());
    if(sl)
        while(!match(endtok))
        {
            HLNode *p = declOrStmt();
            if(p)
                sl->u.list.add(p, *this);
            else
                break;
        }
    return sl;
}

HLNode *Parser::emitConstant(const Val& v)
{
    if(v.type == PRIMTYPE_NIL)
        return NULL;

    HLNode *node = ensure(hlir->constantValue());
    if(node)
        node->u.constant.val = v;
    return node;
}

void Parser::outOfMemory()
{
    errorAtCurrent("out of memory");
}
