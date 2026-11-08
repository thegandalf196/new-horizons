/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommittedElementalEffects.h"
#include "CBattleInfoCallback.h"
#include "BattleInfo.h"
#include "Unit.h"
#include "UnitCreatureResolver.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>
namespace battle
{
CommittedElementalEffects::CommittedElementalEffects(const CBattleInfoCallback & observer)
{
	const auto * native = dynamic_cast<const BattleInfo *>(observer.getBattle());
	if(!native) throw std::invalid_argument("Current elemental effect capture requires the native source interval");
	// Compile only effects already supplied by the current armies. No dormant
	// elemental activation, spell offense or future hero action is captured.
	static constexpr std::array<std::string_view, 6> keys = {
		"", "core:airElemental", "core:fireElemental", "core:waterElemental", "core:earthElemental", "core:phoenix"};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto profile = native->getElementalAttunementProfile(side);
		if(profile.stoneboundMagic)
			if(const auto * hero = native->getSideHero(side))
				profile.stoneboundBasisPoints = newHorizonsElementalAttunement::stoneboundReductionBasisPoints(
					hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
		for(size_t family = 0; family < keys.size(); ++family)
			effects.at(side)[family] = newHorizonsElementalAttunement::passivesForCreatureKey(
				profile, native->getElementalAttunementMask(side), keys[family]);
	}
}
newHorizonsElementalAttunement::UnitPassives CommittedElementalEffects::evaluate(const Unit & unit) const
{
	std::string key;
	if(const auto * resolver = unit.unitCreatureResolver())
		key = resolver->encode(unit.creatureId().getNum());
	else if(const auto * type = unit.creatureId().toCreature()) key = type->getJsonKey();
	size_t family = 0;
	if(key == "core:airElemental" || key == "core:stormElemental") family = 1;
	else if(key == "core:fireElemental" || key == "core:energyElemental") family = 2;
	else if(key == "core:waterElemental" || key == "core:iceElemental") family = 3;
	else if(key == "core:earthElemental" || key == "core:magmaElemental") family = 4;
	else if(key == "core:firebird" || key == "core:phoenix") family = 5;
	// Control changes do not transfer the original army's hero bonuses.
	return effects.at(unit.unitSide())[family];
}
}
