/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include <stdexcept>
namespace forecast
{
bool NativeAdapter::ordinaryOrdersSupported(const HypotheticBattle & branch, BattleSide side)
{
	const auto lease = branch.preparedHeroOrderStates(side);
	if(!lease) return false; // Unknown is never certified as an empty collection.
	if(lease->empty()) return true;
	if(!heroCommands::isCanonicalRules(branch.getHeroCommandRules())) return false;
	SideInBattle::validateOrderCollection(*lease);
	for(const auto & order : *lease)
	{
		if(!heroCommands::supportedByRules(branch.getHeroCommandRules(), order.command)
			|| order.passiveDivineMandate) return false;
		switch(order.command)
		{
		case HeroCommand::CHARGE:
		case HeroCommand::RIPOSTE:
		case HeroCommand::BRACE:
		case HeroCommand::FOCUS_FIRE:
		case HeroCommand::HOLD_THE_LINE:
		case HeroCommand::FEINT:
			break;
		default: return false; // Special activation/status owners require their receipts.
		}
	}
	return true;
}

void NativeAdapter::refreshOrderDerived(HypotheticBattle & prospective, State & draft)
{
	for(const auto id : draft.ids())
	{
		const auto unit = prospective.getForUpdate(id);
		auto next = *draft.unit(id);
		const auto warfare = prospective.unitWarfareStatBonus(unit.get());
		const auto retal = unit->counterAttacks.observeWithoutLatch();
		const Counter retaliation{retal.available, retal.total, retal.limited};
		const Counter shots{unit->shots.available(), unit->shots.total(), unit->shots.isLimited()};
		const bool shotsChanged = next.activation.shots.available != shots.available
			|| next.activation.shots.total != shots.total || next.activation.shots.limited != shots.limited;
		const bool statsChanged = next.warfareStat != warfare;
		const bool counterChanged = next.activation.retaliation.total != retaliation.total
			|| next.activation.retaliation.available != retaliation.available
			|| next.activation.retaliation.limited != retaliation.limited;
		if(!statsChanged && !counterChanged && !shotsChanged) continue;
		if(statsChanged)
		{
			for(size_t mode = 0; mode < 2; ++mode)
			{
				auto facts = nativeDamage::prepareUnit(*unit, mode != 0);
				const auto & leadership = draft.definition(id)->leadership;
				if(facts && leadership.known) facts->leadershipEfficiency = leadership.active
					? newHorizonsHeroes::squareRootLeadershipEfficiency(next.health.getCount(), leadership.capacity) : 1.0;
				next.numeric[mode] = facts ? std::make_shared<const nativeDamage::PreparedUnit>(*facts) : nullptr;
			}
			if(next.currentModifiers && next.numeric[0] && next.numeric[1])
			{
				// The raw records are unchanged; a membership-only Warfare update
				// reuses their owner but must replace its scalar reduction frame.
				const std::array<nativeDamage::PreparedUnit, 2> base{*next.numeric[0], *next.numeric[1]};
				auto frame = next.currentModifiers->refresh(next.health.getCount(), bonusDependencies::Change::None,
					base, warfare);
				if(!frame) throw std::logic_error("Admitted Order modifier reduction rejected");
				next.modifierNumbers = std::make_shared<const bonusDependencies::NumericFrame>(std::move(*frame));
				for(size_t mode = 0; mode < 2; ++mode)
					next.numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(next.modifierNumbers->prepared[mode]);
			}
			draft.counters.add(&Counters::derivedRefreshes);
		}
		next.warfareStat = warfare;
		next.activation.retaliation = retaliation;
		next.activation.shots = shots;
		draft.commit(id, std::move(next), Damage | Queue, false);
	}
	// Peer membership is an owned environment revision, even for recipients
	// whose numerical facts happen to remain equal. Preserve full native values.
	for(const auto id : draft.ids())
	{
		const auto unit = prospective.getForUpdate(id);
		stageNativeReadValue(draft, id, *unit, unit->preparedModifierWarfare());
	}
	refreshReadViews(draft, prospective, draft.orderedIds(), true);
}
std::optional<State> NativeAdapter::prepareOrderChange(HypotheticBattle & branch, BattleSide side,
	std::shared_ptr<const std::vector<HeroOrderState>> orders)
{
	if(!branch.compactForecast) return {};
	if(!orders) throw std::invalid_argument("Order replacement must retain an owning collection");
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(branch.compactForecast->ids().size()));
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	HypotheticBattle prospective(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
	prospective.heroOrderStates.at(side) = std::move(orders);
	if(!ordinaryOrdersSupported(prospective, side))
		throw std::invalid_argument("Order transition exceeds the admitted compact event plan");
	++prospective.bonusTreeVersion;
	auto draft = *branch.compactForecast;
	refreshOrderDerived(prospective, draft);
	return draft;
}
std::optional<State> NativeAdapter::prepareFocusFireChange(HypotheticBattle & branch, BattleSide side,
	const std::optional<FocusFireState> & mark)
{
	if(!branch.compactForecast) return {};
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(branch.compactForecast->ids().size()));
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	HypotheticBattle prospective(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
	prospective.focusFireStates.at(side) = mark;
	++prospective.bonusTreeVersion;
	auto draft = *branch.compactForecast;
	refreshOrderDerived(prospective, draft);
	return draft;
}
State NativeAdapter::prepareMembershipGeometry(HypotheticBattle & branch, State draft)
{
	const bool orders = !branch.localHeroOrderStates(BattleSide::ATTACKER).empty()
		|| !branch.localHeroOrderStates(BattleSide::DEFENDER).empty();
	bool cartChanged = false;
	for(const auto id : draft.ids())
	{
		const auto * unit = branch.battleGetUnitByID(id);
		if(unit->isAmmoCart() && unit->alive() != (draft.unit(id)->health.available() > 0)) cartChanged = true;
	}
	if(!orders && !cartChanged) return draft;
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(draft.ids().size()));
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	HypotheticBattle prospective(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, branch.searchBudget());
	prospective.projectedRound = draft.currentRound();
	for(const auto id : draft.ids())
		if(!publish(draft, id, prospective, *prospective.getForUpdate(id)))
			throw std::logic_error("Invalid prepared Order membership geometry");
	refreshOrderDerived(prospective, draft);
	return draft;
}
}
