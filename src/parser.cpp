#include "parser.h"
#include "hlir.h"
#include "strings.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

static const Str InvalidStr = {0, 0};

static Val valerr(const char *err)
{
    Val v;
    v.type = PRIMTYPE_NORETURN; // These stay inside the parser
    v.u.p = (void*)err;
    return v;
}


static int digitval(unsigned char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'z')
        return 10 + c - 'a';
    if(c >= 'A' && c <= 'Z')
        return 10 + c - 'A';
    return 99;
}

static bool isalnumchar(unsigned char c)
{
    return digitval(c) != 99;
}

// Accepts:
//   123  0x7f  0b101  0o17  1_000  (unsigned integers)
//   1.5  .5  5.  1e10  1.5e-3  0x1.8  (floats; decimal or hex)
static Val makenum(const char *s, const char *end)
{
    // Copy without '_' separators, which must sit between two digits
    char buf[128];
    size_t n = 0;
    for(const char *p = s; p < end; ++p)
    {
        if(*p == '_')
        {
            if(p == s || p + 1 == end || !isalnumchar(p[-1]) || !isalnumchar(p[1]))
                return valerr("Digit separator '_' must be between two digits");
            continue;
        }
        if(n + 1 >= sizeof(buf))
            return valerr("Number literal too long");
        buf[n++] = *p;
    }
    buf[n] = 0;

    unsigned base = 10;
    const char *digits = buf;
    if(buf[0] == '0' && buf[1])
    {
        switch(buf[1])
        {
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2; break;
            case 'o': case 'O': base = 8; break;
        }
        if(base != 10)
        {
            digits += 2;
            if(!*digits)
                return valerr("Missing digits after number prefix");
        }
    }

    bool isfloat = false;
    for(const char *p = digits; *p; ++p)
        if(*p == '.' || (base == 10 && (*p == 'e' || *p == 'E')))
            isfloat = true;

    if(isfloat)
    {
        if(base != 10 && base != 16)
            return valerr("Binary and octal literals can't have a fraction");
        char *pend;
        const real f = (real)strtod(buf, &pend); // FIXME: use grisu2
        if(*pend)
            return valerr("Malformed float literal");
        if((f - f) != (f - f)) // inf or nan
            return valerr("Float literal out of range");
        return Val(f);
    }

    uint u = 0;
    for(const char *p = digits; *p; ++p)
    {
        const unsigned d = (unsigned)digitval(*p);
        if(d >= base)
            return valerr(base == 10 ? "Invalid character in number literal" : "Invalid digit for this number base");
        if(mul_check_overflow(&u, u, uint(base)) || add_check_overflow(&u, u, uint(d)))
            return valerr("Integer literal too large");
    }
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
    Str q = strpool.put(s, end - s);
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
            else if(decl.external)
            {
                std::ostringstream os;
                os << "'" << name.s << "': declared after it was already used as an external symbol";
                errorAt(tok, os.str().c_str(), "Declare it with '= defer' before the first use");
                panic = false;
                errorAt(decl.external->firstuse, "(First used here)");
            }
        }
        break;

        case IDENT_USAGE_USE:
        {
            Symstore::Lookup f = syms.lookup(strid, tok, symref, true);
            assert(f.sym);

            printf("DEBUG: Referring '%s' (slot %d) in line %u as %s from line %u\n",
                name.s, f.sym->slot, node->line, f.namewhere(), f.sym->linedefined());

            // A deferred symbol has no value until its real declaration, but an inner function may refer to it
            if((f.sym->referencedHow & SYMREF_DEFERRED) && f.where == SCOPEREF_LOCAL)
            {
                std::ostringstream os;
                os << "'" << name.s << "': deferred symbol used before its declaration";
                errorAt(tok, os.str().c_str(), "Only inner functions may refer to it before it is declared");
            }

            ident.symid = syms.getuid(f.sym);
            ident.ex.u.scoperef = f.where;
            node->flags |= IDENTFLAGS_RHS;
        }
    }
}

