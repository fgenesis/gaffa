#include "parser.h"
#include "hlir.h"
#include "strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

static const Str InvalidStr = {0, 0};


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

bool Parser::_checkname(const Lexer::Token& tok, const char *whatfor)
{
    if(tok.tt == Lexer::TOK_IDENT)
        return true;

    std::ostringstream os, hint;
    bool gothint = false;
    os << "Expected " << whatfor << " name";
    if(const char *label = Lexer::GetTokenText(tok.tt))
    {
        os << ", got '" << label << "'";

        if(Lexer::IsKeyword(tok.tt))
        {
            os << " (which is a reserved identifier)";

            if(!strcmp(whatfor, "method"))
            {
                hint << ":[\"" << label << "\"]";
                gothint = true;
            }
        }
    }

    errorAt(tok, os.str().c_str(), gothint ? hint.str().c_str() : NULL);
    return false;
}


const Parser::ParseRule Parser::Rules[] =
{
    // grouping
    { Lexer::TOK_LPAREN, &Parser::grouping, &Parser::fncall, NULL,             Parser::PREC_CALL  },
    { Lexer::TOK_COLON,  NULL,              &Parser::mthcall,NULL,             Parser::PREC_CALL  },

    // math operators
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD   },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_EXCL  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_SLASH2X,NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },
    { Lexer::TOK_PERC,   NULL,              &Parser::binary, NULL,             Parser::PREC_MUL   },

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
    { Lexer::TOK_HASH  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_UNARY  },
    { Lexer::TOK_CONCAT, NULL,              &Parser::binary, NULL,             Parser::PREC_CONCAT  },

    // ranges
    { Lexer::TOK_DOTDOT, &Parser::unaryRange,&Parser::binaryRange,&Parser::postfixRange, Parser::PREC_UNARY  },

    // values
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            NULL,             Parser::PREC_NONE  },
    { Lexer::TOK_IDENT,  &Parser::_identInExpr,NULL,            NULL,             Parser::PREC_NONE  },
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

Parser::Parser(Lexer* lex, const char *fn, GC& gc, StringPool& strpool)
    : hlir(NULL), strpool(strpool), _lex(lex), _fn(fn), hadError(false), panic(false), gc(gc)
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
            ret = ensure(hlir->continu());
            break;

        case Lexer::TOK_BREAK:
            advance();
            ret = ensure(hlir->brk());
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
            ret = ensure(hlir->retn());
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

    panic = false;

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
    // '$' was just eaten
    eat(Lexer::TOK_LCUR);
    return block();
}

HLNode* Parser::conditional()
{
    // 'if' was consumed
    HLNode *node = ensure(hlir->conditional());
    if(node)
    {
        eat(Lexer::TOK_LPAREN);
        node->u.conditional.condition = expr();
        eat(Lexer::TOK_RPAREN);
        node->u.conditional.ifblock = stmt();
        node->u.conditional.elseblock = tryeat(Lexer::TOK_ELSE) ? stmt() : NULL;
    }
    return node;
}

HLNode* Parser::forloop()
{
    // 'for' was consumed
    HLNode *node = ensure(hlir->forloop());
    if(node)
    {
        eat(Lexer::TOK_LPAREN);
        node->u.forloop.iter = decl(); // must decl new var(s) for the loop
        eat(Lexer::TOK_RPAREN);
        node->u.forloop.body = stmt();
    }
    return node;
}

HLNode* Parser::whileloop()
{
    // 'while' was consumed
    HLNode *node = ensure(hlir->whileloop());
    if(node)
    {
        eat(Lexer::TOK_LPAREN);
        node->u.whileloop.cond = expr();
        eat(Lexer::TOK_RPAREN);
        node->u.whileloop.body = stmt();
    }
    return node;
}

