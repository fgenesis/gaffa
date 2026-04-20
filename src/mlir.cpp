#include "mlir.h"
#include "hlir.h"
#include "serialio.h"
#include "valstore.h"


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
            dst = &arr[arr.sz];

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
            return 1;

        case ML_DECL:
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

    byte hdr[] = { MAGIC_FILE_ID_BYTES, 'M', 0, 0, 0 };
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

size_t MLNode::numchildren() const
{
    return m.cmd != ML_LIST ? mlirNumChildren((MLCmd)m.cmd) : list.len;
}

static void mlirGenerateNode(HLNode *root)
{
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
                    const HLNode * const * const ch = r.node->children();
                    for(size_t i = 0; i < nch; ++i)
                    {
                        r.node = ch[i]; // This may be NULL. Push it anyway to get the correct count; it's handled later
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

void MLIR::importSymbols(const Symstore& syms)
{
}

typedef void (*ConvertFunc)(MLNode& m, HLNode& h);

void MLIR::convertVarDef(MLNode& m, HLNode& h)
{
    HLVarDef *def = h.as<HLVarDef>();
    //def->ident->as<HLIdent>()->
}

void MLIR::convertList(HLNode& list)
{
    const HLNode *const * ch = list.children();
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

/* Optimization ideas:
- Any null-node in a list can get removed (there shouldn't be any?)
- Any length-1 list can be replaced by its only child (simply forward parent's childOffs to new child)
*/