const Parser::ParseRule Parser::Rules[] =
{
    // grouping, calls, indexing
    { Lexer::TOK_LPAREN, &Parser::grouping, &Parser::fncall, NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_COLON,  NULL,              &Parser::mthcall,NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_DOT,    NULL,              &Parser::dotindex,NULL,            Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_LSQ,    &Parser::arraycons,&Parser::sqindex,NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_DBLCOLON,NULL,             &Parser::nsindex,NULL,             Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_LCUR,   &Parser::tablecons,&Parser::structcons,NULL,          Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_QM,     NULL,              &Parser::badTypeMod,NULL,          Parser::PREC_CALL,      Parser::OPF_NONE },
    { Lexer::TOK_TRIDOT, &Parser::varargs,  &Parser::badTypeMod,NULL,          Parser::PREC_CALL,      Parser::OPF_NONE },

    // math operators
    { Lexer::TOK_PLUS  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD,       Parser::OPF_OVL  },
    { Lexer::TOK_MINUS , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_ADD,       Parser::OPF_OVL  },
    { Lexer::TOK_STAR  , &Parser::unary,    &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_EXCL  , &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY,     Parser::OPF_OVL  },
    { Lexer::TOK_SLASH , NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_SLASH2X,NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },
    { Lexer::TOK_MOD,    NULL,              &Parser::binary, NULL,             Parser::PREC_MUL,       Parser::OPF_OVL  },

    // bitwise
    { Lexer::TOK_SHL   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT, Parser::OPF_OVL  },
    { Lexer::TOK_SHR   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_SHIFT, Parser::OPF_OVL  },
    { Lexer::TOK_BITAND, NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_AND,   Parser::OPF_OVL  },
    { Lexer::TOK_BITOR , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_OR,    Parser::OPF_OVL  },
    { Lexer::TOK_HAT   , NULL,              &Parser::binary, NULL,             Parser::PREC_BIT_XOR,   Parser::OPF_OVL  },
    { Lexer::TOK_TILDE , &Parser::unary,    NULL,            NULL,             Parser::PREC_BIT_XOR,   Parser::OPF_OVL  },

    // logical
    { Lexer::TOK_EQ    , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY,  Parser::OPF_OVL  },
    { Lexer::TOK_NEQ   , NULL,              &Parser::binary, NULL,             Parser::PREC_EQUALITY,  Parser::OPF_NONE },
    { Lexer::TOK_LT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_NONE },
    { Lexer::TOK_LTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_NONE },
    { Lexer::TOK_GT    , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_NONE },
    { Lexer::TOK_GTE   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_NONE },
    { Lexer::TOK_CMP   , NULL,              &Parser::binary, NULL,             Parser::PREC_COMPARISON,Parser::OPF_OVL  },
    { Lexer::TOK_LOGAND, NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_AND, Parser::OPF_NONE },
    { Lexer::TOK_LOGOR , NULL,              &Parser::binary, NULL,             Parser::PREC_LOGIC_OR,  Parser::OPF_NONE },
    { Lexer::TOK_AND,    NULL,              &Parser::andor,  NULL,             Parser::PREC_AND,       Parser::OPF_NONE },
    { Lexer::TOK_OR,     NULL,              &Parser::andor,  NULL,             Parser::PREC_OR,        Parser::OPF_NONE },

    // special
    { Lexer::TOK_QQM,    &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY,     Parser::OPF_NONE },
    { Lexer::TOK_HASH  , &Parser::unary,    NULL,            NULL,             Parser::PREC_UNARY,     Parser::OPF_OVL  },
    { Lexer::TOK_CONCAT, NULL,              &Parser::binary, NULL,             Parser::PREC_CONCAT,    Parser::OPF_OVL  },

    // Only valid in if() and for(), which parse them directly. Anywhere else, these rules report an error.
    { Lexer::TOK_FATARROW,NULL,             &Parser::badUnwrap,NULL,           Parser::PREC_SPECIAL,   Parser::OPF_NONE },
    { Lexer::TOK_DOTDOT, &Parser::badRange, &Parser::badRangeInfix,NULL,       Parser::PREC_SPECIAL,   Parser::OPF_NONE },

    // values
    { Lexer::TOK_LITNUM, &Parser::litnum,   NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_LITSTR, &Parser::litstr,   NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_IDENT,  &Parser::_identInExpr,NULL,         NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_SINK,   &Parser::badSink,     NULL,        NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_WSINK,  &Parser::badSink,     NULL,        NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
    { Lexer::TOK_DOLLAR, &Parser::valblock, NULL,            NULL,             Parser::PREC_NONE,      Parser::OPF_NONE },
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
    : hlir(NULL), strpool(strpool), _lex(lex), _fn(fn), hadError(false), panic(false), _nerrors(0), gc(gc)
{
    curtok.tt = Lexer::TOK_E_ERROR;
    prevtok.tt = Lexer::TOK_E_ERROR;
    lookahead.tt = Lexer::TOK_E_UNDEF;
    const FuncCtx fc = { 0, 0, false, false };
    _fctx = fc;
}

HLNode *Parser::parse()
{
    advance();

    // The file's statements are parsed as a single block.
    // Much later this will eventually become a function, so we track variables etc in an initial function scope.
    _beginFunction();
    HLNode *root = stmtlist(Lexer::TOK_E_EOF);
    _endFunction();

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
        case Lexer::TOK_BREAK:
            if(!_fctx.loops)
                errorAtCurrent(curtok.tt == Lexer::TOK_BREAK ? "'break' outside of a loop" : "'continue' outside of a loop");
            ret = ensure(curtok.tt == Lexer::TOK_BREAK ? hlir->brk() : hlir->continu());
            advance();
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
        case Lexer::TOK_EMIT:
            if(curtok.tt == Lexer::TOK_EMIT && !_fctx.valblocks)
                errorAtCurrent("'emit' outside of a value block", "Use $ to make a value block, eg. var x = ${ emit 1 }");
            ret = ensure(hlir->retn());
            ret->tok = curtok.tt; // so that we can distinguish yield and return
            advance();
            {
                // Values must start on the same line, otherwise the next line is a new statement
                const ParseRule *rule = GetRule(curtok.tt);
                ret->u.retn.what = curtok.line == prevtok.line && rule && rule->prefix
                    ? _exprlist()
                    : ensure(hlir->list());
            }
            break;

        case Lexer::TOK_LCUR:
            advance();
            ret = block();
            break;

        case Lexer::TOK_EXPORT:
            advance();
            ret = _export();

        break;

        default: // assignment, or function call with ignored returns
        {
            const Lexer::Token beg = curtok;
            ret = _assignTarget();
            if(match(Lexer::TOK_COMMA) || match(Lexer::TOK_CASSIGN) || match(Lexer::TOK_MASSIGN))
                ret = _assignment(ret, beg);
            else if(!(ret->iscall() || ret->type == HLNODE_CALLADJ || ret->type == HLNODE_NONE))
                errorAt(beg, "Expected statement", "An expression on its own must be a call");
        }
        break;
    }

    tryeat(Lexer::TOK_SEMICOLON);

    return ret;
}

// export DECL      // export a var decl
// export func(...) // export a named function
// export (EXPR) DECL // explicit named export
HLNode* Parser::_export()
{

    // 'export' keyword was consumed
    if(_fctx.infunc || syms.peek().boundary != SCOPE_FUNCTION)
        error("'export' is only allowed at file scope");

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
    if(!rule || !rule->prefix)
    {
        const char *what = Lexer::GetTokenText(curtok.tt);
        std::ostringstream os;
        os << "Expected expression";
        if(Lexer::IsReserved(curtok.tt))
            os << ", got '" << what << "' (reserved keyword, not implemented yet)";
        else if(what)
            os << ", got '" << what << "'";
        errorAtCurrent(os.str().c_str());
        return _errnode();
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

        // 'f' followed by '(' on a later line could be a call or the start of a new statement
        if(curtok.tt == Lexer::TOK_LPAREN && curtok.line != prevtok.line)
        {
            errorAt(curtok, "Ambiguous syntax: function call or new statement?",
                "Put '(' on the same line to make it a call, or end the previous statement with ';'");
            break;
        }

        // 'T{...}' is only construction sugar on the same line; otherwise it's a block
        if(curtok.tt == Lexer::TOK_LCUR && curtok.line != prevtok.line)
            break;

        advance();

        node = (this->*(rule->infix))(ctx, rule, node);
    }
    return node;

}

HLNode *Parser::_errnode()
{
    return ensure(hlir->dummy());
}

// $stmt -- any statement, usually ${ ... } or $while(...) {...}
HLNode* Parser::valblock(Context ctx)
{
    // '$' was just eaten
    HLNode *node = ensure(hlir->valblock());
    _beginScope(SCOPE_VALBLOCK);
    ++_fctx.valblocks;
    node->u.valblock.body = stmt();
    --_fctx.valblocks;
    _endScope();
    return node;
}

// if(cond) stmt [else stmt]
// if(expr => name) stmt [else stmt]   -- name is only visible in the if-branch
HLNode* Parser::conditional()
{
    // 'if' was consumed
    HLNode *node = ensure(hlir->conditional());
    eat(Lexer::TOK_LPAREN);
    HLNode *cond = parsePrecedence(PREC_OR); // stops before '=>'
    const bool bind = tryeat(Lexer::TOK_FATARROW);
    if(bind)
    {
        _beginScope(SCOPE_BLOCK);
        HLNode *ub = ensure(hlir->unwrapbind(), prevtok);
        ub->u.unwrapbind.val = cond;
        ub->u.unwrapbind.ident = ident("variable", IDENT_USAGE_DECL, SYMREF_VISIBLE);
        cond = ub;
        if(!match(Lexer::TOK_RPAREN))
            errorAtCurrent("'x => name' must be the entire if() condition");
    }
    node->u.conditional.condition = cond;
    eat(Lexer::TOK_RPAREN);
    node->u.conditional.ifblock = stmt();
    if(bind)
        _endScope();
    node->u.conditional.elseblock = tryeat(Lexer::TOK_ELSE) ? stmt() : NULL;
    return node;
}

// for(decl = iter; decl = iter; ...) stmt
HLNode* Parser::forloop()
{
    // 'for' was consumed
    HLNode *node = ensure(hlir->forloop());
    eat(Lexer::TOK_LPAREN);
    _beginScope(SCOPE_BLOCK);
    HLNode *iters = ensure(hlir->list());
    iters->type = HLNODE_ITER_DECLLIST;
    do
        iters->u.list.add(_iterdecl(), gc);
    while(tryeat(Lexer::TOK_SEMICOLON));
    node->u.forloop.iter = iters;
    eat(Lexer::TOK_RPAREN);
    ++_fctx.loops;
    node->u.forloop.body = stmt();
    --_fctx.loops;
    _endScope();
    return node;
}

// var a, b = EXPR or RANGE
HLNode* Parser::_iterdecl()
{
    HLNode *node = ensure(hlir->vardecllist());
    HLNode *decls = _decllist(SYMREF_HIDDEN, false);
    if(tryeat(Lexer::TOK_MASSIGN))
        error("Loop variables can't be mutable", "Use '=' instead of ':='");
    else
        eat(Lexer::TOK_CASSIGN);
    HLNode *vals = ensure(hlir->list());
    vals->u.list.add(_iterexpr(), gc);
    node->u.vardecllist.decllist = decls;
    node->u.vardecllist.vallist = vals;
    _finishDecl(decls, false, false);
    return node;
}

// EXPR, or a range:
//   a..b   ..b   a..   a..b..step   ..b..step   a.. ..step
HLNode* Parser::_iterexpr()
{
    HLNode *begin = NULL;
    const Lexer::Token tok = curtok;
    if(!match(Lexer::TOK_DOTDOT))
    {
        begin = parsePrecedence(PREC_OR); // stops before '..'
        if(!match(Lexer::TOK_DOTDOT))
            return begin;
    }
    eat(Lexer::TOK_DOTDOT);

    HLNode *node = ensure(hlir->range(), tok);
    node->u.range.begin = begin;
    node->u.range.end = NULL;
    node->u.range.step = NULL;

    // 'a.. ..step' has no end; otherwise an end follows unless the range is open
    if(!match(Lexer::TOK_DOTDOT))
    {
        const ParseRule *rule = GetRule(curtok.tt);
        if(rule && rule->prefix)
            node->u.range.end = parsePrecedence(PREC_OR);
        else if(!begin)
            errorAtCurrent("Expected range end after '..'");
    }
    if(tryeat(Lexer::TOK_DOTDOT))
        node->u.range.step = parsePrecedence(PREC_OR);

    return node;
}

HLNode* Parser::whileloop()
{
    // 'while' was consumed
    HLNode *node = ensure(hlir->whileloop());
    eat(Lexer::TOK_LPAREN);
    node->u.whileloop.cond = expr();
    eat(Lexer::TOK_RPAREN);
    ++_fctx.loops;
    node->u.whileloop.body = stmt();
    --_fctx.loops;
    return node;
}

void Parser::_beginFunction(ScopeType scope /* = SCOPE_FUNCTION */)
{
    syms.push(scope);
}

void Parser::_endFunction()
{
    Symstore::Frame f;
    syms.pop(f);
    assert(f.boundary >= SCOPE_FUNCTION);
    _checkClosedScope(f);
}

void Parser::_beginScope(ScopeType scope)
{
    assert(scope < SCOPE_FUNCTION);
    syms.push(scope);
}

void Parser::_endScope()
{
    Symstore::Frame f;
    syms.pop(f);
    assert(f.boundary < SCOPE_FUNCTION);
    _checkClosedScope(f);
}

void Parser::_checkClosedScope(const Symstore::Frame& f)
{
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
    Lexer::Token sep, nametok;
    OperatorId op = OP_ERROR;
    HLNode *ns = NULL;
    if(pname) // Named functions can be declared in a namespace
    {
        const Lexer::Token tok = curtok; // used in case of error
        HLNode *nname = ident("function or namespace", IDENT_USAGE_UNTRACKED, SYMREF_VISIBLE); // namespace or function name

        sep = curtok;
        if(tryeat(Lexer::TOK_DOT)) // . follows -> it's a namespaced function (namespace can be a type or table)
            hasns = 1;
        else if(tryeat(Lexer::TOK_COLON)) // : follows -> it's a method (syntax sugar only; Lua style)
            hasns = 2;
        else if(match(Lexer::TOK_DBLCOLON))
            errorAtCurrent("Use 'func T.name()' or 'func T:name()' to declare a namespaced function or method", "Or assign: T::name = func(...)");

        if(hasns)
        {
            // The namespace is used; the function name is not a symbol, since namespaces are resolved later
            ns = nname;
            if(ns->type == HLNODE_IDENT)
                _applyUsage(tok, ns, IDENT_USAGE_USE, SYMREF_NOFLAGS);
            nname = _methodname();
        }
        else if(nname->type == HLNODE_IDENT)
            _applyUsage(tok, nname, IDENT_USAGE_DECL, SYMREF_VISIBLE); // visible right away for recursion

        *pname = nname;
        *pnamespac = ns;
    }

    const FuncCtx outer = _fctx;
    const FuncCtx inner = { 0, 0, false, true };
    _fctx = inner;

    _beginFunction();

    // Lua-style: ns:mth(...) -> ns.mth(ns self, ...)
    // Declared before the other params so that the params' symbols stay in order
    HLNode *self = NULL;
    if(hasns == 2)
    {
        self = ensure(hlir->vardef(), sep); // Whatev; use the ':' as anchoring point in case there are errors to report
        Lexer::Token selftok = sep;
        selftok.begin = "self";
        selftok.u.len = 4;
        selftok.tt = Lexer::TOK_IDENT;
        self->u.vardef.ident = _ident(selftok, "implicit 'self' in method def", IDENT_USAGE_DECL, SYMREF_VISIBLE);
        self->u.vardef.type = ns;
    }

    HLNode *params = _funcparams(); // This returns NULL if there are no parameters
    _fctx.variadic = params && (params->flags & HLFLAG_VARIADIC);

    if(self)
    {
        if(!params)
            params = ensure(hlir->list());

        // Shift parameters up to make space at the front
        HLList *list = &params->u.list;
        const size_t n = list->used;
        list->add(NULL, gc);
        for(size_t i = n; i > 0; --i)
            list->list[i] = list->list[i-1];
        list->list[0] = self;
    }

    h->u.fhdr.paramlist = params;

    if(tryeat(Lexer::TOK_RARROW))
        h->u.fhdr.rettypes = _funcreturns();

    h->flags = (HLFlags)flags;

    f->u.func.hdr = h;
    f->u.func.body = functionbody();

    h->u.fhdr.lineend = prevtok.line;

    _endFunction();
    _fctx = outer;

    const HLFunctionHdr::Values args = h->u.fhdr.nargs();
    const HLFunctionHdr::Values rets = h->u.fhdr.nrets();
    printf("DEBUG: Finalized function '%s', nargs = %u%s, nrets = %d%s\n",
        pname ? symbolname(*pname) : NULL,
        args.n, args.variadic ? " variadic" : "",
        rets.n, rets.variadic ? " variadic" : "");

    return f;
}

// Methods and namespace operator overloading. After one of:
//   func namespace:
//   func namespace.
// Eg.:
//   namespace:hello
//   namespace.hello
// The shorter token form is the intended, safe way to do operator overloading:
//   int.+  int.prefix-  T.[]  T.[]=  T.[]?  T.()  T:<=>
// When written like this, it's a compile error to define an operator that doesn't exist or can't be overloaded.
//
// Alternatively, a method can be defined with any name via a single string:
//   int.@"a method name with spaces"
//   string.@"break"   (A method name that is a reserved keyword)
//   int.@"+"      (this is a valid operator+ overload)
//   T:@"whatev*<.-_:= \u{1234}"   (this is just a weird method name)
//   int.@"&&"      (this is a method that reads like operator&&, but that is not overloadable, so this can only be called manually)
//   int.@"prefix??" (same, ??x is for catching errors and not an operator that can be overloaded)
HLNode * Parser::_methodname()
{
    Lexer::Token starttok = curtok;
    const char *beg = curtok.begin;
    size_t mthlen = curtok.u.len;

    // Simple case: A regular method name or a potential operator overload as a single string. Use that as-is.
    if(curtok.tt == Lexer::TOK_IDENT)
    {
        // If this yields an an operator, it's an overload as a string, eg. @"+" or @"prefix+"
        OperatorId op = Lexer::GetOperatorFromName(curtok.begin, mthlen);
        // Any identifier that's not a potential operator overload prefix is fine as method name
        if(op != OP_ERROR || !Lexer::IsOperatorNamePrefix(beg, mthlen))
        {
            return name("method");
        }
    }

    // Not-so-simple case: A couple tokens that are not necessarily strings that may form an operator.
    // Keep eating tokens as long as this could be an operator overload. Spaces between tokens not supported.
    size_t eaten = 0;
    for(;;)
    {
        advance();
        ++eaten;

        if(!(curtok.u.len && curtok.begin == beg + mthlen))
            break;

        size_t newlen = mthlen + curtok.u.len;
        if(!Lexer::IsOperatorNamePrefix(beg, newlen))
            break;

        mthlen = newlen;
    }

    // curtok is no longer part of a potential operator name. See what we have now.
    // This could be a single string that happens to be the start of operator names, eg. "prefix",
    // or a bunch of tokens strung together.
    OperatorId op = Lexer::GetOperatorFromName(beg, mthlen);
    if(op == OP_ERROR)
    {
        // Last chance: Single identifier on its own that happened to be a prefix of an operator overload
        if(!(eaten == 1 && starttok.tt == Lexer::TOK_IDENT))
            errorAt(starttok, "Expected function name or operator");
    }

    HLNode *node = ensure(hlir->name(), starttok);
    node->u.name.nameStrId = strpool.put(beg, mthlen).id;
    return node;
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
            errorAtCurrent("Function attribute 'pure' is not implemented yet"); // TODO: *pfuncflags |= FUNCFLAGS_PURE;
        else
            errorAtCurrent("Expected function attrib: pure");

        advance();
        tryeat(Lexer::TOK_COMMA);

    }
    eatmatching(Lexer::TOK_RSQ, opening);
}

