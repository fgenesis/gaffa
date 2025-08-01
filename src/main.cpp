#include "lex.h"
#include "parser.h"
#include "hlir.h"
#include "gainternal.h"
#include "strings.h"
#include "gaimpdbg.h"
#include "mlir.h"
#include "table.h"
#include "gc.h"
#include "dedupset.h"
#include "typing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>


static char s_content[64*1024];

const char *slurp(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    if(!f)
        return NULL;
    size_t done = fread(s_content, 1, sizeof(s_content), f);
    fclose(f);
    s_content[done] = 0;
    return &s_content[0];
}

static void *myalloc(void *ud, void *ptr, size_t osz, size_t nsz)
{
    if(ptr && !nsz)
    {
        //printf("free  %u bytes\n", (unsigned)osz);
        free(ptr);
        return NULL;
    }
    //printf("alloc %u bytes\n", (unsigned)nsz);
    return realloc(ptr, nsz);
}

/*
void testref()
{
    GC gc = {0};
    gc.alloc = myalloc;

    RefTable<size_t> t;
    size_t a = t.addref(gc, 100);
    size_t b = t.addref(gc, 200);
    size_t c = t.addref(gc, 300);
    assert(*t.getref(a) == 100);
    assert(*t.getref(b) == 200);
    assert(*t.getref(c) == 300);
}*/

void testtable()
{
    GC gc = {0};
    gc.alloc = myalloc;

    Type tt = {PRIMTYPE_ANY};
    Table *t = Table::GCNew(gc, tt, tt);

    for(uint i = 1; i < 6; ++i)
        t->set(gc, i, i*10);

    int a = 0;
}

void testdedup()
{
    GC gc = {0};
    gc.alloc = myalloc;

    Dedup dd(gc, 0);
    dd.init();

    sref a = dd.putCopy("1234", 5);
    sref b = dd.putCopy("abc", 4);
    sref a2 = dd.putCopy("1234", 5);
    assert(a == a2);

    puts(dd.get(a).p);
    puts(dd.get(b).p);

    PodArray<sref> aa;
    /*for(unsigned i = 0; i < 99999; ++i)
        aa.push_back(gc, dd.putCopy(&i, sizeof(i)));*/
    dd.sweepstep(0);
    dd.sweepfinish(true);

    /*for(unsigned i = 0; i < 99999; ++i)
    {
        sref r = aa[i];
        MemBlock b = dd.get(r);
        assert(b.n == sizeof(i));
        assert(!memcmp(b.p, &i, sizeof(i)));
    }*/
    //dd.mark(b);
    dd.sweepstep(0);
    dd.sweepfinish(true);

    aa.dealloc(gc);
}

void testtype()
{
    GC gc = {0};
    gc.alloc = myalloc;

    Dedup st(gc, 1);
    st.init();

    TypeRegistry tr(gc);
    tr.init();

    sref x = st.putCopy("x", 1);
    sref y = st.putCopy("y", 1);

    Table *tvec = Table::GCNew(gc, Type{PRIMTYPE_STRING}, Type{PRIMTYPE_TYPE});
    tvec->set(gc, _Str(x), Type{PRIMTYPE_FLOAT});
    tvec->set(gc, _Str(y), Type{PRIMTYPE_FLOAT});
    {
        KV xe = tvec->index(0);
        assert(xe.k.type.id == PRIMTYPE_STRING);
        assert(xe.v.type.id == PRIMTYPE_TYPE);
        KV ye = tvec->index(1);
        assert(ye.k.type.id == PRIMTYPE_STRING);
        assert(ye.v.type.id == PRIMTYPE_TYPE);
    }

    Type fvec2 = tr.mkstruct(*tvec, true);
    const TDesc *td = tr.getstruct(fvec2);
    printf("Struct of %u:\n", (unsigned)td->size());
    for(tsize i = 0; i < td->size(); ++i)
    {
        const char *name = (const char*)st.get(td->names()[i]).p;
        printf("%s %u\n", name, td->types()[i].id);
    }
    tr.dealloc();
    st.dealloc();
}

extern void vmtest();

int main(int argc, char **argv)
{
    vmtest();
    return 0;

    //testdedup();
    //testref();
    //testtable();
    //testtype();
    //return 0;

    const char *fn = "test.txt";

    GC gc = {0};
    gc.alloc = myalloc;

    const char *code = slurp(fn);
    if(!code)
        return 1;

    StringPool strtab(gc);
    strtab.init();
    Lexer lex(code);
    Parser pp(&lex, fn, gc, strtab);
    HLIRBuilder hb(gc);
    pp.hlir = &hb;
    HLNode *node = pp.parse();
    if(!node)
        return 1;

    hlirDebugDump(strtab, node);

    MLIRContainer mc(gc);
    mc.import(node, strtab, fn);


   return 0;
}
