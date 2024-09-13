#include "lex.h"
#include "parser.h"
#include "hlir.h"
#include "gainternal.h"
#include "strings.h"
#include "gaimpdbg.h"
#include "mlir.h"
#include "table.h"
#include "gc.h"

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
}

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

int main(int argc, char **argv)
{
    testref();
    testtable();
    return 0;

    const char *fn = "test.txt";
    const GaAlloc ga = { myalloc, NULL };

    const char *code = slurp(fn);
    if(!code)
        return 1;

    StringPool strtab;
    Lexer lex(code);
    Parser pp(&lex, fn, ga, strtab);
    HLIRBuilder hb(ga);
    pp.hlir = &hb;
    HLNode *node = pp.parse();
    if(!node)
        return 1;

    //hlirDebugDump(strtab, node);

    MLIRContainer mc;
    mc.import(node, strtab, fn);


   return 0;
}

