#pragma once

#include "defs.h"
#include <vector>


enum ScopeType
{
    SCOPE_NONE = 0,
    SCOPE_BLOCK,
    SCOPE_VALBLOCK,
    SCOPE_FUNCTION
};

enum ScopeReferral
{
    SCOPEREF_LOCAL,    // in local scope
    SCOPEREF_UPVAL,    // not in local scope but exists in an outer scope across a function boundary
    SCOPEREF_EXTERNAL, // unknown identifier
};

enum SymbolRefContext
{
    SYMREF_STANDARD = 0x00,  // Just any symbol (plain old variable, no special properties)
    SYMREF_MUTABLE  = 0x01,  // Symbol is target of an assignment after declaration
    SYMREF_TYPE     = 0x02,  // Symbol is used as a type
    SYMREF_EXPORTED = 0x04,  // Symbol is exported
    SYMREF_NOTAVAIL = 0x08,  // Force symbol lookup to fail
};

enum SymbolUsageFlags
{
    SYMUSE_UNUSED   = 0x00, // Variable is unused
    SYMUSE_USED     = 0x01, // Variable is used at least once
    SYMUSE_UPVAL    = 0x02, // Local variable is used as an upvalue and must be closed
};

class SlotDistrib
{
public:
    SlotDistrib() : _used(0) {}
    u32 pick();
    void putback(u32 slot);
    u32 totalused() const { return _used; }

private:
    u32 _used;
    std::vector<u32> avail;
};

class Symstore
{
public:
    struct Sym
    {
        unsigned nameStrId;
        unsigned linedefined;
        unsigned lineused; // first usage; if 0, var is never used
        unsigned referencedHow; // MLSymbolRefContext
        unsigned usage;
        unsigned localslot; // FIXME remove this

        inline bool used() const { return usage || (referencedHow & SYMREF_MUTABLE); }
        inline bool mustclose() const { return (usage & SYMUSE_UPVAL) && (referencedHow & SYMREF_MUTABLE); }
    };
    struct Frame
    {
        ScopeType boundary;
        std::vector<unsigned> symids;
        // FIXME: This should be in the next lowering stage
        SlotDistrib localids; // only used when boundary == SCOPE_FUNCTION
    };
    struct Lookup
    {
        const Sym *sym;
        ScopeReferral where;
        int symindex;
        const char *namewhere() const;
    };
    struct Decl
    {
        Sym *sym;
        Sym *clashing;
    };

    Symstore();
    ~Symstore();

    void push(ScopeType boundary);
    void pop(Frame& f);

    const Frame& peek() const;


    Lookup lookup(unsigned strid, unsigned line, SymbolRefContext referencedHow);

    // If NULL: ok; otherwise: clashing symbol
    Decl decl(unsigned strid, unsigned line, SymbolRefContext referencedHow);

    Sym *getsym(unsigned uid);
    unsigned getuid(const Sym *sym);

    Sym& makeUsable(unsigned uid); // helper for the parser; removes SYMREF_NOTAVAIL flag
    Sym& makeMutable(unsigned uid); // helper for the parser; removes SYMREF_NOTAVAIL flag

    std::vector<unsigned> missing;

private:

    Frame& funcframe();
    Sym *newsym();
    Sym *findinframe(const unsigned *uids, size_t n, sref strid);
    unsigned indexinframe(const unsigned *uids, size_t n, const Sym *sym);

    unsigned _indexbase;
    std::vector<Frame> frames;
    std::vector<Sym> allsyms;
};
