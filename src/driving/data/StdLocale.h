#ifndef DRIVING_DATA_STDLOCALE_H_
#define DRIVING_DATA_STDLOCALE_H_

// The C++ library's locale helpers just below the debug variables' streams (0x000378c0..0x000379c0): a facet's
// reference count, its destructors, ~locale, ios_base::getloc and ios_base's deleting destructor, as the game
// compiled them (Dinkumware, MSVC 7). The string streams (StdStreams.cpp) reach them through getloc and the
// facets' destructors; the reference counts are taken under the library's locale lock.
//
// They are members of Facet, Locale and IosBase (StdStreams.h).

#include "StdStreams.h"

#endif // DRIVING_DATA_STDLOCALE_H_
