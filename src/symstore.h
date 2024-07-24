#pragma once

#include "defs.h"
#include <vector>


enum ScopeType
{
	SCOPE_BLOCK,
	SCOPE_VALBLOCK,
	SCOPE_FUNCTION
};

enum ScopeReferral
{
	SCOPEREF_CONSTANT, // in any scope, but known to be constant value
	SCOPEREF_LOCAL,    // in local scope
	SCOPEREF_UPVAL,    // not in local scope but exists in an outer scope across a function boundary
	SCOPEREF_EXTERNAL, // unknown identifier
};

class Symstore
{
public:
	struct Sym
	{
		unsigned nameStrId;
		unsigned linedefined;
		unsigned lineused;
		unsigned usagemask; // MLSymbolRefContext
	};
	struct Frame
	{
		std::vector<Sym> syms;
		ScopeType boundary;
	};
	struct Lookup
	{
		const Sym *sym;
		ScopeReferral where;
	};

	Symstore();
	~Symstore();

	void push(ScopeType boundary);
	void pop(Frame& f);


	Lookup lookup(unsigned strid, unsigned line, unsigned usage);

	// Lookup::sym should be NULL, else it's the clashing symbol
	const Sym *decl(unsigned strid, unsigned line, unsigned usage); // If 0: ok; otherwise: line of conflicting decl

	std::vector<Sym> missing;

private:

	std::vector<Frame> frames;
};
