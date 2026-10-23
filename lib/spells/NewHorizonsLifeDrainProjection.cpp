/*
 * NewHorizonsLifeDrainProjection.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsLifeDrainProjection.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
namespace newHorizonsLifeDrain
{
bool supportsProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	const auto * battle = mechanics.battle();
	if(!spell || spell->getJsonKey() != SPELL_KEY || !spell->counteredSpells.empty()
		|| !battle || !mechanics.getHeroCaster() || mechanics.isMagicMirror()
		|| !newHorizonsMagic::rulesActive(battle->getBattle()->getMagicRules())
		|| battle->getBattle()->getMagicRules()["rulesetVersion"].Integer() != newHorizonsMagic::CURRENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(battle->getBattle()->getMagicRules(), spell->getId()))
		return false;
	const auto schools = battle->battleGetSpellSchools(spell->getId());
	if(schools.size() != 1 || schools.front().serializationKey() != "new-horizons:shadow"
		|| prepared.size() != 1)
		return false;
	const auto & [effect, destinations] = prepared.front();
	if(!effect || effect->indirect || effect->optional
		|| effect->forecastImplementation(&mechanics) != spells::effects::ForecastImplementation::CANONICAL_LIFE_DRAIN
		|| destinations.size() != 2)
		return false;
	const auto * enemy = destinations[0].unitValue;
	const auto * friendly = destinations[1].unitValue;
	return enemy && friendly && enemy->unitId() != friendly->unitId();
}

std::optional<ProjectionReceipt> captureProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared, const spells::Target & originalAim,
	int32_t concentrationBonusPercent)
{
	if(!supportsProjection(mechanics, prepared) || originalAim.size() != 2)
		return std::nullopt;
	const auto & [effect, destinations] = prepared.front();
	for(size_t index = 0; index < 2; ++index)
	{
		const auto * unit = destinations[index].unitValue;
		if(!originalAim[index].unitValue || unit->unitId() != originalAim[index].unitValue->unitId()
			|| destinations[index].hexValue != unit->getPosition()
			|| originalAim[index].hexValue != destinations[index].hexValue
			|| !mechanics.ownerMatches(unit, index == 1))
			return std::nullopt;
	}
	return ProjectionReceipt{destinations[0].unitValue->unitId(), destinations[0].hexValue,
		effect->name, concentrationBonusPercent, mechanics.getEffectLevel(), mechanics.getEffectPower(),
		mechanics.getEffectValue(), mechanics.getEffectDuration(), mechanics.getSpellPowerCoefficientBasisPoints(),
		mechanics.getCastSpellPowerComponentBonusPercent()};
}
}
