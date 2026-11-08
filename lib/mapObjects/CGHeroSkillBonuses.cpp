/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../GameLibrary.h"
#include "../CSkillHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include "../battle/NewHorizonsSylvanTactics.h"
void CGHeroInstance::rebuildPreparedNativeRules()
{
	auto perks = perkState.prepareNativeRules();
	auto creatures = nativeRules::preparePilotCreature(primaryGrowthRules, getHeroTypeID());
	preparedPilotPerks = std::move(perks);
	preparedCreatureMechanics = std::move(creatures);
}
void CGHeroInstance::recreateSecondarySkillsBonuses()
{
	perkState.validate();
	if(perkRulesCaptured && newHorizonsHeroes::usesPerkRules(perkState.rules) && !preparedPilotPerks)
		throw std::runtime_error("Captured hero rank definitions were not prepared");
	auto secondarySkillsBonuses = getBonusesFrom(BonusSource::SECONDARY_SKILL);
	for(const auto & bonus : *secondarySkillsBonuses)
		removeBonus(bonus);

	for(const auto & skill_info : secSkills)
		if(skill_info.second > 0)
			updateSkillBonus(SecondarySkill(skill_info.first), skill_info.second);
}

void CGHeroInstance::updateSkillBonus(const SecondarySkill & which, int val)
{
	if(perkRulesCaptured && newHorizonsHeroes::usesPerkRules(perkState.rules) && !preparedPilotPerks)
		throw std::runtime_error("Captured hero rank definitions were not prepared");
	std::vector<std::shared_ptr<Bonus>> incoming;

	if(val > 0)
	{
		const auto newHorizonsOffenseSkill = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), SecondarySkill::entityType(), "new-horizons:offense", true);
		const int offenseSpecialtyPercent = newHorizonsOffenseSkill && which == SecondarySkill(*newHorizonsOffenseSkill)
			? getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)) : 0;
		const auto newHorizonsEstatesSkill = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), SecondarySkill::entityType(), "new-horizons:estates", true);
		const int estatesSpecialtyPercent = newHorizonsEstatesSkill && which == SecondarySkill(*newHorizonsEstatesSkill)
			? getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ESTATES)) : 0;
		const auto authored = preparedPilotPerks ? preparedPilotPerks->rank(which, val) : std::nullopt;
		const auto & source = authored ? *authored : (*LIBRARY->skillh)[which]->at(val).effects;
		auto skillBonus = newHorizonsSylvanTactics::rankBonuses(*this, which, val, source);
		for(const auto& b : skillBonus)
		{
			auto bonus = std::make_shared<Bonus>(*b);
			if(offenseSpecialtyPercent > 0
				&& bonus->type == BonusType::PERCENTAGE_DAMAGE_BOOST
				&& bonus->subtype == BonusCustomSubtype::damageTypeMelee
				&& bonus->valType == BonusValueType::BASE_NUMBER
				&& bonus->source == BonusSource::SECONDARY_SKILL
				&& bonus->sid == BonusSourceID(which))
				bonus->val = bonus->val * (100 + offenseSpecialtyPercent) / 100;
			if(estatesSpecialtyPercent > 0
				&& bonus->type == BonusType::GENERATE_RESOURCE
				&& bonus->subtype == BonusSubtypeID(GameResID(EGameResID::GOLD))
				&& bonus->valType == BonusValueType::BASE_NUMBER
				&& bonus->source == BonusSource::SECONDARY_SKILL
				&& bonus->sid == BonusSourceID(which))
				bonus->val = bonus->val * (100 + estatesSpecialtyPercent) / 100;
			incoming.push_back(std::move(bonus));
		}
	}

	const auto luckSkillIndex = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), SecondarySkill::entityType(), "new-horizons:luck", true);
	if(luckSkillIndex && which == SecondarySkill(*luckSkillIndex)
		&& hasActivePerk("new-horizons:luck", "new-horizons:luck.fortuneSFavor"))
	{
		incoming.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE, BonusSource::SECONDARY_SKILL, 25, BonusSourceID(which)));
	}
	// Prepare every source-dependent payload before removing the old rank.
	removeBonuses(Selector::source(BonusSource::SECONDARY_SKILL, BonusSourceID(which)));
	for(auto & bonus : incoming) addNewBonus(bonus);
}

