#include "defs.h"
#include "parser.h"
#include "util.h"
#include <string.h>

struct OpDef
{
    unsigned op;
    char s[4];
    int precedence;
    PrimType resultType;
};

const OpDef s_binops[] =
{
    {OP_ADD,      "+",    10, PRIMTYPE_UNK},
    {OP_SUB,      "-",    10, PRIMTYPE_UNK},
    {OP_MUL,      "*",    5 , PRIMTYPE_UNK},
    {OP_DIV,      "/",    5 , PRIMTYPE_UNK},
    {OP_INTDIV,   "//",   5 , PRIMTYPE_UNK},
    {OP_BIN_AND,  "&",    3 , PRIMTYPE_UNK},
    {OP_BIN_OR,   "|",    3 , PRIMTYPE_UNK},
    {OP_BIN_XOR,  "^",    3 , PRIMTYPE_UNK},
    {OP_SHL,      "<<",   15, PRIMTYPE_UNK},
    {OP_SHR,      ">>",   15, PRIMTYPE_UNK},
    {OP_C_EQ,     "==",   20, PRIMTYPE_BOOL},
    {OP_C_NEQ,    "!=",   20, PRIMTYPE_BOOL},
    {OP_C_LT,     "<",    20, PRIMTYPE_BOOL},
    {OP_C_GT,     ">",    20, PRIMTYPE_BOOL},
    {OP_C_LTE,    "<=",   20, PRIMTYPE_BOOL},
    {OP_C_GTE,    ">=",   20, PRIMTYPE_BOOL},
    {OP_C_AND,    "&&",   30, PRIMTYPE_BOOL},
    {OP_C_OR,     "||",   30, PRIMTYPE_BOOL},
    {OP_EVAL_AND, "and",  40, PRIMTYPE_UNK},
    {OP_EVAL_OR,  "or",   40, PRIMTYPE_UNK},
    {OP_CONCAT,   "..",   15, PRIMTYPE_STRING},
};

const OpDef s_unops[] =
{
    {UOP_POS,      "+",    0, PRIMTYPE_UNK},
    {UOP_NEG,      "-",    0, PRIMTYPE_UNK},
    {UOP_BIN_COMPL,"~",    0, PRIMTYPE_UNK},
    {UOP_TRY,      "??",   0, PRIMTYPE_UNK},
    {UOP_UNWRAP,   "*",    0, PRIMTYPE_UNK},
};

Top::Top(Parser* ps)
: _ps(ps), _p(ps->_p), _astpos(ps->_ast.size()), _accepted(false)
{
}

void Top::_rollback()
{
    _ps->_p = _p;
    _ps->_ast.resize(_astpos);
}

bool Parser::parse(const char* code, size_t n)
{
    _end = code + n;
    _p = code;

    for(;;)
    {
        if(_p == _end)
            return true;
        _skipws();
        if(!_p_decl())
            return false;
        _skipws();
    }
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
            return false;
        ++i;
    }
    _p += i;
    return true;
}

static const bool isws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const char *eatws(const char *p)
{
    while(isws(*p))
        ++p;
    return p;
}

bool Parser::_ws()
{
    const char * const oldp = _p;
    _p = eatws(_p);
    return _p != oldp;
}

bool Parser::_skipws()
{
    _p = eatws(_p);
    return true;
}

bool Parser::_p_nil()
{
    return _eat("nil") && _emit(_Nil());
}

bool Parser::_p_bool()
{
    return (_eat("true") && _emit(true))
        || (_eat("false") && _emit(false));
}

bool Parser::_p__decimal(MaybeNum& num)
{
    num = strtouint(_p);
    bool ok = !!num;
    if(ok)
        _p += num.used;
    return ok;
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
        return false;

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
    return false;
}

bool Parser::_p_literal()
{
    return _p_nil() || _p_bool() || _p_numeric() || _p_string();
}

bool Parser::_p_decl()
{
    Top top(this);

    std::string tmp; // TODO make this ptr + size
    if(!_p__ident(tmp))
        return false;
    if(!_ws())
        return false;
    const unsigned typenameid = _pool.put(tmp); // TODO: postfix '?'

    if(!_p__ident(tmp))
        return false;
    const unsigned varnameid = _pool.put(tmp);

    return _skipws() && _eat('=') && _skipws() && _p_declexpr() && _skipws()
        && _emit(ASTNode(TT_DECL, Val(_Str(typenameid)), varnameid))
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
        return false;

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
    Top top(this);

    return (_p_literal()
        || _p_prefixexpr()
        || (top.reset() && _p_expr() && _p_binop() && _p_expr())
        || (top.reset() && _p_unop() && _p_expr())
        ) && top.accept();
}

bool Parser::_p_prefixexpr()
{
    Top top(this);

    return (_p_varref()
        || _p_call()
        || (_eat('(') && _skipws() && _p_expr() && _skipws() && _eat(')'))
        ) && top.accept();
}

bool Parser::_p_binop()
{
    return false;
}

bool Parser::_p_unop()
{
    return false;
}

bool Parser::_p_varref()
{
    std::string name;
    if(_p__ident(name))
    {
        unsigned strid = _pool.put(name);
        return _emit(ASTNode(TT_VARREF, _Str(strid)));
    }

    Top top(this);
    _emit(ASTNode(TT_INDEX));
    if(_p_prefixexpr() && _skipws())
    {
        if(_eat('.') && _skipws() && _p_identAsLiteral())
            return top.accept();
        if((_eat('[') && _skipws() && _p_expr() && _skipws() && _eat(']')))
            return top.accept();
    }
    return false;
}


// f(x)
// obj:mth(x)
// obj:[EXPR](x)
bool Parser::_p_call()
{
    Top top(this);
    if(!_p_prefixexpr())
        return false;
    TokenType tt = TT_FNCALL;
    if(_skipws() && _eat(':') && _skipws())
        tt = TT_MTHCALL;
    _emit(ASTNode(tt));
    const size_t nodeidx = _ast.size();
    if(tt == TT_MTHCALL && !_p_key())
        return false;
    return _p_args(_ast[nodeidx].n) && top.accept();
}

// Identifier
// [EXPR]
bool Parser::_p_key()
{
    if(_p_identAsLiteral())
        return true;

    Top top(this);
    return _eat('[') && _skipws() && _p_expr() && _skipws() && _eat(']') && top.accept();
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
        _skipws();
        if(!_eat(','))
            break;
    }
    if(n)
        top.accept();
    return n;
}

// (EXPR, EXPR)
bool Parser::_p_args(size_t& outN)
{
    Top top(this);
    size_t n = 0;
    if(_eat('(') && _skipws())
    {
        if(_eat(')'))
            return top.accept(); // special case for empty arglist

        n = _p_exprlist();
        if(n && _skipws() && _eat(')'))
        {
            outN = n;
            return top.accept();
        }
    }
    return false;
}
