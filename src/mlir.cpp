#include "mlir.h"
#include "hlir.h"
#include "serialio.h"
#include "valstore.h"
#include "symstore.h"
#include "runtime.h"

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
    STATIC_ASSERT(_ML_OP_MAX < ML_CONST); // If this fails, increase ML_CONST

    if(cmd < _ML_OP_MAX)
        return true;

    switch(cmd)
    {
    case ML_NAMEDECL:
    case ML_DECL:
    case ML_CLOSE:
    case ML_ASSIGN:
    case ML_IFELSE:
    case ML_WHILE:
    case ML_FOR:
    case ML_RETURN:
    case ML_EMIT:
    case ML_EXPORT:
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
        case ML_NAMEDECL:
        case ML_DECL:
        case ML_FUNC:
            return 1;

        case ML_CLOSE:
            return 2;

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
        case ML_EXT:
        case ML_CLOSE:
        case _ML_VAL:
            return 0;

        case ML_RETURN:
        case ML_EMIT:
        case ML_ITERPACK:
        case ML_NEW_ARRAY:
        case ML_NEW_TABLE:
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
}

size_t MLIR::indexOf(const MLNode* node) const
{
    assert(node);
    const MLNode *base = nodes.data();
    assert(base <= node && node < base + nodes.size());
    return node - base;
}