// -> int
// -> ...
// -> int...
// -> int, float, blah
// -> bool, int?, ...
// -> bool, int?...
HLNode* Parser::_funcreturns()
{
    // '->' was just eaten

    HLNode *list = ensure(hlir->list());
    if(list) do
    {
        HLNode *part = NULL;

        if(match(Lexer::TOK_IDENT)) // identifier, optionally followed by '?' or '...'
        {
            part = typeident(true);
        }
        else if(tryeat(Lexer::TOK_TRIDOT)) // Just '...'
        {
            part = ensure(hlir->dummy());
            if(part)
                part->flags |= HLFLAG_VARIADIC;
        }

        if(!part)
        {
            errorAtCurrent("Expected type or '...'");
            break;
        }

        list->u.list.add(part, gc);
        if(part->flags & HLFLAG_VARIADIC)
        {
            list->flags |= HLFLAG_VARIADIC;
            if(match(Lexer::TOK_COMMA))
                errorAt(prevtok, "'...' must be the last entry in a return type list");
            break;
        }
    }
    while(tryeat(Lexer::TOK_COMMA));

    return list;
}

// a, b.c, d[e] = EXPRLIST
// The first target was already parsed.
static bool isWideSink(const HLNode *node)
{
    return node && node->type == HLNODE_SINK && node->tok == Lexer::TOK_WSINK;
}

