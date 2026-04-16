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
    Queue<MLNode*> q;
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

        default: ;
    }

    assert(false);
    return 0;
}

static void mlirDumpNode(MLDumper& dump, ValStore& vals, MLNode *node)
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
        MLNode *c = node->firstChild();
        for(size_t i = 0; i < nch; ++i)
            dump.q.push(vals.gc, &c[i]);
    }
}

static void mlirDump(BufSink *sk, StringPool& sp, MLNode *root)
{
    ValStore vals(sp.gc);
    MLDumper dump(sp.gc);

    MLNode *node = root;
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

Val MLNode::asVal()
{
    STATIC_ASSERT(STATIC_OFFSETOF(MLNode, m.cmd) >= STATIC_END_OF(MLNode, val.type));

    assert(m.cmd == _ML_VAL);
    return val;
}


static void mlirGenerateNode(HLNode *root)
{
}

MLIRBuilder::MLIRBuilder(GC& gc)
    : gc(gc)
{
}

size_t MLIRBuilder::indexOf(MLNode* node) const
{
    const MLNode *base = nodes.data();
    assert(base <= node && node < base + nodes.size());
    return node - base;
}

static void visitRec(MLVisitorPre pre, MLVisitorPost post, MLNode *node, MLNode *parent)
{
    const uintptr_t aux = pre ? pre(node, parent) : 0;
    if(size_t nch = mlirNumChildren((MLCmd)node->m.cmd))
    {
        MLNode *ch = node->firstChild();
        for(size_t i = 0; i < nch; ++i)
            visitRec(pre, post, ch + i, node);
    }
    if(post)
        post(node, parent, aux);
}

void MLIRBuilder::visit(MLVisitorPre pre, MLVisitorPost post)
{
    visitRec(pre, post, &nodes[0], NULL);
}


void MLIRBuilder::construct(const HLNode* root)
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

            if(size_t nch = r.node->numchildren())
            {
                r.parentidx = a.size();
                const HLNode * const * const ch = r.node->children();
                for(size_t i = 0; i < nch; ++i)
                {
                    r.node = ch[i];
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

static MLCmd convertNode(MLNode& m, const HLNode& h)
{
    MLCmd cmd = ML_LIST;
    switch(h.type)
    {
#define CASE(hltype, mltype) break; case hltype: cmd = mltype;

        CASE(HLNODE_LIST, ML_LIST)
            m.list.len = h.u.list.used;
        CASE(HLNODE_WHILELOOP, ML_WHILE)
        CASE(HLNODE_FORLOOP, ML_FOR)

#undef CASE
        break;
        default:
            m.list.len = h.numchildren(); // For testing only
            unreachable();
    }
    return cmd;
}

void MLIRBuilder::convert()
{
    infos.resize(gc, nodes.size());

    for(size_t i = 0; i < nodes.size(); ++i)
    {
        MLNode& m = nodes[i];
        if(m.m.cmd != _ML_HL_TODO)
            continue;

        const HLNode& h = *m.hl.node;

        MLInfo& info = infos[i];
        info.line = h.line;
        info.column = h.column;

        m.m.cmd = convertNode(m, h);
    }
}