// named:
//   func [optional, attribs] hello(funcparams) -> returns
// closure:
// ... = func [optional, attribs] (funcparams) -> returns
HLNode* Parser::_functiondef(HLNode **pname, HLNode **pnamespac)
{
    // 'func' was just eaten
    HLNode *f = ensure(hlir->func());
    HLNode *h = ensure(hlir->fhdr());
    if(!(f && h))
        return NULL;

    unsigned flags = 0;

    if(tryeat(Lexer::TOK_LSQ))
        _funcattribs(&flags);

    if(pname) // Named functions can be declared in a namespace
    {
        HLNode *nname = ident("function or namespace"); // namespace or function name
        HLNode *ns = NULL;

        if(tryeat(Lexer::TOK_DOT)) // . follows -> it's a namespaced function (namespace can be a type or table)
        {
            ns = nname;
            nname = ident("functionName");
        }
        else if(tryeat(Lexer::TOK_COLON)) // : follows -> it's a method (syntax sugar only; Lua style)
        {
            ns = nname;
            nname = ident("methodName");
            flags |= FUNCFLAGS_METHOD_SUGAR;
        }
        *pname = nname;
        *pnamespac = ns;
    }

    h->u.fhdr.paramlist = _funcparams();

    if(tryeat(Lexer::TOK_RARROW))
        h->u.fhdr.rettypes = _funcreturns(&flags);
    else
        flags |= FUNCFLAGS_DEDUCE_RET;

    h->flags = (FuncFlags)flags;

    f->u.func.hdr = h;
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

    HLNode *decl = ensure(hlir->funcdecl());
    if(decl)
        decl->u.funcdecl.value = _functiondef(&decl->u.funcdecl.ident, &decl->u.funcdecl.namespac);
    return decl;
}

HLNode* Parser::closurecons(Context ctx)
{
    return _functiondef(NULL, NULL);
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
            list->u.list.add(typeident(), gc);
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

    lhs->u.list.add(firstLhs, gc);
    while(tryeat(Lexer::TOK_COMMA))
    {
        lhs->u.list.add(suffixedexpr(), gc);
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
    HLNode * const node = ensure(hlir->list());
    if(!node)
        return NULL;

    bool isvar = curtok.tt == Lexer::TOK_VAR;
    HLNode *lasttype = NULL;
    HLNode *first = NULL;
    HLNode *second = NULL;

    if(isvar)
        advance();
    else
        lasttype = first = typeident();

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
            first = typeident(); // type name, if present
            if(!match(Lexer::TOK_IDENT))
            {
                second = first;
                first = NULL;
            }
        }
begin:

        HLNode * const def = hlir->vardef();

        if(!second)
            second = ident("variable"); // var name

        if(isvar) // 'var x'
        {
            lasttype = NULL;
            assert(!first);
            def->u.vardef.ident = second; // always var name
            if(tryeat(Lexer::TOK_COLON))
            {
                unsigned flags = 0;
                if(tryeat(Lexer::TOK_LIKE))
                    errorAt(prevtok, "FIXME: implement var x: like EXPR"); // FIXME
                HLNode *texpr = expr();
                //texpr->u.vardef.type->flags = flags // INCORRECT
                def->u.vardef.type = texpr;
            }
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

        node->u.list.add(def, gc);
    }

    return node;
}

HLNode* Parser::_paramlist()
{
    // '(' was eaten
    unsigned begin = prevtok.line;
    HLNode *ret = match(Lexer::TOK_RPAREN) ? NULL : _exprlist();
    eatmatching(Lexer::TOK_RPAREN, '(', begin);
    return ret;
}

HLNode* Parser::_fncall(HLNode* callee)
{
    // '(' was already eaten
    HLNode *node = ensure(hlir->fncall());
    if(node)
    {
        node->u.fncall.callee = callee;
        node->u.fncall.paramlist = _paramlist(); // this eats everything up to and including the terminating ')'
    }
    return node;
}

HLNode* Parser::_methodcall(HLNode* obj)
{
    // ':' was just eaten

    HLNode *node = ensure(hlir->mthcall());
    if(node)
    {
        node->u.mthcall.obj = obj;
        if(tryeat(Lexer::TOK_LSQ))
        {
            node->u.mthcall.mth = expr();
            eat(Lexer::TOK_RSQ);
        }
        else
            node->u.mthcall.mth = name("method");

        eat(Lexer::TOK_LPAREN);
        node->u.mthcall.paramlist = _paramlist();
    }
    return node;
}


