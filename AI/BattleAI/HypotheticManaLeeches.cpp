/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
std::optional<int64_t> HypotheticBattle::getProjectedManaAvailable(BattleSide side) const
{
	const auto pools = getProjectedMana(side);
	return pools ? std::optional<int64_t>(pools->getTotal()) : std::nullopt;
}
std::optional<newHorizonsHeroes::SpellPointState> HypotheticBattle::getProjectedMana(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;
	const auto & state = forecastSide(side);
	return state && state->mana ? std::optional<newHorizonsHeroes::SpellPointState>(state->mana->pools) : std::nullopt;
}
int32_t HypotheticBattle::getTemporaryBufferRemaining(BattleSide side) const
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return 0;
	const auto & state = forecastSide(side);
	return state && state->mana ? state->mana->temporaryBufferRemaining : 0;
}
void HypotheticBattle::projectManaLeech(const newHorizonsManaLeeches::Entitlement & rule,
	int64_t primaryDamage, bool & sequenceSpent)
{
	if(rule.amount <= 0 || primaryDamage <= 0 || sequenceSpent
		|| rule.beneficiary == BattleSide::NONE || rule.victim == BattleSide::NONE)
		return;
	if(rule.beneficiary == rule.victim) return;
	const auto & ownState = forecastSide(rule.beneficiary);
	const auto & enemyState = forecastSide(rule.victim);
	if(!ownState->hasVisibleHero() || !enemyState->hasVisibleHero() || !ownState->mana || !enemyState->mana) return;
	auto own = *ownState->mana;
	auto enemy = *enemyState->mana;
	const auto amounts = newHorizonsManaLeeches::transfer(rule, primaryDamage, sequenceSpent,
		enemy.pools.getTotal(), own.pools.getNormal(), own.normalCapacity);
	if(amounts.stolen <= 0) return;
	if(!battle::spellPoints::spend(enemy, amounts.stolen)) return;
	if(amounts.restored > 0 && !own.pools.restoreNormal(amounts.restored, own.normalCapacity)) return;
	const auto ownReceipt = forecast::SideTransitions::replaceMana(ownState, *ownState->mana, own, searchBudget().get());
	const auto enemyReceipt = forecast::SideTransitions::replaceMana(enemyState, *enemyState->mana, enemy, searchBudget().get());
	if(!ownReceipt || !enemyReceipt) return;
	if(!manaLeechApplied)
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) manaLeechStartingPools.at(side) = forecastSide(side)->mana;
	// Both exact bases were checked before either side can be published.
	if(!commitForecastSide(rule.beneficiary, *ownReceipt) || !commitForecastSide(rule.victim, *enemyReceipt))
		throw std::logic_error("Stale synchronous Mana Leech receipts");
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
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & before = forecastSide(side)->mana;
		const auto & after = preview.forecastSide(side)->mana;
		if(before.has_value() != after.has_value()) return std::nullopt;
		if(before)
		{
			receipt.sides.at(side) = forecast::SideTransitions::replaceMana(forecastSide(side), *before, *after, searchBudget().get());
			if(!receipt.sides.at(side)) return std::nullopt;
		}
	}
	receipt.changed = true;
	return receipt;
}
void HypotheticBattle::applyManaLeechReplay(const ManaLeechReplay & receipt) noexcept
{
	if(receipt.changed)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(receipt.sides.at(side) && receipt.sides.at(side)->expected() != forecastSide(side)) return;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(receipt.sides.at(side)) commitForecastSide(side, *receipt.sides.at(side));
		manaLeechApplied = true;
	}
}
