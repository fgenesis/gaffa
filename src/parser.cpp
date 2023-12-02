#include "defs.h"
#include "parser.h"
#include "util.h"
#include <string.h>
#include <sstream>

struct OpDef
{
    unsigned op;
    char s[4];
    int precedence;
};

const OpDef s_binops[] =
{
    {OP_ADD,      "+",    10, },
    {OP_SUB,      "-",    10, },
    {OP_MUL,      "*",    5 , },
    {OP_DIV,      "/",    5 , },
    {OP_INTDIV,   "//",   5 , },
    {OP_MOD,      "%",    5 , },
    {OP_BIN_AND,  "&",    3 , },
    {OP_BIN_OR,   "|",    3 , },
    {OP_BIN_XOR,  "^",    3 , },
    {OP_SHL,      "<<",   15, },
    {OP_SHR,      ">>",   15, },
    {OP_C_EQ,     "==",   20, },
    {OP_C_NEQ,    "!=",   20, },
    {OP_C_LT,     "<",    20, },
    {OP_C_GT,     ">",    20, },
    {OP_C_LTE,    "<=",   20, },
    {OP_C_GTE,    ">=",   20, },
    {OP_C_AND,    "&&",   30, },
    {OP_C_OR,     "||",   30, },
    {OP_EVAL_AND, "and",  40, },
    {OP_EVAL_OR,  "or",   40, },
    {OP_CONCAT,   "..",   15, },
};

const OpDef s_unops[] =
{
    {UOP_NOT,      "!",    0, },
    {UOP_POS,      "+",    0, },
    {UOP_NEG,      "-",    0, },
    {UOP_BIN_COMPL,"~",    0, },
    {UOP_TRY,      "??",   0, },
    {UOP_UNWRAP,   "*",    0, },
};

Top::Top(Parser* ps)
    : _ps(ps)
    , _p(ps->_p)
    , _linebegin(ps->_linebegin)
    , _astpos(ps->_ast.size())
    , _errpos(ps->_errors.size())
    , _accepted(false)
{
}

bool Top::accept()
{
    _ps->_errors.resize(_errpos); // rollback error
    _accepted = true;
    return true;
}

void Top::_rollback()
{
    _ps->_p = _p;
    _ps->_ast.resize(_astpos);
    // ... and accept error
}

bool Parser::parse(const char* code, size_t n)
{
    _line = 1;
    _end = code + n;
    _p = code;
    _linebegin = code;
    _maxerror.clear();
    _errors.clear();
    _maxerrorpos = code;

    for(;;)
    {
        if(_p == _end)
            return true;
        _skipws();
        if(!_p_stmt())
            return _error("Expected statement");
        _skipws();
    }
}

const char* Parser::getError() const
{
    return _maxerror.c_str();
}

bool Parser::_error(const char* info, const char *info2)
{
    std::ostringstream os;
    size_t charpos = _p - _linebegin + 1;
    os << "(" << _line << "," << charpos << "): Parse error: " << info;
    if(info2)
        os << " " << info2;
    _errors.push_back(os.str());

    if(_p > _maxerrorpos)
    {
        _maxerrorpos = _p;
        _maxerror = os.str();
    }

    return false;
}

bool Parser::_emit(const ASTNode& node)
{
    _ast.push_back(node);
    return true;
}

bool Parser::_emit(const Val& val)
{
    return _emit(ASTNode(val));
}

bool Parser::_eat(char c)
{
    unsigned same = *_p == c;
    _p += same;
    return same;
}

bool Parser::_eat(const char* s)
{
    size_t i = 0;
    while(s[i])
    {
        if(_p[i] != s[i])
            return _error("Expected ", s);
        ++i;
    }
    _p += i;
    return true;
}

bool Parser::_eatws(char c)
{
    return _skipws() && _eat(c) && _skipws();
}

bool Parser::_eatws(const char* s)
{
    return _skipws() && _eat(s) && _skipws();
}


struct WsNoms
{
    const char *p;
    const char *pnl; // first char after newline, NULL if no newline was skipped
    size_t nl;
};

static WsNoms eatws(const char *p)
{
    size_t nl = 0;
    const char *pnl = NULL;
    bool skiptoend = false;
    while(char c = *p)
        if(skiptoend || c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            ++p; // it's whitespace, skip it
            if(c == '\n' || c == '\r') // account for newlines for diagnostics
            {
                skiptoend = false;
                ++nl;
                if(*p != c && (*p == '\n' || *p == '\r'))
                    ++p; // skip one extra char in case of windows line endings
                pnl = p;
            }

        }
        else if(c == '-' && p[1] == '-') // for simplicity, consider comments whitespace
        {
            skiptoend = true;
            p += 2;
        }
        else
            break;
    WsNoms ret = {p, pnl, nl};
    return ret;
}

