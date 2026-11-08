#include "lex.h"
#include "util.h"

#include <string.h>

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
    { "++", Lexer::TOK_CONCAT, },
    { "+%", Lexer::TOK_PLUSWRAP },
    { "+",  Lexer::TOK_PLUS,   },
    { "-%", Lexer::TOK_MINUSWRAP },
    { "-",  Lexer::TOK_MINUS,  },
    { "*%", Lexer::TOK_STARWRAP },
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
    { "<=>",Lexer::TOK_CMP,    },
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
    { "%",  Lexer::TOK_MOD     }
};

struct Keyword
{
    const char *kw;
    Lexer::TokenType tt;
};

static const Keyword Keywords[] =
{
    { "if",        Lexer::TOK_IF       },
    { "else",      Lexer::TOK_ELSE     },
    { "func",      Lexer::TOK_FUNC     },
    { "var",       Lexer::TOK_VAR      },
    { "for",       Lexer::TOK_FOR      },
    { "while",     Lexer::TOK_WHILE    },
    { "break",     Lexer::TOK_BREAK    },
    { "return",    Lexer::TOK_RETURN   },
    { "continue",  Lexer::TOK_CONTINUE },
    { "and",       Lexer::TOK_AND      },
    { "or",        Lexer::TOK_OR       },
    { "export",    Lexer::TOK_EXPORT   },
    { "emit",      Lexer::TOK_EMIT     },
    { "defer",     Lexer::TOK_DEFER    },
    // Below: Not handled yet, reserving for later
    { "like",      Lexer::TOK_LIKE     },
    { "do",        Lexer::TOK_DO       },
    { "try",       Lexer::TOK_TRY      },
    { "catch",     Lexer::TOK_CATCH    },
    { "orelse",    Lexer::TOK_ORELSE   },
    { "iterator",  Lexer::TOK_ITERATOR },
    { "pragma",    Lexer::TOK_PRAGMA   }
};


