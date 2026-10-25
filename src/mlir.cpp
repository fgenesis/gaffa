#include "mlir.h"
#include "hlir.h"
#include "serialio.h"
#include "valstore.h"
#include "symstore.h"
#include "runtime.h"
#include "hashfunc.h"

#include <sstream>

/* TODO:
- When constant folding calls, [], {}, or anything that created an object for which isMutableRefType() is true,
  the original expression must not be eliminated, unless we can shallow copy the object, instead of creating it.
  -> ie. it only contains values for which isMutableRefType() is false.
  We can still create
  Use case:
  type Pair = { int, int }
  var Pair = struct { int, int }
  etc, to be able to fold these at compile time when they appear inside of a function.

- Calling a non-comptime function or method on a comptime object, or a comptime method with non-comptime parameters,
  must immediately invalidate that object since it's no longer compile-time constant.
  -> Failing to optimize away any function call on an object makes it no longer comptime constant.
  ->Invalidating a comptime object means its comptime representation is shallow-copy-created at runtime if that is possible.

- Declarations can only be removed when every isMutableRefType() object stays comptime-constant

*/

struct MLWriter
{
    MLWriter(GC& gc) : gc(gc) {}
    PodArray<byte> arr;
    GC& gc;

    ~MLWriter()
    {
        arr.dealloc(gc);
    }

    void write(int x)
    {
        byte *dst;
        size_t oldsz = arr.sz;
        if(oldsz + 8 > arr.cap)
            dst = arr.alloc_n(gc, 32);
        else
            dst = arr.data() + arr.sz;

        u32 done = vu128enc(dst, zigzagenc(x));
        printf("%08X -> %08X -> %u\n", x, zigzagenc(x), done);
        arr.sz = oldsz + done;
    }

    void flush(BufSink *sk)
    {
        sk->Write(sk, arr.data(), arr.size());
        arr.sz = 0;
    }
};

struct MLDumper
{
    MLDumper(GC& gc) : tree(gc), dbg(gc), vals(gc) {}
    ~MLDumper() { q.dealloc(tree.gc); }
    Queue<const MLNode*> q;
    MLWriter tree;
    MLWriter dbg;
    ValStore vals;
};

// statements have no associated type
static bool mlirIsStmt(MLCmd cmd)
{
    if(cmd == ML_LIST)
        return true;
    if(cmd < _ML_OP_MAX)
        return false;

    switch(cmd)
    {
    case ML_NAMEDECL:
    case ML_DECL:
    case ML_ASSIGN:
    case ML_IFELSE:
    case ML_WHILE:
    case ML_FOR:
    case ML_RETURN:
    case ML_EMIT:
    case ML_EXPORT:
    case ML_SINK:  // not a stmt, but has no value either
    case ML_WSINK:
        return true;
    default: ;
    }
    return false;
}

static size_t mlirNumParams(MLCmd cmd)
{
    switch(cmd)
    {
        case ML_CONST:
        case ML_VAR:
        case _ML_UPVAL:
        case ML_NAMEDECL:
        case ML_DECL:
        case ML_FUNC:
        case _ML_FUNCIDX:
            return 1;

        default: ;
    }

    return 0;
}

static size_t mlirNumChildren(MLCmd cmd)
{
    if(cmd < _ML_OP_MAX)
        return GetOperatorArity((OperatorId)cmd);

    switch(cmd)
    {
        case ML_CONST:
        case ML_VAR:
        case _ML_UPVAL:
        case ML_EXT:
        case _ML_VAL:
        case _ML_FUNCIDX:
        case _ML_ERROR:
        case _ML_EMPTY:
            return 0;

        case ML_RETURN:
        case ML_EMIT:
        case ML_ITERPACK:
        case ML_NEW_ARRAY:
        case ML_NEW_TABLE:
        case ML_EXPORT:
        case ML_SINK:
        case ML_WSINK:
            return 1;

        case ML_NAMEDECL:
        case ML_DECL:
        case ML_ASSIGN:
        case ML_WHILE:
        case ML_FOR:
        case ML_GETINDEX:
        case ML_GETNS:
        case ML_CALLADJ:
            return 2;

        case ML_IFELSE:
        case ML_MTHCALL:
        case ML_FUNC:
            return 3;

        case ML_LIST:
            assert(false && "use MLNode::numchildren()");
        default: ;
    }

    assert(false);
    return 0;
}

struct MLSig
{
    // Exclude ML_LIST
    enum { Fields = _ML_MAX - 1, SigSize = 1 + ((Fields + 1) / 2) };
    uhash hash;

    MLSig()
    {
        byte sig[SigSize];
        size_t w = 0;
        sig[w++] = _ML_MAX;
        size_t t = 1;
        for(unsigned i = 1; i < _ML_MAX; ++i)
        {
            MLCmd cmd = (MLCmd)i;
            t <<= 4u;
            t |= !!mlirIsStmt(cmd) | (mlirNumParams(cmd) << 1u) | (mlirNumChildren(cmd) << 2u);
            if(t > 0xff)
            {
                sig[w++] = (byte)t;
                t = 1;
            }
        }
        if(t > 1)
            sig[w++] = (byte)t;

        hash = fnv1aHash(&sig[0], sizeof(sig));
    }

};

static const MLSig mlsig;


static void mlirDumpNode(MLDumper& dump, const MLNode *node, size_t idx)
{
    MLCmd cmd = (MLCmd)node->m.cmd;
    printf("dump mlir %u (cmd %u)\n", (unsigned)idx, node->m.cmd);

    size_t nch = 0;

    // special cases
    switch(cmd)
    {
        case _ML_VAL:
            dump.tree.write(ML_CONST);
            dump.tree.write(dump.vals.put(node->val));
            return;

        case ML_LIST:
            nch = node->list.len;
            if(nch == 1) // Skip lists that have exactly 1 child and emit that child in place of the list
                goto dochildren;
            // It's intentionally impossible to construct lists of length 1
            dump.tree.write(nch ? -(int)(nch - 1) : 0);
            break;

        default:
            nch = mlirNumChildren(cmd);
            dump.tree.write(cmd);
            break;

        case _ML_DEAD:
            unreachable();
    }

    // params follow directly after the enum type
    if(size_t nparams = mlirNumParams(cmd))
        for(size_t i = 0; i < nparams; ++i)
            dump.tree.write(node->m.p[i]);

    // dump each child. via queue because that way all child nodes for any node are sequential in memory,
    // plus there's no way that while decoding deep nesting could cause a stack overflow.
    if(nch)
    {
dochildren:
        const MLNode *c = node->firstChild();
        for(size_t i = 0; i < nch; ++i)
            dump.q.push(dump.vals.gc, &c[i]);
    }
}

void MLIR::dump(BufSink *sk, const StringPool& sp, Options options) const
{
    MLDumper dump(sp.gc);

    const bool debuginfo = !infos.empty() && !(options & STRIP_DEBUGINFO);
    const MLNode * const root = nodes.data();
    const MLNode *node = root;
    for(;;)
    {
        mlirDumpNode(dump, node, node - root);

        if(debuginfo)
        {
            size_t idx = indexOf(node);
            MLInfo info = infos[idx];
            dump.dbg.write(info.line);
            dump.dbg.write(info.column);
        }
        if(dump.q.empty())
            break;
        node = dump.q.pop();
    }

    byte hdr[] = MAGIC_FILE_VARIANT('M');
    sk->Write(sk, hdr, sizeof(hdr));

    dump.vals.serialize(sk, sp);

    dump.tree.flush(sk);

    // TODO: make this conditional
    dump.dbg.flush(sk);
}

MLNode* MLNode::firstChild()
{
    assert(m.cmd != _ML_VAL);
    return m.chOffs ? this + m.chOffs : NULL;
}

MLSub MLNode::aslist()
{
    MLSub ret;
    if(m.cmd == ML_LIST)
    {
        ret.ch = firstChild();
        ret.n = list.len;
    }
    else
    {
        ret.ch = this;
        ret.n = 1;
    }
    return ret;
}

void MLNode::makedummy()
{
    m.cmd = ML_LIST;
    list.len = 0;
}

const MLNode* MLNode::firstChild() const
{
    assert(m.cmd != _ML_VAL);
    return m.chOffs ? this + m.chOffs : NULL;
}

Val MLNode::asVal() const
{
    STATIC_ASSERT(STATIC_OFFSETOF(MLNode, m.cmd) >= STATIC_END_OF(MLNode, val.type));

    assert(isconst());
    return val;
}

void MLNode::setVal(const ValU& v)
{
    val = v;
    m.cmd = _ML_VAL; // Write this later in case the compiler decides to copy .val plus padding area
}

bool MLNode::isconst() const
{
    assert(m.cmd != ML_CONST); // This should not be present when loaded
    return m.cmd == _ML_VAL;
}

void MLNode::setError(sref s)
{
    m.cmd = _ML_ERROR;
    m.p[0] = s;
}

void MLNode::setError(StringPool& sp, const char* err)
{
    setError(sp.put(err).id);
}

size_t MLNode::numchildren() const
{
    return m.cmd != ML_LIST ? mlirNumChildren((MLCmd)m.cmd) : list.len;
}

Type MLNode::type() const
{
    MLCmd cmd = (MLCmd)m.cmd;
    assert(cmd != ML_CONST);

    if(cmd == _ML_VAL)
        return val.type;

    if(mlirIsStmt(cmd))
        return PRIMTYPE_NOTYPE;

    return x.exprtype;

}

// ------------------------------------------------------------

MLIR::MLIR(GC& gc)
    : gc(gc)
{
}