bool Parser::_ws() // mandatory whitespace
{
    WsNoms nom = eatws(_p);
    if(nom.p == _p)
        return _error("Expected whitespace");

    _p = nom.p;
    _line += nom.nl;
    if(nom.pnl)
        _linebegin = nom.pnl;
    return true;
}

bool Parser::_skipws() // optional whitespace
{
    WsNoms nom = eatws(_p);
    _p = nom.p;
    _line += nom.nl;
    if(nom.pnl)
        _linebegin = nom.pnl;
    return true;
}

bool Parser::_p_nil()
{
    return _eat("nil") && _emit(_Nil());
}

bool Parser::_p_bool()
{
    return (_eat("true") && _emit(true))
        || (_eat("false") && _emit(false))
        || _error("Expected bool literal");
}

bool Parser::_p__decimal(MaybeNum& num)
{
    num = strtouint(_p);
    bool ok = !!num;
    if(ok)
        _p += num.used;
    return ok || _error("Expected decimal");
}

bool Parser::_p__mantissa(real& f, uint i)
{
    MaybeNum m = strtouint(_p);

    if(!m)
        return false;

    uint div = 1;
    for(size_t i = 0; i < m.used; ++i)
        if (mul_check_overflow<uint>(&div, div, 10))
            return false;

    f = real(double(i) + (double(m.val.ui) / double(div)));
    _p += m.used;
    return true;
}

bool Parser::_p_numeric()
{
    Top top(this);
    const bool neg = _eat('-');
    MaybeNum m;
    const bool intpart = _p__decimal(m);
    if(!intpart)
        m.val.ui = 0;

    if(_eat('.'))
    {
        real f;
        return _p__mantissa(f, m.val.ui) && _emit(neg ? -f : f) && top.accept();
    }

    if(!intpart)
        return _error("Expected numeric, don't have int part");

    if(neg)
        m.val.si = -m.val.si;

    // integer
    if(_eat('u'))
        _emit(m.val.ui);
    else
        _emit(m.val.si);

    return top.accept();
}

bool Parser::_p_string()
{
    const char q = *_p;
    if(!(q == '"' || q == '\''))
        return false;

    const char *p = ++_p;
    bool ignorenext = false;
    char c;
    std::string tmp;
    while( (c = *p++) )
    {
        if(c == '"' || c == '\'')
            break;

        if(c != '\\')
            tmp += c;
        else switch(*p)
        {
        case 'x': case 'X': // TODO: hex
            assert(false);
            break;
        default:
            tmp += *p++;
        }
    }

    if(c != q)
        return _error("Unterminated string");

    unsigned ref = _pool.put(tmp);
    _p = p;

    return _emit(ASTNode(TT_LITERAL, _Str(ref)));
}

bool Parser::_p_literal()
{
    return _p_nil() || _p_bool() || _p_numeric() || _p_string() || _error("Expected literal");
}

// int i = ...
// int a, float b = ...
bool Parser::_p_decl()
{
    Top top(this);

    std::string tmp; // TODO make this ptr + size
    if(!_p__ident(tmp))
        return _error("Expected type name");
    if(!_ws())
        return _error("Expected whitespace after type name");
    const unsigned typenameid = _pool.put(tmp); // TODO: postfix '?'

    return (_emit(ASTNode(TT_DECL, Val(typenameid))) && _p_assign() && top.accept())
        || _error("Expected assignment after decl");
}

bool Parser::_p_stmt()
{
    return _skipws()
        && (_p_decl() || _p_primexpr() || _eat(';'))
        && _skipws();
}

bool Parser::_p_assign()
{
    Top top(this);

    std::string tmp;
    if(!_p__ident(tmp))
        return _error("Expected identifier");
    const unsigned varnameid = _pool.put(tmp);

    return _eatws('=') && _p_declexpr() && _skipws()
        && _emit(ASTNode(TT_ASSIGN, Val(_Str(varnameid)), varnameid))
        && top.accept();
}

static const char *eatident(const char * const in)
{
    const char *s = in;
    char c;
    while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        ++s;
    if(s != in)
        while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9')))
            ++s;
    return s;
}

bool Parser::_p__ident(std::string& str)
{
    const char *e = eatident(_p);
    if(e == _p)
        return _error("Invalid identifier");

    str.assign(_p, e - _p);
    _p = e;
    return true;
}

bool Parser::_p_identAsLiteral()
{
    std::string name;
    if(!_p__ident(name))
        return false;
    unsigned strid = _pool.put(name);
    return _emit(ASTNode(TT_LITERAL, _Str(strid)));
}

