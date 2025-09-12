#include "lex.h"
#include "util.h"

struct ShortEntry
{
    char s[4];
    Lexer::TokenType tt;
};

// Order matters! Must make sure that single-char identifiers are not accepted
// before a multi-char token that starts with the same char
static const ShortEntry ShortTab[] =
{
    { "(",  Lexer::TOK_LPAREN, },
    { ")",  Lexer::TOK_RPAREN, },
    { "[",  Lexer::TOK_LSQ,    },
    { "]",  Lexer::TOK_RSQ,    },
    { "{",  Lexer::TOK_LCUR,   },
    { "}",  Lexer::TOK_RCUR,   },
    { "->", Lexer::TOK_RARROW, },
    { "<-", Lexer::TOK_LARROW, },
    { "++", Lexer::TOK_CONCAT, },
    { "+",  Lexer::TOK_PLUS,   },
    { "-",  Lexer::TOK_MINUS,  },
    { "*",  Lexer::TOK_STAR,   },
    { "//", Lexer::TOK_SLASH2X },
    { "/",  Lexer::TOK_SLASH,  },
    { ">>", Lexer::TOK_SHR,    },
    { "<<", Lexer::TOK_SHL,    },
    { "==", Lexer::TOK_EQ,     },
    { "=>", Lexer::TOK_FATARROW},
    { "=",  Lexer::TOK_CASSIGN },
    { ":=", Lexer::TOK_MASSIGN },
    { "!=", Lexer::TOK_NEQ,    },
    { "<=", Lexer::TOK_LTE,    },
    { ">=", Lexer::TOK_GTE,    },
    { "<",  Lexer::TOK_LT,     },
    { ">",  Lexer::TOK_GT,     },
    { "??", Lexer::TOK_QQM,    },
    { "?",  Lexer::TOK_QM,     },
    { ",",  Lexer::TOK_COMMA,  },
    { "::", Lexer::TOK_DBLCOLON},
    { ":",  Lexer::TOK_COLON   },
    { "...",Lexer::TOK_TRIDOT, },
    { "..", Lexer::TOK_DOTDOT, },
    { ".",  Lexer::TOK_DOT,    },
    { ";",  Lexer::TOK_SEMICOLON},
    { "!",  Lexer::TOK_EXCL    },
    { "^",  Lexer::TOK_HAT     },
    { "~",  Lexer::TOK_TILDE   },
    { "#",  Lexer::TOK_HASH    },
    { "&&", Lexer::TOK_LOGAND  },
    { "||", Lexer::TOK_LOGOR   },
    { "&",  Lexer::TOK_BITAND  },
    { "|",  Lexer::TOK_BITOR   },
    { "$",  Lexer::TOK_DOLLAR  },
};

struct Keyword
{
    const char *kw;
    Lexer::TokenType tt;
};

static const Keyword Keywords[] =
{
    { "nil",       Lexer::TOK_NIL      },
    { "true",      Lexer::TOK_TRUE     }, // TODO: these don't have to be keywords. could be toplevel constant typed values.
    { "false",     Lexer::TOK_FALSE    },
    { "if",        Lexer::TOK_IF       },
    { "else",      Lexer::TOK_ELSE     },
    { "func",      Lexer::TOK_FUNC     },
    { "var",       Lexer::TOK_VAR      },
    { "for",       Lexer::TOK_FOR      },
    { "while",     Lexer::TOK_WHILE    },
    { "const",     Lexer::TOK_CONST    },
    { "break",     Lexer::TOK_BREAK    },
    { "return",    Lexer::TOK_RETURN   },
    { "continue",  Lexer::TOK_CONTINUE },
    { "and",       Lexer::TOK_AND      },
    { "or",        Lexer::TOK_OR       },
    { "yield",     Lexer::TOK_YIELD    },
    { "export",    Lexer::TOK_EXPORT   },
    { "emit",      Lexer::TOK_EMIT     },
    { "defer",     Lexer::TOK_DEFER    },
};


