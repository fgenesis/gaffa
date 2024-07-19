#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "hlir.h"
#include "strings.h"

static const Str InvalidStr {0, 0};


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

Str Parser::_tokenStr(const Lexer::Token& tok)
{
    return strpool.put(tok.begin, tok.u.len);
}

Str Parser::_identStr(const Lexer::Token& tok)
{
    if(tok.tt == Lexer::TOK_IDENT)
        return _tokenStr(tok);

    errorAt(tok, "Expected identifier");
    return InvalidStr;
}


const Parser::ParseRule Parser::Rules[] =
{
    // grouping
    { Lexer::TOK_LPAREN, &Parser::grouping, NULL,            NULL,             Parser::PREC_NONE  },

    // math operators
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_EXCL  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_SLASH2X,NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },

    // bitwise
    { Lexer::TOK_SHL   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT },
    { Lexer::TOK_SHR   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT },
    { Lexer::TOK_BITAND, NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_AND   },
    { Lexer::TOK_BITOR , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_OR    },
    { Lexer::TOK_HAT   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_XOR   },
    { Lexer::TOK_TILDE , &Parser::unary,    NULL,            NULL,             Parser::PREC_BIT_XOR   },

    // logical
    { Lexer::TOK_EQ    , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY   },
    { Lexer::TOK_NEQ   , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY   },
    { Lexer::TOK_LT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON },
    { Lexer::TOK_LTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON },
    { Lexer::TOK_GT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON },
    { Lexer::TOK_GTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON },
    { Lexer::TOK_EXCL  , NULL,              &Parser::binary, NULL,             Parser::PREC_UNARY      },
    { Lexer::TOK_LOGAND, NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_AND  },
    { Lexer::TOK_LOGOR , NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_OR   },

    // special
    { Lexer::TOK_QQM,    &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY  },
    { Lexer::TOK_FATARROW,NULL,             &Parser::binary, NULL,             Parser::PREC_UNWRAP  },
    { Lexer::TOK_HASH  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_UNWRAP  },

    // ranges
    { Lexer::TOK_DOTDOT, &Parser::unaryRange,&Parser::binaryRange,&Parser::postfixRange, Parser::PREC_UNARY  },

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
    { Lexer::TOK_FUNC,   &Parser::closurecons,NULL,            NULL,             Parser::PREC_NONE  },

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
    return parsePrecedence(PREC_NONE);
}