MLIR::~MLIR()
{
    nodes.dealloc(gc);
    infos.dealloc(gc);
    vars.dealloc(gc);
    externals.dealloc(gc);
    funcs.dealloc(gc);
    symToVarRemap.dealloc(gc);
}

size_t MLIR::indexOf(const MLNode* node) const
{
    assert(node);
    const MLNode *base = nodes.data();
    assert(base <= node && node < base + nodes.size());
    return node - base;
}

size_t MLIR::indexOf(const MLVar* v) const
{
    size_t offs = v - vars.data();
    assert(offs < vars.size());
    return offs;
}

const MLInfo * MLIR::infoOf(const MLNode * node) const
{
    size_t idx = indexOf(node);
    return idx < infos.size() ? &infos[idx] : NULL;
}

MLVar* MLIR::getVarFromSymid(size_t symid)
{
    size_t idx = symToVarRemap[symid];
    return &vars[idx];
}

static void visitRec(PodArray<MLNode>& nodes, MLVisitorPre pre, MLVisitorPost post, void *ud, u32 nodeidx, u32 parentidx)
{
    // Careful: Visiting may:
    // 1) reallocate nodes[] -> don't keep pointers
    // 2) Morph nodes into something else -> don't keep any values like #children, either
    MLPreVisitResult vis { VISIT_CONTINUE, 0 };
    if(pre)
        vis = pre(&nodes[nodeidx], &nodes[parentidx], ud);

    {
        MLNode *node = &nodes[nodeidx];
        if(size_t nch = node->numchildren())
        {
            size_t firstChildIdx = nodeidx + node->m.chOffs;
            for(size_t i = 0; i < nch; ++i)
                visitRec(nodes, pre, post, ud, firstChildIdx + i, nodeidx);
        }
    }

    if(post)
        post(&nodes[nodeidx], &nodes[parentidx], ud, vis.aux);
}

void MLIR::visit(MLVisitorPre pre, MLVisitorPost post, void *ud)
{
    visitRec(nodes, pre, post, ud, 0, NULL);
}

/*static void invalidatePost(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    node->m.cmd = _ML_DEAD;
}*/

void MLNode::invalidate()
{
    //visitRec(NULL, invalidatePost, NULL, this, NULL);
    m.cmd = _ML_DEAD;
}

// This is put on any external symToVarRemap[] so that referencing an external symbol accentidally
// even though a local is intended it WILL cause an assert to fail
enum { EXTERNAL_BIT = 1 << 31 };

// does not reallocate nodes[]
void MLIR::_cons(Queue<Cons>& q, MLNode *dst, const HLNode *hl)
{
    dst->m.chOffs = 0;
    if(hl)
    {
        dst->m.cmd = 0xff; // For debugging: Mark as queued
        Cons next { hl, indexOf(dst) };
        q.push(gc, next);
    }
    else
        dst->makedummy();
}

static void makeEmpty(MLNode *node)
{
    node->m.cmd = _ML_EMPTY;
    node->m.chOffs = 0;
    node->x.exprtype = PRIMTYPE_AUTO;
}

// '_' or '___' with an optional type
void MLIR::_sink(Queue<Cons>& q, MLNode *& dst, const HLNode *sink, const HLNode *type)
{
    assert(sink->type == HLNODE_SINK);
    MLCh s = _setupCh(dst, sink->tok == Lexer::TOK_WSINK ? ML_WSINK : ML_SINK);
    MLNode *te = &nodes[s.chIdx];
    if(type)
        _cons(q, te, type);
    else
        makeEmpty(te);
}

// Returns the var index of the first declared var. The vars are consecutive; sinks have no var.
u32 MLIR::_decllist(Queue<Cons>& q, MLNode *& dst, const HLNode* decllist, const StringPool& sp)
{
    if(!decllist || !decllist->numchildren())
    {
        dst->makedummy();
        return 0;
    }
    const HLNode * const *dch = decllist->children();
    const size_t dstidx = indexOf(dst);

    MLCh s = _setupList(dst, decllist->numchildren());
    assert(s.n);

    u32 firstvar = 0;
    u32 nvars = 0;

    printf("MLIR::_decllist size %u\n", (unsigned)s.n);
    for(size_t i = 0; i < s.n; ++i)
    {
        const HLVarDef *vd = dch[i]->as<HLVarDef>();
        MLNode *ch = &nodes[s.chIdx + i]; // _sink() may reallocate
        if(vd->ident->type == HLNODE_SINK)
        {
            _sink(q, ch, vd->ident, vd->type);
            continue;
        }

        const HLIdent *id = vd->ident->as<HLIdent>();
        const char *name = sp.lookup(id->nameStrId).s;
        printf("ML decl %u '%s', symid %u, strid %u\n", (unsigned)i, name, id->symid, id->nameStrId);

        // Symbols are declared in a row. Their symids may have externals in between
        // (eg. type names first used in the list), but those don't get vars.
        const u32 varidx = symToVarRemap[id->symid];
        assert(!(varidx & EXTERNAL_BIT));
        if(!nvars)
            firstvar = varidx;
        assert(varidx == firstvar + nvars);
        ++nvars;

        if(vd->type)
            _cons(q, ch, vd->type);
        else
            makeEmpty(ch); // type deduced from the value
    }

    dst = &nodes[dstidx];
    return firstvar;
}

// Unnamed variadic params ('uint ...') and '_' have no var
static bool allParamsNamed(const HLNode *decllist)
{
    const size_t n = decllist ? decllist->numchildren() : 0;
    const HLNode * const *dch = n ? decllist->children() : NULL;
    for(size_t i = 0; i < n; ++i)
    {
        const HLNode *id = dch[i]->as<HLVarDef>()->ident;
        if(!id || id->type != HLNODE_IDENT)
            return false;
    }
    return true;
}

MLIR::MLCh MLIR::_setupChDefault(Queue<Cons>& q, MLNode* node, const HLNode* hl, MLCmd cmd)
{
    const HLAssignment& a = hl->u.assignment;
    const MLCh s = _setupCh(node, cmd);
    MLNode *ch = &nodes[s.chIdx];
    const size_t N = hl->numchildren();
    assert(N <= s.n);
    const HLNode * const * hlch = hl->children();
    for(size_t i = 0; i < N; ++i)
        _cons(q, &ch[i], hlch[i]);
    return s;
}

void MLIR::_opr(Queue<Cons>& q, MLNode *& dst, OperatorId op, const HLNode *hl)
{
    assert(op != OP_ERROR && op < _OP_MAX);
    MLCh s = _setupCh(dst, (MLCmd)op);
    assert(s.n == GetOperatorArity(op));
    assert(s.n == hl->numchildren());
    const HLNode * const *hlch = hl->children();
    MLNode *ch = &nodes[s.chIdx];
    for(size_t i = 0; i < s.n; ++i)
        _cons(q, &ch[i], hlch[i]);
}

