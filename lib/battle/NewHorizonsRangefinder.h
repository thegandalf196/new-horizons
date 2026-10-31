/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include <optional>
class IBonusBearer;
class CBattleInfoCallback;
class BattleHex;
struct Bonus;
namespace battle { class Unit; }
namespace newHorizonsRangefinder
{
// Existing LIMITED_SHOOTING_RANGE metadata, identified by exact specialty source.
// Its val255 is a neutral legal envelope; parameters carry only full-damage range.
DLL_LINKAGE void configureEnvelope(Bonus & bonus, int capturedDistance);
DLL_LINKAGE bool isEnvelope(const Bonus & bonus);
DLL_LINKAGE std::optional<int> fullDamageDistance(const IBonusBearer & shooter);
DLL_LINKAGE std::shared_ptr<const Bonus> legalRangeBonus(const IBonusBearer & shooter);
DLL_LINKAGE std::optional<bool> distancePenalty(const IBonusBearer & shooter,
	const BattleHex & source, const BattleHex & destination, const CBattleInfoCallback & battle);
}