// does not include declarations: if(x) stmt (without {})
HLNode* Parser::stmt()
{
    HLNode *ret = NULL;
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

        case Lexer::TOK_WHILE:
            advance();
            ret = whileloop();
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

        default: // assignment, function call with ignored returns
        {
            ret = suffixedexpr();
            if(curtok.tt == Lexer::TOK_COMMA || curtok.tt == Lexer::TOK_CASSIGN)
            {
                ret = _restassign(ret);
            }
            // else it's just a function / method call, already handled
        }
        break;
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

    const Context ctx = CTX_DEFAULT;
    HLNode *node = (this->*(rule->prefix))(ctx);

    for(;;)
    {
        rule = GetRule(curtok.tt);

        // No rule? Stop here, it's probably the end of the expr
        if(!rule || p > rule->precedence || !rule->infix)
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

// named:
//   func [optional, attribs] hello(funcparams)
// closure:
// ... = func [optional, attribs] (funcparams)
HLNode* Parser::_functiondef(HLNode **pname)
{
    // 'func' was just eaten
    HLNode *f = ensure(hlir->func());
    HLNode *h = ensure(hlir->fhdr());
    if(!(f && h))
        return NULL;

    unsigned flags = 0;

    if(tryeat(Lexer::TOK_LSQ))
        _funcattribs(&flags);

    if(pname)
        *pname = ident();

    h->u.fhdr.paramlist = _funcparams();

    if(tryeat(Lexer::TOK_ARROW))
        h->u.fhdr.rettypes = _funcreturns(&flags);
    else
        flags |= FUNCFLAGS_DEDUCE_RET;

    h->u.fhdr.flags = (FuncFlags)flags;

    f->u.func.decl = h;
    f->u.func.body = functionbody();
    return f;
}

HLNode* Parser::_funcparams()
{
    eat(Lexer::TOK_LPAREN);
    const unsigned openbegin = prevtok.line;
    HLNode *node = match(Lexer::TOK_RPAREN) ? NULL : _decllist();
    eatmatching(Lexer::TOK_RPAREN, '(', openbegin);
    return node;
}


HLNode* Parser::namedfunction()
{
    // 'func' was just eaten
    //
    // make it a const variable assignment
    // ie turn this:
    //   func hello() {...}
    // into this:
    //   var hello = func() {...}
    // with the extra property that the function knows its own identifier, to enable recursion

    HLNode *decl = ensure(hlir->autodecl());
    if(decl)
        decl->u.autodecl.value = _functiondef(&decl->u.autodecl.ident);
    return decl;
}

HLNode* Parser::closurecons(Context ctx)
{
    return _functiondef(NULL);
}

HLNode* Parser::functionbody()
{
    return stmt();
}

// [pure, this, that]
void Parser::_funcattribs(unsigned *pfuncflags)
{
    // '[' was just eaten
    const unsigned linebegin = prevtok.line;
    const Str pure = this->strpool.put("pure");

    while(match(Lexer::TOK_IDENT))
    {
        Str s = _identStr(curtok);
        if(pure.id && s.id == pure.id)
            *pfuncflags |= FUNCFLAGS_PURE;
        else
            errorAtCurrent("Expected function attrib: pure");

        advance();
        tryeat(Lexer::TOK_COMMA);

    }
    eatmatching(Lexer::TOK_RSQ, '[', linebegin);
}

// -> nil
// -> ...
// -> ?
// -> int, float, blah
// -> bool, int?, ...
// (nil or '?' or a list of types)
HLNode* Parser::_funcreturns(unsigned *pfuncflags)
{
    // '->' was just eaten

    if(tryeat(Lexer::TOK_NIL))
        return NULL;

    HLNode *list = ensure(hlir->list());
    if(list) for(;;)
    {
        if(match(Lexer::TOK_IDENT))
        {
            list->u.list.add(typeident(), *this);
            if(!tryeat(Lexer::TOK_COMMA))
                break;
        }
        else if(match(Lexer::TOK_QM) || tryeat(Lexer::TOK_TRIDOT))
        {
            *pfuncflags |= FUNCFLAGS_VAR_RET;
            if(match(Lexer::TOK_COMMA))
                errorAt(prevtok, "? or ... must be the last entry in a return type list");
            break;
        }
        else
        {
            errorAtCurrent("Expected type, '?' or '...'");
            break;
        }
    }
    return list;
}

// = EXPRLIST
// This can follow after:
// IDENT = ...
// EXPR.ident = ...
// EXPR[index] = ...
HLNode* Parser::_assignmentWithPrefix(HLNode *lhs)
{
    eat(Lexer::TOK_CASSIGN); // eat '='
    HLNode *node = ensure(hlir->assignment());
    if(node)
    {
        node->u.assignment.dstlist = lhs;
        node->u.assignment.vallist = _exprlist();
    }
    return node;
}

HLNode* Parser::_restassign(HLNode* firstLhs)
{
    HLNode *lhs = ensure(hlir->list());
    if(!lhs)
        return NULL;

    while(tryeat(Lexer::TOK_COMMA))
    {
        lhs->u.list.add(suffixedexpr(), *this);
    }

    return _assignmentWithPrefix(lhs);
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

HLNode* Parser::_fncall(HLNode* callee)
{
    HLNode *node = ensure(hlir->fncall());
    if(node)
    {
        node->u.fncall.callee = callee;
        node->u.fncall.paramlist = _paramlist();
    }
    return node;
}

HLNode* Parser::_methodcall(HLNode* obj)
{
    // ':' was just eaten
    // TODO: allow lookup via :[expr]()

    HLNode *node = ensure(hlir->mthcall());
    if(node)
    {
        node->u.mthcall.obj = obj;
        node->u.mthcall.mthname = ident();
        node->u.mthcall.paramlist = _paramlist();
    }
    return node;
}


HLNode* Parser::_exprlist()
{
    HLNode *node = ensure(hlir->list());
    if(node) do
        node->u.list.add(expr(), *this);
    while(tryeat(Lexer::TOK_COMMA));
    return node;
}

// var i ('var' + ident...)
// int i (2 identifiers)
HLNode* Parser::trydecl()
{
    if(match(Lexer::TOK_VAR) || match(Lexer::TOK_IDENT) || match(Lexer::TOK_FUNC))
    {
        lookAhead();
        if(lookahead.tt == Lexer::TOK_IDENT)
            return decl();
    }

    return NULL;
}

HLNode* Parser::decl()
{
    if(tryeat(Lexer::TOK_FUNC))
        return namedfunction();

    HLNode *node = ensure(hlir->vardecllist());
    if(node)
    {
        node->u.vardecllist.decllist = _decllist();
        if(tryeat(Lexer::TOK_MASSIGN))
            node->u.vardecllist.mut = true;
        else
            eat(Lexer::TOK_CASSIGN);
        node->u.vardecllist.vallist = _exprlist();
    }

    tryeat(Lexer::TOK_SEMICOLON);

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
    HLNode *node = stmtlist(Lexer::TOK_RCUR);
    eat(Lexer::TOK_RCUR);
    return node;
}

// (expr)
// ident
HLNode* Parser::primaryexpr()
{
    return tryeat(Lexer::TOK_LPAREN)
        ? grouping(CTX_DEFAULT)
        : ident();
}

/* EXPR followed by one of:
  '.' ident
  [expr]
  (args)
  :mth(args)
*/
HLNode* Parser::suffixedexpr()
{
    HLNode *node = primaryexpr();
    for(;;)
    {
        HLNode *next = NULL;
        switch(curtok.tt)
        {
            case Lexer::TOK_DOT:
                advance();
                next = hlir->index();
                next->u.index.lhs = node;
                next->u.index.nameStrId = _identStr(curtok).id;
                advance();
                break;

            case Lexer::TOK_LSQ:
                advance();
                next = hlir->index();
                next->u.index.lhs = node;
                next->u.index.expr = expr();
                eat(Lexer::TOK_RSQ);
                break;

            case Lexer::TOK_LPAREN:
                advance();
                next = _fncall(node);
                break;

            case Lexer::TOK_COLON:
                advance();
                next = _methodcall(node);
                break;

            default:
                return node;

        }
        node = next;
    }
}

/*
// ident
// prefixexpr[expr]
// prefixexpr.ident
// ie. anything that can be the target of an assignment
HLNode* Parser::holder(Context ctx)
{
    if(match(Lexer::TOK_IDENT))
        return ident();

    HLNode *ex = prefixexpr(ctx);
    HLNode *idx = NULL;

    const Lexer::TokenType tt = curtok.tt;

    switch(tt)
    {
        case Lexer::TOK_LSQ:
            idx = expr();
            break;

        case Lexer::TOK_DOT:
            idx = ident();
            break;

        default:
            errorAtCurrent("Expected identifier, '[' or '.' to follow this expression");
    }

    HLNode *node = ensure(hlir->binary());
    if(node)
    {
        node->u.binary.lhs = ex;
        node->u.binary.rhs = idx;
        node->u.binary.tok = tt;
    }
    return node;
}
*/

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

HLNode* Parser::typeident()
{
    HLNode *node = ident();
    if(tryeat(Lexer::TOK_QM))
        node->u.ident.flags = IDENTFLAGS_OPTIONAL;
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
    Str s = _tokenStr(tok);
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
