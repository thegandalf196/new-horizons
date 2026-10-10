/*
 * NewHorizonsPurify.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPurify.h"
#include "../battle/PhysicalAffliction.h"

#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"

#include "../CStack.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"

namespace
{
constexpr uint16_t temporaryBattleDurationMask = BonusDuration::ONE_BATTLE
	| BonusDuration::N_TURNS
	| BonusDuration::UNTIL_BEING_ATTACKED
	| BonusDuration::UNTIL_ATTACK
	| BonusDuration::STACK_GETS_TURN
	| BonusDuration::COMMANDER_KILLED
	| BonusDuration::UNTIL_OWN_ATTACK
	| BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE
	| BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE
	| BonusDuration::STACK_ACTIVATION;

bool isTemporaryCombatBonus(const Bonus & bonus)
{
	return (bonus.duration & temporaryBattleDurationMask) != 0;
}

bool unitTouchesArea(const battle::Unit & unit, const BattleHex & center)
{
	for(const auto & occupiedHex : unit.getHexes())
	{
		if(BattleHex::getDistance(center, occupiedHex) <= newHorizonsPurify::AREA_RADIUS)
			return true;
	}
	return false;
}
}

namespace newHorizonsPurify
{
SpellID spellID()
{
	static const SpellID id(SpellID::decode(std::string(SPELL_ID)));
	return id;
}

SpellID physicalPoisonChoiceID()
{
	return SpellID::NONE;
}

bool enabled(const JsonNode & magicRules, const SpellID spell)
{
	if(spell != spellID() || !newHorizonsMagic::rulesActive(magicRules)
		|| magicRules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spell))
		return false;

	const auto * definition = spell.toSpell();
	if(!definition || definition->getJsonKey() != SPELL_ID
		|| newHorizonsMagic::spellLevel(magicRules, spell) != 4)
		return false;

	const SpellSchool light(SpellSchool::decode("new-horizons:light"));
	const auto schools = newHorizonsMagic::spellSchools(magicRules, spell);
	if(!vstd::contains(schools, light))
		return false;

	for(int mastery = 0; mastery <= 3; ++mastery)
	{
		if(newHorizonsMagic::spellCost(magicRules, spell, mastery) != 15)
			return false;
	}

	return true;
}

bool hasPurifierPerk(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(LIGHT_MAGIC_SKILL), std::string(PURIFIER_PERK));
}

int maximumSpellEffectChoices(const int32_t spellPower)
{
	if(spellPower < 0)
		throw std::invalid_argument("Purify Spell Power cannot be negative");

	return std::min(MAX_SPELL_EFFECT_CHOICES,
		1 + spellPower / SPELL_POWER_FOR_SECOND_CHOICE);
}

std::vector<SpellID> eligibleSpellEffectGroups(const JsonNode & magicRules, const battle::Unit * unit)
{
	std::vector<SpellID> result;
	if(!unit || !newHorizonsMagic::rulesActive(magicRules)
		|| magicRules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return result;

	std::set<SpellID> eligibleSources;
	const auto bonuses = unit->getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT));
	if(!bonuses)
		return result;

	for(const auto & bonus : *bonuses)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT
			|| !bonus->sid.as<SpellID>().hasValue() || !isTemporaryCombatBonus(*bonus))
			continue;

		const SpellID sourceSpell = bonus->sid.as<SpellID>();
		const auto * definition = sourceSpell.toSpell();
		if(!definition || !definition->isNegative() || definition->isPersistent() || definition->isAdventure())
			continue;

		eligibleSources.insert(sourceSpell);
	}

	result.assign(eligibleSources.begin(), eligibleSources.end());
	std::sort(result.begin(), result.end(), [](const SpellID & lhs, const SpellID & rhs)
	{
		return lhs.getNum() < rhs.getNum();
	});
	return result;
}

std::vector<Bonus> spellEffectGroupBonuses(const battle::Unit * unit, const SpellID sourceSpell)
{
	std::vector<Bonus> result;
	if(!unit || !sourceSpell.hasValue())
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(sourceSpell)));
	if(!bonuses)
		return result;

	result.reserve(bonuses->size());
	for(const auto & bonus : *bonuses)
		if(bonus)
			result.emplace_back(*bonus);
	return result;
}

bool isMagicalSpellEffectGroup(const battle::Unit * unit, const SpellID sourceSpell)
{
	if(!unit || spellEffectGroupBonuses(unit, sourceSpell).empty())
		return false;

	const auto afflictions = physicalAfflictions::enumerate(*unit);
	return std::none_of(afflictions.begin(), afflictions.end(), [&](const physicalAfflictions::Affliction & affliction)
	{
		return !affliction.storedPoison && affliction.source == BonusSource::SPELL_EFFECT
			&& affliction.sourceID.as<SpellID>() == sourceSpell;
	});
}

bool hasPhysicalPoison(const battle::Unit * unit)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	return state && state->physicalPoisonBaseDamage > 0
		&& state->physicalPoisonActivationsRemaining > 0;
}

bool clearPhysicalPoison(battle::CUnitState * state)
{
	if(!state || (state->physicalPoisonBaseDamage <= 0 && state->physicalPoisonActivationsRemaining <= 0
		&& state->physicalPoisonSourceStackId < 0))
		return false;

	state->physicalPoisonBaseDamage = 0;
	state->physicalPoisonActivationsRemaining = 0;
	state->physicalPoisonSourceStackId = -1;
	return true;
}

std::vector<EligibleStack> eligibleStacks(const CBattleInfoCallback & battle, const BattleSide casterSide,
	const BattleHex & center, const int32_t spellPower, const bool purifierPerk)
{
	std::vector<EligibleStack> result;
	const auto * state = battle.getBattle();
	if(!state || !center.isAvailable()
		|| !enabled(state->getMagicRules(), spellID())
		|| (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return result;

	const int cap = maximumSpellEffectChoices(spellPower);
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost()
			|| battle.playerToSide(battle.battleGetOwner(unit)) != casterSide
			|| newHorizonsMagic::isProtectedAreaCenter(battle, state->getSideHero(casterSide),
				spellID(), *unit, center, casterSide)
			|| !unitTouchesArea(*unit, center))
			continue;

		EligibleStack stack;
		stack.unitId = static_cast<int32_t>(unit->unitId());
		stack.spellEffectGroups = eligibleSpellEffectGroups(state->getMagicRules(), unit);
		stack.physicalPoison = hasPhysicalPoison(unit);
		stack.physicalPoisonAutomaticallyCleared = purifierPerk && stack.physicalPoison;
		stack.maximumSpellEffectChoices = cap;

		if(stack.spellEffectGroups.empty() && !stack.physicalPoison)
			continue;
		result.push_back(std::move(stack));
	}

	std::sort(result.begin(), result.end(), [](const EligibleStack & lhs, const EligibleStack & rhs)
	{
		return lhs.unitId < rhs.unitId;
	});
	return result;
}
}
