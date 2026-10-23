// Test suite runner. Runs every .txt file in the stage directories below.
// <stage>_pos: must pass all stages up to and including <stage>.
// <stage>_neg: must pass all stages before <stage> and fail at <stage>.
// A file may hold several test cases separated by lines starting with "###"
// (text after ### is the case title). Other lines starting with "##" are reserved
// for test parameters. Both are blanked out before compiling (line numbers stay intact).

#include "lex.h"
#include "parser.h"
#include "hlir.h"
#include "gainternal.h"
#include "strings.h"
#include "symtable.h"
#include "runtime.h"
#include "mlir.h"
#include "gavm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dirent.h>
#endif

enum Stage
{
    STAGE_PARSE,
    STAGE_FOLD,
    STAGE_NONE // all stages passed
};

static const char * const StageNames[] = { "parser", "typecheck", "(none)" };

struct TestDir
{
    const char *dir;
    Stage stage;
    bool positive;
};

static const TestDir Dirs[] =
{
    { "parser_pos",    STAGE_PARSE, true  },
    { "parser_neg",    STAGE_PARSE, false },
    { "typecheck_pos", STAGE_FOLD,  true  },
    { "typecheck_neg", STAGE_FOLD,  false },
};

static void *alloc(void *ud, void *ptr, size_t osz, size_t nsz)
{
    (void)ud; (void)osz;
    if(!nsz)
    {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, nsz);
}

// Mirrors the stage setup in main.cpp; adapt when the pipeline changes.
// Returns the stage that failed, or STAGE_NONE if all stages up to 'upto' passed.
static Stage compile(const char *fn, const char *code, Stage upto)
{
    Runtime rt;
    bool ok = rt.init(alloc);
    assert(ok);

    Lexer lex(code);
    Parser pp(&lex, fn, rt.gc, rt.sp);
    HLIRBuilder hb(rt.gc);
    pp.hlir = &hb;
    HLNode *node = pp.parse();
    if(!node)
        return STAGE_PARSE;
    if(upto == STAGE_PARSE)
        return STAGE_NONE;

    SymTable *env = SymTable::GCNew(rt.gc);
    VM vm;
    vm.rt = &rt;
    rtinit(*env, rt.gc, rt.sp, rt.tr);

    MLIR ml(rt.gc);
    ml.construct(node, pp.syms, rt.sp, MLIR::DEFAULT);
    if(!ml.fold(vm, pp.syms, *env))
        return STAGE_FOLD;

    return STAGE_NONE;
}

struct TestCase
{
    std::string title; // text after ###, if any
    unsigned line;     // first line of the case
    std::string code;
};

// A file is one test case, or several separated by lines starting with "###".
// Lines of other cases and "##" lines are blanked so that line numbers stay intact.
// When a file is split, cases containing only whitespace are dropped, and text before
// the first ### is only a case if it contains more than comments.
static bool readTests(const std::string& fn, std::vector<TestCase>& out)
{
    FILE *f = fopen(fn.c_str(), "rb");
    if(!f)
        return false;
    std::string src;
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)))
        src.append(buf, n);
    fclose(f);

    std::vector<std::string> lines;
    for(size_t pos = 0; pos < src.size(); )
    {
        size_t eol = src.find('\n', pos);
        eol = eol == std::string::npos ? src.size() : eol + 1;
        lines.push_back(src.substr(pos, eol - pos));
        pos = eol;
    }

    std::vector<size_t> starts(1, 0);
    for(size_t i = 0; i < lines.size(); ++i)
        if(!lines[i].compare(0, 3, "###"))
            starts.push_back(i);
    const bool split = starts.size() > 1;

    for(size_t c = 0; c < starts.size(); ++c)
    {
        const size_t beg = starts[c];
        const size_t end = c + 1 < starts.size() ? starts[c + 1] : lines.size();
        TestCase tc;
        tc.line = (unsigned)beg + 1;
        if(c)
        {
            const std::string& hdr = lines[beg];
            size_t b = hdr.find_first_not_of("# \t");
            size_t e = hdr.find_last_not_of(" \t\r\n");
            if(b != std::string::npos && e != std::string::npos && e >= b)
                tc.title = hdr.substr(b, e - b + 1);
        }
        for(size_t i = 0; i < lines.size(); ++i)
        {
            if(i >= beg && i < end && lines[i].compare(0, 2, "##"))
                tc.code += lines[i];
            else
                tc.code += '\n';
        }
        if(split && tc.code.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;
        if(split && !c) // Text before the first ### is a file description unless it contains code
        {
            bool code = false;
            for(size_t i = beg; i < end && !code; ++i)
            {
                size_t b = lines[i].find_first_not_of(" \t\r\n");
                code = b != std::string::npos && lines[i].compare(b, 2, "--") && lines[i].compare(0, 2, "##");
            }
            if(!code)
                continue;
        }
        out.push_back(tc);
    }
    return true;
}

static std::vector<std::string> listTxt(const std::string& dir)
{
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "/*.txt").c_str(), &fd);
    if(h != INVALID_HANDLE_VALUE)
    {
        do
            if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                out.push_back(fd.cFileName);
        while(FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    if(DIR *d = opendir(dir.c_str()))
    {
        while(dirent *e = readdir(d))
        {
            size_t len = strlen(e->d_name);
            if(len > 4 && !strcmp(e->d_name + len - 4, ".txt"))
                out.push_back(e->d_name);
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

int main()
{
    std::vector<std::string> failures;
    unsigned total = 0;

    for(size_t d = 0; d < sizeof(Dirs) / sizeof(Dirs[0]); ++d)
    {
        const TestDir& td = Dirs[d];
        std::vector<std::string> files = listTxt(std::string(GAFFA_TEST_DIR) + "/" + td.dir);
        if(files.empty())
            printf("WARNING: no tests in %s\n", td.dir);
        for(size_t i = 0; i < files.size(); ++i)
        {
            const std::string fname = std::string(td.dir) + "/" + files[i];
            std::vector<TestCase> cases;
            if(!readTests(std::string(GAFFA_TEST_DIR) + "/" + fname, cases))
            {
                printf("FAIL %s: can't read file\n", fname.c_str());
                failures.push_back(fname);
                ++total;
                continue;
            }

            for(size_t k = 0; k < cases.size(); ++k)
            {
                const TestCase& tc = cases[k];
                std::string name = fname;
                if(cases.size() > 1 || tc.line > 1)
                    name += ":" + std::to_string(tc.line);
                if(!tc.title.empty())
                    name += " (" + tc.title + ")";
                printf("==== %s\n", name.c_str());
                ++total;

                Stage failed = compile(fname.c_str(), tc.code.c_str(), td.stage);
                Stage expected = td.positive ? STAGE_NONE : td.stage;
                if(failed == expected)
                    printf("PASS %s\n", name.c_str());
                else
                {
                    printf("FAIL %s: failed at stage '%s', expected '%s'\n",
                        name.c_str(), StageNames[failed], StageNames[expected]);
                    failures.push_back(name);
                }
            }
        }
    }

    printf("\n%u passed, %u failed, %u total\n", total - (unsigned)failures.size(), (unsigned)failures.size(), total);
    for(size_t i = 0; i < failures.size(); ++i)
        printf("  FAILED: %s\n", failures[i].c_str());
    return failures.empty() ? 0 : 1;
}