Lexer::Lexer(const char* text)
    : _p(text),  _linebegin(text), _line(1)
{
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

// returns ptr to first char not part of ident
static const char *eatident(const char *s)
{
    char c;
    while((c = *s) && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9')))
        ++s;
    return s;
}

/*
static unsigned char hexnorm(unsigned char x)
{
    if(x >= '0' && x <= '9')
        return x;
    if(x >= 'A' && x <= 'F')
        return '0' + 10 + x - 'A';
    else if(x >= 'a' && x <= 'f')
        return '0' + 10 + x - 'a';

    return 0;
}
*/

// no parsing of escape sequences etc. just finds the end of the string.
// returns pointer to first char behind the closing "
static WsNoms eatstr(const char *p, char term)
{
    WsNoms ret = {NULL, NULL, 0};
    for(char c; (c = *p++); )
    {
        if(c == term)
        {
            ret.p = p;
            break;
        }
        if(c == '\\')
            ++p;
        else if(c == '\n')
        {
            ++ret.nl;
            ret.pnl = p + 1;
        }
    }
    return ret;
}

// very inaccurate. skips ahead until it doesn't look like a number (bin/hex/dec/anything) anymore.
// takes special care to catch a dot only once
static const char *eatnum(const char *p)
{
    bool dot = false;
    for(char c; (c = *p); )
    {
        if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        {} // nothing to do
        else if(c == '.')
        {
            if(p[1] == '.') // special case: don't treat '0..' as '0.'+'.'
                break;
            if(dot)
                break;
            dot = true;
        }
        else
            break;
        ++p;
    }
    return p;
}

static const unsigned match(const char *haystack, const char *needle)
{
    unsigned i = 0;
    for(;;)
    {
        const unsigned char n = needle[i];
        if(!n)
            return i;
        const unsigned char h = haystack[i];
        if(h != n)
            break;
        ++i;
    }
    return 0;
}

Lexer::Token Lexer::next()
{
    const char *p = skipws();
    const char c = *p;
    if(c >= 'a' && c <= 'z') // keyword or ident
    {
        // keyword (all keywords are lowercase)
        for(size_t i = 0; i < Countof(Keywords); ++i)
            if(unsigned len = match(p, Keywords[i].kw))
                return tok(Keywords[i].tt, p, p+len);
isident:
        return ident(p);

    }
    else if((c >= 'A' && c <= 'Z') || c == '_') // ident
        goto isident;
    else if((c >= '0' && c <= '9') || (c == '.' && (p[1] >= '0' && p[1] <= '9'))) // numeric
        return litnum(p);
    else if(c == '\"' || c == '\'') // "string literal"
        return litstr(p+1, c);
    else if(!c)
        return tok(TOK_E_EOF, p, p);
    else
        for(size_t i = 0; i < Countof(ShortTab); ++i)
            if(unsigned len = match(p, &ShortTab[i].s[0]))
                return tok(ShortTab[i].tt, p, p+len);

    return errtok("Unrecognized token");
}

const char* Lexer::GetTokenText(TokenType tt)
{
    for(size_t i = 0; i < Countof(Keywords); ++i)
        if(Keywords[i].tt == tt)
            return Keywords[i].kw;
    for(size_t i = 0; i < Countof(ShortTab); ++i)
        if(ShortTab[i].tt == tt)
            return &ShortTab[i].s[0];
    switch(tt)
    {
        case TOK_IDENT: return "identifier";
        case TOK_LITNUM: return "number literal";
        case TOK_LITSTR: return "string literal";
        default: ;
    }
    return NULL;
}

bool Lexer::IsKeyword(TokenType tt)
{
    for(size_t i = 0; i < Countof(Keywords); ++i)
        if(Keywords[i].tt == tt)
            return true;
    return false;
}

Lexer::Token Lexer::tok(TokenType tt, const char *where, const char *end)
{
    assert(where <= end);
    unsigned len = end - where;
    _p = end;
    Token t;
    t.tt = tt;
    t.begin = where;
    t.linebegin = _linebegin;
    t.line = _line;
    t.u.len = len;
    return t;
}

Lexer::Token Lexer::errtok(const char *msg)
{
    Token t;
    t.tt = TOK_E_ERROR;
    t.begin = _p;
    t.linebegin = _linebegin;
    t.line = _line;
    t.u.err = msg;
    return t;
}

const char* Lexer::skipws()
{
    WsNoms ws = eatws(_p);
    if(ws.pnl)
        _linebegin = ws.pnl;
    _line += ws.nl;
    return (_p = ws.p);
}

Lexer::Token Lexer::ident(const char* where)
{
    const char *end = eatident(where);
    TokenType tt = *where == '_' && where + 1 == end ? TOK_SINK : TOK_IDENT;
    return tok(tt, where, end);
}

Lexer::Token Lexer::litstr(const char* where, unsigned char term)
{
    WsNoms s = eatstr(where, term);
    if(!s.p)
        return errtok("Unterminated literal string");

    _line += s.nl;
    if(s.pnl)
        _linebegin = s.pnl;

    Token t = tok(TOK_LITSTR, where, s.p);
    t.begin++; // skip opening and closing "
    t.u.len--;
    return t;
}

Lexer::Token Lexer::litnum(const char* where)
{
    const char *end = eatnum(where);
    return tok(TOK_LITNUM, where, end);
}