// Don't call this recursively, call _cons() instead
void MLIR::_construct(Queue<Cons>& q, MLNode *dst, const HLNode *hl, StringPool& sp)
{
    const HLNodeType hltype = (HLNodeType)hl->type;

    switch(hltype)
    {
        case HLNODE_BLOCK:
        case HLNODE_LIST:
        {
dolist:
            const MLCh s = _setupList(dst, hl->numchildren()); // add all children
            const HLNode * const *hlch = hl->children();
            MLNode *ch = &nodes[s.chIdx];
            for(size_t i = 0; i < s.n; ++i)
                _cons(q, &ch[i], hlch[i]);
            return;
        }

        case HLNODE_CONSTANT_VALUE:
            dst->setVal(hl->u.constant.val);
            return;

        case HLNODE_IDENT:
            dst->tmp.hlnode = hl;
            dst->m.cmd = _ML_UVAR; // To be resolved later
            return;

        case HLNODE_VARDECLASSIGN:
        {
            MLCh s = _setupCh(dst, ML_DECL);
            assert(s.n == 2);

            const HLNode *decls = hl->u.vardecllist.decllist;
            MLNode *ch = &nodes[s.chIdx];
            const size_t idx = indexOf(dst);
            const u32 firstvar = _decllist(q, ch, decls, sp); // may reallocate nodes[]
            nodes[idx].m.p[0] = firstvar;

            // Continue expanding RHS expressions
            const HLNode *vals = hl->u.vardecllist.vallist;
            _cons(q, &ch[1], vals);
            return;
        }

        case HLNODE_FUNCDECL:
        {
            const HLFuncDecl& decl = hl->u.funcdecl;
            if(decl.namespac) // namespaced names are not symbols, just names
            {
                dst->m.p[0] = decl.ident->as<HLName>()->nameStrId;
                dst->m.p[1] = 0;
            }
            else
            {
                const HLIdent *hname = decl.ident->as<HLIdent>();
                dst->m.p[0] = hname->nameStrId;
                dst->m.p[1] = hname->symid;
            }

            MLCh s = _setupCh(dst, ML_NAMEDECL);
            assert(s.n == 2);
            MLNode *ch = &nodes[s.chIdx];

            _cons(q, &ch[0], decl.namespac);
            _cons(q, &ch[1], decl.value);
            return;
        }

        case HLNODE_FUNCTION:
        {
            const HLFunction& f = hl->u.func;
            const HLFunctionHdr *fh = f.hdr->as<HLFunctionHdr>();
            if(!allParamsNamed(fh->paramlist))
            {
                dst->setError(sp, "Variadic and '_' parameters not implemented yet in MLIR");
                return;
            }

            MLCh s = _setupCh(dst, ML_FUNC);
            assert(s.n == 3);
            MLNode *ch = &nodes[s.chIdx];

            const size_t idx = indexOf(dst);
            const u32 firstvar = _decllist(q, ch, fh->paramlist, sp); // may reallocate nodes[]
            nodes[idx].m.p[0] = firstvar;

            _cons(q, &ch[1], fh->rettypes);
            _cons(q, &ch[2], f.body);
            return;
        }

        case HLNODE_RETURNEMIT:
        {
            const HLReturnEmit& ry = hl->u.retn;
            MLCmd cmd;
            switch(hl->tok)
            {
                case Lexer::TOK_RETURN: cmd = ML_RETURN; break;
                case Lexer::TOK_EMIT: cmd = ML_EMIT; break;
                default: unreachable();
            }
            MLCh s = _setupCh(dst, cmd);
            assert(s.n == 1);
            MLNode *ch = &nodes[s.chIdx];
            _cons(q, &ch[0], ry.what);
            return;
        }

        case HLNODE_UNARY:
            _opr(q, dst, hl->u.unary.opid, hl);
            return;

        case HLNODE_BINARY:
            _opr(q, dst, hl->u.binary.opid, hl);
            return;

        case HLNODE_ARRAYCONS:
        {
            MLCh s = _setupCh(dst, ML_NEW_ARRAY);
            assert(s.n == 1);
            dst = &nodes[s.chIdx];
            goto dolist;
        }

        case HLNODE_TABLECONS:
        {
            MLCh s = _setupCh(dst, ML_NEW_TABLE);
            assert(s.n == 1);

            const HLNode * const *hlch = hl->children();
            const size_t nch = hl->numchildren();
            assert(nch % 2 == 0);
            size_t numkv = 0;
            for(size_t i = 0; i < nch; i += 2)
                numkv += !!hlch[i];
            dst->m.p[0] = numkv; // For parsing: This many entries are pairs, the rest is just values
            const size_t onlyv = (nch - numkv * 2) / 2;

            MLNode *ch = &nodes[s.chIdx];
            MLCh ls = _setupList(ch, numkv * 2 + onlyv);
            ch = &nodes[ls.chIdx]; // This now points to the list elements

            // Key+value goes first...
            MLNode *kv = ch;
            for(size_t i = 0; i < nch; i += 2)
                if(const HLNode *hk = hlch[i])
                {
                    assert(hlch[i+1]);
                    if(hk->type == HLNODE_NAME) // Ensure that names are really just literal strings, ie. {k=v} becomes {["k"]=v}
                        kv[0].setVal(Val(_Str(hk->u.name.nameStrId)));
                    else
                        _cons(q, &kv[0], hk);
                    _cons(q, &kv[1], hlch[i+1]);
                    kv += 2;
                }

            // ... afterwards just values
            MLNode *a = &ch[numkv * 2];
            for(size_t i = 0; i < nch; i += 2)
                if(!hlch[i])
                    _cons(q, a++, hlch[i+1]);

            assert(a == &ch[ls.n]); // Ensure we got exactly as many children as expected
            return;

        }

        case HLNODE_ASSIGNMENT:
        {
            const HLNode *dsts = hl->u.assignment.dstlist;
            const size_t n = dsts->numchildren();
            for(size_t i = 0; i < n; ++i)
            {
                const HLNodeType t = (HLNodeType)dsts->children()[i]->type;
                if(t != HLNODE_IDENT && t != HLNODE_SINK)
                {
                    dst->setError(sp, "Assigning to fields, indexes and namespaces is not implemented yet in MLIR");
                    return;
                }
            }
            _setupChDefault(q, dst, hl, ML_ASSIGN);
            return;
        }

        case HLNODE_SINK: // assignment target
            _sink(q, dst, hl, NULL);
            return;

        case HLNODE_CALL:
            _setupChDefault(q, dst, hl, (MLCmd)OP_CALL);
            return;

        case HLNODE_MTHCALL:
            _setupChDefault(q, dst, hl, ML_MTHCALL);
            return;

        case HLNODE_INDEX:
            _setupChDefault(q, dst, hl, ML_GETINDEX);
            return;

        case HLNODE_NS_INDEX:
            _setupChDefault(q, dst, hl, ML_GETNS);
            return;

        case HLNODE_CALLADJ:
            _setupChDefault(q, dst, hl, ML_CALLADJ);
            return;


        case HLNODE_FUNCTIONHDR: // handled as part of HLNODE_FUNCTION
            assert(false);
            break;

        // not adding a default label here to keep compiler warnings
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "HLIR node type %u not implemented yet in MLIR", hltype);
    dst->setError(sp, buf);
}

void MLIR::construct(const HLNode* root, Symstore& syms, StringPool& sp, Options options)
{
    _setupVars(syms, sp);

    Queue<Cons> q;
    PodArray<size_t> unresolved; // index of each node to fix
    unresolved.reserve(gc, syms.knownSize());

    Cons r { root, indexOf(_add(1)) };
    for(;;)
    {
        _construct(q, &nodes[r.mlidx], r.hl, sp); // This may reallocate nodes[], don't keep a pointer

        if(!(options & STRIP_DEBUGINFO))
        {
            if(infos.size() < nodes.size())
            {
                const size_t n = nodes.size() - infos.size();
                MLInfo *ni = infos.alloc_n(gc, n);
                memset(ni, 0, n * sizeof(MLInfo)); // not all nodes come from a HLNode
            }

            MLInfo &info = infos[r.mlidx];
            info.line = r.hl->line;
            info.column = r.hl->column;
        }

        switch((MLCmd)nodes[r.mlidx].m.cmd)
        {
            case _ML_UVAR:
                unresolved.push_back(gc, r.mlidx);
                break;

            default: ;
        }

        if(q.empty())
            break;
        r = q.pop();
    }

    q.dealloc(gc);

    _resolveVars(unresolved.data(), unresolved.size(), syms);

    unresolved.dealloc(gc);
}

// Note: This could be split up into a function that does the vars and a postproc step
// instead of pushing into unresolved vec (above). Would remove an allocation.
void MLIR::_setupVars(Symstore& syms, StringPool& sp)
{
    const size_t nsyms = syms.knownSize();
    vars.reserve(gc, nsyms); // Need at least this many; maybe some more for upvalues

    symToVarRemap.resize(gc, nsyms);

    // Setup all symbols
    for(size_t symid = 0; symid < nsyms; ++symid)
    {
        // This inits only the fields that are serialized; the rest stays uninited for now until folding
        const Symstore::Sym *sym = syms.getsym(symid);
        bool mut = sym->referencedHow & SYMREF_MUTABLE;

        if(sym->referencedHow & SYMREF_EXTERNAL)
        {
            assert(!mut);
            assert(!(sym->usage & SYMUSE_UPVAL));

            u32 extid = (u32)externals.size();
            symToVarRemap[symid] = extid | EXTERNAL_BIT;

            MLExternal *e = externals.alloc_n(gc, 1);
            e->name = sym->nameStrId;

            printf("EXT[%u]: %s @ symidx %u\n",
                extid, sp.lookup(e->name).s, (unsigned)symid);
        }
        else
        {
            symToVarRemap[symid] = (u32)vars.size();

            MLVar *v = vars.alloc_n(gc, 1);

            v->name = sym->nameStrId;
            v->kind = mut ? MLVar::MUTVAR : MLVar::CVAR;

            printf("VAR[%u]: %s [kind %u] @ symidx %u\n",
                symToVarRemap[symid], sp.lookup(v->name).s, v->kind, (unsigned)symid);
        }
    }
}

void MLIR::_resolveVars(const size_t *unresolved, size_t N, Symstore& syms)
{
    printf("Resolve %u occurences\n", (unsigned)N);
    for(size_t i = 0; i < N; ++i)
    {
        MLNode *node = &nodes[unresolved[i]];

        switch((MLCmd)node->m.cmd)
        {
            case _ML_UVAR: // Resolve and decide whether it becomes a local or an external
            {
                // TODO: given EXTERNAL_BIT, can we get rid of storing the HLNode?
                const HLIdent *hlident = node->tmp.hlnode->as<HLIdent>();
                ScopeReferral referral = (ScopeReferral)hlident->ex.u.scoperef;
                assert(referral != SCOPEREF_NOREF);
                const Symstore::Sym *sym = syms.getsym(hlident->symid);

                node->m.cmd = (sym->referencedHow & SYMREF_EXTERNAL) ? ML_EXT : ML_VAR;
                u32 idx = symToVarRemap[hlident->symid]; // var idx or ext idx
                node->m.p[0] = idx & ~EXTERNAL_BIT;
                break;
            }

            default:
                unreachable();
        }
    }
}

typedef void (*ConvertFunc)(MLNode& m, HLNode& h);

void MLIR::convertVarDef(MLNode& m, HLNode& h)
{
    HLVarDef *def = h.as<HLVarDef>();
    //def->ident->as<HLIdent>()->
}

MLNode* MLIR::_add(size_t n)
{
    return n ? nodes.alloc_n(gc, n) : NULL;
}

MLIR::MLCh MLIR::_setupCh(MLNode *& node, MLCmd cmd)
{
    assert(cmd != ML_LIST);
    size_t idx = indexOf(node);
    size_t n = mlirNumChildren(cmd);
    MLNode *ch = _add(n); // may reallocate & invalidate node. TODO: handle alloc fail
    node = &nodes[idx]; // fix ptr
    node->m.chOffs = ch ? ch - node : 0;
    node->m.cmd = cmd;

    for(size_t i = 0; i < n; ++i) // For debugging
        ch[i].m.cmd = 0xfc;

    MLCh ret;
    ret.n = n;
    ret.chIdx = indexOf(ch);
    return ret;
}

