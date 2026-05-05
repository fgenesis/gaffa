#include "mlir.h"
#include "hlir.h"
#include "serialio.h"
#include "valstore.h"
#include "symstore.h"


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
        x = zigzagenc(x);
        byte *dst;
        size_t oldsz = arr.sz;
        if(oldsz + 8 > arr.cap)
            dst = arr.alloc_n(gc, 32);
        else
            dst = arr.data() + arr.sz;

        u32 done = vu128enc(dst, x);
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
    MLDumper(GC& gc) : tree(gc), dbg(gc) {}
    ~MLDumper() { q.dealloc(tree.gc); }
    Queue<const MLNode*> q;
    MLWriter tree;
    MLWriter dbg;
};

// statements have no associated type
static bool mlirIsStmt(MLCmd cmd)
{
    if(cmd < _ML_OP_MAX)
        return true;

    switch(cmd)
    {
    case ML_CONST:
    case ML_VAR:
    case ML_FNCALL:
    case ML_MTHCALL:
    case ML_FUNC:
    case ML_NEW_ARRAY:
    case ML_NEW_TABLE:
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
        case ML_CLOSE:
        case _ML_VAL:
            return 0;

        case ML_RETURN:
        case ML_YIELD:
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
        case ML_FNCALL:
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

static void mlirDumpNode(MLDumper& dump, ValStore& vals, const MLNode *node)
{
    MLCmd cmd = (MLCmd)node->m.cmd;

    size_t nch = 0;

    // special cases
    switch(cmd)
    {
        case _ML_VAL:
            dump.tree.write(ML_CONST);
            dump.tree.write(vals.put(node->val));
            return;

        case ML_LIST:
            nch = node->list.len;
            if(nch == 1) // Skip lists that have exactly 1 child and emit that child in place of the list
                goto dochildren;
            // It's intentionally impossible to construct lists of length 1
            dump.tree.write(nch ? -(int)(nch - 1) : ML_LIST);
            break;

        default:
            nch = mlirNumChildren(cmd);
            dump.tree.write(cmd);
            break;

        case _ML_DEAD:
        case _ML_HL_TODO:
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
            dump.q.push(vals.gc, &c[i]);
    }
}

static void mlirDump(BufSink *sk, StringPool& sp, const MLNode *root)
{
    ValStore vals(sp.gc);
    MLDumper dump(sp.gc);

    const MLNode *node = root;
    for(;;)
    {
        mlirDumpNode(dump, vals, node);
        if(dump.q.empty())
            break;
        node = dump.q.pop();
    }

    byte hdr[] = MAGIC_FILE_VARIANT('M');
    sk->Write(sk, hdr, sizeof(hdr));

    vals.serialize(sk, sp);

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

    assert(m.cmd == _ML_VAL);
    return val;
}

void MLNode::setVal(const ValU& v)
{
    val = v;
    m.cmd = _ML_VAL; // Write this later in case the compiler decides to copy .val plus padding area
}

size_t MLNode::numchildren() const
{
    return m.cmd != ML_LIST ? mlirNumChildren((MLCmd)m.cmd) : list.len;
}

MLIR::MLIR(GC& gc)
    : gc(gc)
{
}

MLIR::~MLIR()
{
    nodes.dealloc(gc);
    infos.dealloc(gc);
}

size_t MLIR::indexOf(MLNode* node) const
{
    assert(node);
    const MLNode *base = nodes.data();
    assert(base <= node && node < base + nodes.size());
    return node - base;
}

static void visitRec(MLVisitorPre pre, MLVisitorPost post, void *ud, MLNode *node, MLNode *parent)
{
    MLPreVisitResult vis { VISIT_CONTINUE, 0 };
    if(pre)
        vis = pre(node, parent, ud);
    if(size_t nch = node->numchildren())
    {
        MLNode *ch = node->firstChild();
        for(size_t i = 0; i < nch; ++i)
            visitRec(pre, post, ud, ch + i, node);
    }
    if(post)
        post(node, parent, ud, vis.aux);
}

void MLIR::visit(MLVisitorPre pre, MLVisitorPost post, void *ud)
{
    visitRec(pre, post, ud, &nodes[0], NULL);
}

void MLIR::dump(BufSink* sink, StringPool& sp) const
{
    mlirDump(sink, sp, nodes.data());
}

static void invalidatePost(MLNode *node, MLNode *parent, void *ud, uintptr_t aux)
{
    node->m.cmd = _ML_DEAD;
}

void MLNode::invalidate()
{
    visitRec(NULL, invalidatePost, NULL, this, NULL);
}

void MLIR::_delayExpand(MLNode * node, const HLNode * hl)
{
    node->hl.node = hl;
    node->m.cmd = _ML_HL_TODO;
}

// does not reallocate nodes[]
void MLIR::_cons(Queue<Cons>& q, MLNode *dst, const HLNode *hl)
{
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
            const size_t myidx = indexOf(dst);
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
            dst->m.cmd = ML_VAR;
            dst->m.p[0] = hl->u.ident.symid;
            return;

        case HLNODE_VARDECLASSIGN:
        {
            MLCh s = _setupCh(dst, ML_DECL);
            assert(s.n == 2);

            const HLNode *decls = hl->u.vardecllist.decllist;
            MLNode *ch = &nodes[s.chIdx];
            dst->m.p[0] = _decllist(q, ch, decls); // may reallocate hlch

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

        case HLNODE_RETURNYIELD:
        {
            const HLReturnYield& ry = hl->u.retn;
            MLCmd cmd;
            switch(hl->tok)
            {
                case Lexer::TOK_RETURN: cmd = ML_RETURN; break;
                case Lexer::TOK_YIELD: cmd = ML_YIELD; break;
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

        case HLNODE_FUNCTIONHDR: // handled as part of HLNODE_FUNCTION
            ; // not reached

        // not adding a default label here to keep compiler warnings. Let the assert below catch it.
    }

    assert(false);
    unreachable();
}

void MLIR::construct(const HLNode* root)
{
    Queue<Cons> q;
    nodes.reserve(gc, 128);

    Cons r { root, indexOf(_add(1)) };
    for(;;)
    {
        _construct(q, &nodes[r.mlidx], r.hl); // This may reallocate nodes[], don't keep a pointer
        if(q.empty())
            break;
        r = q.pop();
    }

    q.dealloc(gc);
}


#if 0
void MLIR::construct(const HLNode* root)
{
    struct HLRef
    {
        const HLNode *node;
        size_t parentidx; // if 0, no parent, otherwise the parent is located at this index - 1
    };

    PodArray<HLRef> a;

    // First step: Collect all nodes; MLNode ordering applies.
    // To be able to patch child offsets into parent MLNodes, also store the index of the parent
    {
        Queue<HLRef> q;
        HLRef r { root, 0 };
        for(;;)
        {
            a.push_back(gc, r); // Keep all nodes around in the same order for the next step

            if(r.node)
                if(size_t nch = r.node->numchildren())
                {
                    r.parentidx = a.size();
                    const HLNode * const * const hlch = r.node->children();
                    for(size_t i = 0; i < nch; ++i)
                    {
                        r.node = hlch[i]; // This may be NULL. Push it anyway to get the correct count; it's handled later
                        q.push(gc, r);
                    }
                }

            if(q.empty())
                break;
            r = q.pop();
        }
        q.dealloc(gc);
    }

    // Next step: Now that we know the total count, create MLNode array and link each one with its HLNode.
    {
        // All the MLNodes get stored in a single block of memory
        const size_t N = a.size();
        MLNode * const base = nodes.resize(gc, N);
        const u32 NO_PARENT = (u32)-1;

        size_t previdx = 0; // The first parentidx is 0 and will be skipped
        for(size_t i = 0; i < N; ++i)
        {
            const HLRef r = a[i];
            MLNode *m = &base[i];
            m->m.cmd = _ML_HL_TODO;
            m->m.chOffs = NO_PARENT; // This is overwritten later if the node has a child referring back
            m->hl.node = r.node;

            // All children follow in a row -> only take the first child to update the parent
            if(r.parentidx != previdx)
            {
                MLNode *parent = &base[r.parentidx - 1];
                assert(parent < m);
                assert(parent->m.chOffs == NO_PARENT); // Make sure nobody else has touched this so far
                parent->m.chOffs = (u32)(m - parent);
                previdx = r.parentidx;
            }
        }
        a.dealloc(gc);
    }
}

void MLIR::importSymbols(const Symstore& syms, const StringPool& sp)
{
    const size_t N = syms.allsyms.size();
    for(size_t i = 0; i < N; ++i)
    {
        const Symstore::Sym& sym = syms.allsyms[i];
        const Strp name = sp.lookup(sym.nameStrId);
        if(!(sym.usage & SYMUSE_USED))
        {
            printf("MLIR: Skip unused symbol [%s]\n", name.s);
            continue;
        }

        MLVar v = {};
        v.kind = MLVar::LOCAL;
        v.slot = sym.slot;
        v.name = sym.nameStrId;

        if(sym.slot < 0)
            v.kind = MLVar::EXT;
        if(sym.usage & SYMUSE_UPVAL)
            v.kind = MLVar::UPVAL;

        printf("MLIR: Import symbol [%s], kind = %u, slot = %d\n", name.s, v.kind, v.slot);
    }
}
#endif

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
    // There are no lists of length 1, re-use already existing node as the one list child.
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


#if 0
void MLIR::convertList(HLNode& listnode)
{
    const HLList *list = listnode.as<HLList>();
    const HLNode *const * hlch = list->list;
}

void MLIR::convertNode(MLNode& m, const HLNode& h, MLNode *parent)
{
    MLCmd cmd = ML_LIST;
    HLNodeType hltype = (HLNodeType)h.type;

    if(h._nch == HLList::Children)
    {
        // It's a list node. Make it a list.
        m.list.len = h.u.list.used;
    }

    switch(hltype)
    {
        case HLNODE_WHILELOOP: cmd = ML_WHILE; break;
        case HLNODE_FORLOOP: cmd = ML_FOR; break;
        case HLNODE_RETURNYIELD:
            switch(h.tok)
            {
                case Lexer::TOK_RETURN: cmd = ML_RETURN; break;
                case Lexer::TOK_YIELD: cmd = ML_YIELD; break;
                case Lexer::TOK_EMIT: cmd = ML_EMIT; break;
                default: unreachable();
            }
            break;
        case HLNODE_CONSTANT_VALUE:
            cmd = _ML_VAL;
            m.val = h.u.constant.val;
            break;
        case HLNODE_DECLLIST:
            ; //h.u.vardecllist.decllist

        default:
            unreachable();

        // These cases don't need further handling and can be ignored
        case HLNODE_LIST:
        case HLNODE_BLOCK:
            assert(h._nch == HLList::Children);
        ;
    }

    assert(cmd != ML_LIST || h._nch == HLList::Children);
    assert(m.numchildren() == h.numchildren());

    m.m.cmd = cmd;
}

static MLPreVisitResult convertPre(MLNode *node, MLNode *parent, void *ud)
{
    MLPreVisitResult res { VISIT_CONTINUE, 0 };

    MLIR& mlir = *(MLIR*)ud;
    if(node->m.cmd != _ML_HL_TODO)
        return res;

    const size_t idx = mlir.indexOf(node);
    const HLNode *h = node->hl.node;
    MLInfo& info = mlir.infos[idx];
    if(h)
    {
        info.line = h->line;
        info.column = h->column;
        mlir.convertNode(*node, *h, parent);
    }
    else // A NULL node becomes an empty list to indicate 'nothing there'
    {
        info.line = 0;
        info.column = 0;
        node->m.cmd = ML_LIST;
        node->list.len = 0;
    }
    return res;
}

void MLIR::convert()
{
    infos.resize(gc, nodes.size());
    visit(convertPre, NULL, this);
}
#endif

/* Optimization ideas:
- Any null-node in a list can get removed (there shouldn't be any?)
- Any length-1 list can be replaced by its only child (simply forward parent's childOffs to new child)
*/
