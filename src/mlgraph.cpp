#include "mlgraph.h"



/*
Folding rules:
- variables are replaced with constants if their values are known
- any operator/function/method that is known at compile time because types are known can be inlined
  (instead of looking it up at runtime)
- (constant op constant) can be calculated and replaced with a constant
- functions known to be compile-time executable can be replaced with a value when all params are constant values
- implicit casts are fine where allowed (x+1 where x is a known sint; 1 is uint)
*/
void MLNode::fold(FoldTracker& ft)
{
}

bool MLNode::typecheck(TypeTracker& tt)
{
	return false;
}
