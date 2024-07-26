#include "lex.h"
#include "parser.h"
#include "hlir.h"
#include "gainternal.h"
#include "strings.h"
#include "gaimpdbg.h"
#include "mlir.h"

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

int main(int argc, char **argv)
{
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