static int hexval(unsigned char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if(c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return 99;
}

static size_t utf8encode(char *dst, unsigned cp)
{
    if(cp < 0x80)
    {
        dst[0] = (char)cp;
        return 1;
    }
    if(cp < 0x800)
    {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if(cp < 0x10000)
    {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static inline Lexer::UnescResult unescErr(const char *s)
{
    Lexer::UnescResult ret = {};
    ret.err = s;
    return ret;
}

// Escapes: \n \t \r \0 \\ \" \' \a \b \f \v  \xHH  \u{H...} (UTF-8 encoded)
// A backslash before a line break keeps the line break.
Lexer::UnescResult Lexer::unescape(const char *s, const char *end)
{
    assert(s && s <= end);
    bool doit = false;
    for(const char *p = s; p < end; ++p)
        if(*p == '\\')
        {
            doit = true;
            break;
        }
    if(!doit)
        return unescErr(NULL); // Nothing to unescape

    char *dst = (char*)_strs.alloc(end - s + 1); // +1 for \0
    if(!dst)
        return unescErr("Out of memory");

    char * const beg = dst;
    for(const char *p = s; p < end; )
    {
        char c = *p++;
        if(c != '\\')
        {
            *dst++ = c;
            continue;
        }
        if(p >= end)
            return unescErr("String ends with a lone backslash"); // the lexer doesn't produce this, but be safe
        c = *p++;
        switch(c)
        {
            case 'n':  *dst++ = '\n'; break;
            case 't':  *dst++ = '\t'; break;
            case 'r':  *dst++ = '\r'; break;
            case '0':  *dst++ = '\0'; break;
            case 'a':  *dst++ = '\a'; break;
            case 'b':  *dst++ = '\b'; break;
            case 'f':  *dst++ = '\f'; break;
            case 'v':  *dst++ = '\v'; break;
            case '\\': *dst++ = '\\'; break;
            case '\"': *dst++ = '\"'; break;
            case '\'': *dst++ = '\''; break;
            case '\r': case '\n': // keep the line break, including \r\n
                *dst++ = c;
                if(p < end && (*p == '\r' || *p == '\n') && *p != c)
                    *dst++ = *p++;
                break;
            case 'x':
            {
                if(end - p < 2 || hexval(p[0]) >= 16 || hexval(p[1]) >= 16)
                    return unescErr("\\x needs exactly 2 hex digits");
                *dst++ = (char)(hexval(p[0]) * 16 + hexval(p[1]));
                p += 2;
                break;
            }
            case 'u':
            {
                if(p >= end || *p != '{')
                    return unescErr("\\u needs a code point in braces, eg. \\u{20AC}");
                ++p;
                unsigned cp = 0, ndig = 0;
                for( ; p < end && *p != '}'; ++p, ++ndig)
                {
                    const int d = hexval(*p);
                    if(d >= 16 || ndig >= 6)
                    {
                        ndig = 0; // Take the error path below
                        break;
                    }
                    cp = cp * 16 + d;
                }
                if(p >= end || !ndig)
                    return unescErr("Invalid code point in \\u{...}");
                ++p; // '}'
                // It's not up to the lexer to decide what is a valid unicode character.
                //if(cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                //    return "Code point in \\u{...} is not a valid Unicode scalar value";
                dst += utf8encode(dst, cp);
                break;
            }
            default:
                return unescErr("Unknown escape sequence in string");
        }
    }
    *dst++ = 0;

    UnescResult ret;
    ret.s = beg;
    ret.len = dst - beg - 1; // Does not include the terminating \0
    ret.err = NULL;
    return ret;
}

Lexer::Lexer(GC& gc, const char* text)
    : _p(text),  _linebegin(text), _line(1), _strs(gc)
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
        {
            if(!*p)
                break;
            if(*p != '\n') // an escaped newline is still counted in the next iteration
                ++p;
            continue;
        }
        if(c == '\n')
        {
            ++ret.nl;
            ret.pnl = p;
        }
    }
    return ret;
}

// very inaccurate. skips ahead until it doesn't look like a number (bin/hex/dec/anything) anymore.
// takes special care to catch a dot only once
static const char *eatnum(const char *p)
{
    bool dot = false;
    const bool hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
    for(char c; (c = *p); )
    {
        // signed exponent: 1e-5, 2E+3
        if(!hex && (c == 'e' || c == 'E') && (p[1] == '+' || p[1] == '-') && p[2] >= '0' && p[2] <= '9')
            p += 2;
        else if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
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
                if(eatident(p + len) == p + len) // keyword must not be directly followed by something that could be an identifier, otherwise it's an identifier
                    return tok(Keywords[i].tt, p, p+len);
isident:
        return ident(p);

    }
    else if((c >= 'A' && c <= 'Z') || c == '_') // ident
        goto isident;
    else if(c == '@')
    {
        if(p[1] == '\"' || p[1] == '\'')
            return atident(p+1);
        return errtok("Expected string literal after '@'", p+1);
    }
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

    const char *skip = p + 1;
    while((*skip & 0xC0) == 0x80) // skip the rest of a UTF-8 sequence
        ++skip;
    return errtok("Unrecognized token", skip);
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
        case TOK_SYNTHETIC: return "synthetic";
        case TOK_E_EOF: return "end of file";
        case TOK_SINK: return "_";
        case TOK_WSINK: return "___";
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

bool Lexer::IsReserved(TokenType tt)
{
    switch(tt)
    {
        case TOK_LIKE:
        case TOK_DO:
        case TOK_TRY:
        case TOK_CATCH:
        case TOK_ORELSE:
        case TOK_ITERATOR:
        case TOK_PRAGMA:
            return true;
        default:
            return false;
    }
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

Lexer::Token Lexer::errtok(const char *msg, const char *skipto)
{
    Token t;
    t.tt = TOK_E_ERROR;
    t.begin = _p;
    t.linebegin = _linebegin;
    t.line = _line;
    t.u.err = msg;
    assert(skipto > _p);
    _p = skipto;
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
    TokenType tt = TOK_IDENT;
    if(end - where == 1 && where[0] == '_')
        tt = TOK_SINK;
    else if(end - where == 3 && where[0] == '_' && where[1] == '_' && where[2] == '_')
        tt = TOK_WSINK;
    return tok(tt, where, end);
}

// @"name with spaces"
// Parse a literal string but make it a freeform identifier.
// The token's text is the decoded name, which is stored in the lexer and not part of the source.
Lexer::Token Lexer::atident(const char* where)
{
    Lexer::Token t = litstr(where + 1, *where);
    if(t.tt != TOK_LITSTR)
        return t;

    t.tt = TOK_IDENT;
    return t;
}

// where points to first actual character of literal (after the opening ")
Lexer::Token Lexer::litstr(const char* where, unsigned char term)
{
    WsNoms s = eatstr(where, term);
    if(!s.p)
    {
        const char *end = where;
        while(*end)
            ++end;
        return errtok("Unterminated literal string", end);
    }

    _line += s.nl;
    if(s.pnl)
        _linebegin = s.pnl;

    Token t = tok(TOK_LITSTR, where, s.p);
    t.u.len--; // skip closing "

    // Unescape if necessary
    UnescResult ur = unescape(t.begin, t.begin + t.u.len);
    if(ur.err)
    {
        t.tt = TOK_E_ERROR; // _p is already past the literal
        t.u.err = ur.err;
        return t;
    }
    else if(ur.s) // Something was unescaped, use that string instead
    {
        t.begin = ur.s;
        t.u.len = ur.len;
    }

    return t;
}

Lexer::Token Lexer::litnum(const char* where)
{
    const char *end = eatnum(where);
    return tok(TOK_LITNUM, where, end);
}

OperatorId Lexer::TokenToBinOp(TokenType tok)
{
    switch(tok)
    {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        case TOK_STAR: return OP_MUL;
        case TOK_PLUSWRAP: return OP_ADDWRAP;
        case TOK_MINUSWRAP: return OP_SUBWRAP;
        case TOK_STARWRAP: return OP_MULWRAP;
        case TOK_SLASH: return OP_FDIV;
        case TOK_SLASH2X: return OP_IDIV;
        case TOK_MOD: return OP_MOD;
        case TOK_SHL: return OP_SHL;
        case TOK_SHR: return OP_SHR;
        case TOK_BITAND: return OP_BITAND;
        case TOK_BITOR: return OP_BITOR;
        case TOK_HAT: return OP_XOR;
        case TOK_EQ: return OP_EQ;
        case TOK_NEQ: return OP_NEQ;
        case TOK_LT: return OP_LT;
        case TOK_LTE: return OP_LTE;
        case TOK_GT: return OP_GT;
        case TOK_GTE: return OP_GTE;
        case TOK_CMP: return OP_CMP;
        case TOK_LOGAND: return OP_LOGAND;
        case TOK_LOGOR: return OP_LOGOR;
        case TOK_CONCAT: return OP_CONCAT;
        case TOK_LSQ: return OP_GETINDEX;
        //case TOK_LSQ: return OP_SETINDEX; // This is set on demand by the parser
    }
    assert(false);
    return OP_ERROR;
}

struct OpSpelling
{
    OperatorId op;
    bool prefix;
    Lexer::TokenType tok[3]; // unused entries are TOK_E_ERROR
};

// Names of overloadable operators, in OperatorId order
static const OpSpelling OpSpellings[] =
{
    { OP_UPLUS,       true,  { Lexer::TOK_PLUS } },
    { OP_UNEG,        true,  { Lexer::TOK_MINUS } },
    { OP_UNOT,        true,  { Lexer::TOK_EXCL } },
    { OP_UBITNOT,     true,  { Lexer::TOK_TILDE } },
    { OP_ULEN,        true,  { Lexer::TOK_HASH } },
    { OP_UNEGWRAP,    true,  { Lexer::TOK_MINUSWRAP } },
    { OP_ADD,         false, { Lexer::TOK_PLUS } },
    { OP_SUB,         false, { Lexer::TOK_MINUS } },
    { OP_MUL,         false, { Lexer::TOK_STAR } },
    { OP_FDIV,        false, { Lexer::TOK_SLASH } },
    { OP_IDIV,        false, { Lexer::TOK_SLASH2X } },
    { OP_MOD,         false, { Lexer::TOK_MOD } },
    { OP_SHL,         false, { Lexer::TOK_SHL } },
    { OP_SHR,         false, { Lexer::TOK_SHR } },
    { OP_BITAND,      false, { Lexer::TOK_BITAND } },
    { OP_BITOR,       false, { Lexer::TOK_BITOR } },
    { OP_XOR,         false, { Lexer::TOK_HAT } },
    { OP_ADDWRAP,     false, { Lexer::TOK_PLUSWRAP } },
    { OP_SUBWRAP,     false, { Lexer::TOK_MINUSWRAP } },
    { OP_MULWRAP,     false, { Lexer::TOK_STARWRAP } },
    { OP_EQ,          false, { Lexer::TOK_EQ } },
    { OP_CMP,         false, { Lexer::TOK_CMP } },
    { OP_CONCAT,      false, { Lexer::TOK_CONCAT } },
    { OP_GETINDEX,    false, { Lexer::TOK_LSQ, Lexer::TOK_RSQ } },
    { OP_SETINDEX,    false, { Lexer::TOK_LSQ, Lexer::TOK_RSQ, Lexer::TOK_CASSIGN } },
    { OP_GETINDEXOPT, false, { Lexer::TOK_LSQ, Lexer::TOK_RSQ, Lexer::TOK_QM } },
    { OP_CALL,        false, { Lexer::TOK_LPAREN, Lexer::TOK_RPAREN } },
};

struct OpNames
{
    char names[_OP_MAX_FUNC][12];

    OpNames()
    {
        static_assert(Countof(OpSpellings) == _OP_MAX_FUNC - 1, "OpSpellings must have an entry for each operator function");
        memset(names, 0, sizeof(names));
        for(size_t i = 0; i < Countof(OpSpellings); ++i)
        {
            const OpSpelling& sp = OpSpellings[i];
            assert(sp.op == i + 1);
            char *s = &names[sp.op][0];
            if(sp.prefix)
                strcat(s, "prefix");
            for(size_t k = 0; k < Countof(sp.tok) && sp.tok[k] != Lexer::TOK_E_ERROR; ++k)
                strcat(s, Lexer::GetTokenText(sp.tok[k]));
            assert(strlen(s) < sizeof(names[0]));
        }
    }
};

static const OpNames s_opnames; // ShortTab is constant-initialized, so this can use it

const char *Lexer::GetOperatorName(OperatorId op)
{
    return op > OP_ERROR && op < _OP_MAX_FUNC ? &s_opnames.names[op][0] : NULL;
}

OperatorId Lexer::GetOperatorFromName(const char *s, size_t len)
{
    for(size_t i = 1; i < _OP_MAX_FUNC; ++i)
    {
        const char *name = &s_opnames.names[i][0];
        if(!strncmp(name, s, len) && !name[len])
            return (OperatorId)i;
    }
    return OP_ERROR;
}

bool Lexer::IsOperatorNamePrefix(const char *s, size_t len)
{
    for(size_t i = 1; i < _OP_MAX_FUNC; ++i)
        if(!strncmp(&s_opnames.names[i][0], s, len))
            return true;
    return false;
}

OperatorId Lexer::TokenToUnOp(TokenType tok)
{
    switch(tok)
    {
        case TOK_PLUS: return OP_UPLUS;
        case TOK_MINUS: return OP_UNEG;
        case TOK_EXCL: return OP_UNOT;
        case TOK_TILDE: return OP_UBITNOT;
        case TOK_HASH: return OP_ULEN;
        case TOK_MINUSWRAP: return OP_UNEGWRAP;
        case TOK_QQM: return OP_QQM;
        case TOK_STAR: return OP_UNWRAP;
    }

    assert(false);
    return OP_ERROR;
}