bool Parser::_p_declexpr()
{
    return _p_expr();
}

bool Parser::_p_expr()
{
    return _p_subexpr(); // TODO: precedence boundary
}


// very broken and WIP
bool Parser::_p_subexpr()
{
    Top top(this);
    const size_t phidx = _ast.size();
    _emit(TT_PLACEHOLDER);

    size_t opIdx;
    bool unop = _p__unop(opIdx) && _p_subexpr();
    if(!(unop || _p_simpleexpr()))
        return false;

    if(!_p__binop(opIdx))
    {
        if(unop)
        {
            _ast[phidx].tt = TT_UNOP;
            _ast[phidx].n = opIdx;
        }
        return top.accept(); // no binop? expr ends here
    }

    // FIXME: decide whether to use RPN or not

    _ast[phidx].tt = TT_BINOP;

    if(!_p_subexpr())
        return false;

    for(size_t n;;)
    {
        _emit(ASTNode(TT_BINOP, _Nil(), opIdx));
    }
}

bool Parser::_p_simpleexpr()
{
    return _p_literal() || _p_primexpr();
}

// NAME
// (expr)
bool Parser::_p_prefixexpr()
{
    Top top(this);
    return _skipws() && (
           _p_varref()
        || (_eatws('(') && _p_expr() && _eatws(')'))
        ) && top.accept();
}

// NAME           // var ref
// f(x)           // call
// obj:mth(x)     // method
// obj:[EXPR](x)  // dynamic method
bool Parser::_p_primexpr()
{
    if(!_p_prefixexpr())
        return false;

    Top top(this);
    for(;;)
    {
        TokenType tt = TT_INVALID;
        switch(*_p) // look-ahead
        {
            case '.': case '[':
                if((!_p_index(true) && top.accept()))
                    return false;
                break;
            case '(':
                tt = TT_FNCALL;
                break;
            case ':':
                ++_p;
                tt = TT_MTHCALL;
                break;
            default:
                return top.accept();
        }

        if(tt == TT_FNCALL || tt == TT_MTHCALL)
        {
            // notes:
            // - TT_MTHCALL followed by TT_INDEX followed by TT_LITERAL is a simple method call
            // - TT_MTHCALL followed by TT_INDEX followed by TT_EXPR is an expr method call
            _emit(ASTNode(tt));
            const size_t nodeidx = _ast.size() - 1;
            if(tt == TT_MTHCALL && !_p_index(false))
                return false;
            size_t nargs = 0;
            if(!_p_args(nargs) && top.accept())
                return false;
            _ast[nodeidx].n = nargs;
        }
    }
}

// caller must emit dummy ASTNode before both exprs that is then patched later
bool Parser::_p__binop(size_t& opIdx)
{
    for(size_t i = 0; i < Countof(s_binops); ++i)
        if(_eatws(s_binops[i].s))
        {
            opIdx = i;
            return true;
        }
    return _error("Expected binary operator");
}

bool Parser::_p__unop(size_t& opIdx)
{
    for(size_t i = 0; i < Countof(s_unops); ++i)
        if(_eatws(s_unops[i].s))
        {
            opIdx = i;
            return true;
        }
    return _error("Expected unary operator");
}

bool Parser::_p_varref()
{
    std::string name;
    if(_p__ident(name))
    {
        unsigned strid = _pool.put(name);
        return _emit(ASTNode(TT_VARREF, _Str(strid)));
    }
    return false;
}


// .Identifier
// [EXPR]
bool Parser::_p_index(bool dot)
{
    Top top(this);
    _emit(ASTNode(TT_INDEX));
    return (((!dot || _eatws('.')) && _p_identAsLiteral())
        || (_eatws('[') && _p_expr() && _eatws(']'))
    ) && top.accept();
}


// EXPR
// EXPR, EXPR, EXPR
size_t Parser::_p_exprlist()
{
    Top top(this);
    size_t n = 0;
    for(;;)
    {
        _skipws();
        if(!_p_expr())
            return 0;
        ++n;
        if(!_eatws(','))
            break;
    }
    if(n)
        top.accept();
    else
        _error("Expected non-empty expr list");
    return n;
}

// (EXPR, EXPR)
bool Parser::_p_args(size_t& outN)
{
    Top top(this);
    size_t n = 0;
    if(_eatws('('))
    {
        if(_eatws(')'))
            return top.accept(); // special case for empty arglist

        n = _p_exprlist();
        if(n && _eatws(')'))
        {
            outN = n;
            return top.accept();
        }
    }
    return false;
}
