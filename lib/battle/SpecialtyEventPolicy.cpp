/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpecialtyEventPolicy.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "CUnitState.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
#include <array>
namespace newHorizonsSpecialtyEvents::detail
{
namespace
{
bool member(CreatureID type, Family kind)
{
	static const std::array<CreatureID, 8> families{
		CreatureID(CreatureID::decode("core:waterElemental")), CreatureID(CreatureID::decode("core:goblin")),
		CreatureID(CreatureID::decode("core:walkingDead")), CreatureID(CreatureID::decode("core:serpentFly")),
		CreatureID(CreatureID::decode("core:swordsman")), CreatureID(CreatureID::decode("core:wyvern")),
		CreatureID(CreatureID::decode("core:mage")), CreatureID(CreatureID::decode("core:cyclop"))};
	const auto base = families.at(static_cast<size_t>(kind));
	return type.hasValue() && base.hasValue() && base.toCreature() && type.toCreature()
		&& (type == base || base.toCreature()->isMyDirectOrIndirectUpgrade(type.toCreature()));
}
}
bool family(const battle::Unit & unit, Family kind)
{
	return newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit) && member(unit.creatureId(), kind);
}
bool nativeFamily(const battle::CUnitState & unit, Family kind)
{
	return member(unit.hasBattleForm() ? unit.battleFormOriginalCreature() : unit.creatureId(), kind);
}

int coefficient(const CBattleInfoCallback & battle, const battle::Unit & unit,
	const char * heroIdentity, const char * key, bool originalArmy)
{
	const auto side = originalArmy ? unit.unitSide() : battle.playerToSide(battle.battleGetOwner(&unit));
	const auto visible = battle.battleGetVisibleHeroPolicy(side);
	const auto * hero = visible.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO ? visible.legacyHero() : nullptr;
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != heroIdentity)
		return 0;
	const auto & policy = hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const auto & value = policy[key];
	if(value.isNull()) return 0;
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > 100)
		throw std::runtime_error("Invalid captured event specialty coefficient");
	return static_cast<int>(value.Integer());
}
}
