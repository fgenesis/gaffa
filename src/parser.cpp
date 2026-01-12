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
    return Val(f);
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
    return Val(u);
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

    return Val(u);
}

const char *Parser::symbolname(const HLNode *node) const
{
    if(node) switch(node->type)
    {
        case HLNODE_IDENT:
            return symbolname(syms.getsym(node->u.ident.symid));
        case HLNODE_NAME:
            return strpool.lookup(node->u.name.nameStrId).s;

        default: ;
    }
    return NULL;
}

const char* Parser::symbolname(const Symstore::Sym* sym) const
{
    if(!sym)
        return NULL;
    Strp sp = strpool.lookup(sym->nameStrId);
    return sp.s;
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
            os << " (which is a reserved keyword)";

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

// Establish how a symbol is used (LHS or RHS) and do the necessary symbol management
void Parser::_applyUsage(const Lexer::Token& tok, HLNode* node, IdentUsage usage, SymbolRefContext symref)
{
    if(usage == IDENT_USAGE_UNTRACKED)
        return;

    HLIdent& ident = *node->as<HLIdent>();
    const sref strid = ident.nameStrId;
    Strp name = strpool.lookup(strid);

    switch(usage)
    {
        case IDENT_USAGE_DECL:
        {
            //assert(symref == SYMREF_HIDDEN || symref == SYMREF_VISIBLE);
            node->flags |= IDENTFLAGS_LHS;
            Symstore::Decl decl = syms.decl(strid, tok, symref);
            if(decl.sym)
            {
                ident.symid = syms.getuid(decl.sym);
                printf("DEBUG: Declared symbol '%s' (symid %u, slot %d) in line %u\n",
                    name.s, ident.symid, decl.sym->slot, tok.line);
            }
            if(decl.clashing)
            {
                std::ostringstream os;
                os << "'" << name.s << "': redefinition; first defined in line " << decl.clashing->linedefined();
                errorAt(tok, os.str().c_str());
                panic = false;
                errorAt(decl.clashing->tok, "(Previously defined here)");
            }
        }
        break;

        case IDENT_USAGE_USE:
        {
            Symstore::Lookup f = syms.lookup(strid, tok, symref, true);
            assert(f.sym);

            printf("DEBUG: Referring '%s' (slot %d) in line %u as %s from line %u\n",
                name.s, f.sym->slot, node->line, f.namewhere(), f.sym->linedefined());

            ident.symid = syms.getuid(f.sym);
            node->flags |= IDENTFLAGS_RHS;
        }
    }
}

const Parser::ParseRule Parser::Rules[] =
{
    // grouping
    { Lexer::TOK_LPAREN, &Parser::grouping, &Parser::fncall, NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_COLON,  NULL,              &Parser::mthcall,NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },

    // math operators
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD,       Parser::OPF_OVL  },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD,       Parser::OPF_OVL  },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_EXCL  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_SLASH2X,NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_PERC,   NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },

    // bitwise
    { Lexer::TOK_SHL   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT, Parser::OPF_OVL  },
    { Lexer::TOK_SHR   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT, Parser::OPF_OVL  },
    { Lexer::TOK_BITAND, NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_AND,   Parser::OPF_OVL  },
    { Lexer::TOK_BITOR , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_OR,    Parser::OPF_OVL  },
    { Lexer::TOK_HAT   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_XOR,   Parser::OPF_OVL  },
    { Lexer::TOK_TILDE , &Parser::unary,    NULL,            NULL,             Parser::PREC_BIT_XOR,   Parser::OPF_OVL  },

    // logical
    { Lexer::TOK_EQ    , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY,  Parser::OPF_OVL  },
    { Lexer::TOK_NEQ   , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY,  Parser::OPF_OVL  },
    { Lexer::TOK_LT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_OVL  },
    { Lexer::TOK_LTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_OVL  },
    { Lexer::TOK_GT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_OVL  },
    { Lexer::TOK_GTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_OVL  },
    { Lexer::TOK_EXCL  , NULL,              &Parser::binary, NULL,             Parser::PREC_UNARY,     Parser::OPF_OVL  },
    { Lexer::TOK_LOGAND, NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_AND, Parser::OPF_NONE },
    { Lexer::TOK_LOGOR , NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_OR,  Parser::OPF_NONE },

    // special
    { Lexer::TOK_QQM,    &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY,     Parser::OPF_NONE },
    { Lexer::TOK_FATARROW,NULL,             &Parser::binary, NULL,             Parser::PREC_UNWRAP,    Parser::OPF_NONE },
    { Lexer::TOK_HASH  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_UNARY,     Parser::OPF_OVL  },
    { Lexer::TOK_CONCAT, NULL,              &Parser::binary, NULL,             Parser::PREC_CONCAT,    Parser::OPF_OVL  },

    // ranges
    { Lexer::TOK_DOTDOT, &Parser::unaryRange,&Parser::binaryRange,&Parser::postfixRange, Parser::PREC_UNARY, Parser::OPF_NONE },

    // values
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_IDENT,  &Parser::_identInExpr,NULL,         NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_NIL,    &Parser::nil,      NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    //{ Lexer::TOK_DOLLAR, &Parser::valblock, NULL,          NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_LCUR,   &Parser::tablecons,NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_LSQ,    &Parser::arraycons,NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_FUNC,   &Parser::closurecons,NULL,          NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },

    { Lexer::TOK_E_ERROR,NULL,              NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE }
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

    // The file's statements are parsed as a single block.
    // Wrap that into a function that takes no parameters and has no speficied return values.
    HLNode *root = ensure(hlir->func());
    HLNode *hdr = ensure(hlir->fhdr());
    if(root && hdr)
    {
        hdr->u.fhdr.paramlist = NULL;
        hdr->u.fhdr.rettypes = NULL; // FIXME: Should probably make this a void return?
        root->u.func.hdr = hdr;

        _beginFunction();
        root->u.func.body = stmtlist(Lexer::TOK_E_EOF);
        _endFunction();
    }

    if(hadError)
    {
        syms.dealloc(gc);
        return NULL;
    }

    return root;
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
        case Lexer::TOK_YIELD:
            ret = ensure(hlir->retn());
            ret->tok = curtok.tt; // so that we can distinguish yield and return
            advance();
            ret->u.retn.what = _exprlist();
            break;

        case Lexer::TOK_LCUR:
            advance();
            ret = block();
            break;

        case Lexer::TOK_EXPORT:
            advance();
            ret = _export();

        break;

        default: // assignment, function call with ignored returns
        {
            const Lexer::Token beg = curtok;
            ret = suffixedexpr();
            if(curtok.tt == Lexer::TOK_COMMA || curtok.tt == Lexer::TOK_CASSIGN)
            {
                ret = _restassign(ret, beg);
            }
            // else it's just a function / method call, already handled

            if(ret && ret->type == HLNODE_IDENT)
                errorAt(beg, "identifier stands alone, expected statement");
        }
        break;
    }

    tryeat(Lexer::TOK_SEMICOLON);

    panic = false;

    return ret;
}

