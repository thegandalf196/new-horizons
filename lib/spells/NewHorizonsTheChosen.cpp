/*
 * NewHorizonsTheChosen.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsTheChosen.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusSelector.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsChosenSpecialties.h"

namespace newHorizonsTheChosen
{
namespace
{
int32_t integer(const JsonNode & node, int32_t minimum, int32_t maximum)
{
	if(node.getType() != JsonNode::JsonType::DATA_INTEGER
		|| node.Integer() < minimum || node.Integer() > maximum)
		throw std::runtime_error("Unsupported The Chosen integer");
	return static_cast<int32_t>(node.Integer());
}
}

bool canTransferTo(const CBattleInfoCallback & callback, uint32_t originalSourceUnitId,
	const battle::Unit & recipient)
{
	const SpellID chosen(SpellID::decode(std::string(SPELL_KEY)));
	const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(chosen));
	const auto existing = callback.battleGetUnitsIf([&](const battle::Unit * unit)
	{
		return unit->unitSide() == recipient.unitSide() && unit->unitId() != recipient.unitId()
			&& unit->unitId() != originalSourceUnitId && unit->hasBonus(source);
	});
	return existing.empty();
}

std::optional<Rules> rulesFor(const JsonNode & savedRules)
{
	const auto & row = savedRules["spells"][std::string(SPELL_KEY)];
	if(!row.isStruct() || !row.Struct().contains("theChosen"))
		return std::nullopt;
	if(savedRules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| savedRules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		throw std::runtime_error("The Chosen requires saved v3 magic rules");
	const auto & profile = row["theChosen"];
	if(!profile.isStruct() || profile.Struct().size() != 5)
		throw std::runtime_error("Unsupported The Chosen profile");
	Rules result{
		integer(profile["baseDuration"], 1, 1000),
		integer(profile["maximumDuration"], 1, 1000),
		integer(profile["spellPowerPerRound"], 1, 1000000),
		integer(profile["joustingPercent"], 1, 10000),
		integer(profile["exaltedChampionDurationBonus"], 0, 1000)};
	if(result.baseDuration > result.maximumDuration)
		throw std::runtime_error("The Chosen base exceeds its nominal duration cap");
	return result;
}

int32_t exaltedChampionDurationBonus(const Rules & rules, const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.crusader")
		? rules.exaltedChampionDurationBonus : 0;
}

void validate(const JsonNode & savedRules)
{
	(void)rulesFor(savedRules);
}

void validateSpellRow(const JsonNode & savedRules, std::string_view identity, const JsonNode & row)
{
	if(identity == SPELL_KEY)
		validateSerialization(savedRules, true);
	else if(row.Struct().contains("theChosen"))
		throw std::runtime_error("The Chosen profile requires its canonical spell row");
}

void validateSerialization(const JsonNode & savedRules, bool supported)
{
	const auto & row = savedRules["spells"][std::string(SPELL_KEY)];
	if(!row.isStruct())
		return;
	if(!supported)
		throw std::runtime_error("The Chosen requires the revised Orders save format");
	if(!rulesFor(savedRules))
		throw std::runtime_error("The Chosen requires its captured profile");
}

int32_t finishDuration(const Rules & rules, const CGHeroInstance * hero, SpellID spell, int32_t adjustedNominal)
{
	const int specialty = hero ? newHorizonsHeroes::chosenSpecialtyDurationBonus(
		hero->getPrimaryGrowthRules(), hero->getHeroType()->getJsonKey(), spell) : 0;
	const int64_t turns = static_cast<int64_t>(adjustedNominal)
		+ exaltedChampionDurationBonus(rules, hero) + specialty;
	return static_cast<int32_t>(std::clamp<int64_t>(turns, 0, std::numeric_limits<int32_t>::max()));
}

int32_t nominalDuration(const Rules & rules, int32_t rawSpellPower, int32_t schoolPercent)
{
	const int64_t powerTerm = static_cast<int64_t>(std::max(0, rawSpellPower))
		* std::max(0, schoolPercent) / (100LL * rules.spellPowerPerRound);
	return static_cast<int32_t>(std::min<int64_t>(rules.maximumDuration, rules.baseDuration + powerTerm));
}
}

namespace spells
{
int32_t Mechanics::getTheChosenNominalDuration() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	const auto rules = state ? newHorizonsTheChosen::rulesFor(state->getMagicRules()) : std::nullopt;
	if(!rules)
		return 0;
	const auto * hero = getHeroCaster();
	const int32_t rawPower = hero ? hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) : getEffectPower();
	return newHorizonsTheChosen::nominalDuration(*rules, rawPower, getSchoolRankPowerCoefficientPercent());
}

int32_t Mechanics::getTheChosenDuration() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	const auto rules = state ? newHorizonsTheChosen::rulesFor(state->getMagicRules()) : std::nullopt;
	if(!rules)
		return 0;
	return newHorizonsTheChosen::finishDuration(*rules, getHeroCaster(), getSpellId(),
		adjustEffectDuration(getTheChosenNominalDuration()));
}

int32_t Mechanics::getTheChosenJoustingPercent() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	const auto rules = state ? newHorizonsTheChosen::rulesFor(state->getMagicRules()) : std::nullopt;
	return rules ? rules->joustingPercent : 0;
}
}
