/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
namespace
{
newHorizonsHeroes::SpellPointState snapshotMana(const CGHeroInstance & hero)
{
	newHorizonsHeroes::SpellPointState pools;
	pools.restoreSnapshot(hero.getNormalSpellPoints(), hero.getBufferSpellPoints(),
		std::numeric_limits<int32_t>::max());
	return pools;
}
}
std::optional<int64_t> HypotheticBattle::getProjectedManaAvailable(BattleSide side) const
{
	const auto pools = getProjectedMana(side);
	return pools ? std::optional<int64_t>(pools->getTotal()) : std::nullopt;
}
std::optional<newHorizonsHeroes::SpellPointState> HypotheticBattle::getProjectedMana(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;
	if(projectedMana.at(side))
		return projectedMana.at(side)->pools;
	if(const auto * hero = battleGetFightingHero(side))
		return snapshotMana(*hero);
	return std::nullopt;
}
int32_t HypotheticBattle::getTemporaryBufferRemaining(BattleSide side) const
{
	if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER) && projectedMana.at(side))
		return projectedMana.at(side)->temporaryBufferRemaining;
	return BattleProxy::getTemporaryBufferRemaining(side);
}
void HypotheticBattle::projectManaLeech(const newHorizonsManaLeeches::Entitlement & rule,
	int64_t primaryDamage, bool & sequenceSpent)
{
	if(rule.amount <= 0 || primaryDamage <= 0 || sequenceSpent
		|| rule.beneficiary == BattleSide::NONE || rule.victim == BattleSide::NONE)
		return;
	auto & own = projectedMana.at(rule.beneficiary);
	auto & enemy = projectedMana.at(rule.victim);
	const auto * hero = battleGetFightingHero(rule.beneficiary);
	const auto * opposingHero = battleGetFightingHero(rule.victim);
	if(!hero || !opposingHero)
		return;
	if(!own)
		own = ProjectedMana{snapshotMana(*hero), getTemporaryBufferRemaining(rule.beneficiary)};
	if(!enemy)
		enemy = ProjectedMana{snapshotMana(*opposingHero), getTemporaryBufferRemaining(rule.victim)};
	if(!manaLeechApplied)
		manaLeechStartingPools = projectedMana;
	const auto amounts = newHorizonsManaLeeches::transfer(rule, primaryDamage, sequenceSpent,
		enemy->pools.getTotal(), own->pools.getNormal(), hero->manaLimit());
	if(amounts.stolen <= 0)
		return;
	const auto bufferBefore = enemy->pools.getBuffer();
	enemy->pools.spend(amounts.stolen);
	enemy->temporaryBufferRemaining = std::max<int32_t>(0, enemy->temporaryBufferRemaining
		- std::min(enemy->temporaryBufferRemaining, bufferBefore - enemy->pools.getBuffer()));
	if(amounts.restored > 0)
		own->pools.restoreNormal(amounts.restored, hero->manaLimit());
	sequenceSpent = true;
	manaLeechApplied = true;
}

void HypotheticBattle::projectManaLeech(const newHorizonsManaLeeches::Entitlement & rule,
	int64_t primaryDamage, bool * sequenceSpent)
{
	bool singleBlowSpent = false;
	projectManaLeech(rule, primaryDamage, sequenceSpent ? *sequenceSpent : singleBlowSpent);
}

std::optional<HypotheticBattle::ManaLeechReplay> HypotheticBattle::prepareManaLeechReplay(
	const HypotheticBattle & preview) const
{
	ManaLeechReplay receipt;
	if(!preview.manaLeechApplied)
		return receipt;
	// Validate both pools before either resources or selected unit state changes.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto current = getProjectedMana(side);
		const auto & expected = preview.manaLeechStartingPools.at(side);
		if(current.has_value() != expected.has_value()
			|| (current && (current->getNormal() != expected->pools.getNormal()
				|| current->getBuffer() != expected->pools.getBuffer()
				|| getTemporaryBufferRemaining(side) != expected->temporaryBufferRemaining)))
			return std::nullopt;
	}
	receipt.pools = preview.projectedMana;
	receipt.changed = true;
	return receipt;
}
void HypotheticBattle::applyManaLeechReplay(const ManaLeechReplay & receipt) noexcept
{
	if(receipt.changed)
	{
		projectedMana = receipt.pools;
		manaLeechApplied = true;
	}
}
