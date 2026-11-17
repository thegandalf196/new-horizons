/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/constants/NumericConstants.h"
void HypotheticBattle::captureSideCombatState(const CBattleInfoCallback & source, BattleSide side)
{
	focusFireStates[side] = source.battleGetFocusFireState(side);
	fortuneStates[side] = source.getBattle()->getSylvanLuckState(side);
	perfectFortuneStates[side] = source.getBattle()->getPerfectFortuneState(side);
	luckSerendipityStates[side] = source.getBattle()->getLuckSerendipityState(side);
	adverseRerollStates[side] = source.getBattle()->getAdverseCombatRerollState(side);
	moraleSuppressionStates[side] = source.getBattle()->getMoraleSuppressionState(side);
	bloodrageRanks[side] = source.getBattle()->getBloodrageRank(side);
	bloodrageDamagePercents[side] = source.getBattle()->getBloodrageDamagePercent(side);
	bloodrageCaps[side] = source.getBattle()->getBloodrageCapPercent(side);
	bloodrageSpeedBonuses[side] = source.getBattle()->getBloodrageSpeedBonus(side);
	bloodrageAdditionalRetaliations[side] = source.getBattle()->getBloodrageAdditionalRetaliations(side);
	bloodrageLowHealthIncrements[side] = source.getBattle()->getBloodrageLowHealthIncrement(side);
	bloodragePainIncrements[side] = source.getBattle()->getBloodragePainIncrement(side);
}

void HypotheticBattle::captureForecastSides(const CBattleInfoCallback & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
	{
		sideStates = parent->sideStates;
		return;
	}
	BattleSideArray<std::shared_ptr<const forecast::SideState>> captured;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto state = forecast::SideState::capture(source, side);
		if(!state) throw std::invalid_argument("Unsupported compact forecast side snapshot");
		captured.at(side) = std::make_shared<const forecast::SideState>(std::move(*state));
	}
	sideStates = std::move(captured);
}

bool HypotheticBattle::commitForecastSide(BattleSide side, const forecast::SideReceipt & receipt)
{
	if(!receipt.after() || receipt.after()->side != side || forecastSide(side) != receipt.expected()) return false;
	// Prepare all owned derived values BEFORE publishing the side pointer.
	auto derived = forecast::NativeAdapter::prepareSideChange(*this, side, receipt);
	if(!forecast::SideTransitions::commit(sideStates.at(side), receipt)) return false;
	if(derived) compactForecast = std::move(derived);
	return true;
}

bool HypotheticBattle::hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| level < 1 || level > GameConstants::SPELL_LEVELS)
		return false;

	const auto bit = static_cast<std::uint8_t>(1u << (level - 1));
	const auto & state = forecastSide(side);
	return state->ledger && (state->ledger->completedLevelMask & bit) != 0;
}

const SpellResponseState & HypotheticBattle::getSpellResponseState(BattleSide side) const
{
	static const SpellResponseState empty;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return empty;
	const auto & state = forecastSide(side);
	return state->ledger ? state->ledger->response : empty;
}

void HypotheticBattle::setSpellResponseState(BattleSide side, const SpellResponseState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Spell Response side");
	if(!forecastSide(side)->hasVisibleHero()) throw std::invalid_argument("Unknown projected Spell Response owner");
	state.validate();
	if(state.hasState() && !state.isReadyAt(projectedRound))
		throw std::runtime_error("Hypothetical Spell Response state is outside its round window");
	if(state.hasState())
	{
		// Validate an internal native receipt against its actual side owner.
		// Public hero queries remain player-filtered.
		const auto * hero = getSideHero(side);
		if(!hero || !hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)))
			throw std::runtime_error("Hypothetical Spell Response state requires Counterpressure");
	}
	const auto receipt = forecast::SideTransitions::setResponse(forecastSide(side), state, searchBudget().get());
	if(!receipt || !commitForecastSide(side, *receipt)) throw std::invalid_argument("Unknown projected Spell Response owner");
}

const OverwhelmingFormulaState & HypotheticBattle::getOverwhelmingFormulaState(BattleSide side) const
{
	static const OverwhelmingFormulaState empty;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return empty;
	const auto & state = forecastSide(side);
	return state->ledger ? state->ledger->overwhelming : empty;
}

void HypotheticBattle::setOverwhelmingFormulaState(BattleSide side, const OverwhelmingFormulaState & state)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Invalid hypothetical Overwhelming Formula side");
	state.validateShape();
	const auto receipt = forecast::SideTransitions::setOverwhelming(forecastSide(side), state, searchBudget().get());
	if(!receipt || !commitForecastSide(side, *receipt)) throw std::invalid_argument("Unknown projected Overwhelming Formula owner");
}

const HeroActionAllowanceState & HypotheticBattle::getHeroActionAllowances(BattleSide side) const
{
	static const HeroActionAllowanceState unknown;
	const auto & state = forecastSide(side);
	return state->ledger ? state->ledger->allowances : unknown;
}
