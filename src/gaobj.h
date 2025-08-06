#pragma once

#include "defs.h"
#include "table.h"
#include "typing.h"

/* Idea:
Use a butterfly construction:
<obj header stuff>
--- obj ptr points here ---
<any number of bytes follows>

That way write<T> to obj memory can be the same for all objs -> obj+offs (userdata or not)
Only need to ensure that we always know if a pointer is headered or not (user pointers won't be)
*/


class DType;

class DObj : public GCobj
{
    DObj(DType *dty);
public:
    static DObj *GCNew(GC& gc, DType *dty);

    //Table *dfields; // Extra fields, usually NULL
    usize nmembers;

    inline       Val *memberArray()        { return reinterpret_cast<      Val*>(this + 1); }
    inline const Val *memberArray() const  { return reinterpret_cast<const Val*>(this + 1); }

    Val *member(const Val& key);
    tsize memberOffset(const Val *pmember) const; // Returns offset for memberAtOffset()

    FORCEINLINE Val *memberAtOffset(tsize offs)
    {
        return (Val*)((char*)this + offs);
    }

    // additional extra storage space for members follows
};

// generated from TDesc
class DType : public DObj
{
public:
    TDesc *tdesc;
    Table fieldIndices;
    tsize numfields() const { return tdesc->size(); }
};
