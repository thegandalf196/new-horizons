/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "ForecastHeroActionRouting.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include <stdexcept>
std::shared_ptr<HypotheticBattle> BattleEvaluator::createCreatureCandidateBattle(
	const CStack * actor, const CSpell * spell, const spells::Target & destination,
	std::optional<int> spellLevel) const
{
	const auto source = cb->getBattle(battleID);
	const bool reusable = decisionBaseOpen && decisionBase && source == decisionNativeSource
		&& source->battleGetRound() == decisionBase->battleGetRound()
		&& source->getBattle()->getActivationSerial() == decisionBase->getActivationSerial()
		&& source->getBattle()->getCapturedMagicRules() == decisionBase->getCapturedMagicRules();
	HypotheticBattle::Subject current = source;
	if(reusable) current = decisionBase;
	auto route = forecast::heroAction::Route::CurrentNativeReference;
	if(reusable && actor && spell && source->battleGetUnitByID(actor->unitId()) == actor)
	{
		if(workBudget) workBudget->require(battleSearch::WorkKind::Node,
			static_cast<uint32_t>(decisionBase->forecastState()->ids().size() + 1));
		auto target = destination;
		for(auto & value : target)
			if(value.unitValue) value.unitValue = current->battleGetUnitByID(value.unitValue->unitId());
		std::optional<newHorizonsPuppetMaster::ActionControllerCaster> proxy;
		const auto * currentActor = current->battleGetUnitByID(actor->unitId());
		if(!currentActor) throw std::logic_error("Creature candidate lost its admitted current actor");
		const spells::Caster * caster = currentActor;
		if(spellLevel || newHorizonsHeroes::isExplicitGenieLuckCast(*actor, spell->getId()))
		{
			const auto * unit = current->battleGetUnitByID(actor->unitId());
			proxy.emplace(unit, current->battleGetActionController(unit)); caster = &*proxy;
		}
		spells::BattleCast cast(current.get(), caster, spells::Mode::CREATURE_ACTIVE, spell);
		if(spellLevel) cast.setSpellLevel(*spellLevel);
		const auto mechanics = spell->battleMechanics(&cast);
		if(mechanics) route = forecast::heroAction::classifyCreature(*current, *mechanics, target).route;
	}
	const auto & input = route == forecast::heroAction::Route::Compact ? current : HypotheticBattle::Subject(source);
	auto result = forecast::heroAction::makeBranch(env.get(), input, workBudget, route);
	if(!result->forecastWorkSink() && decisionReadWork && !result->ownedForecastSize())
		result->setForecastWorkSink(decisionReadWork);
	result->setMovementQueryCache(movementMemo);
	return result;
}
