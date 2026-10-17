/*
 * NativeBonusGraphAccess.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NativeBonusGraphAccess.h"

namespace
{
std::recursive_mutex & nativeGraphMutex()
{
	// Constructed before the first admitted native node completes construction.
	// No world, GUI, scheduler or library ownership is hidden in this mutex.
	static std::recursive_mutex mutex;
	return mutex;
}
}

NativeBonusGraphAccess::NativeBonusGraphAccess() : access(nativeGraphMutex()) {}
NativeBonusGraphAccess::~NativeBonusGraphAccess() = default;
