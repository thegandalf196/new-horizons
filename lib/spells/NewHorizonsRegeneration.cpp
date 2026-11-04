/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsRegeneration.h"
#include "BattleSpellMechanics.h"
#include "CurePhysicalAffliction.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMetamagicEffects.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/CUnitState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>
#include <set>

namespace newHorizonsRegenerationEffects
{
bool isSpell(const CSpell * spell, const JsonNode & savedRules)
{
	if(!spell || !newHorizonsMagic::spellAllowedBySavedRoster(savedRules, spell->getId()))
		return false;
	const auto base = newHorizonsMagic::spellVariantBase(savedRules, spell->getId());
	const auto * definition = base.toSpell();
	return definition && definition->getJsonKey() == newHorizonsMagic::NATURE_REGENERATION_SPELL;
}

bool isMassSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return isSpell(spell, savedRules)
		&& newHorizonsMagic::spellVariantBase(savedRules, spell->getId()) != spell->getId();
}

bool eligible(const battle::Unit * unit)
{
	return spells::curePhysical::livingTarget(unit) && !unit->isClone() && unit->getPhantomInitialIntegrity() <= 0;
}

bool hasMarker(const battle::Unit * unit, SpellID regenerationSpell)
{
	if(!unit)
		return false;
	return unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(regenerationSpell))
		.And(Selector::type()(BonusType::HP_REGENERATION)));
}

int32_t rate(const spells::BattleSpellMechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool herbalist = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST));
	return newHorizonsMagic::regenerationRateMillionthsBasisPointsWithSpecialty(
		std::max<int32_t>(0, mechanics.getEffectPower()), mechanics.getSpellPowerCoefficientBasisPoints(),
		herbalist, mechanics.getEmpowerSpellBonusPercent(),
		mechanics.getHerbalSurgeonRegenerationPercent());
}

void applyRateSnapshot(ServerCallback * server, const CBattleInfoCallback * battle,
	const battle::Units & affectedUnits, const int32_t regenerationRate, SpellID regenerationSpell,
	const newHorizonsMetamagic::CastProfile * metamagic)
{
	if(!server || !battle)
		return;

	std::set<uint32_t> affectedUnitIds;
	for(const auto * unit : affectedUnits)
		if(unit)
			affectedUnitIds.insert(unit->unitId());

	for(const auto unitId : affectedUnitIds)
	{
		const auto * unit = battle->battleGetUnitByID(unitId);
		if(!unit || !hasMarker(unit, regenerationSpell))
			continue;

		auto state = unit->acquireState();
		if(!state)
			continue;
		state->regenerationRateMillionths = newHorizonsMetamagic::outgoingMagnitude(regenerationRate, metamagic, unit);

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = battle->getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		server->apply(changed);
	}
}

void filterMassTargets(spells::effects::Effects::EffectsToApply & effectsToApply)
{
	for(auto & effect : effectsToApply)
		vstd::erase_if(effect.second, [](const battle::Destination & destination)
		{
			return !eligible(destination.unitValue);
		});
}

}
