/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../../GameLibrary.h"
#include "../../../battle/CUnitState.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../texts/MetaString.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
/// The original non-unit effect has identity target transforms and no hex changes.
class SummonEffect : public Effect
{
public:
	void adjustAffectedHexes(BattleHexArray &, const Mechanics *, const Target &) const override {}
	Target filterTarget(const Mechanics *, const Target & target) const override { return target; }
	Target transformTarget(const Mechanics *, const Target &, const Target & target) const override { return target; }
protected:
	JsonNode parameters;
	void initImpl(JsonNode data) override { parameters = std::move(data); }
};
namespace summon
{
inline bool phantomCaster(const Mechanics * mechanics)
{
	const auto * unit = mechanics->getUnitCaster();
	return unit && unit->getPhantomInitialIntegrity() > 0;
}
inline const Creature * creature(const battle::Unit * unit) { return unit->creatureId().toEntity(LIBRARY); }
inline SpellEffectValue value(int64_t health, int64_t count, const Creature * type)
{
	SpellEffectValue result;
	result.hpDelta = health;
	result.unitsDelta = count;
	result.unitType = type;
	return result;
}
inline bool canonical(const Mechanics * mechanics, const std::string & name)
{
	return mechanics->usesNewHorizonsMagicV3() && mechanics->getSpell()->getJsonKey() == name;
}
inline BattleHex selectedHex(const Mechanics * mechanics, const Creature * type, const Target & target)
{
	if(!type || target.size() != 1 || !target.front().hexValue.isValid()) return BattleHex::INVALID;
	const auto hex = target.front().hexValue;
	const auto available = mechanics->battle()->getAvailableHex(type, mechanics->getCasterSide(), hex);
	return available.isValid() && available == hex ? hex : BattleHex::INVALID;
}
inline bool perk(const Mechanics * mechanics, const std::string & skill, const std::string & name)
{
	const auto * hero = mechanics->getHeroCaster();
	return hero && hero->hasActivePerk(skill, name);
}
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createRelocationEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createPhantomArmyEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createSummonEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createTimedSummonEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createDemonSummonEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createReanimateEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createSacrificeEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createNatureHealthEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createOrderedNatureEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createVerdantPrisonEffect(const std::string &);
std::shared_ptr<spells::effects::Effect> createTransfigureMatterEffect(const std::string &);
}