MLIR::MLCh MLIR::_setupList(MLNode *& node, size_t n)
{
    // There are no lists of length 1 -> re-use already existing node as the one list child.
    // This is just an optimizaton and the code should be correct with or without this shortcut.
    MLNode *ch;
    if(n != 1)
    {
        size_t idx = indexOf(node);
        ch = _add(n); // may reallocate & invalidate node. TODO: handle alloc fail
        node = &nodes[idx]; // fix ptr
        node->m.cmd = ML_LIST;
        node->list.len = n;
        node->m.chOffs = ch ? ch - node : 0;
    }
    else
    {
        ch = node;
    }

    for(size_t i = 0; i < n; ++i)
        ch[i].m.cmd = 0xf1;

    MLCh ret;
    ret.n = n;
    ret.chIdx = ch ? indexOf(ch) : 0;
    return ret;
}

struct MLFoldTracker
{
    MLIR &mlir;
    VM &vm;
    Symstore& syms;
    SymTable &env;
    bool ok;
    // -----

    std::string filename;
    std::vector<u32> typesrc; // x = typesrc[y] -> Which other node x did get node y its type from
    std::vector<u32> funcStack; // indices in MLIR::funcs[], so that [i-1] encloses [i]
    std::vector<DFunc*> funcProtos; // DFunc objects that need to be built early for reasons and have yet to be integrated

    enum { NONE = u32(-1) };

    // How the number of values must match the targets in declarations and assignments.
    // Folding runs in source order, so this can be switched at any point (eg. by a pragma).
    enum AssignMode
    {
        ASSIGN_EXACT,       // default: as many values as targets, unless a trailing '___' takes the rest
        ASSIGN_DROP_EXCESS, // Lua-like: at least as many values as targets; the rest is dropped
    };
    AssignMode assignMode;

    // Functions currently being folded, innermost last
    struct FoldFunc
    {
        u32 nodeidx;
        u32 varidx;   // var that a named function declaration assigns to, or NONE
        Type declrets; // type list; PRIMTYPE_AUTO if not declared
        Type deducedrets; // type list; PRIMTYPE_AUTO until the first return
    };
    std::vector<FoldFunc> foldFuncs;

    // Per var: index in MLIR::funcs[] of the function that has it as a local, and its slot there
    std::vector<u32> varOwner;
    std::vector<u32> varSlot;
    u32 nextFunc; // next function index while splitting

    void error(const MLNode *where, const char *msg);
    void warn(const MLNode *where, const char *msg);
    void info(const MLNode *where, const char *msg);
    void extrainfo(const MLNode *where);

    // Ensures sub < reference. 'where' is the thing to typecheck, 'decl' is where reference comes from
    bool checktype(Type sub, Type reference, const char *what, const MLNode *where, const MLNode *decl);

    // Helpers because lazy
    inline Runtime& rt() { return *vm.rt; }
    inline GC& gc() { return rt().gc; }
    inline StringPool& sp() { return rt().sp; }
    inline TypeRegistry& tr() { return rt().tr; }
    inline Strp str(sref id) { return sp().lookup(id); }
    inline Str putstr(const char *s) { return sp().put(s); }

    const DFunc *getObjectOperator(const GCobj *obj, OperatorId op);
    const DFunc *getCallable(const GCobj *obj);
};

void MLFoldTracker::extrainfo(const MLNode *where)
{
    switch((MLCmd)where->m.cmd)
    {
        case ML_VAR:
            if(const MLVar *v = &mlir.vars[where->m.p[0]])
                if(v->decl)
                    info(&mlir.nodes[v->decl], "Declared here");
        break;

        default: ;
    }
}

void MLFoldTracker::info(const MLNode* where, const char *msg)
{
    const MLInfo *info = mlir.infoOf(where);
    const u32 line = info ? info->line : 0;
    printf("(%s:%u): %s\n", filename.c_str(), line, msg);
}

void MLFoldTracker::error(const MLNode* where, const char *msg)
{
    const MLInfo *info = mlir.infoOf(where);
    const u32 line = info ? info->line : 0;
    printf("(%s:%u): %s\n", filename.c_str(), line, msg);
    ok = false;
}

void MLFoldTracker::warn(const MLNode* where, const char *msg)
{
    const MLInfo *info = mlir.infoOf(where);
    const u32 line = info ? info->line : 0;
    printf("(%s:%u): Warning: %s\n", filename.c_str(), line, msg);
}

bool MLFoldTracker::checktype(Type sub, Type reference, const char *what, const MLNode* where, const MLNode* decl)
{
    if(vm.rt->tr.isListCompatible(sub, reference))
        return true; // all good

    // oh no. it's error time
    std::ostringstream os;
    os << what << " has mismatched type";
    error(where, os.str().c_str());
    error(decl, "(type should be same as here)");
    return false;
}

const DFunc *MLFoldTracker::getObjectOperator(const GCobj *obj, OperatorId op)
{
    sref method = rt().cs.operatorNames[op];
    const Val *v = env.lookupObjectMethod(obj, method);
    return v ? v->asFunc() : NULL;
}

const DFunc *MLFoldTracker::getCallable(const GCobj * obj)
{
    if(!obj)
        return NULL;

    if(obj->primtype() == PRIMTYPE_FUNC)
        return (DFunc*)obj;

    return getObjectOperator(obj, OP_CALL);
}

struct NumValuesResult
{
    enum Verdict
    {
        ERROR,
        EXACT_NUMBER,
        MIN_NUMBER, // when variadic
    };
    Verdict verdict;
    size_t n;
};

static NumValuesResult numResultValues(MLNode *node, MLFoldTracker& ft);

static NumValuesResult numResultValuesOfList(MLNode *ch, size_t nch, MLFoldTracker& ft)
{
    NumValuesResult ret = { NumValuesResult::EXACT_NUMBER, 0 };
    for(size_t i = 0; i < nch; ++i)
    {
        const MLNode *node = &ch[i];
        NumValuesResult r = numResultValues(&ch[i], ft);
        switch(r.verdict)
        {
            case NumValuesResult::ERROR:
                ret.verdict = r.verdict;
                return ret;

            case NumValuesResult::MIN_NUMBER:
                ret.verdict = r.verdict;
                if(i+1 == nch)
                {
                    case NumValuesResult::EXACT_NUMBER:
                    ret.n += r.n;
                    break;
                }
                else
                {
                    ft.error(node, "In an expression list, only the last expr can be variadic");
                    ret.verdict = NumValuesResult::ERROR;
                    return ret;
                }
        }
    }
    return ret;
}

static NumValuesResult numResultValues(MLNode *node, MLFoldTracker& ft)
{
    MLCmd cmd = (MLCmd)node->m.cmd;
    if(cmd == ML_LIST)
        return numResultValuesOfList(node->firstChild(), node->numchildren(), ft);

    // Statements don't return anything
    if(mlirIsStmt(cmd))
    {
        ft.error(node, "Attempt to use a statement as expression");
        NumValuesResult ret = { NumValuesResult::ERROR, 0 };
        return ret;
    }

    switch(cmd)
    {
        case _ML_ERROR:
        {
            NumValuesResult ret = { NumValuesResult::ERROR, 0 };
            return ret;
        }

        case OP_CALL:
        {
            NumValuesResult ret = { NumValuesResult::ERROR, 0 };
            const Type t = node->x.exprtype;
            if(t == PRIMTYPE_AUTO) // call couldn't be typed; already reported
                return ret;
            TypeIdList tl = ft.tr().aslist(t);
            ret.verdict = NumValuesResult::EXACT_NUMBER;
            ret.n = tl.n;
            if(tl.n && (tl.ptr[tl.n - 1] & TYPEBIT_VARIADIC))
            {
                ret.verdict = NumValuesResult::MIN_NUMBER;
                ret.n = tl.n - 1;
            }
            return ret;
        }


        case ML_CALLADJ:
        {
            NumValuesResult ret = { NumValuesResult::ERROR, 0 };

            MLNode *numexpr = node->firstChild();
            MLNode *myexpr = numexpr + 1;
            if(!numexpr->isconst() || numexpr->val.type != PRIMTYPE_UINT)
            {
                numexpr->setError(ft.sp(), "Value-adjustment must result in a compile-time constant uint value");
                return ret;
            }

            ret.n = numexpr->asVal().u.ui;
            if(ret.n > 255)
            {
                numexpr->setError(ft.sp(), "Value-adjustment can not result in more values than a non-variadic function can return (255)");
                return ret;
            }

            NumValuesResult orig = numResultValues(myexpr, ft);
            if(orig.verdict == NumValuesResult::ERROR)
                return orig;

            if(ret.n > orig.n && orig.verdict == NumValuesResult::EXACT_NUMBER)
            {
                std::ostringstream os;
                os << "Expresssion results in " << orig.n << " values; attempt to adjust to " << ret.n;
                myexpr->setError(ft.sp(), os.str().c_str());
                return ret;
            }

            ret.verdict = NumValuesResult::EXACT_NUMBER;
            return ret; // This is always exact
        }


        default:
            if(cmd < _OP_MAX) // Operators always return a single value
            {
                case _ML_VAL:
                case ML_VAR:
                case ML_GETINDEX:
                case ML_GETNS:
                case ML_FUNC:
                case ML_ITERPACK:
                case ML_NEW_ARRAY:
                case ML_NEW_TABLE:
                {
                    NumValuesResult ret = { NumValuesResult::EXACT_NUMBER, 1 };
                    return ret;
                }
            }
    }

    assert(false);
    NumValuesResult ret = { NumValuesResult::ERROR, 0 };
    return ret;
}

