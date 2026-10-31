/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsInfernoStrikeDebuffs.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <array>
#include <stdexcept>
namespace newHorizonsInfernoStrikeDebuffs
{
namespace
{
struct StrikeDebuff
{
	const char * hero;
	const char * creature;
	const char * identity;
	const char * status;
	const char * name;
	bool shooting;
	BonusType type;
	BonusSubtypeID subtype;
	BonusValueType valueType;
};
const std::array<StrikeDebuff, 2> debuffs{{
	{"core:calh", "core:gog", "new-horizons:ordinary-creature-specialty:core:calh:scorchingImpact",
		"scorchingImpact", "Scorching Impact", true, BonusType::GENERAL_ATTACK_REDUCTION,
		BonusSubtypeID(), BonusValueType::BASE_NUMBER},
	{"core:nymus", "core:pitFiend", "new-horizons:ordinary-creature-specialty:core:nymus:cruelOverseer",
		"cruelOverseer", "Cruel Overseer", false, BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE}
}};
}
bool isEffect(const Bonus * bonus)
{
	if(!bonus)
		return false;
	for(const auto & row : debuffs)
	{
		if(bonus->stacking != row.identity)
			continue;
		const bool valueValid = row.shooting ? bonus->val > 0 && bonus->val <= 100
			: bonus->val < 0 && bonus->val >= -100;
		if(bonus->source != BonusSource::HERO_SPECIAL
			|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode(row.hero)))
			|| bonus->type != row.type || bonus->subtype != row.subtype
			|| bonus->duration != BonusDuration::STACK_ACTIVATION
			|| bonus->valType != row.valueType || !valueValid
			|| bonus->targetSourceType != BonusSource::OTHER || bonus->turnsRemain != 0
			|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy
			|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
			|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
			|| bonus->statusIdentity != row.status
			|| bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE}
			|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
			|| !bonus->customIconPath.empty())
			throw std::runtime_error("Invalid Inferno strike specialty provenance");
		return true;
	}
	return false;
}
std::shared_ptr<Bonus> primaryHit(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0)
		return nullptr;
	for(const auto & row : debuffs)
	{
		if(row.shooting != attack.shooting || (!row.shooting && !attack.physicalDamage))
			continue;
		const int amount = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, row.hero);
		if(amount <= 0)
			continue;
		const auto family = CreatureID(CreatureID::decode(row.creature));
		const auto creature = attack.attacker->creatureId();
		if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
			continue;
		auto effect = std::make_shared<Bonus>(BonusDuration::STACK_ACTIVATION, row.type,
			BonusSource::HERO_SPECIAL, row.shooting ? amount : -amount,
			BonusSourceID(HeroTypeID(HeroTypeID::decode(row.hero))), row.subtype, row.valueType);
		effect->stacking = row.identity;
		effect->statusIdentity = row.status;
		effect->statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
		effect->description.appendRawString(std::string(row.name) + ": "
			+ (row.shooting ? "ordinary attack damage -" : "Defense -") + std::to_string(amount)
			+ (row.shooting ? "%" : "")
			+ " through the next accepted Creature Action (including Wait). Cure and Dispel do not remove this effect.");
		return effect;
	}
	return nullptr;
}
}
