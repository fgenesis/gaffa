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
	HLNODE_STMTLIST,
	HLNODE_FORLOOP,
	HLNODE_WHILELOOP,
	HLNODE_CONST_DECL,
	HLNODE_MUT_DECL,
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

struct HLStmtList
{
	enum { EnumType = HLNODE_STMTLIST };
	size_t used;
	size_t cap;
	HLNode **list;

	HLNode *add(HLNode *node, const GaAlloc& ga); // returns node, unless memory allocation fails
};

struct HLDecl
{
	// const is the default, but HLNode::type can be set to HLNODE_MUT_DECL to make mutable
	enum { EnumType = HLNODE_CONST_DECL };
	HLNode *var;
	HLNode *type; // if explicitly specified, otherwise NULL to auto-deduce
	HLNode *value;
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
		HLStmtList stmtlist;
		HLDecl decl;
	} u;
};


class HLIRBuilder
{
public:
	HLIRBuilder(const GaAlloc& a);
	~HLIRBuilder();

	inline HLNode *constantValue() { return allocT<HLConstantValue>(); }
	inline HLNode *unary() { return allocT<HLUnary>(); }
	inline HLNode *binary() { return allocT<HLBinary>(); }
	inline HLNode *conditional() { return allocT<HLConditional>(); }
	inline HLNode *stmtlist() { return allocT<HLStmtList>(); }

private:
	struct Block
	{
		Block *prev;
		size_t used;
		size_t cap;
		// payload follows
		HLNode *alloc();
	};
	template<typename T> inline HLNode *allocT()
	{
		return alloc(HLNodeType(T::EnumType));
	}
	HLNode *alloc(HLNodeType ty);
	void clear();
	Block *allocBlock(size_t sz);
	GaAlloc galloc;
	Block *b;
};