// Replace any external symbols with their value.
// Any EXT encountered afterwards will be turned into an error node since that symbol wasn't resolvable.
static void preFoldExternals(MLExternal *ext, size_t N, MLFoldTracker& ft)
{

    for(size_t i = 0; i < N; ++i)
    {
        MLExternal& e = ext[i];
        const Val *val = ft.env.lookupSymbol(e.name);
        e.val = val ? *val : _Notype(); // PRIMTYPE_NOTYPE is never valid for externals
    }
}

static void preFoldVars(MLVar *vars, size_t N, MLFoldTracker& ft)
{
    for(size_t i = 0; i < N; ++i)
    {
        MLVar& v = vars[i];
        v.u.local.type = PRIMTYPE_AUTO; // actual types are assigned or deduced later
        v.u.val.type = PRIMTYPE_AUTO; // error marker, not used, for debugging only
        v.decl = 0; // filled when encountered in ML_DECL during folding
    }
}

// >= 0: folded into this manu values
// < 0: error or not folded
static int tryFoldCall(MLNode *call, const DFunc& func, MLNode *args, size_t argc, int maxresults, MLFoldTracker& ft)
{
    if(!func.isPure() || argc < func.info.nargs)
        return -1;

    // Need all args to be constant
    // TODO: typeof(x) needs special handling -- only needs known type, not full result
    // TODO: At some point handle special cases like x*0, x*1, 0+x, etc
    // -> Extra DFunc funcptr that exists only for functions supporting this feature (C-side-only!)
    for(size_t i = 0; i < argc; ++i)
        if(!args[i].isconst())
            return -2;

    size_t spaceneeded = std::max<size_t>(func.info.nrets, argc);

    PodArray<ValU> argspace;
    ValU * const argp = argspace.alloc_n_exact(ft.gc(), spaceneeded);
    if(!argp)
        return -3;

    for(size_t i = 0; i < argc; ++i)
        argp[i] = args[i].asVal();

    // If the function call is successful, argp will hold the return values
    int status = func.call(&ft.vm, reinterpret_cast<Val*>(argp)); // FIXME: typecheck this?
    if(status >= 0)
    {
        if(maxresults >= 0 && status > maxresults)
        {
            std::ostringstream os;
            os << "Function folding resulted in " << status << " return values, but only "
                << maxresults << " used";
            ft.warn(call, os.str().c_str());
            status = maxresults;
        }

        // Make sure nothing references the params anymore
        // (if they are encountered during traversal an assert will trap)
        for(size_t i = 0; i < argc; ++i)
            args[i].invalidate();

        if(status == 0) // Function call resulted in no values -> Optimize it out
            call->makedummy();
        else if(status == 1) // Single result -> Replace the call node directly with the result
            call->setVal(argp[0]);
        else if(status > 1) // More -> Transform the call into a list node
        {
            MLNode *rets = args; // Optimistically assume we can re-use the args nodes to store returned values

            if(argc > (size_t)status) // Doesn't fit? Must add new nodes in the end.
            {
                size_t callidx = ft.mlir.indexOf(call); // This is going to reallocate...
                rets = ft.mlir.nodes.alloc_n(ft.gc(), status);
                call = &ft.mlir.nodes[callidx]; // ... and restore
            }

            for(size_t i = 0; i < (size_t)status; ++i)
                rets[i].setVal(argp[i]);

            // Make list and reference child nodes
            call->m.cmd = ML_LIST;
            call->list.len = status;
            assert(call < rets);
            call->m.chOffs = rets - call;
        }
    }
    else
    {
        // TODO: Make it so that the function can return a warning or an error to display and handle here
        call->setError(ft.sp(), "Function folding failed with an error");
    }

    argspace.dealloc(ft.gc());
    return status;
}

// A single value has its own type, more than one are a type list
static Type singleOrList(MLFoldTracker& ft, Type list)
{
    TypeIdList tl = ft.tr().aslist(list);
    return tl.n == 1 ? tl.ptr[0] : list;
}

// Type list of all values of an expression list
static Type typeListOf(MLFoldTracker& ft, const MLSub& exprs)
{
    std::vector<Type> ts(exprs.n);
    for(size_t i = 0; i < exprs.n; ++i)
        ts[i] = exprs.ch[i].type();
    return ft.tr().mklistDesc(ts.data(), ts.size())->id();
}

// Type list from a list of type expressions; PRIMTYPE_AUTO if empty or not all are known types
static Type typeListFromTypeExprs(MLFoldTracker& ft, MLNode *typeexprs)
{
    MLSub s = typeexprs->aslist();
    if(!s.n)
        return PRIMTYPE_AUTO;
    std::vector<Type> ts(s.n);
    for(size_t i = 0; i < s.n; ++i)
    {
        MLNode *te = &s.ch[i];
        if(!te->isconst() || te->val.type != PRIMTYPE_TYPE)
        {
            if(te->m.cmd != _ML_ERROR)
                te->setError(ft.sp(), "Expected a type");
            return PRIMTYPE_AUTO;
        }
        ts[i] = te->asVal().asDType()->tid;
    }
    return ft.tr().mklistDesc(ts.data(), ts.size())->id();
}

// func(params) -> rets, with param types taken from the function's locals
static Type funcType(MLFoldTracker& ft, MLNode *func, Type rets)
{
    const size_t firstvar = func->m.p[0];
    const size_t nargs = func->firstChild()->aslist().n;
    std::vector<Type> ts(nargs);
    for(size_t i = 0; i < nargs; ++i)
        ts[i] = ft.mlir.vars[firstvar + i].u.local.type;
    Type params = ft.tr().mklistDesc(ts.data(), ts.size())->id();
    return ft.tr().mkfuncDesc(params, rets)->id();
}

static bool hasErrorChild(MLNode *node)
{
    const size_t n = node->numchildren();
    MLNode *ch = n ? node->firstChild() : NULL;
    for(size_t i = 0; i < n; ++i)
        if(ch[i].m.cmd == _ML_ERROR || (!mlirIsStmt((MLCmd)ch[i].m.cmd) && ch[i].type() == PRIMTYPE_AUTO))
            return true;
    return false;
}

static bool tryFoldOpr(MLNode *node, MLFoldTracker& ft)
{
    if(hasErrorChild(node)) // already reported
    {
        node->x.exprtype = PRIMTYPE_AUTO;
        return false;
    }

    MLNode *L = node->firstChild();

    OperatorId opid = (OperatorId)node->m.cmd;
    size_t arity = GetOperatorArity(opid);
    assert(arity);
    assert(node->numchildren() == arity);

    // Left side of a binary operator defines the namespace it's looked up in
    Type ns = L->type();
    assert(ns != PRIMTYPE_AUTO); // We're post-folding, type should be resolved at this point

    if(opid == OP_QQM)
    {
        node->x.exprtype = ns | TYPEBIT_OPTIONAL;
        return true;
    }

    // Operator is unary or binary operator that resolves to a function

    const char *opname = Lexer::GetOperatorName(opid);
    if(!opname)
    {
        node->setError(ft.sp(), "Operator not implemented yet (needs lowering to <=> or ==)");
        return false;
    }
    Str name = ft.putstr(opname);


    const Val *opr = ft.env.lookupInNamespace(ns, name.id);
    if(!opr)
    {
        std::ostringstream os;
        os << "type has no operator '" << opname << "'";
        node->setError(ft.sp(), os.str().c_str());
        return false;
    }
    const DFunc *func = opr->asFunc();
    if(!func)
    {
        std::ostringstream os;
        os << "type's '" << opname << "' is not a function";
        node->setError(ft.sp(), os.str().c_str());
        return false;
    }

    node->x.exprtype = singleOrList(ft, func->info.rettype);

    // TODO: is there an operator that results in more than 1 return value?
    return tryFoldCall(node, *func, L, arity, 1, ft) > 0;
}

static MLPreVisitResult foldPre(MLNode *node, MLNode *parent, void *ud)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    MLPreVisitResult res = { VISIT_CONTINUE, 0 };

    switch((MLCmd)node->m.cmd)
    {
        case ML_CONST:
            assert(false && "This should have been taken care of during loading");
            break;

        case ML_FUNC:
        {
            MLFoldTracker::FoldFunc ff = { (u32)ft.mlir.indexOf(node), MLFoldTracker::NONE, PRIMTYPE_AUTO, PRIMTYPE_AUTO };
            if(parent->m.cmd == ML_NAMEDECL && node == parent->firstChild() + 1 && !parent->firstChild()->aslist().n)
                ff.varidx = ft.mlir.symToVarRemap[parent->m.p[1]];
            ft.foldFuncs.push_back(ff);
        }
        break;

        default: ;
    }

    return res;
}

static void assignVarType(MLVar *v, Type t, MLFoldTracker& ft)
{
    const Type valt = v->u.local.type;

    // FIXME: make a thing in TR that pretty-prints a type name
    const char *tn = GetPrimTypeName(t);
    const char *cn = GetPrimTypeName(valt);
    if(!tn)
        tn = "(unnamed)";
    if(!cn)
        cn = "(unnamed)";

    const char *vname = ft.str(v->name).s;

    printf("assignVarType '%s' = %s (currently: %s)\n", vname, tn ? tn : "(unnamed)", cn ? cn : "(unnamed)");

    if(valt == PRIMTYPE_AUTO)
    {
        v->u.local.type = t;
        printf("Var '%s' now has type %s\n", vname, tn ? tn : "(unnamed)");
    }
    else if(valt != t)
    {
        std::ostringstream os;
        os << "'" << vname << "' already has a type; previously declared as '"
            << cn << "', now declaring as '" << tn << "'";
        ft.error(&ft.mlir.nodes[v->decl], os.str().c_str()); // FIXME: this is bad
    }
}

