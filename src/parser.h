#pragma once

/* Basic grammar draft:

CODE: [STMT]
BLOCK: BlockPrefix? '{' CODE '}'
STMT: (DECL | EXPR | IF) ';'?
DECL: Type varname '=' DeclExpr
EXPR:  Literal | PrefixExpr | EXPR BinOp EXPR | UnOp EXPR
Literal: 'nil' | Bool | Num | Str
ExprList: {EXPR ','} EXPR
DeclExpr: 'nil' | EXPR
Args: '(' ExprList ')'
Key: Name | '[' EXPR ']'
FuncCall: PrefixExpr (Args | (':' Key Args))
Var: Name | PrefixExpr '[' EXPR ']' | PrefixExpr '.' Name
PrefixExpr: Name | FunctionCall | '(' EXPR ')'
Bool: 'false' | 'true'
operator: '+' | '*' | '/' | '//' | '-'
UnOp: operator | '!' | '~' | '??'
BinOp: operator | '|' | '&' | '||' | '&&' | '/' | '//'
BlockPrefix: 'comptime' | 'safe' | 'sloppy'
TypeName: Name | 'var' | 'any' | 'int' | 'uint' | 'float'
Type: TypeName '?'?

WS:  ' ' | '\t' | '\n' | '\r'
*/

#include "ast.h"
#include "util.h"
#include <vector>
#include <string>
#include "strings.h"

class Parser;

class Top
{
public:
    Top(Parser *ps);
    ~Top() { if(!_accepted) _rollback(); }
    bool accept() { _accepted = true; return true; }
    bool reset() { _rollback(); return true; }
private:
    void _rollback();
    Parser *_ps;
    const char *_p;
    size_t _astpos;
    bool _accepted;
};

class Parser
{
    friend class Top;
public:
    bool parse(const char *code, size_t n);
private:
    bool _emit(const ASTNode& node);
    bool _emit(const Val& val);
    template<typename T> bool _emit(T v) { return _emit(Val(v)); }

    bool _eat(char c);
    bool _eat(const char *s);
    bool _ws(); // mandatory whitespace; plus any extraneous whitespace
    bool _skipws(); // skip optional whitspace

    bool _p_nil();
    bool _p_bool();
    bool _p__decimal(MaybeNum& num);
    bool _p__mantissa(real& f, uint i);
    bool _p_numeric(); // parses (u)int (dec/hex/oct/bin), float,
    bool _p_string();
    bool _p_literal(); // any literal value: nil, bool, numeric

    bool _p_stmt();
    bool _p_decl();
    bool _p__ident(std::string& s);
    bool _p_identAsLiteral();
    bool _p_declexpr(); // decl: anything after a TYPE x = ...
    bool _p_expr();
    bool _p_prefixexpr();
    bool _p_binop();
    bool _p_unop();
    bool _p_varref();
    bool _p_call();
    bool _p_key();
    size_t _p_exprlist();
    bool _p_args(size_t& n);


    const char *_p, *_end;
    std::vector<ASTNode> _ast;
    StringPool _pool;
};

