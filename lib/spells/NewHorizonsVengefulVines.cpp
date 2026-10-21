/*
 * NewHorizonsVengefulVines.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "NewHorizonsVengefulVines.h"

#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "../battle/Unit.h"

#include <algorithm>
#include <array>
#include <set>

namespace newHorizonsVengefulVines
{
bool supportsProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared)
{
	using Implementation = spells::effects::ForecastImplementation;
	if(prepared.size() != 2)
		return false;
	const std::array expected{Implementation::CANONICAL_DAMAGE, Implementation::CANONICAL_TIMED};
	for(size_t index = 0; index < prepared.size(); ++index)
	{
		const auto * effect = prepared[index].first;
		if(!effect || effect->indirect || effect->forecastImplementation(&mechanics) != expected[index])
			return false;
	}
	return true;
}

std::optional<ProjectionReceipt> captureProjection(const spells::Mechanics & mechanics,
	const spells::effects::Effects::EffectsToApply & prepared, int32_t concentrationBonusPercent)
{
	if(!supportsProjection(mechanics, prepared))
		return std::nullopt;
	ProjectionReceipt receipt;
	receipt.concentrationBonusPercent = concentrationBonusPercent;
	receipt.effectLevel = mechanics.getEffectLevel();
	receipt.effectPower = mechanics.getEffectPower();
	receipt.effectValue = mechanics.getEffectValue();
	receipt.effectDuration = mechanics.getEffectDuration();
	receipt.spellPowerCoefficientBasisPoints = mechanics.getSpellPowerCoefficientBasisPoints();
	receipt.castSpellPowerComponentBonusPercent = mechanics.getCastSpellPowerComponentBonusPercent();
	for(const auto & [effect, targets] : prepared)
	{
		auto & captured = receipt.effects.emplace_back(ProjectionEffect{
			effect->name, effect->forecastImplementation(&mechanics), {}});
		for(const auto & destination : targets)
		{
			const auto * unit = destination.unitValue;
			if(!unit || destination.hexValue != unit->getPosition())
				return std::nullopt;
			captured.destinations.push_back({unit->unitId(), destination.hexValue});
		}
	}
	return receipt;
}

bool enabled(const JsonNode & savedRules, const SpellID spell)
{
	const auto * definition = spell.toSpell();
	return definition && definition->getJsonKey() == SPELL_KEY
		&& newHorizonsMagic::rulesActive(savedRules)
		&& savedRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(savedRules, spell);
}

BattleHexArray footprint(const battle::Target & target)
{
	if(target.size() != 3)
		return {};

	BattleHexArray selected;
	for(const auto & destination : target)
	{
		const auto & hex = destination.hexValue;
		if(destination.unitValue || !hex.isAvailable() || selected.contains(hex))
			return {};

		if(!selected.empty())
		{
			const bool touchesSelection = std::ranges::any_of(selected, [&hex](const BattleHex & previous)
			{
				return BattleHex::mutualPosition(previous, hex) != BattleHex::NONE;
			});
			if(!touchesSelection)
				return {};
		}

		selected.insert(hex);
	}

	return selected;
}

const std::vector<BattleHexArray> & connectedTriples()
{
	static const std::vector<BattleHexArray> triples = []
	{
		std::vector<BattleHexArray> result;
		std::set<std::array<int, 3>> seen;

		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex first(index);
			if(!first.isAvailable())
				continue;

			for(const auto & second : BattleHexArray::getNeighbouringTiles(first))
			{
				BattleHexArray thirdCandidates = BattleHexArray::getNeighbouringTiles(first);
				thirdCandidates.insert(BattleHexArray::getNeighbouringTiles(second));

				for(const auto & third : thirdCandidates)
				{
					if(third == first || third == second || !third.isAvailable())
						continue;

					auto key = std::array<int, 3>{first.toInt(), second.toInt(), third.toInt()};
					std::ranges::sort(key);
					if(!seen.insert(key).second)
						continue;

					BattleHexArray triple;
					triple.insert(first);
					triple.insert(second);
					triple.insert(third);
					result.push_back(std::move(triple));
				}
			}
		}
		return result;
	}();

	return triples;
}
}