static bool isSink(const MLNode *node)
{
    return node->m.cmd == ML_SINK || node->m.cmd == ML_WSINK;
}

// One value of a flattened value list
struct ValueSlot
{
    MLNode *expr; // the expression that produces this value
    Type t;
    bool single;  // expr produces only this value
};

// Flatten exprs into one slot per value. A variadic tail is not included; returns false on error.
static bool flattenValues(MLFoldTracker& ft, MLNode *ch, size_t n, std::vector<ValueSlot>& slots)
{
    for(size_t i = 0; i < n; ++i)
    {
        MLNode *e = &ch[i];
        if(e->m.cmd == ML_LIST) // a call folded into several values
        {
            if(!flattenValues(ft, e->firstChild(), e->numchildren(), slots))
                return false;
            continue;
        }
        const NumValuesResult nv = numResultValues(e, ft);
        if(nv.verdict == NumValuesResult::ERROR)
            return false;
        if(nv.n == 1 && nv.verdict == NumValuesResult::EXACT_NUMBER)
        {
            ValueSlot vs = { e, e->type(), true };
            slots.push_back(vs);
        }
        else if(nv.n)
        {
            TypeIdList tl = ft.tr().aslist(e->type());
            assert(tl.n >= nv.n);
            for(size_t k = 0; k < nv.n; ++k)
            {
                ValueSlot vs = { e, tl.ptr[k], false };
                slots.push_back(vs);
            }
        }
    }
    return true;
}

// Check that the values fit the targets: nfixed targets take exactly 1 value each, a trailing '___' (wide) takes the rest.
// On success, slots holds one entry per value (without a variadic tail).
static bool balanceValues(MLFoldTracker& ft, const MLNode *where, size_t nfixed, bool wide, MLSub exprs, std::vector<ValueSlot>& slots)
{
    const NumValuesResult nv = numResultValuesOfList(exprs.ch, exprs.n, ft); // also checks that only the last one is variadic
    if(nv.verdict == NumValuesResult::ERROR)
        return false;
    if(!flattenValues(ft, exprs.ch, exprs.n, slots))
        return false;
    const bool variadic = nv.verdict == NumValuesResult::MIN_NUMBER;
    const size_t n = slots.size();

    if(n < nfixed)
    {
        std::ostringstream os;
        os << "Getting " << n << (variadic ? " or more" : "") << " values, but " << nfixed << " are needed";
        if(variadic)
            os << " (use #N to set the number of values)";
        ft.error(where, os.str().c_str());
        return false;
    }

    if(wide || ft.assignMode == MLFoldTracker::ASSIGN_DROP_EXCESS)
        return true;

    if(variadic)
    {
        std::ostringstream os;
        os << "Getting " << n << " or more values, but " << nfixed << " are assigned (use ___ to ignore the rest, or #N to set the number of values)";
        ft.error(where, os.str().c_str());
        return false;
    }
    if(n > nfixed)
    {
        std::ostringstream os;
        os << "Getting " << n << " values, but only " << nfixed << " are assigned (use ___ to ignore the rest)";
        ft.error(where, os.str().c_str());
        return false;
    }
    return true;
}

// Type of a sink's type expression; PRIMTYPE_AUTO if untyped or not a type
static Type sinkType(MLFoldTracker& ft, MLNode *sink)
{
    MLNode *te = sink->firstChild();
    if(te->m.cmd == _ML_EMPTY || te->m.cmd == _ML_ERROR)
        return PRIMTYPE_AUTO;
    if(te->isconst() && te->val.type == PRIMTYPE_TYPE)
        return te->asVal().asDType()->tid;
    te->setError(ft.sp(), "Expected a type");
    return PRIMTYPE_AUTO;
}

// stmt is the decl or assignment, for error locations (sinks have no debug info)
static void checkSinkValue(MLFoldTracker& ft, MLNode *sink, const ValueSlot& vs, const MLNode *stmt)
{
    const Type t = sinkType(ft, sink);
    if(t != PRIMTYPE_AUTO)
        ft.checktype(vs.t, t, "Value", vs.expr, stmt);
}

// Count fixed targets and check for a trailing '___'
static size_t countTargets(const MLSub& targets, bool *wide)
{
    *wide = targets.n && targets.ch[targets.n - 1].m.cmd == ML_WSINK;
    return targets.n - *wide;
}

// exprlist is optional, it must be present for ML_DECL nodes,
// but is not needed for function parameters and deferred decls
static void doDecl(MLFoldTracker& ft, u32 localid, MLNode& declroot, MLNode& typelist, MLNode *exprlist)
{
    MLSub targets = typelist.aslist();
    const size_t declidx = ft.mlir.indexOf(&declroot);

    // Assign declared types first
    size_t varidx = localid;
    for(size_t i = 0; i < targets.n; ++i)
    {
        MLNode *te = &targets.ch[i];
        if(isSink(te))
            continue;
        MLVar *v = &ft.mlir.vars[varidx++];
        v->decl = declidx;
        if(te->m.cmd == _ML_EMPTY)
            continue;
        switch(v->kind)
        {
            case MLVar::CVAR:
            case MLVar::MUTVAR:
                if(te->isconst())
                {
                    Val tv = te->asVal();
                    if(tv.type == PRIMTYPE_TYPE)
                        assignVarType(v, tv.asDType()->tid, ft);
                    else
                    {
                        std::ostringstream os;
                        os << "Attempt to use a value as variable '" << ft.str(v->name) << "' type that is not a type";
                        te->setError(ft.sp(), os.str().c_str());
                    }
                }
                else
                {
                    std::ostringstream os;
                    os << "Variable '" << ft.str(v->name) << "' type can't be deduced at this stage, assuming 'any'";
                    ft.warn(te, os.str().c_str());
                    v->u.local.type = PRIMTYPE_ANY;
                }
                break;

            case MLVar::CONSTVAL:
                assert(false && "constval during decl, should not be folded yet");
                break;

        }
    }

    if(!exprlist)
        return;

    bool wide;
    const size_t nfixed = countTargets(targets, &wide);
    std::vector<ValueSlot> slots;
    if(!balanceValues(ft, &declroot, nfixed, wide, exprlist->aslist(), slots))
        return;

    // Assign type based on value if automatic, or report type clash
    varidx = localid;
    for(size_t i = 0; i < nfixed; ++i)
    {
        MLNode *te = &targets.ch[i];
        const ValueSlot& vs = slots[i];
        if(te->m.cmd == ML_SINK)
        {
            checkSinkValue(ft, te, vs, &declroot);
            continue;
        }

        MLVar *v = &ft.mlir.vars[varidx++];
        // TODO: support multiple values constant expr (_ML_MULTIVAL using an array internally?)
        if(!v->isMutable() && vs.single && vs.expr->isconst())
        {
            v->kind = MLVar::CONSTVAL;
            v->u.val = vs.expr->asVal();
        }
        else
            assignVarType(v, vs.t, ft);
    }
    if(wide)
        for(size_t i = nfixed; i < slots.size(); ++i)
            checkSinkValue(ft, &targets.ch[nfixed], slots[i], &declroot);

    // DO NOT ELIMINATE DECLS, even if all vars became constants: post-folding needs them
}

static void doAssign(MLFoldTracker& ft, MLNode *node)
{
    MLNode *ch = node->firstChild();
    MLSub targets = ch[0].aslist();

    bool wide;
    const size_t nfixed = countTargets(targets, &wide);
    std::vector<ValueSlot> slots;
    if(!balanceValues(ft, node, nfixed, wide, ch[1].aslist(), slots))
        return;

    for(size_t i = 0; i < nfixed; ++i)
    {
        MLNode *dst = &targets.ch[i];
        const ValueSlot& vs = slots[i];
        switch(dst->m.cmd)
        {
            case ML_SINK:
                checkSinkValue(ft, dst, vs, node);
                break;

            case ML_VAR:
            {
                const MLVar& v = ft.mlir.vars[dst->m.p[0]];
                assert(v.isMutable()); // the parser checks this
                ft.checktype(vs.t, dst->type(), "Assigned value", vs.expr, v.decl ? &ft.mlir.nodes[v.decl] : dst);
            }
            break;

            case _ML_ERROR: // reported later
                break;

            default:
                assert(false); // _construct() only lets idents and sinks through
        }
    }
}

