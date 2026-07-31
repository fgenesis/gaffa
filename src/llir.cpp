#include "llir.h"
#include <assert.h>
#include "mlir.h"

void LLCodegen::emit(LLCmd cmd, u32 a, u32 b, u32 c)
{
	LLIns *z = code.alloc_n(gc, 1); // TODO: handle OOM
	z->cmd = cmd;
	z->p[0] = a;
	z->p[1] = b;
	z->p[2] = c;
	// TODO: handle overflow (refuse codegen?)
	assert(z->p[0] == a);
	assert(z->p[1] == b);
	assert(z->p[2] == c);
}

void LLCodegen::load(Reg dst, const Val& v)
{
	tsize a = consts.put(v);
	loadk(dst, a);
}



void LLCodegen::lower(const MLNode& ml)
{
	const size_t nch = ml.m.nch;
	const MLNode *ch = nch ? ml.firstChild() : NULL;
	switch((MLCmd)ml.m.cmd)
	{
		case ML_IFELSE: assert(nch == 3);
		{
			Label Lelse = label();
			Label Lend = label();
			conditionAndJumpOnFail(ch[0], Lelse);
			lower(ch[1]);
			j(Lend);
			Lelse.here();
			lower(ch[2]);
			Lend.here();
		}
		break;

		case ML_WHILE: assert(nch == 2);
		{
			Label check = label();
			Label after = label();
			check.here();
			conditionAndJumpOnFail(ch[0], after);
			lower(ch[1]);
			j(check);
			after.here();
		}
		break;

		// TODO? DOWHILE
		/*{
			LabelId Lbegin = labelhere();
			lower(ch[0]);
			conditionAndJumpOnSuccess(ch[1], Lbegin);
		}
		break;*/

		case ML_FOR: assert(nch == 2);
		{
			Label forbody = label();
			Label forclose = label();
			Label fornext = label();
			Label forend = label();
			// TODO: push TWO scopes: for setup scope + for scope (with all labels)

			lower(ch[0]); // iterator decls
			// TODO: pop for setup scope
			j(fornext);
			forbody.here();
			lower(ch[1]); // body
			forclose.here();
			// TODO: close loop vars that are used as upvals
			fornext.here();
			// TODO: iternext
			forend.here();

			// TODO: pop for scope
		}
		break;

		case ML_DECL: assert(nch == 2);
		{
		}
		break;

		case ML_ASSIGN: assert(nch == 2);
		{
		}
		break;



	}
}

void LLCodegen::conditionAndJumpOnFail(const MLNode& ml, LabelId fail)
{
}

void LLCodegen::conditionAndJumpOnSuccess(const MLNode& ml, LabelId success)
{
}