// export DECL      // export a var decl
// export func(...) // export a named function
// export (EXPR) DECL // explicit named export
HLNode* Parser::_export()
{

    // 'export' keyword was consumed
    HLNode *ret = ensure(hlir->exprt());
    if(ret)
    {
        if(tryeat(Lexer::TOK_LPAREN))
        {
            const Lexer::Token opening = prevtok;
            ret->u.exprt.name = expr();
            eatmatching(Lexer::TOK_RPAREN, opening);
        }

        ret->u.exprt.what = decl();
    }
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

void Parser::_beginFunction(ScopeType scope /* = SCOPE_FILE */)
{
    syms.push(scope);
}

void Parser::_endFunction()
{
    Symstore::Frame f;
    syms.pop(f);

    assert(f.boundary >= SCOPE_FUNCTION);

    const size_t N = f.symids.size();
    for(size_t i = 0; i < N; ++i)
    {
        const Symstore::Sym *sym = syms.getsym(f.symids[i]);

        if(!(sym->referencedHow & SYMREF_VISIBLE))
        {
            const char *name = symbolname(sym);
            std::ostringstream os;
            os << "'" << name << "': INTERNAL ERROR: Identifier is not visible while closing function scope";
            errorAt(sym->tok, os.str().c_str());
        }

        // Any symbol that's still marked as deferred was not re-declared -> this is bad
        if(sym->referencedHow & SYMREF_DEFERRED)
        {
            const char *name = symbolname(sym);
            if(sym->lineused())
            {
                std::ostringstream os;
                os << "'" << name << "': Deferred constant is used but was never re-declared";
                errorAt(sym->firstuse, os.str().c_str());
                panic = false;
                errorAt(sym->tok, "(Originally introduced here)");
            }
            else
            {
                printf("WARNING: '%s': Deferred symbol unused\n", name); // TODO: proper warnings
            }
        }

        if(f.boundary < SCOPE_FILE)
        {
            if(sym->referencedHow & SYMREF_EXPORTED)
            {
                const char *name = symbolname(sym);
                std::ostringstream os;
                os << "'" << name << "': Can't export locals declared inside functions";
                errorAt(sym->firstuse, os.str().c_str(), "Move this to file scope");
                panic = false;
                errorAt(sym->tok, "(Originally declared here)");
            }
        }
    }
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

    unsigned hasns = 0;
    Lexer::Token sep;
    HLNode *ns = NULL;
    if(pname) // Named functions can be declared in a namespace
    {
        const Lexer::Token tok = curtok; // used in case of error
        HLNode *nname = ident("function or namespace", IDENT_USAGE_UNTRACKED, SYMREF_VISIBLE); // namespace or function name
        SymbolRefContext nameref = SYMREF_VISIBLE;

        sep = curtok;
        if(tryeat(Lexer::TOK_DOT)) // . follows -> it's a namespaced function (namespace can be a type or table)
            hasns = 1;
        else if(tryeat(Lexer::TOK_COLON)) // : follows -> it's a method (syntax sugar only; Lua style)
            hasns = 2;

        if(hasns)
        {
            ns = nname;
            nname = _methodname();
        }

        *pname = nname;
        *pnamespac = ns;

        _applyUsage(tok, nname, ns ? IDENT_USAGE_USE : IDENT_USAGE_DECL, nameref);
    }

    _beginFunction();

    HLNode *params = _funcparams(); // This returns NULL if there are no parameters

    if(hasns == 2) // Lua-style: ns:mth(...) -> ns.mth(ns self, ...)
    {
        if(!params)
            params = ensure(hlir->list());

        // Shift parameters up to make space at the front
        HLList *list = &params->u.list;
        const size_t n = list->used;
        list->add(NULL, gc);
        for(size_t i = 0; i < n; ++i)
            list->list[i+1] = list->list[i];

        // Inject a 'ns self' parameter
        HLNode *self = ensure(hlir->vardef(), sep); // Whatev; use the ':' as anchoring point in case there are errors to report
        Lexer::Token selftok = sep;
        selftok.begin = "self";
        selftok.u.len = 4;
        selftok.tt = Lexer::TOK_IDENT;
        self->u.vardef.ident = _ident(selftok, "implicit 'self' in method def", IDENT_USAGE_DECL, SYMREF_VISIBLE);
        self->u.vardef.type = ns;
        list->list[0] = self;
    }

    h->u.fhdr.paramlist = params;

    if(tryeat(Lexer::TOK_RARROW))
        h->u.fhdr.rettypes = _funcreturns();

    h->flags = (HLFlags)flags;

    f->u.func.hdr = h;
    f->u.func.body = functionbody();

    _endFunction();

    printf("DEBUG: Finalized function '%s', nargs = %d, nrets = %d\n",
        pname ? symbolname(*pname) : NULL, h->u.fhdr.nargs(), h->u.fhdr.nrets());

    return f;
}

// namespace:hello
// int.+
// int.@"+"
HLNode * Parser::_methodname()
{
    bool adv = true;
    if(curtok.tt != Lexer::TOK_IDENT)
        for(size_t i = 0; i < Countof(Rules); ++i)
            if(curtok.tt == Rules[i].tok)
            {
                if(Rules[i].opflags & OPF_OVL) // Can overload operator? accept that as a "function name" token too...
                {
                    adv = false;
                    curtok.tt = Lexer::TOK_IDENT; // ... but make it a regular identifier
                    advance();
                    if(curtok.tt == Lexer::TOK_SINK) // "func T.+_" ?
                    {
                        // Special case notation for prefix/unary operators
                        adv = true;
                        // Extend previous token to cover also the "_"
                        unsigned oldlen = prevtok.u.len;
                        prevtok.u.len = curtok.begin - prevtok.begin + curtok.u.len;
                        if(prevtok.u.len != oldlen + 1) // Code is immutable so we can't collapse the space here
                            error("Malformed prefix operator: _ must follow without space");
                    }
                    break;
                }
                else
                    error("Attempt to overload an operator that is not overloadable");
            }
    if(adv)
        advance();
    return _ident(prevtok, "function or method name or operator", IDENT_USAGE_DECL, SYMREF_VISIBLE);
}

HLNode* Parser::_funcparams()
{
    eat(Lexer::TOK_LPAREN);
    const Lexer::Token opening = prevtok;
    // In a function param list, there's no RHS, so symbols should become visible right away
    HLNode *node = match(Lexer::TOK_RPAREN)
        ? NULL
        : _decllist(SymbolRefContext(SYMREF_VISIBLE | SYMREF_FUNCARG), true);
    eatmatching(Lexer::TOK_RPAREN, opening);
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
    const Lexer::Token opening = prevtok;
    const Str pure = this->strpool.put("pure");

    while(match(Lexer::TOK_IDENT))
    {
        Str s = _identStr(curtok);
        if(pure.id && s.id == pure.id)
        {
            //*pfuncflags |= FUNCFLAGS_PURE;
            assert(false);
        }
        else
            errorAtCurrent("Expected function attrib: pure");

        advance();
        tryeat(Lexer::TOK_COMMA);

    }
    eatmatching(Lexer::TOK_RSQ, opening);
}

// -> nil
// -> ...
// -> ?
// -> int, float, blah
// -> bool, int?, ...
// (nil or '?' or a list of types)
HLNode* Parser::_funcreturns()
{
    // '->' was just eaten

    if(tryeat(Lexer::TOK_NIL))
        return NULL;

    HLNode *list = ensure(hlir->list());
    if(list) for(;;)
    {
        if(match(Lexer::TOK_IDENT))
        {
            HLNode *ident = typeident();
            if(tryeat(Lexer::TOK_TRIDOT))
            {
                list->flags |= HLFLAG_VARIADIC;
                ident->flags |= HLFLAG_VARIADIC;
            }
            list->u.list.add(ident, gc);
            if(!tryeat(Lexer::TOK_COMMA))
                break;
        }
        else if(match(Lexer::TOK_QM) || tryeat(Lexer::TOK_TRIDOT))
        {
            HLNode *dummy = ensure(hlir->dummy());
            if(dummy)
            {
                dummy->flags |= HLFLAG_VARIADIC;
                list->flags |= HLFLAG_VARIADIC;
            }
            list->u.list.add(dummy, gc);
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
    assert(lhs->as<HLList>());

    eat(Lexer::TOK_CASSIGN); // eat '='
    HLNode *node = ensure(hlir->assignment());
    if(node)
    {
        node->u.assignment.dstlist = lhs;
        node->u.assignment.vallist = _exprlist();
    }
    return node;
}

HLNode* Parser::_restassign(HLNode* firstLhs, const Lexer::Token& lhsTok)
{
    HLNode *lhs = ensure(hlir->list());
    if(!lhs)
        return NULL;

    _checkAssignTarget(firstLhs, lhsTok);
    lhs->u.list.add(firstLhs, gc);
    while(tryeat(Lexer::TOK_COMMA))
    {
        const Lexer::Token tok = curtok;
        HLNode *n = suffixedexpr();
        _checkAssignTarget(n, tok);
        lhs->u.list.add(n, gc);
    }

    return _assignmentWithPrefix(lhs);
}

// Check that any symbol directly assigned to is mutable and a local or upvalue.
// Don't allow assignment to external symbols.
void Parser::_checkAssignTarget(const HLNode* node, const Lexer::Token& nodetok)
{
    if(node->type == HLNODE_IDENT)
    {
        sref symid = node->u.ident.symid;
        Symstore::Sym *sym = syms.getsym(symid);

        // Undeclared symbols are assumed to be an imported external symbol (which are always non-mutable).
        // So if we get an external symbol, that's the equivalent of trying to assign to an undeclared variable
        if(sym->referencedHow & SYMREF_EXTERNAL)
        {
            const char *sn = symbolname(sym);
            //if(node->line == sym->lineused() && node->column == sym->firstuse.column())
            if(sym->firstuse == nodetok)
            {
                std::ostringstream os;
                os << "'" << sn << "': undeclared identifier";
                errorAt(nodetok, os.str().c_str());
            }
            else
            {
                {
                    std::ostringstream os;
                    os << "'" << sn << "': is an external symbol that can't be assigned to";
                    errorAt(nodetok, os.str().c_str());
                }
                {
                    std::ostringstream os;
                    const char *t = (sym->referencedHow & SYMREF_TYPE) ? "type" : "var";
                    os << "(To import as mutable, put '" << t << " " << sn << " := " << sn << "' above)";
                    panic = false; // HACK
                    errorAt(sym->firstuse, "(Initially imported here)", os.str().c_str());
                }
            }
        }

        else if(!(sym->referencedHow & SYMREF_MUTABLE))
        {
            const char *sn = symbolname(sym);
            Symstore::Lookup lookup = syms.lookup(sym->nameStrId, nodetok, SYMREF_NOFLAGS, false);
            assert(lookup.sym == sym);
            {
                std::ostringstream os;
                os << "'" << sn << "': Can't assign, " << lookup.namewhere() << " is not mutable";
                errorAt(nodetok, os.str().c_str());
            }
            if(sym->tok.line)
            {
                panic = false; // HACK
                const char *diag = (sym->referencedHow & SYMREF_FUNCARG)
                    ? "(Function parameters are never mutable)"
                    : "(Use := for declaring as mutable)";
                errorAt(sym->tok, "(Previously declared here)", diag);
            }
        }
    }
}

// var x, y
// var x:int, y:float
// int x, blah y, T z
// int a, float z
// T a, b, Q c, d, e
// int x, y, var z,q
// Note that in a regular declaration, the symbol becomes visible only when the entire declaration itself is finished.
// Otherwise we end up with something like 'var x = x' where x would refer to itself.
// (This statement is actually legal, but the '= x' refers to the next best x in an upper scope, not the x being declared)
HLNode* Parser::_decllist(SymbolRefContext symref, bool allowVariadic)
{
    // curtok is ident or 'var'
    HLNode * const node = ensure(hlir->list());
    if(!node)
        return NULL;

    HLNode *lasttype = NULL;


    do
    {
        HLNode *first = NULL;

        // (int x, float y, ...)
        bool variadic = allowVariadic && tryeat(Lexer::TOK_TRIDOT);

        if(variadic)
        {
            // Keep the type and do nothing else
        }
        // either 'var' or a type name. This switches context.
        // In 'var' context, types are deduced, in 'C' context, they need to be known.
        else if(tryeat(Lexer::TOK_VAR))
        {
            lasttype = NULL;
        }
        else
        {
            lookAhead();
            // If two identifiers in a row ('int x') --  First is the type, second one the variable.
            // If only one, it's parsed below, using the previous type.
            if(lookahead.tt == Lexer::TOK_IDENT)
                lasttype = first = typeident();
            else if(allowVariadic && lookahead.tt == Lexer::TOK_TRIDOT)
            {
                // (int x, float y, int...)
                variadic = true; // with known type
                eat(Lexer::TOK_TRIDOT);
            }
            else if(!lasttype)
            {
                // This branch is only hit when we've just entered the for loop
                error("A declaration needs to start with 'var' or a type name");
                break;
            }
        }

        HLNode * const def = hlir->vardef();
        node->u.list.add(def, gc);

        if(!variadic)
            def->u.vardef.ident = ident("variable", IDENT_USAGE_DECL, symref); // var name

        if(!lasttype) // 'var x'
        {
            assert(!first);

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
            def->u.vardef.type = first ? first : lasttype;
        }

        if(variadic)
        {
            // The variadic entry is marked as such and terminates the list
            // We need that extra entry to optionally carry type information (ie. only int... follows)
            def->flags |= HLFLAG_VARIADIC; // Mark this as the terminating variadic element
            node->flags |= HLFLAG_VARIADIC; // The list is marked as variadic too

            if(match(Lexer::TOK_COMMA))
                errorAt(prevtok, "'...' must be the last entry if used");
            break;
        }
    }
    while(tryeat(Lexer::TOK_COMMA));

    return node;
}

HLNode* Parser::_paramlist()
{
    // '(' was eaten
    const Lexer::Token opening = prevtok;
    HLNode *ret = match(Lexer::TOK_RPAREN) ? NULL : _exprlist();
    eatmatching(Lexer::TOK_RPAREN, opening);
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
    if(match(Lexer::TOK_IDENT))
    {
        lookAhead();
        if(lookahead.tt == Lexer::TOK_IDENT)
            return decl();
    }
    else if(match(Lexer::TOK_VAR) || match(Lexer::TOK_FUNC))
        return decl();

    return NULL;
}

// DECLLIST = a, b, c
// DECLLIST = defer
HLNode* Parser::decl()
{
    if(tryeat(Lexer::TOK_FUNC))
        return namedfunction();

    HLNode *node = ensure(hlir->vardecllist());
    if(node)
    {
        HLNode *decls = _decllist(SYMREF_HIDDEN, false);
        const bool mut = tryeat(Lexer::TOK_MASSIGN);
        if(!mut)
            eat(Lexer::TOK_CASSIGN);

        node->u.vardecllist.decllist = decls;

        const bool deferred = !mut && tryeat(Lexer::TOK_DEFER);

        if(!deferred)
            node->u.vardecllist.vallist = _exprlist();

        // Declaration is finished, make all declared symbols usable and optionally mutable
        if(decls)
        {
            HLNode **ch = decls->u.list.list;
            const size_t N = decls->u.list.used;
            for(size_t i = 0; i < N; ++i)
            {
                HLNode *c = ch[i];
                assert(c->type == HLNODE_VARDEF);

                sref symid = c->as<HLVarDef>()->ident->as<HLIdent>()->symid;
                Symstore::Sym *sym = syms.getsym(symid);
                if(mut)
                    sym->makeMutable();
                if(deferred)
                    sym->makeDeferred();
                sym->makeUsable();
            }
        }

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
    // '{' was just eaten
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
        : ident("identifier", IDENT_USAGE_USE, SYMREF_NOFLAGS);
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

HLNode* Parser::ident(const char *whatfor, IdentUsage usage, SymbolRefContext symref)
{
    HLNode *node = _ident(curtok, whatfor, usage, symref);
    advance();
    return node;
}

HLNode* Parser::typeident()
{
    unsigned flags = 0;
    if(tryeat(Lexer::TOK_LIKE))
    {
        //flags |= IDENTFLAGS_DUCKYTYPE;
        assert(false);
    }
    HLNode *node = ident("type", IDENT_USAGE_USE, SYMREF_TYPE);
    if(tryeat(Lexer::TOK_QM))
        flags |= IDENTFLAGS_OPTIONALTYPE;
    if(node)
        node->flags = flags;
    return node;
}

HLNode* Parser::_identInExpr(Context ctx)
{
    return _suffixed(_ident(prevtok, "identifier", IDENT_USAGE_USE, SYMREF_NOFLAGS));
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
    const Lexer::Token mytok = prevtok; // Keep this around; parsePrecedence() advances the parser state
    const ParseRule *rule = GetRule(mytok.tt);
    HLNode *rhs = parsePrecedence(PREC_UNARY);

    // Special case?
    if(rule->tok == Lexer::TOK_PLUS || rule->tok == Lexer::TOK_MINUS)
    {
        if(rhs->type == HLNODE_CONSTANT_VALUE)
        {
            ValU &v = rhs->u.constant.val;
            switch(v.type)
            {
                case PRIMTYPE_UINT: // Change type from uint to sint when explicitly prefixed with sign.
                    // This is important for automatic type deduction. C/C++ uses '42u' to denote unsigned;
                    // we use unsigned-by-default and make it signed by explicitly stating +42 or -42.
                    v.type = PRIMTYPE_SINT;
                    if(rule->tok == Lexer::TOK_MINUS)
                        v.u.si = -v.u.si; // FIXME: limit, MAX_INT handling
                    return rhs;

                case PRIMTYPE_FLOAT: // Flip sign instead of emitting an unary op.
                    // Doing this here is not strictly necessary since the type doesn't change,
                    // but this way it doesn't rely on constant folding to not suck.
                    if(rule->tok == Lexer::TOK_MINUS)
                        v.u.f = -v.u.f;
                    return rhs;
            }
        }
    }

    // Regular unary op
    HLNode *node = ensure(hlir->unary(), mytok);
    if(node)
    {
        // Initially, we're a uniary operator node.
        node->tok = rule->tok;
        node->u.unary.a = rhs;
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
        node->u.binary.a = rhs;
        node->u.binary.b = lhs;
    }
    return node;
}

HLNode* Parser::ensure(HLNode* node)
{
    return ensure(node, curtok);
}

HLNode* Parser::ensure(HLNode* node, const Lexer::Token& tok)
{
    if(node)
    {
        node->line = tok.line;
        node->column = tok.column();
    }
    else
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
    if(lookahead.tt == Lexer::TOK_E_UNDEF)
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
    sprintf(&buf[0], "Expected '%s', got '%s'", Lexer::GetTokenText(tt), Lexer::GetTokenText(curtok.tt));
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

void Parser::eatmatching(Lexer::TokenType tt, const Lexer::Token& begintok)
{
    if(curtok.tt == tt)
    {
        advance();
        return;
    }

    char buf[64], hint[64];
    sprintf(&buf[0], "Expected to close %c in line %u", *begintok.begin, begintok.line);
    errorAt(curtok, &buf[0]);

    sprintf(&buf[0], "'%s' expected", Lexer::GetTokenText(tt));
    panic = false;
    errorAt(begintok, "(Opened here)", &buf[0]);
}

void Parser::errorAt(const Lexer::Token& tok, const char *msg, const char *hint)
{
    if(panic)
        return;

    //printf("(%s:%u): %s (Token: %s)\n", _fn, tok.line, msg, Lexer::GetTokenText(tok.tt));
    printf("(%s:%u): %s\n", _fn, tok.line, msg);

    const char * const beg = tok.linebegin; //_lex->getLineBegin();
    assert(beg);
    if(beg <= tok.begin)
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
    const Lexer::Token opening = prevtok;

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
                key = name("key"); // key=...
                eat(Lexer::TOK_CASSIGN);
            }
            // else it's a single var lookup; handle as expr
        }

        node->u.list.add(key, gc);
        node->u.list.add(expr(), gc);
    }
    while(tryeat(Lexer::TOK_COMMA) || tryeat(Lexer::TOK_SEMICOLON));

    eatmatching(Lexer::TOK_RCUR, prevtok);

    return node;
}

HLNode* Parser::arraycons(Context ctx)
{
    // [ was just eaten while parsing an expr
    const Lexer::Token opening = prevtok;

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

    eatmatching(Lexer::TOK_RSQ, opening);

    return node;
}

HLNode* Parser::_ident(const Lexer::Token& tok, const char *whatfor, IdentUsage usage, SymbolRefContext symref)
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
        _applyUsage(tok, node, usage, symref);
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
        node->u.ternary.b = node->u.unary.a;
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