HLNode* Parser::_exprlist()
{
    HLNode *node = ensure(hlir->list());
    if(node) do
        node->u.list.add(expr(), gc);
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
        HLNode *decls = _decllist();

        if(tryeat(Lexer::TOK_MASSIGN))
        {
            node->flags = DECLFLAG_MUTABLE;
            // Mutable decl assignment, give all individual HLVarDef that flag too
            if(decls)
            {
                HLNode **ch = decls->u.list.list;
                const size_t N = decls->u.list.used;
                for(size_t i = 0; i < N; ++i)
                {
                    assert(ch[i]->type == HLNODE_VARDEF);
                    ch[i]->flags |= DECLFLAG_MUTABLE;
                }
            }
        }
        else
            eat(Lexer::TOK_CASSIGN);

        node->u.vardecllist.decllist = decls;
        node->u.vardecllist.vallist = _exprlist();
    }

    tryeat(Lexer::TOK_SEMICOLON);

    panic = false;

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

// when used in binary expr "operator ("
HLNode* Parser::fncall(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // '(' was just eaten
    return _fncall(lhs);
}

HLNode* Parser::mthcall(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // ':' was just eaten
    return _methodcall(lhs);
}

// (expr)
// ident
HLNode* Parser::primaryexpr()
{
    return tryeat(Lexer::TOK_LPAREN)
        ? grouping(CTX_DEFAULT)
        : ident("identifier");
}

HLNode* Parser::suffixedexpr()
{
    return _suffixed(primaryexpr());
}


/* EXPR followed by one of:
  '.' ident
  [expr]
  (args)
  :mth(args)
*/
HLNode* Parser::_suffixed(HLNode *prefix)
{
    HLNode *node = prefix;
    for(;;)
    {
        HLNode *next = NULL;
        switch(curtok.tt)
        {
            case Lexer::TOK_DOT:
                advance();
                next = hlir->index();
                next->u.index.lhs = node;
                next->u.index.idx = name("field"); // eats the name and advances
                break;

            case Lexer::TOK_LSQ:
                advance();
                next = hlir->index();
                next->u.index.lhs = node;
                next->u.index.idx = expr();
                eat(Lexer::TOK_RSQ);
                break;

            /*case Lexer::TOK_LPAREN:
                advance();
                next = _fncall(node);
                break;*/

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

HLNode* Parser::name(const char *whatfor)
{
    if(!_checkname(curtok, whatfor))
        return NULL;

    advance();

    HLNode *node = ensure(hlir->name());
    if(node)
    {
        Str s = _tokenStr(prevtok);
        node->u.name.nameStrId = s.id;
    }
    return node;
}

HLNode* Parser::ident(const char *whatfor)
{
    HLNode *node = _ident(curtok, whatfor);
    advance();
    return node;
}

HLNode* Parser::typeident()
{
    unsigned flags = 0;
    if(tryeat(Lexer::TOK_LIKE))
        flags |= IDENTFLAGS_DUCKYTYPE;
    HLNode *node = ident("type");
    if(tryeat(Lexer::TOK_QM))
        flags |= IDENTFLAGS_OPTIONALTYPE;
    if(node)
        node->flags = flags;
    return node;
}

HLNode* Parser::_identInExpr(Context ctx)
{
    return _suffixed(_ident(prevtok, "identifier"));
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
        node->tok = rule->tok;
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
        node->tok = rule->tok;
        node->u.binary.rhs = rhs;
        node->u.binary.lhs = lhs;
    }
    return node;
}

HLNode* Parser::ensure(HLNode* node)
{
    if(!node)
        outOfMemory();
    node->line = curtok.line; // HACK: FIXME: pass line as param
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

bool Parser::match(Lexer::TokenType tt) const
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

void Parser::errorAt(const Lexer::Token& tok, const char *msg, const char *hint)
{
    if(panic)
        return;

    printf("(%s:%u): %s (Token: %s)\n", _fn, tok.line, msg, Lexer::GetTokenText(tok.tt));

    const char * const beg = _lex->getLineBegin();
    if(beg < tok.begin)
    {
        const char *p = beg;
        for( ; *p && *p != '\r' && *p != '\n'; ++p) {}
        const int linelen = int(p - beg);
        printf("|%.*s\n|", linelen, beg);
        for(const char *q = beg; q < tok.begin; ++q)
            putchar(' ');
        putchar('^');
        if(tok.tt != Lexer::TOK_E_ERROR)
            for(size_t i = 1; i < tok.u.len; ++i)
                putchar('~');
    }

    if(hint)
        printf(" %s", hint);

    putchar('\n');

    hadError = true;
    panic = true;
}

void Parser::error(const char* msg, const char *hint)
{
    errorAt(prevtok, msg, hint);
}

void Parser::errorAtCurrent(const char* msg, const char *hint)
{
    errorAt(curtok, msg, hint);
}

HLNode *Parser::nil(Context ctx)
{
    return ensure(hlir->constantValue());
}

HLNode* Parser::tablecons(Context ctx)
{
    // { was just eaten while parsing an expr
    const unsigned beginline = prevtok.line;

    HLNode *node = ensure(hlir->list());
    if(!node)
        return NULL;

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
                key = ident("key"); // key=...
                eat(Lexer::TOK_CASSIGN);
            }
            // else it's a single var lookup; handle as expr
        }

        node->u.list.add(key, gc);
        node->u.list.add(expr(), gc);
    }
    while(tryeat(Lexer::TOK_COMMA) || tryeat(Lexer::TOK_SEMICOLON));

    eatmatching(Lexer::TOK_RCUR, '{', beginline);

    return node;
}