HLNode* Parser::_assignment(HLNode* firstLhs, const Lexer::Token& lhsTok)
{
    HLNode *lhs = ensure(hlir->list());
    _checkAssignTarget(firstLhs, lhsTok);
    lhs->u.list.add(firstLhs, gc);
    bool wide = isWideSink(firstLhs);
    Lexer::Token widetok = lhsTok;
    while(tryeat(Lexer::TOK_COMMA))
    {
        if(wide)
        {
            errorAt(widetok, "'___' must be the last assignment target", "It takes all remaining values");
            wide = false;
        }
        const Lexer::Token tok = curtok;
        HLNode *n = _assignTarget();
        _checkAssignTarget(n, tok);
        lhs->u.list.add(n, gc);
        if(isWideSink(n))
        {
            wide = true;
            widetok = tok;
        }
    }

    if(tryeat(Lexer::TOK_MASSIGN))
        error("':=' is only valid in declarations", "Use '=' to assign");
    else
        eat(Lexer::TOK_CASSIGN);

    HLNode *node = ensure(hlir->assignment());
    node->u.assignment.dstlist = lhs;
    node->u.assignment.vallist = _exprlist();
    return node;
}

// '_' and '___' are only valid as a whole assignment target, so they don't go through the expression parser
HLNode* Parser::_assignTarget()
{
    if(!(tryeat(Lexer::TOK_SINK) || tryeat(Lexer::TOK_WSINK)))
        return expr();
    if(!(match(Lexer::TOK_COMMA) || match(Lexer::TOK_CASSIGN) || match(Lexer::TOK_MASSIGN)))
        return badSink(CTX_DEFAULT);
    return _sink(prevtok);
}

