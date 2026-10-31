/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <string_view>
#include <vector>
class JsonNode;
namespace battle { class Unit; }
namespace spells::curePhysical
{
/// Shared historical living-body admission, unchanged by physical status kinds.
DLL_LINKAGE bool livingTarget(const battle::Unit * unit);
DLL_LINKAGE bool validKind(std::string_view kind);
DLL_LINKAGE std::vector<Bonus> graveDustRemoval(const battle::Unit & unit);
DLL_LINKAGE bool present(const JsonNode & rules, const battle::Unit & unit, std::string_view kind);
}