static void foldNode(MLNode *node, MLFoldTracker& ft)
{
    const MLCmd cmd = (MLCmd)node->m.cmd;

    switch(cmd)
    {
        case _ML_VAL: // Constants can't be folded further
        case ML_LIST: // all good, took care of the list recursively already before returning here
        case _ML_ERROR: // reported later
        case _ML_EMPTY:
            return;

        case ML_FUNC:
        {
            const MLFoldTracker::FoldFunc ff = ft.foldFuncs.back();
            ft.foldFuncs.pop_back();
            assert(ff.nodeidx == ft.mlir.indexOf(node));
            Type rets = ff.declrets;
            if(rets == PRIMTYPE_AUTO)
                rets = ff.deducedrets;
            if(rets == PRIMTYPE_AUTO) // no return
                rets = ft.tr().mklistDesc(NULL, 0)->id();
            node->x.exprtype = funcType(ft, node, rets);
            return;
        }

        case ML_RETURN:
        {
            if(ft.foldFuncs.empty()) // file-level return
                break;
            MLFoldTracker::FoldFunc& ff = ft.foldFuncs.back();
            MLSub vals = node->firstChild()->aslist();
            NumValuesResult nv = numResultValuesOfList(vals.ch, vals.n, ft);
            if(nv.verdict == NumValuesResult::ERROR)
                break;
            const Type t = typeListOf(ft, vals);
            if(ff.declrets != PRIMTYPE_AUTO)
                ft.checktype(t, ff.declrets, "Return value", node, &ft.mlir.nodes[ff.nodeidx]);
            else if(ff.deducedrets == PRIMTYPE_AUTO)
                ff.deducedrets = t;
            else if(ff.deducedrets != t)
            {
                node->setError(ft.sp(), "Returned values have a different type than an earlier return in the same function");
                return;
            }
        }
        break;

        case ML_EXT:
        {
            MLExternal *e = &ft.mlir.externals[node->m.p[0]];
            if(e->val.type != PRIMTYPE_NOTYPE) // set by preFoldExternals() if unresolved
                node->setVal(e->val);
            else
            {
                std::ostringstream os;
                os << "Failed to resolve external symbol '" << ft.str(e->name) << '\'';
                node->setError(ft.sp(), os.str().c_str());
            }
            return;
        }


        case ML_VAR:
        {
            u32 varid = node->m.p[0];
            MLVar *v = &ft.mlir.vars[varid];
            switch(v->kind)
            {
                case MLVar::CONSTVAL:
                    node->setVal(v->u.val);
                    return;

                case MLVar::CVAR:
                case MLVar::MUTVAR:
                {
                    Type deduced = v->u.local.type;
                    if(deduced == PRIMTYPE_AUTO)
                    {
                        // FIXME: This is probably bad and shouldn't happen?
                        std::ostringstream os;
                        os << "Type of variable '" << ft.str(v->name) << "' not deduced, falling back to 'any'";
                        ft.warn(node, os.str().c_str());
                        deduced = PRIMTYPE_ANY;
                        v->u.local.type = deduced;
                    }
                    node->x.exprtype = deduced;
                    return;
                }
            }

            assert(v->decl);

            // Make it an upvalue reference if the var is declared outside of the currrent function
            // FIXME
            //if(varid < funcLocalsStart)
            //    node->m.cmd = _ML_UPVAL;
        }
        break;

        case ML_GETNS:
        {
            MLNode *ns = node->firstChild();
            MLNode *key = ns + 1;

            if(!ns->isconst())
            {
                node->setError(ft.sp(), "Namespace must be compile-time known");
                return;
            }
            if(!key->isconst())
            {
                node->setError(ft.sp(), "Namespaced identifier must be compile-time known");
                return;
            }

            DType *nstype = ns->asVal().asDType();
            Val keyval = key->asVal();
            if(!nstype)
            {
                node->setError(ft.sp(), "Namespace must be a type");
                return;
            }
            if(keyval.type != PRIMTYPE_STRING)
            {
                node->setError(ft.sp(), "Namespaced identifier must be a string");
                return;
            }

            if(const Val *val = ft.env.lookupInNamespace(nstype->tid, keyval.u.str))
            {
                printf("%s::%s replaced with constant from env\n",
                    GetPrimTypeName(val->type), ft.str(keyval.u.str).s);
                node->setVal(*val);
                return;
            }
            else
            {
                std::ostringstream os;
                const char *nss = GetPrimTypeName(nstype->tid);
                if(!nss)
                    nss = "(unnamed)";
                os << "Failed to resolve namespaced symbol " << nss << "::" << nss;
                node->setError(ft.sp(), os.str().c_str());
                return;
            }
        }
        unreachable(); // Namespace lookup must either be folded or error
        break;

        case ML_NAMEDECL:
        {
            MLNode *ns = node->firstChild();
            MLNode *valnode = ns + 1;
            sref nameid = node->m.p[0];
            u32 symid = node->m.p[1];
            const char *name = ft.str(nameid);
            printf("ML namedecl '%s', symid %u\n", name, symid);
            switch(ns->aslist().n)
            {
                case 0:
                {
                    // It's a dummy entry -> no namespace -> make it a const local, potentially exported
                    // This is likely a function, but functions aren't availble yet.
                    // Store the index of the node, which should hopefully be folded into a function
                    // or some other constant value later.
                    MLVar *v = ft.mlir.getVarFromSymid(symid);
                    assert(v->name == nameid);
                    v->kind = MLVar::CVAR;
                    v->decl = ft.mlir.indexOf(node);

                    switch(valnode->m.cmd)
                    {
                        case ML_FUNC:
                            v->u.local.type = valnode->type();
                            break;
                        case _ML_VAL:
                            v->u.val = valnode->asVal();
                            break;
                        case _ML_ERROR:
                            break;
                        default:
                            assert(false);
                            break;
                    }

                    //ft.env.addSymbol(ft.gc(), nameid, val);

                    // TODO: exporting
                }
                break;
#if 0
                case 1:
                    // Regsiter in namespace
                    if(!ns->isconst())
                    {
                        // TODO: This restriction should be loosened at some point
                        node->setError(ft.sp(), "Namespace should be a compile-time constant value");
                        return;
                    }

                    if(val.isconst())
                    {
                        // Known at compile time -> register now
                    }
                    else
                    {
                        // Need to do it at runtime
                    }
#endif
                default:
                    node->setError(ft.sp(), "Namespaced function declarations not implemented yet");
                    return;
            }
        }
        break;


        case ML_DECL:
        {
            MLNode *ch = node->firstChild();
            const size_t localid = node->m.p[0];
            const bool deferred = !ch[1].aslist().n; // x = defer; declared again later
            doDecl(ft, localid, *node, ch[0], deferred ? NULL : &ch[1]);
        }
        break;

        case ML_ASSIGN:
            doAssign(ft, node);
            break;

        /*case ML_FUNC:
        {
            MLNode *argtypes = node->firstChild();
            const size_t localid = node->m.p[0];
            doDecl(ft, localid, *node, *argtypes, NULL);
        }
        break;*/

        case OP_CALL:
        {
            MLNode *funcexpr = node->firstChild();
            assert(funcexpr->m.cmd != ML_LIST);
            if(hasErrorChild(node)) // already reported
            {
                node->x.exprtype = PRIMTYPE_AUTO;
                return;
            }
            const DFunc *df = funcexpr->isconst() ? ft.getCallable(funcexpr->asVal().asAnyObj()) : NULL;
            FuncTypes fty;
            if(!ft.tr().lookupFunc(&fty, df ? df->info.functype : funcexpr->type()))
            {
                node->setError(ft.sp(), "Attempt to call something that is not a function");
                return;
            }

            MLSub args = funcexpr[1].aslist();
            NumValuesResult nv = numResultValuesOfList(args.ch, args.n, ft);
            if(nv.verdict == NumValuesResult::ERROR)
            {
                node->x.exprtype = PRIMTYPE_AUTO;
                return;
            }
            const bool variadic = fty.params.n && (fty.params.ptr[fty.params.n - 1] & TYPEBIT_VARIADIC);
            const size_t minargs = fty.params.n - variadic;
            if(nv.n < minargs && nv.verdict == NumValuesResult::EXACT_NUMBER)
            {
                std::ostringstream os;
                os << "Function takes " << (variadic ? "at least " : "") << minargs << " arguments, got " << nv.n;
                node->setError(ft.sp(), os.str().c_str());
                return;
            }
            if(nv.n > fty.params.n && !variadic)
            {
                std::ostringstream os;
                os << "Function takes " << fty.params.n << " arguments, got " << nv.n
                    << (nv.verdict == NumValuesResult::MIN_NUMBER ? " or more" : "");
                node->setError(ft.sp(), os.str().c_str());
                return;
            }

            const TDesc *td = ft.tr().lookupFunc(NULL, df ? df->info.functype : funcexpr->type());
            node->x.exprtype = singleOrList(ft, td->types()[2]);

            if(df)
                tryFoldCall(node, *df, args.ch, args.n, -1, ft);
            return;
        }

        case ML_WHILE:
        {
            MLNode *cond = node->firstChild();
            if(cond->isconst() && !cond->asVal().isTruthy())
                node->makedummy();
        }
        break;

        case ML_IFELSE:
        {
            MLNode *cond = node->firstChild();
            if(cond->isconst())
            {
                MLNode *ifbr = cond + 1;
                MLNode *elsebr = cond + 2;
                MLNode *taken = cond->asVal().isTruthy() ? ifbr : elsebr;
                node->m.cmd = ML_LIST;
                node->list.len = 1;
                node->m.chOffs = taken - node;
            }
        }
        break;

        case _ML_UVAR:
            assert(false); // should have been resolved by now
            break;

        /*case ML_FUNC:
        {
        }
        break;*/

        default:
            if(cmd < _ML_OP_MAX)
            {
                // It's an operator -> technically a function call, but known to return a single value.
                tryFoldOpr(node, ft);
                return;
            }


    }

    // If expr, should have early-returned. If not, it's not handled yet.
    if(!mlirIsStmt((MLCmd)node->m.cmd))
    {
        node->setError(ft.sp(), "Not implemented yet in MLIR folding");
        return;
    }
    // stmts have no type. Don't write x.exprtype, a stmt may use both params.
}