const MLInfo * MLIR::infoOf(const MLNode * node) const
{
    size_t idx = indexOf(node);
    return idx < infos.size() ? &infos[idx] : NULL;
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

sref MLIR::_decllist(Queue<Cons>& q, MLNode *& dst, const HLNode* decllist)
{
    if(!decllist || !decllist->numchildren())
    {
        dst->makedummy();
        return 0;
    }
    const HLNode * const *dch = decllist->children();

    MLCh s = _setupList(dst, decllist->numchildren());
    assert(s.n);
    MLNode *ch = &nodes[s.chIdx];

    const HLIdent *firstIdent = dch[0]->as<HLVarDef>()->ident->as<HLIdent>();

    for(size_t i = 0; i < s.n; ++i)
    {
        const HLVarDef *vd = dch[i]->as<HLVarDef>();
        const HLIdent *id = vd->ident->as<HLIdent>();
        printf("ML decl %u, symid %u, strid %u\n", (unsigned)i, id->symid, id->nameStrId);

        // Symbols are declared in a row -> symbol IDs are given consecutively
        assert(id->symid == firstIdent->symid + i);

        _cons(q, &ch[i], vd->type);
    }

    return firstIdent->symid;
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
void MLIR::_construct(Queue<Cons>& q, MLNode *dst, const HLNode *hl)
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
            dst->m.p[0] = _decllist(q, ch, decls); // may reallocate hlch
            dst->m.cmd = _ML_UDECL; // Must be remapped later

            // Continue expanding RHS expressions
            const HLNode *vals = hl->u.vardecllist.vallist;
            _cons(q, &ch[1], vals);
            return;
        }

        case HLNODE_FUNCDECL:
        {
            const HLFuncDecl& decl = hl->u.funcdecl;
            HLIdent *hname = decl.ident->as<HLIdent>();
            dst->m.p[0] = hname->nameStrId;

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

            MLCh s = _setupCh(dst, ML_FUNC);
            assert(s.n == 3);
            MLNode *ch = &nodes[s.chIdx];

            dst->m.p[0] = _decllist(q, ch, fh->paramlist);

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
            for(size_t i = 0; i < nch; i += 2)
                if(const HLNode *hk = hlch[i])
                {
                    assert(hlch[i+1]);
                    if(hk->type == HLNODE_NAME) // Ensure that names are really just literal strings, ie. {k=v} becomes {["k"]=v}
                        ch[i].setVal(Val(_Str(hl->u.name.nameStrId)));
                    else
                        _cons(q, &ch[i], hlch[i]);
                    _cons(q, &ch[i+1], hlch[i+1]);
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
        case HLNODE_INDEXASSIGN:
            _setupChDefault(q, dst, hl, ML_ASSIGN);
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
            ; // not reached

        // not adding a default label here to keep compiler warnings. Let the assert below catch it.
    }

    assert(false);
    unreachable();
}

void MLIR::construct(const HLNode* root, Symstore& syms, StringPool& sp, Options options)
{
    Queue<Cons> q;
    nodes.reserve(gc, 128); // HACK FIXME
    PodArray<size_t> unresolved; // index of each node to fix
    unresolved.reserve(gc, syms.knownSize());

    Cons r { root, indexOf(_add(1)) };
    for(;;)
    {
        _construct(q, &nodes[r.mlidx], r.hl); // This may reallocate nodes[], don't keep a pointer

        if(!(options & STRIP_DEBUGINFO))
        {
            if(infos.size() < nodes.size())
                infos.alloc_n(gc, nodes.size() - infos.size());

            MLInfo &info = infos[r.mlidx];
            info.line = r.hl->line;
            info.column = r.hl->column;
        }

        switch((MLCmd)nodes[r.mlidx].m.cmd)
        {
            case _ML_UVAR:
            case _ML_UDECL:
                unresolved.push_back(gc, r.mlidx);
                break;

            default: ;
        }

        if(q.empty())
            break;
        r = q.pop();
    }

    q.dealloc(gc);

    _resolveVars(unresolved.data(), unresolved.size(), syms, sp);

    unresolved.dealloc(gc);
}

// Note: This could be split up into a function that does the vars and a postproc step
// instead of pushing into unresolved vec (above). Would remove an allocation.
void MLIR::_resolveVars(const size_t *unresolved, size_t N, Symstore& syms, StringPool& sp)
{
    const size_t nsyms = syms.knownSize();
    printf("Resolve %u occurences referencing %u symbols ...\n", (unsigned)N, (unsigned)nsyms);
    vars.reserve(gc, nsyms); // Need at least this many; maybe some more for upvalues

    PodArray<u32> remap; // Maps symbol index to index in vars[]
    remap.resize(gc, nsyms);

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

            remap[symid] = (u32)externals.size();

            MLExternal *e = externals.alloc_n(gc, 1);
            e->name = sym->nameStrId;

            printf("EXT[%u]: %s @ symidx %u\n",
                remap[symid], sp.lookup(e->name).s, (unsigned)symid);
        }
        else
        {
            remap[symid] = (u32)vars.size();

            MLVar *v = vars.alloc_n(gc, 1);

            v->name = sym->nameStrId;
            v->kind = mut ? MLVar::MUTVAR : MLVar::CVAR;

            printf("VAR[%u]: %s [kind %u] @ symidx %u\n",
                remap[symid], sp.lookup(v->name).s, v->kind, (unsigned)symid);
        }
    }

    for(size_t i = 0; i < N; ++i)
    {
        MLNode *node = &nodes[unresolved[i]];

        switch((MLCmd)node->m.cmd)
        {
            case _ML_UVAR: // Resolve and decide whether it becomes a local or an external
            {
                const HLIdent *hlident = node->tmp.hlnode->as<HLIdent>();
                ScopeReferral referral = (ScopeReferral)hlident->ex.u.scoperef;
                assert(referral != SCOPEREF_NOREF);
                const Symstore::Sym *sym = syms.getsym(hlident->symid);

                node->m.cmd = (sym->referencedHow & SYMREF_EXTERNAL) ? ML_EXT : ML_VAR;
                node->m.p[0] = remap[hlident->symid];
                break;
            }

            case _ML_UDECL:
                // Unresolved decl still refers the symid; refer to var idx instead
                node->m.cmd = ML_DECL;
                node->m.p[0] = remap[node->m.p[0]];
                break;

            default:
                unreachable();
        }
    }

    remap.dealloc(gc);
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
    std::vector<std::string> errors;
    std::vector<u32> typesrc; // x = typesrc[y] -> Which other node x did get node y its type from

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
    // Statements don't return anything
    if(mlirIsStmt(cmd))
    {
        ft.error(node, "Attempt to use a statement as expression");
        NumValuesResult ret = { NumValuesResult::ERROR, 0 };
        return ret;
    }

    switch(cmd)
    {
        case ML_LIST:
            return numResultValuesOfList(node->firstChild(), node->numchildren(), ft);

        case OP_CALL:
        {
            assert(false); // TODO -- ideally we have a folded function at this point that already has its types deduced
        }
        break;

        case ML_MTHCALL:
        {
            assert(false); // TODO
        }
        break;


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
    assert(argc >= func.info.nargs);

    if(!func.isPure())
        return -1;

    // Need all args to be constant
    // TODO: typeof(x) needs special handling -- only needs known type, not full result
    // TODO: At some point handle special cases like x*0, x*1, 0+x, etc
    // -> Extra DFunc funcptr that exists only for functions supporting this feature (C-side-only!)
    for(size_t i = 0; i < argc; ++i)
        if(!args->isconst())
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

static bool tryFoldOpr(MLNode *node, MLFoldTracker& ft)
{
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

    // Operator is unary or binary operator that resolves to a function (__op_*)

    const char *opname = GetOperatorName(opid);
    Str name = ft.putstr(opname);


    const Val *opr = ft.env.lookupInNamespace(ns, name.id);
    if(!opr)
    {
        std::ostringstream os;
        os << "type has no operator '" << opname << "'";
        node->setError(ft.sp(), os.str().c_str());
        return false;
    }
    const DFunc *func = opr->asFunc(); // FIXME: resolve __op_call
    if(!func)
    {
        std::ostringstream os;
        os << "type's '" << opname << "' is not a function";
        node->setError(ft.sp(), os.str().c_str());
        return false;
    }

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

static void foldPost(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    const MLCmd cmd = (MLCmd)node->m.cmd;

    switch(cmd)
    {
        case _ML_VAL: // Constants can't be folded further
        case ML_LIST: // all good, took care of the list recursively already before returning here
            return;

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
            assert(false);
            break;


        case ML_DECL:
        {
            MLNode *ch = node->firstChild();
            const size_t localid = node->m.p[0];

            MLSub typeexprs = ch[0].aslist();
            MLSub exprs = ch[1].aslist();

            const size_t N = typeexprs.n;
            const size_t declidx = ft.mlir.indexOf(node);

            // Assign types first
            for(size_t i = 0; i < N; ++i)
            {
                MLNode *te = &typeexprs.ch[i];
                MLVar *v = &ft.mlir.vars[localid + i];
                v->decl = declidx;
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

            // Check amount
            // Any expression can return a number of results, which needs to be known to know
            // how many variables to typecheck per expr.
            NumValuesResult nv = numResultValuesOfList(exprs.ch, exprs.n, ft);

            switch(nv.verdict)
            {
                case NumValuesResult::ERROR:
                    assert(false);
                    return;

                case NumValuesResult::EXACT_NUMBER:
                    if(nv.n < N)
                    {
                        std::ostringstream os;
                        os << "Getting " << nv.n << " values, but " << N << " are needed";
                        node->setError(ft.sp(), os.str().c_str());
                        return;
                    }
                    else if(nv.n > N)
                    {
                        std::ostringstream os;
                        os << "Getting " << nv.n << " values, but only " << N << " are assigned (" << (nv.n - N) << " are dropped)";
                        ft.warn(node, os.str().c_str());
                    }
                    break;

                case NumValuesResult::MIN_NUMBER:
                    if(nv.n < N)
                    {
                        // TODO: allow this when the tail ones are optionals?
                        std::ostringstream os;
                        os << "Getting " << nv.n << " or more values, but " << N << " are needed";
                        node->setError(ft.sp(), os.str().c_str());
                        return;
                    }
                    else if(nv.n > N)
                    {
                        std::ostringstream os;
                        os << "Getting " << nv.n << " or more values, but only " << N << " are assigned (" << (nv.n - N) << " are always dropped)";
                        ft.warn(node, os.str().c_str());
                    }
                    break;
            }

            size_t idx = localid;

            // Assign type based on value if automatic, or report type clash
            size_t numconstval = 0;
            for(size_t i = 0; i < exprs.n; ++i)
            {
                MLNode *e = &exprs.ch[i];
                NumValuesResult eres = numResultValues(e, ft);
                Type t = e->type();
                assert(eres.n >= 1);
                if(eres.n > 1)
                {
                    TypeIdList tl = ft.tr().getlist(t);
                    for(size_t k = 0; k < tl.n; ++k)
                    {
                        Type tt = tl.ptr[k];
                        MLVar *v = &ft.mlir.vars[idx++];
                        assignVarType(v, tt, ft);
                    }
                    // TODO: support multiple values constant expr (_ML_MULTIVAL using an array internally?)
                }
                else
                {
                    MLVar *v = &ft.mlir.vars[idx++];
                    if(!v->isMutable() && e->isconst())
                    {
                        v->kind = MLVar::CONSTVAL;
                        v->u.val = e->asVal();
                        ++numconstval;
                    }
                    else
                        assignVarType(v, t, ft);
                }
            }

            // ALL values got replaced by compile-time known constants? Don't need this decl node, kick it.
            // All ML_VAR referenced later that were declared here it will be replaced with constants,
            // so there will be no reference left.
            if(numconstval == N)
                node->makedummy();
        }
        break;

        case OP_CALL:
        {
            MLNode *funcexpr = node->firstChild();
            assert(funcexpr->m.cmd != ML_LIST);
            if(funcexpr->isconst())
                if(const DFunc *df = ft.getCallable(funcexpr->asVal().asAnyObj()))
                    tryFoldCall(node, *df, funcexpr + 1, node->numchildren() - 1, -1, ft);
            return;

        }
        break;

        case _ML_UVAR:
            assert(false); // should have been resolved by now
            break;

        default:
            if(cmd < _ML_OP_MAX)
            {
                // It's an operator -> technically a function call, but known to return a single value.
                tryFoldOpr(node, ft);
                return;
            }


    }

    assert(mlirIsStmt((MLCmd)node->m.cmd));

    // If expr, Should have early-returned. If not, type analysis failed.
    // If stmt, that has no type.
    node->x.exprtype = PRIMTYPE_NOTYPE;
}

static MLPreVisitResult foldPreErrorCheck(MLNode *node, MLNode *parent, void *ud)
{
    MLFoldTracker& ft = *(MLFoldTracker*)ud;
    MLPreVisitResult res = { VISIT_CONTINUE, 0 };
    const MLCmd cmd = (MLCmd)node->m.cmd;

    if(node->m.cmd == _ML_ERROR)
        ft.error(node, ft.str(node->m.p[0]).s);
    else
        node->m.nch = mlirNumChildren(cmd); // Annotate for the LLIR stage

    return res;
}

bool MLIR::fold(VM& vm, Symstore& syms, SymTable &env)
{
    MLFoldTracker ft = { *this, vm, syms, env, true };
    ft.typesrc.resize(nodes.size());
    preFoldExternals(externals.data(), externals.size(), ft);
    preFoldVars(vars.data(), vars.size(), ft);
    visit(foldPre, foldPost, &ft);
    visit(foldPreErrorCheck, NULL, &ft); // needs no post
    return ft.errors.empty();
}