HLNode* Parser::arraycons(Context ctx)
{
    // [ was just eaten while parsing an expr
    const unsigned beginline = prevtok.line;

    HLNode *node = ensure(hlir->list());
    if(node)
    {
        node->type = HLNODE_ARRAYCONS;

        do
        {
            if(match(Lexer::TOK_RSQ))
                break;

            node->u.list.add(expr(), gc);
        }
        while(tryeat(Lexer::TOK_COMMA) || tryeat(Lexer::TOK_SEMICOLON));
    }

    eatmatching(Lexer::TOK_RSQ, '[', beginline);

    return node;
}

HLNode* Parser::_ident(const Lexer::Token& tok, const char *whatfor)
{
    if(tok.tt == Lexer::TOK_SINK)
    {
        advance();
        return ensure(hlir->sink());
    }

    if(!_checkname(tok, whatfor))
        return NULL;

    HLNode *node = ensure(hlir->ident());
    if(node)
    {
        Str s = _tokenStr(tok);
        node->u.ident.nameStrId = s.id;
    }
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
    HLNode *node = ensure(hlir->list());
    if(node)
    {
        do
            node->u.list.add(decl(), gc);
        while(tryeat(Lexer::TOK_SEMICOLON));

        node->type = HLNODE_ITER_DECLLIST;
    }
    return node;
}

HLNode* Parser::iterexprs()
{
    HLNode *node = ensure(hlir->list());
    if(node)
    {
        do
            node->u.list.add(expr(), gc);
        while(tryeat(Lexer::TOK_SEMICOLON));

        node->type = HLNODE_ITER_EXPRLIST;
    }
    return node;
}

HLNode* Parser::stmtlist(Lexer::TokenType endtok)
{
    HLNode *sl = ensure(hlir->list());
    if(sl)
    {
        sl->type = HLNODE_BLOCK;
        while(!match(endtok))
        {
            HLNode *p = declOrStmt();
            if(p)
                sl->u.list.add(p, gc);
            else
                break;
        }
    }
    return sl;
}

HLNode *Parser::emitConstant(const Val& v)
{
    HLNode *node = ensure(hlir->constantValue());
    if(node)
        node->u.constant.val = v;
    return node;
}

void Parser::outOfMemory()
{
    errorAtCurrent("out of memory");
}
