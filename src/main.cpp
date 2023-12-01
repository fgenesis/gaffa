#include <stdio.h>
#include <string>
#include <sstream>
#include "parser.h"
#include "ast.h"

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

int main(int argc, char **argv)
{
    const char *code = slurp("test.txt");
    if(!code)
        return 1;
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

    return 0;
}

