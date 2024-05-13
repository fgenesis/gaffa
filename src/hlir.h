#pragma once

#include "gainternal.h"
#include "lex.h"

enum Comparison
{
	JUMP_ALWAYS,
	JUMP_EQ,
	JUMP_NEQ,
	JUMP_LT,
	JUMP_LTE,
	JUMP_GTE
};

enum HLNodeType
{
	HLNODE_NONE,
	HLNODE_CONSTANT_VALUE,
	HLNODE_UNARY,
	HLNODE_BINARY,
	HLNODE_CONDITIONAL,
	HLNODE_LIST,
	HLNODE_FORLOOP,
	HLNODE_WHILELOOP,
	HLNODE_ASSIGNMENT,
	HLNODE_VARDECLASSIGN,
	HLNODE_VARDEF,
	HLNODE_DECLLIST,
	HLNODE_RETURN,
	HLNODE_CALL,
	HLNODE_IDENT,
};

struct HLNode;

struct HLConstantValue
{
	enum { EnumType = HLNODE_CONSTANT_VALUE };
	Val val;
};

struct HLUnary
{
	enum { EnumType = HLNODE_UNARY };
	Lexer::TokenType tok;
	HLNode *rhs;
};

struct HLBinary
{
	enum { EnumType = HLNODE_BINARY };
	Lexer::TokenType tok;
	HLNode *lhs;
	HLNode *rhs;
};

struct HLConditional
{
	enum { EnumType = HLNODE_CONDITIONAL };
	HLNode *condition;
	HLNode *ifblock;
	HLNode *elseblock;
};

struct HLForLoop
{
	enum { EnumType = HLNODE_FORLOOP };
	HLNode *iter;
	HLNode *body;
};

struct HLWhileLoop
{
	enum { EnumType = HLNODE_WHILELOOP };
	HLNode *cond;
	HLNode *body;
};

struct HLList
{
	enum { EnumType = HLNODE_LIST };
	size_t used;
	size_t cap;
	HLNode **list;

	HLNode *add(HLNode *node, const GaAlloc& ga); // returns node, unless memory allocation fails
};

struct HLVarDef
{
	enum { EnumType = HLNODE_VARDEF };
	HLNode *ident;
	HLNode *type;
};

struct HLVarDeclList
{
	// const is the default, but HLNode::type can be set to HLNODE_MUT_DECL to make mutable
	enum { EnumType = HLNODE_VARDECLASSIGN };
	HLNode *decllist;
	HLNode *vallist;
};

struct HLAssignment
{
	// const is the default, but HLNode::type can be set to HLNODE_MUT_DECL to make mutable
	enum { EnumType = HLNODE_ASSIGNMENT };
	HLNode *identlist;
	HLNode *vallist;
};

struct HLReturn
{
	enum { EnumType = HLNODE_RETURN };
	HLNode *what;
};

struct HLBranchAlways
{
	enum { EnumType = HLNODE_NONE };
	HLNode *target;
};

struct HLFnCall
{
	enum { EnumType = HLNODE_CALL };
	HLNode *func;
	HLNode *paramlist;
};

struct HLIdent
{
	enum { EnumType = HLNODE_IDENT };
	const char *begin;
	size_t len;
};

// All of the node types intentionally occupy the same memory.
// This is so that a node type can be easily mutated into another,
// while keeping pointers intact.
// This is to make node-based optimization easier.
struct HLNode
{
	HLNodeType type;
	union
	{
		HLConstantValue constant;
		HLUnary unary;
		HLBinary binary;
		HLConditional conditional;
		HLList list;
		HLIdent ident;
		HLAssignment assignment;
		HLVarDeclList vardecllist;
		HLVarDef vardef;
		HLForLoop forloop;
		HLWhileLoop whileloop;
		HLReturn retn;
		HLBranchAlways branch;
		HLFnCall fncall;
	} u;
};


class HLIRBuilder
{
public:
	HLIRBuilder(const GaAlloc& a);
	~HLIRBuilder();

	inline HLNode *constantValue() { return allocT<HLConstantValue>(); }
	inline HLNode *unary()         { return allocT<HLUnary>();         }
	inline HLNode *binary()        { return allocT<HLBinary>();        }
	inline HLNode *conditional()   { return allocT<HLConditional>();   }
	inline HLNode *list()          { return allocT<HLList>();          }
	inline HLNode *forloop()       { return allocT<HLForLoop>();       }
	inline HLNode *whileloop()     { return allocT<HLWhileLoop>();     }
	inline HLNode *assignment()    { return allocT<HLAssignment>();   }
	inline HLNode *vardecllist()   { return allocT<HLVarDeclList>();   }
	inline HLNode *vardef()        { return allocT<HLVarDef>();        }
	inline HLNode *retn()          { return allocT<HLReturn>();        }
	inline HLNode *continu()       { return allocT<HLBranchAlways>();  }
	inline HLNode *brk()           { return allocT<HLBranchAlways>();  }
	inline HLNode *fncall()        { return allocT<HLFnCall>();  }
	inline HLNode *ident()         { return allocT<HLIdent>();  }


private:
	struct Block
	{
		Block *prev;
		size_t used;
		size_t cap;
		// payload follows
		HLNode *alloc();
	};
	template<typename T> inline HLNode *allocT(HLNodeType ty = HLNodeType(T::EnumType))
	{
		return alloc(ty);
	}
	HLNode *alloc(HLNodeType ty);
	void clear();
	Block *allocBlock(size_t sz);
	GaAlloc galloc;
	Block *b;
};
