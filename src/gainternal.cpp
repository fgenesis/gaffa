#include "gainternal.h"

ga_RT::ga_RT()
    : sp(this->gc), tr(this->gc)
{
}

ga_RT::~ga_RT()
{
    tr.dealloc();
    sp.dealloc();

}