HLNode* Parser::_sink(const Lexer::Token& tok)
{
    HLNode *node = ensure(hlir->sink(), tok);
    if(node)
        node->tok = tok.tt;
    return node;
}

// Check that any symbol directly assigned to is mutable and a local or upvalue.
// Don't allow assignment to external symbols.
void Parser::_checkAssignTarget(HLNode* node, const Lexer::Token& nodetok)
{
    switch(node->type)
    {
        case HLNODE_IDENT:
        case HLNODE_INDEX:
        case HLNODE_NS_INDEX:
        case HLNODE_SINK:
        case HLNODE_NONE: // error was already reported
            break;
        default:
            errorAt(nodetok, "Can't assign to this", "Only variables, fields, and indexed elements can be assigned to");
            return;
    }

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

        // Mutability is decided by the real declaration later, which then must use ':='
        else if(sym->referencedHow & SYMREF_DEFERRED)
            sym->referencedHow |= SYMREF_DEFERRED_ASSIGNED;

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
    else if(node->type == HLNODE_INDEX)
    {
        node->type = HLNODE_INDEXASSIGN;
    }
    else if(node->type == HLNODE_NS_INDEX)
    {
        node->type = HLNODE_NS_INDEXASSIGN;
    }
}

// var x, y
// var x:int, y:float
// int x, blah y, T z
// int a, float z
// T a, b, Q c, d, e
// int x, y, var z,q
// int? xo, yo
// int? xo, yo, ...
// int x, y, ...
// int a, b, float x, y, ...
// int a, b, float...
// int? a, b, float?...
// Note that in a regular declaration, the symbol becomes visible only when the entire declaration itself is finished.
// Otherwise we end up with something like 'var x = x' where x would refer to itself.
// (This statement is actually legal, but the '= x' refers to the next best x in an upper scope, not the x being declared)
HLNode* Parser::_decllist(SymbolRefContext symref, bool allowVariadic)
{
    // curtok is ident, 'var', or '...'

    HLNode * const node = ensure(hlir->list());
    if(!node)
        return NULL;

    HLNode *lasttype = NULL;
    bool varctx = false;
    bool variadic = false;
    bool wide = false;
    Lexer::Token widetok;

    do
    {
        if(wide)
        {
            errorAt(widetok, "'___' must be the last name in a declaration", "It takes all remaining values");
            wide = false;
        }

        // Precond:
        // - We've either just entered, or looped around after a ','
        // - curtok is 'var', ident, or '...'
        // - lasttype may or may not be NULL.

        // 3 ways variadics can appear:
        // - (V1) As the first and only:   ...
        // - (V2) Using the previous type: float x, ...
        // - (V3) Directly after a type:   float x, int...
        variadic = allowVariadic && tryeat(Lexer::TOK_TRIDOT); // This handles cases (V1), (V2)
        if(variadic)
            break;

        // either 'var' or a type name. This switches context.
        // In 'var' context, types are deduced, in 'C' context, they need to be specified.
        else if(tryeat(Lexer::TOK_VAR))
        {
            if(allowVariadic) // only function parameter lists allow variadics, and they need types
                error("Function parameters need a type, 'var' is not allowed here");
            lasttype = NULL;
            varctx = true;
        }
        else // curtok is a type identifier or single identifier
        {
            // Either a C-style decl, or just a single identifier.
            lookAhead();

            // "int?" or "int..."
            // Not a variable just yet but if the curret ident is marked optional or variadic, it must be a type.
            // Next identifier must be a variable name.
            bool istype = lookahead.tt == Lexer::TOK_QM
                       || lookahead.tt == Lexer::TOK_TRIDOT
            // C-style decl, two identifiers in a row ('int x') --  First is the type, second one the variable.
                       || lookahead.tt == Lexer::TOK_IDENT
                       || lookahead.tt == Lexer::TOK_SINK
                       || lookahead.tt == Lexer::TOK_WSINK;

            if(istype)
            {
                varctx = false; // Definitely C-style decl now
                // Eats ident, optionally followed by '?' or '...'
                lasttype = typeident(allowVariadic);
                variadic = lasttype->flags & HLFLAG_VARIADIC; // Case (V3)
                if(variadic)
                    break;
            }
            // Something else after a single identifier, probably ','. Need a C-style type or 'var' earlier.
            else if(!(lasttype || varctx))
            {
                // This branch is only hit when we've just entered the for loop
                error("A declaration needs to start with 'var' or a type name");
                break;
            }
        }

        // Now that the type or 'var' prefix is parsed, the next thing is a variable name.
        // Any '...' leaves the loop before we're here since it's either used instead of an identifier,
        // or as part of typeident()
        assert(!variadic);

        HLNode * const def = hlir->vardef();
        node->u.list.add(def, gc);
        const Lexer::Token nametok = curtok;
        def->u.vardef.ident = ident("variable", IDENT_USAGE_DECL, symref); // var name
        def->u.vardef.type = lasttype; // NULL for 'var', or the C-style type
        if(isWideSink(def->u.vardef.ident))
        {
            if(allowVariadic)
                errorAt(nametok, "'___' can't be a parameter name", "Use '...' for variadic parameters");
            wide = true;
            widetok = nametok;
        }

        assert(varctx == !lasttype);

        if(varctx && tryeat(Lexer::TOK_COLON)) // var x: typeexpr = ...
        {
            //if(tryeat(Lexer::TOK_LIKE))
            //    errorAt(prevtok, "FIXME: implement var x: like EXPR"); // TODO, maybe
            def->u.vardef.type = expr(); // includes a trailing '?'
        }
    }
    while(tryeat(Lexer::TOK_COMMA));

    if(variadic)
    {
        // The variadic entry is marked as such and terminates the list
        // We need that extra entry to optionally carry type information (ie. only int... follows)
        HLNode * const def = hlir->vardef();
        def->flags |= HLFLAG_VARIADIC; // Mark this as the terminating variadic element
        def->u.vardef.type = lasttype; // May or may not be NULL
        node->u.list.add(def, gc);
        node->flags |= HLFLAG_VARIADIC; // The list is marked as variadic too
    }

    return node;
}

