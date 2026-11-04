/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsMagic.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../ResourceSet.h"
#include <algorithm>
namespace newHorizonsWisdom
{
int32_t familiarityCost(int32_t ordinary, uint32_t prior)
{
	if(ordinary <= 0)
		return ordinary;
	const int64_t percent = 100 - 5 * std::min<uint32_t>(20, prior);
	return static_cast<int32_t>(std::max<int64_t>(1, (static_cast<int64_t>(ordinary) * percent + 99) / 100));
}
int32_t effectivenessBonusPercent(const CGHeroInstance * hero, const IBattleInfo & battle, BattleSide side, SpellID spell)
{
	if(!hero || !spell.hasValue() || battle.getSideHero(side) != hero || !newHorizonsMagic::rulesActive(battle.getMagicRules()))
		return 0;
	const auto & state = battle.getWisdomCombatState(side);
	int32_t bonus = 0;
	if(hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneMomentum"))
		bonus += state.momentumBonusPercent(battle.getRound());
	if(hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneInsight") && state.priorCasts(spell) == 0)
		bonus += 25;
	if(archmagePenetrationPercent(hero, battle.getMagicRules(), spell))
		bonus += 20;
	return bonus;
}
int32_t archmagePenetrationPercent(const CGHeroInstance * hero, const JsonNode & rules, SpellID spell)
{
	const int level = spell.hasValue() ? newHorizonsMagic::spellLevel(rules, spell) : 0;
	return hero && newHorizonsMagic::rulesActive(rules) && (level == 4 || level == 5)
		&& hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.archmage") ? 10 : 0;
}
ResourceSet adventureSpellPurchaseCost(const JsonNode & rules, SpellID spell, const CGTownInstance & town)
{
	auto result = newHorizonsMagic::adventureSpellUnlockCost(rules, spell);
	const auto * hero = town.getVisitingHero();
	if(hero && hero->getOwner() == town.getOwner()
		&& hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.spellScholar"))
		for(auto & component : result)
			component = (component * 75 + 99) / 100;
	return result;
}
int32_t conservationRecovery(const CGHeroInstance & hero, int64_t spent)
{
	if(spent <= 0 || !newHorizonsMagic::spellPointRulesActive(hero.getMagicRules())
		|| !hero.hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.manaConservation"))
		return 0;
	return static_cast<int32_t>(std::min<int64_t>(std::min<int64_t>(20, spent / 5),
		std::max<int64_t>(0, static_cast<int64_t>(hero.manaLimit()) - hero.getNormalSpellPoints())));
}
int32_t fastingRecovery(const CGHeroInstance & hero, bool victorious, bool completedHeroCast)
{
	if(!victorious || completedHeroCast || !newHorizonsMagic::spellPointRulesActive(hero.getMagicRules())
		|| !hero.hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.manaFasting"))
		return 0;
	return static_cast<int32_t>(std::min<int64_t>(hero.manaLimit() / 10,
		std::max<int64_t>(0, static_cast<int64_t>(hero.manaLimit()) - hero.getNormalSpellPoints())));
}
}