static void foldPost(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    foldNode(node, ft);

    if(parent && parent->m.cmd == ML_FUNC && node == parent->firstChild())
    {
        // Special case: Just finished the parameter list of a function.
        // Add those to the list of known locals before handling the rest of the function.
        const size_t localid = parent->m.p[0];
        doDecl(ft, localid, *parent, *node, NULL);
    }
    else if(parent && parent->m.cmd == ML_FUNC && node == parent->firstChild() + 1)
    {
        // Just finished the declared return types. Known before the body, so the body can recurse.
        MLFoldTracker::FoldFunc& ff = ft.foldFuncs.back();
        assert(ff.nodeidx == ft.mlir.indexOf(parent));
        ff.declrets = typeListFromTypeExprs(ft, node);
        if(ff.declrets != PRIMTYPE_AUTO && ff.varidx != MLFoldTracker::NONE)
            ft.mlir.vars[ff.varidx].u.local.type = funcType(ft, parent, ff.declrets);
    }
}

static void addLocal(MLFoldTracker& ft, u32 varidx)
{
    const u32 funcidx = ft.funcStack.back();
    MLFunc& f = ft.mlir.funcs[funcidx];
    if(ft.varOwner[varidx] != MLFoldTracker::NONE) // deferred declaration, then the real one
    {
        assert(ft.varOwner[varidx] == funcidx);
        return;
    }
    ft.varOwner[varidx] = funcidx;
    ft.varSlot[varidx] = (u32)f.locals.size();
    f.locals.push_back(ft.gc(), varidx); // TODO: alloc fail
}

// Pass 1 of splitting: report errors, create MLFuncs and assign local slots
static MLPreVisitResult foldPreErrorCheck(MLNode *node, MLNode *parent, void *ud)
{
    // Precondition: The tree has been completely folded (foldPre, foldPost already ran)
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    MLPreVisitResult res = { VISIT_CONTINUE, 0 };
    const MLCmd cmd = (MLCmd)node->m.cmd;

    switch(cmd)
    {
        case _ML_ERROR:
            ft.error(node, ft.str(node->m.p[0]).s);
            break;

        case ML_FUNC:
        {
            assert(!ft.funcStack.empty());

            MLFunc *mf = GA_PLACEMENT_NEW(ft.mlir.funcs.alloc_n(ft.gc(), 1)) MLFunc; // TODO: alloc fail
            mf->myidx = ft.mlir.indexOf(node);
            if(parent->m.cmd == ML_NAMEDECL)
                mf->name = parent->m.p[0];

            // Any function on top of the stack encloses the function created now
            mf->enclosingFuncIdxPlus1 = ft.funcStack.back() + 1;

            ft.funcStack.push_back(ft.mlir.funcs.size() - 1);

            // Func params are the first locals
            const u32 firstvar = node->m.p[0];
            const size_t nargs = node->firstChild()->aslist().n;
            for(size_t i = 0; i < nargs; ++i)
                addLocal(ft, firstvar + i);
        }
        break;

        // -- Cases that introduce new local variables --

        case ML_DECL:
        {
            const u32 firstvar = node->m.p[0];
            const MLSub targets = node->firstChild()->aslist();
            u32 nvars = 0;
            for(size_t i = 0; i < targets.n; ++i)
                if(!isSink(&targets.ch[i]))
                    addLocal(ft, firstvar + nvars++);
        }
        break;

        case ML_NAMEDECL:
            if(!node->firstChild()->aslist().n) // not namespaced
            {
                const u32 varidx = ft.mlir.symToVarRemap[node->m.p[1]];
                addLocal(ft, varidx);
            }
            break;

        default:
            break; // Nothing to do
    }

    return res;
}

static void foldPostErrorCheck(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    if(node->m.cmd == ML_FUNC)
    {
        assert(ft.mlir.funcs[ft.funcStack.back()].myidx == ft.mlir.indexOf(node));
        ft.funcStack.pop_back();
    }
}

// Returns the slot in funcs[funcidx].upvals
static u32 addUpvalueReference(MLFoldTracker& ft, u32 funcidx, u32 varidx)
{
    MLIR& mlir = ft.mlir;
    {
        const MLFunc& f = mlir.funcs[funcidx];
        // Nothing to do if upvalue is already known as such
        for(tsize i = 0; i < f.upvals.size(); ++i)
            if(f.upvals[i].varidx == varidx)
                return i;
        assert(f.enclosingFuncIdxPlus1); // the root function can't have upvalues
    }

    const u32 enclosing = mlir.funcs[funcidx].enclosingFuncIdxPlus1 - 1;

    MLUpvalRef ur;
    ur.varidx = varidx;
    ur.transient = ft.varOwner[varidx] != enclosing;
    ur.slot = ur.transient
        ? addUpvalueReference(ft, enclosing, varidx) // The enclosing function must reference it as an upvalue, too
        : ft.varSlot[varidx];

    MLFunc& f = mlir.funcs[funcidx];
    f.upvals.push_back(mlir.gc, ur); // TODO: alloc fail
    return f.upvals.size() - 1;
}

static void checkVarReference(MLFoldTracker& ft, u32 funcidx, MLNode *node)
{
    const u32 varidx = node->m.p[0];
    const u32 owner = ft.varOwner[varidx];
    assert(owner != MLFoldTracker::NONE); // referenced but never declared?

    if(owner == funcidx)
    {
        node->m.cmd = _ML_LOCAL;
        node->m.p[0] = ft.varSlot[varidx];
    }
    else
    {
        node->m.cmd = _ML_UPVAL;
        node->m.p[0] = addUpvalueReference(ft, funcidx, varidx);
    }
}

// Pass 2 of splitting. Functions are visited in the same order as in pass 1.
static MLPreVisitResult foldPreSplitFunctions(MLNode *node, MLNode *parent, void *ud)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    MLPreVisitResult res = { VISIT_CONTINUE, 0 };
    if(node->m.cmd == ML_FUNC)
    {
        const u32 funcidx = ft.nextFunc++;
        assert(ft.mlir.funcs[funcidx].myidx == ft.mlir.indexOf(node));
        ft.funcStack.push_back(funcidx);
    }
    return res;
}

static void foldPostSplitFunctions(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    const MLCmd cmd = (MLCmd)node->m.cmd;
    if(cmd != ML_LIST)
        node->m.nch = mlirNumChildren(cmd); // Annotate for the LLIR stage

    switch(cmd)
    {
        case ML_FUNC:
        {
            const u32 funcidx = ft.funcStack.back();
            MLFunc& f = ft.mlir.funcs[funcidx];
            assert(f.myidx == ft.mlir.indexOf(node));
            f.node = *node;
            node->m.cmd = _ML_FUNCIDX;
            node->m.p[0] = funcidx;
            ft.funcStack.pop_back();
        }
        break;

        case ML_VAR: // At this point a variable reference becomes a local or an upvalue reference
            checkVarReference(ft, ft.funcStack.back(), node);
            break;

        default: ;
    }
}

static void debugPrintVar(MLFoldTracker& ft, size_t gidx, const char *t, u32 idx)
{
    MLVar& v = ft.mlir.vars[idx];
    const char *name = ft.str(v.name);
    printf("  %s %u: '%s'\n", t, (unsigned)gidx, name);
}

static void debugPrintFuncInfo(MLFoldTracker& ft, const MLFunc& f)
{
    const char *name = ft.str(f.name);
    printf("FUNC '%s', %u locals, %u upvalues:\n",
        name, (unsigned)f.locals.size(), (unsigned)f.upvals.size());
    for(size_t i = 0; i < f.locals.size(); ++i)
        debugPrintVar(ft, i, "local", f.locals[i]);
    for(size_t i = 0; i < f.upvals.size(); ++i)
        debugPrintVar(ft, i, "upval", f.upvals[i].varidx);
}

bool MLIR::fold(VM& vm, Symstore& syms, SymTable &env)
{
    SymTable *localenv = SymTable::GCNew(vm.rt->gc);
    localenv->next = &env;
    // TODO: also add an "exported" env; because we don't want to export ALL the local namedecls
    MLFoldTracker ft = { *this, vm, syms, *localenv, true };
    ft.assignMode = MLFoldTracker::ASSIGN_EXACT;

    ft.typesrc.resize(nodes.size());
    preFoldExternals(externals.data(), externals.size(), ft);
    preFoldVars(vars.data(), vars.size(), ft);

    // This does constant folding & propagation
    visit(foldPre, foldPost, &ft);
    assert(ft.foldFuncs.empty());

    // Create the root function that encompasses the entire file scope
    MLFunc *mf = GA_PLACEMENT_NEW(ft.mlir.funcs.alloc_n(ft.gc(), 1)) MLFunc; // TODO: alloc fail
    mf->myidx = 0;
    mf->enclosingFuncIdxPlus1 = 0;
    ft.varOwner.assign(vars.size(), MLFoldTracker::NONE);
    ft.varSlot.assign(vars.size(), MLFoldTracker::NONE);

    // Report errors and assign local slots per function
    ft.funcStack.push_back(0);
    visit(foldPreErrorCheck, foldPostErrorCheck, &ft);
    ft.funcStack.pop_back();
    assert(ft.funcStack.empty());

    // Break the tree into individual functions. Only if the tree is sound.
    if(ft.ok)
    {
        ft.nextFunc = 1;
        ft.funcStack.push_back(0);
        visit(foldPreSplitFunctions, foldPostSplitFunctions, &ft);
        // TODO: make func return localenv
        ft.funcStack.pop_back();
        assert(ft.funcStack.empty());
        assert(ft.nextFunc == funcs.size());
    }

    printf("Local symtol table: %u entries\n", localenv->size());

    size_t N = funcs.size();
    printf("MLIR: Broke tree into %u functions:\n", (unsigned)N);
    for(size_t i = 0; i < N; ++i)
        debugPrintFuncInfo(ft, funcs[i]);

    return ft.ok;
}
