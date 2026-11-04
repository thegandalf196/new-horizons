/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsMetamagicMarks.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>

namespace newHorizonsMagic
{
int metamagicRank(const CGHeroInstance * hero)
{
	if(!hero || newHorizonsMetamagic::usesPoints(hero->getPerkState().rules)
		|| newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules))
		return 0;
	const int learnedRank = hero->getPerkSkillRank(std::string(METAMAGIC_SKILL));
	if(!rulesActive(hero->getMagicRules()))
		return std::clamp(std::max(learnedRank, hero->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT)), 0, 3);
	return std::clamp(learnedRank, 0, 3);
}

int metamagicCapacity(const CGHeroInstance * hero)
{
	if(hero && (newHorizonsMetamagic::usesPoints(hero->getPerkState().rules)
		|| newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules)))
		return 0; // Passive Metamagic never buys or reserves additional Spell Actions.
	const int rank = metamagicRank(hero);
	if(!hero || !rulesActive(hero->getMagicRules()))
		return rank;
	if(rank == 0)
		return 0;
	// Skill bonuses already supply1/2/3; Halon's additive specialty supplies
	// one more. Capacity is not a synthetic fourth mastery or bonus-only Skill.
	return std::clamp(std::max(rank, hero->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT)), rank, rank + 1);
}

bool hasMetamagicPerk(const CGHeroInstance * hero, std::string_view perkId)
{
	return hero && !newHorizonsMetamagic::usesPoints(hero->getPerkState().rules)
		&& !newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules)
		&& hero->hasActivePerk(std::string(METAMAGIC_SKILL), std::string(perkId));
}

}