HLNode* Parser::_paramlist()
{
    // '(' was eaten
    const Lexer::Token opening = prevtok;
    HLNode *ret = NULL;
    if(!match(Lexer::TOK_RPAREN))
    {
        ret = ensure(hlir->list());
        do
        {
            if(match(Lexer::TOK_RPAREN)) // trailing comma
                break;
            ret->u.list.add(expr(), gc);
        }
        while(tryeat(Lexer::TOK_COMMA));
    }
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
    return _endcall(node);
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
    return _endcall(node);
}

HLNode* Parser::_endcall(HLNode* call)
{
    // f()#N -- N binds tightly: f()#2 + 1 is (f()#2) + 1
    if(call && tryeat(Lexer::TOK_HASH))
    {
       if(HLNode *node = ensure(hlir->calladj()))
       {
           node->u.calladj.adj = parsePrecedence(PREC_CALL);
           node->u.calladj.call = call;
           return node;
       }
    }
    return call;
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
        if(lookahead.tt == Lexer::TOK_IDENT || lookahead.tt == Lexer::TOK_SINK || lookahead.tt == Lexer::TOK_WSINK
            || lookahead.tt == Lexer::TOK_QM || lookahead.tt == Lexer::TOK_TRIDOT)
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
    HLNode *decls = _decllist(SYMREF_HIDDEN, false);
    const bool mut = tryeat(Lexer::TOK_MASSIGN);
    if(!mut)
        eat(Lexer::TOK_CASSIGN);

    node->u.vardecllist.decllist = decls;

    bool deferred = false;
    if(match(Lexer::TOK_DEFER))
    {
        if(mut)
            errorAtCurrent("A deferred declaration can't be mutable", "Use '= defer' here; the later declaration may use ':='");
        advance();
        deferred = true;
    }
    else
    {
        node->u.vardecllist.vallist = _exprlist();
    }

    _finishDecl(decls, mut, deferred);

    tryeat(Lexer::TOK_SEMICOLON);

    return node;
}

