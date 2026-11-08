/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <optional>
#include <string_view>

namespace battleSearch
{
/// Search approximation only; never spell/Order legality or native effect rules.
enum class Profile { Full, BeamOne, BeamOneTwoActors };
DLL_LINKAGE std::optional<Profile> profileForAIName(std::string_view name);
DLL_LINKAGE bool forecastBeamOne();
DLL_LINKAGE unsigned forecastFutureActors();

/// One synchronous decision on its executing thread. Nested reference scopes win
/// and restore the preceding policy, including on exceptions. Workers must enter
/// their own scope explicitly; no process-wide or saved gameplay policy exists.
class DLL_LINKAGE Scope
{
	Profile previous;
public:
	explicit Scope(Profile profile);
	~Scope();
	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;
};
}
