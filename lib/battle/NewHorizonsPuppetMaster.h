/*
 * NewHorizonsPuppetMaster.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../spells/ProxyCaster.h"

#include <string_view>

namespace battle
{
	class Unit;
}

class CBattleInfoEssentials;

namespace newHorizonsPuppetMaster
{
inline constexpr std::string_view SPELL_ID = "new-horizons:puppetMaster";
inline constexpr std::string_view LUCIDITY_SPELL_ID = "new-horizons:lucidity";

DLL_LINKAGE bool hasControlMarker(const battle::Unit * unit);
DLL_LINKAGE bool hasValidControlMarker(const CBattleInfoEssentials & battle, const battle::Unit * unit);
DLL_LINKAGE bool hasLucidity(const battle::Unit * unit);
DLL_LINKAGE bool isMentalControlSpell(std::string_view spellJsonKey);
DLL_LINKAGE bool isValidControlMarker(const CBattleInfoEssentials & battle, const battle::Unit * unit,
	const Bonus * marker);

DLL_LINKAGE Bonus controlMarker(SpellID spell, PlayerColor caster);
DLL_LINKAGE Bonus lucidityMarker();

/// Proxies only spell ownership for a player-selected creature ability. All
/// creature-derived spell stats and identity continue to come from the actual
/// caster, and passive/reaction casts should not use this wrapper.
class DLL_LINKAGE ActionControllerCaster final : public spells::ProxyCaster
{
public:
	ActionControllerCaster(const spells::Caster * actualCaster, PlayerColor actionController);
	PlayerColor getCasterOwner() const override;

private:
	PlayerColor actionController;
};
}