// Declaration is finished, make all declared symbols usable and optionally mutable
void Parser::_finishDecl(HLNode *decls, bool mut, bool deferred)
{
    HLNode **ch = decls->u.list.list;
    const size_t N = decls->u.list.used;
    for(size_t i = 0; i < N; ++i)
    {
        const HLNode *id = ch[i]->u.vardef.ident;
        if(!id || id->type != HLNODE_IDENT) // sink, variadic marker, or parse error
            continue;
        Symstore::Sym *sym = syms.getsym(id->u.ident.symid);
        if(!mut && !deferred && (sym->referencedHow & SYMREF_DEFERRED_ASSIGNED))
        {
            std::ostringstream os;
            os << "'" << symbolname(sym) << "': assigned to while deferred, so it must be declared mutable";
            errorAt(sym->tok, os.str().c_str(), "Use ':=' here");
        }
        if(mut)
            sym->makeMutable();
        if(deferred)
            sym->makeDeferred();
        sym->makeUsable();
    }
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
    const Lexer::Token opening = prevtok;
    _beginScope(SCOPE_BLOCK);
    HLNode *node = stmtlist(Lexer::TOK_RCUR);
    _endScope();
    eatmatching(Lexer::TOK_RCUR, opening);
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

// a.name -> a["name"]
HLNode* Parser::dotindex(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // '.' was just eaten
    HLNode *node = ensure(hlir->index(), prevtok);
    node->u.index.lhs = lhs;
    node->u.index.idx = nameAsLitstr("field");
    return node;
}

// a[expr]
HLNode* Parser::sqindex(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // '[' was just eaten
    const Lexer::Token opening = prevtok;
    HLNode *node = ensure(hlir->index(), opening);
    node->u.index.lhs = lhs;
    node->u.index.idx = expr();
    eatmatching(Lexer::TOK_RSQ, opening);
    return node;
}

HLNode* Parser::nsindex(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // '::' was just eaten
    return _namespace(lhs);
}

// T{...} is T({...})
HLNode* Parser::structcons(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    // '{' was just eaten
    HLNode *node = ensure(hlir->fncall(), prevtok);
    HLNode *args = ensure(hlir->list());
    args->u.list.add(tablecons(ctx), gc);
    node->u.fncall.callee = lhs;
    node->u.fncall.paramlist = args;
    return node;
}

// Lua-style a and b, a or b
HLNode* Parser::andor(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    HLNode *node = ensure(hlir->andor(), prevtok);
    node->tok = rule->tok;
    node->u.andor.a = lhs;
    node->u.andor.b = parsePrecedence((Prec)(rule->precedence + 1));
    return node;
}

// '?' or '...' after anything but a type name (those are handled in _identInExpr())
HLNode* Parser::badTypeMod(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    error(rule->tok == Lexer::TOK_QM
        ? "'?' only works directly after a type name"
        : "'...' only works directly after a type name",
        "For other type expressions, use optional(T)");
    return lhs;
}

HLNode* Parser::badUnwrap(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    error("'=>' is only allowed directly in an if() condition", "if(x => name) {...}");
    return lhs;
}

HLNode* Parser::badRangeInfix(Context ctx, const ParseRule *rule, HLNode *lhs)
{
    return badRange(ctx);
}

HLNode* Parser::badRange(Context ctx)
{
    error("Ranges are only allowed in for() and iterator()");
    return _errnode();
}

HLNode* Parser::badSink(Context ctx)
{
    error(prevtok.tt == Lexer::TOK_WSINK ? "'___' can't be used as a value" : "'_' can't be used as a value",
        "It can only be assigned or declared");
    return _errnode();
}

HLNode* Parser::varargs(Context ctx)
{
    // '...' was just eaten
    if(!_fctx.variadic)
        error("'...' used outside of a variadic function");
    return ensure(hlir->varargs(), prevtok);
}

// ns::ident
// ns::["expr"]
HLNode *Parser::_namespace(HLNode *ns)
{
    // '::' was just eaten
    HLNode *node = ensure(hlir->nsindex());
    if(node)
    {
        node->u.ns.ns = ns;
        if(tryeat(Lexer::TOK_LSQ))
        {
            const Lexer::Token opening = prevtok;
            node->u.ns.ident = expr();
            eatmatching(Lexer::TOK_RSQ, opening);
        }
        else
            node->u.ns.ident = nameAsLitstr("name");
    }
    return node;
}


HLNode* Parser::litnum(Context ctx)
{
    return emitConstant(prevtok, makenum(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::litstr(Context ctx)
{
    return emitConstant(prevtok, makestr(prevtok.begin, prevtok.begin + prevtok.u.len));
}

HLNode* Parser::name(const char *whatfor)
{
    if(!_checkname(curtok, whatfor))
        return _errnode();

    advance();

    HLNode *node = ensure(hlir->name(), prevtok);
    Str s = _tokenStr(prevtok);
    node->u.name.nameStrId = s.id;
    return node;
}

HLNode* Parser::nameAsLitstr(const char* whatfor)
{
    HLNode *n = name(whatfor);
    if(n->type == HLNODE_NAME)
    {
        sref strid = n->u.name.nameStrId;
        n->unsafemorph<HLConstantValue>();
        n->u.constant.val = Val(_Str(strid));
    }
    return n;
}

HLNode* Parser::ident(const char *whatfor, IdentUsage usage, SymbolRefContext symref)
{
    HLNode *node = _ident(curtok, whatfor, usage, symref);
    advance();
    return node;
}

HLNode* Parser::typeident(bool allowVariadic)
{
    unsigned flags = 0;
    if(match(Lexer::TOK_LIKE))
    {
        errorAtCurrent("'like' is not implemented yet"); // TODO: flags |= IDENTFLAGS_DUCKYTYPE;
        advance();
    }
    HLNode *node = ident("type", IDENT_USAGE_USE, SYMREF_TYPE);
    node->flags = flags | _typemods(allowVariadic);
    return node;
}

// '?' and/or '...' after a type name: T?  T...  T?...
unsigned Parser::_typemods(bool allowVariadic)
{
    unsigned flags = 0;
    if(tryeat(Lexer::TOK_QM))
        flags |= IDENTFLAGS_OPTIONALTYPE;
    if(allowVariadic && tryeat(Lexer::TOK_TRIDOT))
        flags |= HLFLAG_VARIADIC;
    return flags;
}

// An identifier followed by '?' or '...' is a type name, as in typeident()
HLNode* Parser::_identInExpr(Context ctx)
{
    const bool istype = match(Lexer::TOK_QM) || match(Lexer::TOK_TRIDOT);
    HLNode *node = _ident(prevtok, "identifier", IDENT_USAGE_USE, istype ? SYMREF_TYPE : SYMREF_NOFLAGS);
    if(istype)
        node->flags |= _typemods(true);
    return node;
}

HLNode *Parser::grouping(Context ctx)
{
    // ( was eaten
    const Lexer::Token opening = prevtok;
    HLNode *u = expr();
    eatmatching(Lexer::TOK_RPAREN, opening);
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
                {
                    const uint limit = (uint(1) << (sizeof(uint) * 8 - 1)) - (rule->tok == Lexer::TOK_MINUS ? 0 : 1);
                    if(v.u.ui > limit)
                        errorAt(mytok, "Integer literal too large for int");
                    v.type = PRIMTYPE_SINT;
                    if(rule->tok == Lexer::TOK_MINUS)
                        v.u.ui = uint(0) - v.u.ui; // two's complement; -(2^63) is the minimum
                    return rhs;
                }

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
        node->u.unary.opid = Lexer::TokenToUnOp(rule->tok);
    }

    return node;
}

HLNode * Parser::binary(Context ctx, const Parser::ParseRule *rule, HLNode *lhs)
{
    const Lexer::Token optok = prevtok;
    HLNode *rhs = parsePrecedence((Prec)(rule->precedence + 1));

    HLNode *node = ensure(hlir->binary(), optok);
    if(node)
    {
        node->tok = rule->tok;
        node->u.binary.a = lhs;
        node->u.binary.b = rhs;
        node->u.binary.opid = Lexer::TokenToBinOp(rule->tok);
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
        for(;;)
        {
            lookahead = _lex->next();
            if(lookahead.tt != Lexer::TOK_E_ERROR)
                break;
            errorAt(lookahead, lookahead.u.err);
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
    const char *p = beg;
    for( ; *p && *p != '\r' && *p != '\n'; ++p) {}
    const int linelen = int(p - beg);
    printf("|%.*s\n", linelen, beg);
    // The text of a token from @"..." is the decoded name and not in the source line; no caret then
    const bool caret = beg <= tok.begin && tok.begin <= p;
    if(caret || hint)
    {
        putchar('|');
        if(caret)
        {
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
    }

    hadError = true;
    panic = true;
    ++_nerrors;
}

void Parser::error(const char* msg, const char *hint)
{
    errorAt(prevtok, msg, hint);
}

void Parser::errorAtCurrent(const char* msg, const char *hint)
{
    errorAt(curtok, msg, hint);
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

    eatmatching(Lexer::TOK_RCUR, opening);

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
    // Note: The caller advances past tok
    if(tok.tt == Lexer::TOK_SINK || tok.tt == Lexer::TOK_WSINK)
        return _sink(tok);

    if(!_checkname(tok, whatfor))
        return _errnode();

    HLNode *node = ensure(hlir->ident(), tok);
    Str s = _tokenStr(tok);
    node->u.ident.nameStrId = s.id;
    _applyUsage(tok, node, usage, symref);
    return node;
}

HLNode* Parser::stmtlist(Lexer::TokenType endtok)
{
    HLNode *sl = ensure(hlir->list());
    sl->type = HLNODE_BLOCK;
    for(;;)
    {
        while(tryeat(Lexer::TOK_SEMICOLON)) {} // empty statements
        if(match(endtok) || match(Lexer::TOK_E_EOF))
            break;

        const Lexer::Token start = curtok;
        const unsigned errs = _nerrors;
        sl->u.list.add(declOrStmt(), gc);
        if(_nerrors != errs)
        {
            _sync(endtok);
            if(curtok == start) // guarantee progress
                advance();
        }
    }
    return sl;
}

static bool canStartStatement(Lexer::TokenType tt)
{
    switch(tt)
    {
        case Lexer::TOK_IDENT: case Lexer::TOK_SINK: case Lexer::TOK_WSINK: case Lexer::TOK_LPAREN: case Lexer::TOK_LCUR:
        case Lexer::TOK_DOLLAR: case Lexer::TOK_VAR: case Lexer::TOK_FUNC: case Lexer::TOK_IF:
        case Lexer::TOK_FOR: case Lexer::TOK_WHILE: case Lexer::TOK_RETURN: case Lexer::TOK_EMIT:
        case Lexer::TOK_BREAK: case Lexer::TOK_CONTINUE: case Lexer::TOK_EXPORT:
            return true;
        default:
            return false;
    }
}

// Error recovery: skip ahead to the next statement boundary, which is
// after a ';', before endtok, or a token on a new line that can start a statement.
// Skips over balanced {...} so that a block's closing brace isn't mistaken for endtok.
void Parser::_sync(Lexer::TokenType endtok)
{
    unsigned depth = 0;
    for(;;)
    {
        if(match(Lexer::TOK_E_EOF))
            break;
        if(!depth)
        {
            if(match(endtok))
                break;
            if(tryeat(Lexer::TOK_SEMICOLON))
                break;
            if(curtok.line != prevtok.line && canStartStatement(curtok.tt))
                break;
        }
        if(match(Lexer::TOK_LCUR))
            ++depth;
        else if(match(Lexer::TOK_RCUR) && depth)
            --depth;
        advance();
    }
    panic = false;
}

HLNode *Parser::emitConstant(const Lexer::Token& tok, const Val& v)
{
    if(v.type == PRIMTYPE_NORETURN)
        errorAt(tok, (const char*)v.u.p);

    HLNode *node = ensure(hlir->constantValue());
    if(node)
        node->u.constant.val = v;
    return node;
}

void Parser::outOfMemory()
{
    errorAtCurrent("out of memory");
}
