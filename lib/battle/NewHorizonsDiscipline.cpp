/*
 * NewHorizonsDiscipline.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsDiscipline.h"

#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/NewHorizonsGlyphsOfFear.h"
#include "../bonuses/BonusList.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
#include "../spells/NewHorizonsMagic.h"

#include <unordered_map>
#include <limits>
#include <algorithm>

namespace
{
BonusSourceID disciplineSkillSource()
{
	static const BonusSourceID source{SecondarySkill{SecondarySkill::decode(
		std::string(newHorizonsDiscipline::SKILL))}};
	return source;
}
}

namespace newHorizonsDiscipline
{
bool hasSteadfast(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(STEADFAST));
}

bool hasEspritDeCorps(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(ESPRIT_DE_CORPS));
}

TConstBonusListPtr espritDeCorpsMoraleBonuses(const CGHeroInstance * hero,
	const IBonusBearer & bearer, bool stackBonuses)
{
	const auto selector = Selector::type()(BonusType::MORALE);
	if(!hasEspritDeCorps(hero))
		return stackBonuses ? bearer.getBonuses(selector) : bearer.getUnstackedBonuses(selector);

	return espritDeCorpsMoraleBonuses(bearer.getUnstackedBonuses(selector), hero->tempOwner, stackBonuses);
}

TConstBonusListPtr espritDeCorpsMoraleBonuses(TConstBonusListPtr unstacked,
	PlayerColor owner, bool stackBonuses)
{
	auto adjusted = std::make_shared<BonusList>();
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> copies;
	for(const auto & bonus : *unstacked)
	{
		const bool hostile = bonus->appliedByEnemy
			|| (bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE && bonus->bonusOwner != owner);
		if(bonus->source == BonusSource::ARMY && bonus->val < 0 && !hostile)
		{
			auto & copy = copies[bonus.get()];
			if(!copy)
			{
				copy = std::make_shared<Bonus>(*bonus);
				++copy->val;
			}
			adjusted->push_back(copy);
		}
		else
			adjusted->push_back(bonus);
	}
	if(stackBonuses)
		adjusted->stackBonuses();
	return adjusted;
}

HeroMoraleContext captureHeroMoraleContext(const CGHeroInstance * hero)
{
	HeroMoraleContext result;
	if(!hero)
		return result;
	result.hasHero = true;
	result.espritDeCorps = hasEspritDeCorps(hero);
	result.owner = hero->tempOwner;
	if(const auto limits = newHorizonsMagic::moraleLimits(hero->getMagicRules()))
		result.limits = *limits;
	else
		result.limits = {-static_cast<int32_t>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size()),
			static_cast<int32_t>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size())};
	for(const auto & bonus : *newHorizonsGlyphsOfFear::moraleBonuses(hero))
		result.glyphs.push_back(*bonus);
	return result;
}

int heroMoraleValue(const HeroMoraleContext & context, TConstBonusListPtr unstacked)
{
	if(!context.hasHero)
		return 0;
	const auto [minimum, maximum] = context.limits;
	// AFactionMember precedence: maximum wins even over immunity; immunity wins
	// over floors. Only flags actually admitted by the hero graph participate.
	if(unstacked->getFirst(Selector::type()(BonusType::MAX_MORALE)))
		return maximum;
	const auto unaffected = Selector::type()(BonusType::NON_LIVING).Or(Selector::type()(BonusType::MECHANICAL))
		.Or(Selector::type()(BonusType::UNDEAD)).Or(Selector::type()(BonusType::SIEGE_WEAPON))
		.Or(Selector::type()(BonusType::NO_MORALE));
	if(unstacked->getFirst(unaffected))
		return 0;
	auto ordinary = std::make_shared<BonusList>();
	unstacked->getBonuses(*ordinary, Selector::type()(BonusType::MORALE));
	TConstBonusListPtr morale;
	if(context.espritDeCorps)
		morale = espritDeCorpsMoraleBonuses(ordinary, context.owner);
	else
	{
		ordinary->stackBonuses();
		morale = ordinary;
	}
	// Glyphs are appended AFTER ordinary stacking by CGHeroInstance.
	BonusList effective;
	morale->getAllBonuses(effective);
	for(const auto & bonus : context.glyphs)
		effective.push_back(std::make_shared<Bonus>(bonus));
	int result = std::clamp(effective.totalValue(), minimum, maximum);
	BonusList floors;
	unstacked->getBonuses(floors, Selector::type()(BonusType::MINIMUM_MORALE));
	if(!floors.empty())
	{
		floors.stackBonuses();
		result = std::max(result, floors.totalValue());
	}
	return std::clamp(result, minimum, maximum);
}

int32_t espritDeCorpsMoraleAdjustment(const CGHeroInstance * hero, const IBonusBearer & bearer)
{
	if(!hasEspritDeCorps(hero))
		return 0;
	const auto adjusted = espritDeCorpsMoraleBonuses(hero, bearer);
	const auto original = bearer.getBonusesOfType(BonusType::MORALE);
	const int64_t difference = static_cast<int64_t>(adjusted->totalValue()) - original->totalValue();
	return static_cast<int32_t>(std::clamp<int64_t>(difference,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

bool hasHoldFast(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(HOLD_FAST));
}

bool hasFearless(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(FEARLESS));
}

bool hasVeteranCohesion(const CGHeroInstance * hero)
{
	return hero && hero->getPerkSkillRank(std::string(SKILL)) >= MasteryLevel::ADVANCED
		&& hero->hasActivePerk(std::string(SKILL), std::string(VETERAN_COHESION));
}

TConstBonusListPtr veteranCohesionBonuses(TConstBonusListPtr original, const CSelector & selector, bool earned)
{
	const auto isPersonalModifier = [](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::MORALE && bonus->source == BonusSource::SECONDARY_SKILL
			&& bonus->duration == BonusDuration::ONE_BATTLE
			&& bonus->val == 2 && bonus->valType == BonusValueType::ADDITIVE_VALUE
			&& bonus->stacking == VETERAN_COHESION && !bonus->parameters && !bonus->hasStatusMetadata()
			&& bonus->sid == disciplineSkillSource();
	};
	const bool inherited = std::any_of(original->begin(), original->end(), [&](const auto & bonus)
	{ return isPersonalModifier(bonus.get()); });
	if(!inherited && !earned)
		return original;
	Bonus modifier(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::SECONDARY_SKILL,
		2, disciplineSkillSource());
	modifier.stacking = VETERAN_COHESION;
	modifier.description.appendRawString("Veteran Cohesion: +2 Morale for the rest of combat");
	if(!inherited && (!earned || !selector(&modifier)))
		return original;
	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : *original)
		if(!isPersonalModifier(bonus.get()))
			result->push_back(bonus);
	if(earned && selector(&modifier))
		result->push_back(std::make_shared<Bonus>(std::move(modifier)));
	return result;
}

Bonus holdFastMoraleFloorBonus()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::MINIMUM_MORALE,
		BonusSource::SECONDARY_SKILL, 0, disciplineSkillSource());
	bonus.valType = BonusValueType::INDEPENDENT_MAX;
	bonus.description.appendRawString("Hold Fast");
	return bonus;
}

bool isHoldFastMoraleFloorBonus(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::MINIMUM_MORALE && bonus->val == 0
		&& bonus->valType == BonusValueType::INDEPENDENT_MAX
		&& bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == disciplineSkillSource();
}

CSelector holdFastMoraleFloorBonusSelector()
{
	return CSelector([](const Bonus * bonus)
	{
		return isHoldFastMoraleFloorBonus(bonus);
	});
}
}
