#pragma once

#ifdef __GNUG__
#include <features.h>
#undef __USE_MISC
#endif

#ifdef __USE_MISC
#error This will probably not build. Some old, non-standard POSIX shit that gcc likes to include for some reason.
#endif
