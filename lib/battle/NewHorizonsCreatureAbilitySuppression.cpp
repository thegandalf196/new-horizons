/*
 * NewHorizonsCreatureAbilitySuppression.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"

namespace newHorizonsCreatureAbilitySuppression
{
namespace
{
constexpr int32_t BASE_SUPPRESSION_LEVEL = 1;
constexpr int32_t MINDBREAKER_SUPPRESSION_LEVEL = 2;

bool isIntrinsicAbilityOf(const battle::Unit & unit, const Bonus & bonus)
{
	if(bonus.source == BonusSource::STACK_EXPERIENCE)
		// Tier-wide CREXPBON entries intentionally have no creature SID; the
		// per-creature experience entries must still match the effective form.
		return !bonus.sid.hasValue() || bonus.sid == BonusSourceID(unit.creatureId());

	if(bonus.source != BonusSource::CREATURE_ABILITY)
		return false;

	// This source ID identifies the creature whose native ability is inherited.
	// Do not suppress an effect another creature placed on this unit merely
	// because its source type is CREATURE_ABILITY.
	return bonus.sid == BonusSourceID(unit.creatureId());
}

bool isBaseSuppressedType(const BonusType type)
{
	switch(type)
	{
		// Activated creature abilities and commands.
		case BonusType::ADJACENT_SPELLCASTER:
		case BonusType::CASTS:
		case BonusType::ENCHANTER:
		case BonusType::HEALER:
		case BonusType::RANDOM_SPELLCASTER:
		case BonusType::SPELLCASTER:

		// Special attack modes and combat-triggered abilities. The ordinary
		// single melee strike and its normal retaliation remain available.
		case BonusType::ADDITIONAL_ATTACK:
		case BonusType::ATTACKS_ALL_ADJACENT:
		case BonusType::CATAPULT:
		case BonusType::COMBAT_EVENT_TRIGGER:
		case BonusType::FEROCITY:
		case BonusType::FEARFUL:
		case BonusType::FIRST_STRIKE:
		case BonusType::HP_REGENERATION:
		case BonusType::LONG_WEAPON:
		case BonusType::MAGIC_MIRROR:
		case BonusType::MANA_CHANNELING:
		case BonusType::MANA_DRAIN:
		case BonusType::MULTIHEX_ENEMY_ATTACK:
		case BonusType::MULTIHEX_UNIT_ATTACK:
		case BonusType::ON_COMBAT_EVENT:
		case BonusType::PRISM_HEX_ATTACK_BREATH:
		case BonusType::RANGED_RETALIATION:
		case BonusType::REBIRTH:
		case BonusType::RETURN_AFTER_STRIKE:
		case BonusType::SHOOTS_ALL_ADJACENT:
		case BonusType::SPELL_AFTER_ATTACK:
		case BonusType::SPELL_BEFORE_ATTACK:
		case BonusType::SPELL_LIKE_ATTACK:
		case BonusType::THREE_HEADED_ATTACK:
		case BonusType::TWO_HEX_ATTACK_BREATH:
		case BonusType::WIDE_BREATH:
			return true;
		default:
			return false;
	}
}

bool isMindbreakerSuppressedType(const BonusType type)
{
	switch(type)
	{
		// These are passive offensive advantages, rather than extra commands or
		// triggered effects. Unknown bonus kinds intentionally remain untouched.
		case BonusType::ALWAYS_MAXIMUM_DAMAGE:
		case BonusType::ADDITIONAL_RETALIATION:
		case BonusType::BLOCKS_RETALIATION:
		case BonusType::BLOCKS_RANGED_RETALIATION:
		case BonusType::CHANGES_SPELL_COST_FOR_ENEMY:
		case BonusType::DOUBLE_DAMAGE_CHANCE:
		case BonusType::ENEMY_DEFENCE_REDUCTION:
		case BonusType::HATE:
		case BonusType::HATES_TRAIT:
		case BonusType::JOUSTING:
		case BonusType::NO_DISTANCE_PENALTY:
		case BonusType::NO_MELEE_PENALTY:
		case BonusType::NO_WALL_PENALTY:
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
		case BonusType::REVENGE:
		case BonusType::UNLIMITED_RETALIATIONS:
			return true;
		default:
			return false;
	}
}
}

int32_t suppressionLevel(const battle::Unit & unit)
{
	static const CSelector markerSelector = Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION);
	static const std::string markerCacheKey = "newHorizonsCreatureAbilitySuppression.marker";
	const auto markers = unit.getBonusesBeforeCreatureAbilitySuppression(markerSelector, markerCacheKey, false);
	if(!markers)
		return 0;

	int32_t level = 0;
	for(const auto & marker : *markers)
	{
		if(!marker || marker->type != BonusType::CREATURE_ABILITY_SUPPRESSION
			|| marker->source != BonusSource::SPELL_EFFECT
			|| marker->sid != BonusSourceID(SpellID(SpellID::FORGETFULNESS)))
			continue;

		if(marker->val == BASE_SUPPRESSION_LEVEL || marker->val == MINDBREAKER_SUPPRESSION_LEVEL)
			level = std::max(level, marker->val);
	}

	return level;
}

bool isSuppressed(const Bonus & bonus, const int32_t level)
{
	if(level < BASE_SUPPRESSION_LEVEL || level > MINDBREAKER_SUPPRESSION_LEVEL)
		return false;

	return isBaseSuppressedType(bonus.type)
		|| (level >= MINDBREAKER_SUPPRESSION_LEVEL && isMindbreakerSuppressedType(bonus.type));
}

TConstBonusListPtr filterBonuses(const battle::Unit & unit, const TConstBonusListPtr & bonuses,
	const int32_t level, const bool stack)
{
	if(!bonuses)
		return bonuses;

	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : *bonuses)
	{
		if(bonus && !(level >= BASE_SUPPRESSION_LEVEL
			&& isIntrinsicAbilityOf(unit, *bonus) && isSuppressed(*bonus, level)))
			result->push_back(bonus);
	}

	if(stack)
		result->stackBonuses();
	return result;
}
}
