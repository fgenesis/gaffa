#include <stdio.h>
#include <string>
#include <sstream>
#include "lex.h"
#include "parser.h"
#include "hlir.h"
#include "gainternal.h"

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
        printf("free  %u bytes\n", (unsigned)osz);
        free(ptr);
        return NULL;
    }
    printf("alloc %u bytes\n", (unsigned)nsz);
    return realloc(ptr, nsz);
}

static void lexall(const char *code)
{
    Lexer lex(code);
    for(;;)
    {
        Lexer::Token t = lex.next();
        switch(t.tt)
        {
            case Lexer::TOK_E_ERROR:
                printf("lex(%u): error: %s\n", t.line, t.u.err);
                return;
            default:
                printf("(%u) %.*s | %u\n", t.line, t.u.len, t.begin, t.tt);
                if(t.tt == Lexer::TOK_E_EOF)
                    return;
        }
    }
}


int main(int argc, char **argv)
{
    const GaAlloc ga { myalloc, NULL };

    const char *code = slurp("test.txt");
    if(!code)
        return 1;

    lexall(code);

    Lexer lex(code);
    Parser pp(&lex, "test");
    HLIRBuilder hb(ga);
    pp.hlir = &hb;
    HLNode *node = pp.parse();


    /*
    Parser p;
    if(!p.parse(code, strlen(code)))
    {
        printf("%s\n", p.getError());
        const char *pe = p.getParseErrorPos();
        std::ostringstream os;
        size_t n = 0;
        while(++n < 60 && *pe && *pe != '\n' && *pe != '\r')
            os << *pe++;
        printf("%s\n", os.str().c_str());
        printf("Error stack:\n");
        for(size_t i = 0; i < p._errors.size(); ++i)
            puts(p._errors[i].c_str());
        return 2;
    }
    */

    return 0;
}

