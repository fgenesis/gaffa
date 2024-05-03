#pragma once

#include "gainternal.h"


enum HLOp
{
	HLOP_NONE,
	HLOP_ADD,
	HLOP_SUB,
	HLOP_MUL,
	HLOP_DIV,
	HLOP_JMP,
};

enum Comparison
{
	JUMP_ALWAYS,
	JUMP_EQ,
	JUMP_NEQ,
	JUMP_LT,
	JUMP_LTE,
	JUMP_GTE
};

struct HLNode
{
	enum Type
	{
		NONE,
		CONSTANT_VALUE,
		UNARY,
		BINARY,
		CONDITIONAL,
	};
	Type type;
};

struct HLConstantValue : public HLNode
{
	enum { EnumType = CONSTANT_VALUE };
	Val val;
};

struct HLUnary : public HLNode
{
	enum { EnumType = UNARY };
	HLOp op;
	HLNode *rhs;
};

struct HLBinary : public HLNode
{
	enum { EnumType = BINARY };
	HLOp op;
	HLNode *lhs;
	HLNode *rhs;
};

struct HLConditional : public HLNode
{
	enum { EnumType = CONDITIONAL };
	HLNode *condition;
	HLNode *ifblock;
	HLNode *elseblock;
};



class HLIRBuilder
{
public:
	HLIRBuilder(const GaAlloc& a);
	~HLIRBuilder();

	inline HLConstantValue *constantValue() { return allocT<HLConstantValue>(); }
	inline HLUnary *unary() { return allocT<HLUnary>(); }
	inline HLBinary *binary() { return allocT<HLBinary>(); }
	inline HLConditional *conditional() { return allocT<HLConditional>(); }

private:
	struct Block
	{
		Block *prev;
		size_t used;
		size_t cap;
		// payload follows
		void *alloc(size_t sz);
	};
	template<typename T> inline T *allocT() { return (T*)alloc(sizeof(T), HLNode::Type(T::EnumType)); }
	HLNode *alloc(size_t sz, HLNode::Type ty);
	void clear();
	Block *allocBlock(size_t sz);
	GaAlloc galloc;
	Block *b;
};
