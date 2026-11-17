/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastEffectLimits.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/UnitTransitionSidecars.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace forecast
{
namespace
{
std::optional<SpellID> resolved(const char * key)
{
	const auto id = SpellID::decode(key);
	return id >= 0 ? std::optional<SpellID>(SpellID(id)) : std::nullopt;
}
}
std::shared_ptr<const forecastEffects::State> NativeAdapter::captureEffects(const StackWithBonuses & unit, int32_t round)
{
	if(const auto owned = unit.ownedLocalOtherEffects())
	{
		std::vector<forecastEffects::RawRecord> records;
		uint64_t occurrence = 0;
		const auto owner = static_cast<uint64_t>(unit.unitId());
		for(const auto & raw : unit.localSpellEffects())
			records.push_back({std::make_shared<const Bonus>(raw), {owner, occurrence++}, Bonus::NTurns(&raw),
				battle::isTransientSpellEffect(&raw)});
		for(const auto & raw : *owned) records.push_back({raw, {owner, occurrence++}, false, false});
		forecastEffects::Admission admitted{resolved("new-horizons:entangle"), resolved("new-horizons:forceLance"),
			resolved("new-horizons:focusMagic"), round, unit.frozenLastAppliedRound()};
		return forecastEffects::State::capture(records, std::move(admitted));
	}
	std::vector<recipientBonusOverlay::Layer> layers;
	const CStack * native = nullptr;
	if(!unit.captureRecipientOverlayLayers(layers, native) || !native)
		throw std::invalid_argument("Effects need canonical raw local exporter provenance");
	const auto local = unit.localSpellEffects(); // exact LOCAL native/branch multiset, not selected bonuses
	const auto raw = unit.getUnstackedBonuses(Selector::all);
	if(local.size() > MAXIMUM_EFFECT_RECORDS || raw->size() > MAXIMUM_EFFECT_RECORDS)
		throw std::invalid_argument("Effect capture exceeds finite record bound");
	std::vector<Bonus> unmatched = local;
	std::vector<std::shared_ptr<const Bonus>> localOther;
	std::vector<std::shared_ptr<const Bonus>> authorizedOther;
	for(const auto & row : native->getExportedBonusList()) authorizedOther.push_back(row);
	for(const auto & layer : layers)
		for(const auto * rows : {&layer.added, &layer.updated})
			for(const auto & row : *rows) authorizedOther.push_back(row);
	std::vector<forecastEffects::RawRecord> records;
	uint64_t occurrence = 0;
	const auto owner = static_cast<uint64_t>(unit.unitId());
	for(const auto & value : *raw)
	{
		if(!value) throw std::invalid_argument("Null raw effect record");
		if(value->source == BonusSource::SPELL_EFFECT)
		{
			const auto found = std::ranges::find_if(unmatched, [&](const Bonus & item)
			{ return battle::exactBattleEffectEqual(item, *value); });
			if(found == unmatched.end()) throw std::invalid_argument("Inherited or opaque spell clock owner");
			unmatched.erase(found);
		}
		else if(newHorizonsFrozen::markerApplicationRound(*value)
			|| (value->source == BonusSource::OTHER && value->duration == BonusDuration::STACK_GETS_TURN))
		{
			const auto origin = std::ranges::find_if(authorizedOther, [&](const auto & row)
			{ return row && battle::exactBattleEffectEqual(*row, *value); });
			if(origin == authorizedOther.end()) throw std::invalid_argument("Nonlocal ordinary activation/Frozen owner");
			localOther.push_back(*origin);
		}
	}
	if(!unmatched.empty()) throw std::invalid_argument("Local raw spell export multiset mismatch");
	// The exact local API is backed by canonical native exporter/branch raw
	// layers, including excluded duplicates. The unstacked list above is ONLY
	// a foreign-owner rejection check, never the source of clock ownership.
	for(const auto & value : local)
		records.push_back({std::make_shared<const Bonus>(value), {owner, occurrence++}, Bonus::NTurns(&value),
			battle::isTransientSpellEffect(&value)});
	for(const auto & value : localOther)
		records.push_back({value, {owner, occurrence++}, false, false});
	forecastEffects::Admission admitted{resolved("new-horizons:entangle"), resolved("new-horizons:forceLance"),
		resolved("new-horizons:focusMagic"), round, unit.frozenLastAppliedRound()};
	return forecastEffects::State::capture(records, std::move(admitted));
}
bool NativeAdapter::effectClockOwnersSupported(HypotheticBattle & source)
{
	std::set<const Bonus *> clocks;
	for(const auto * unit : source.battleGetAllUnits(false))
	{
		const auto projected = source.getForUpdateImpl(unit->unitId());
		if(!projected) return false;
		std::vector<recipientBonusOverlay::Layer> layers;
		const CStack * native = nullptr;
		if(!projected->captureRecipientOverlayLayers(layers, native) || !native) return false;
		// Foreign inherited clocks cannot be independently advanced/removed by
		// each recipient. Reject the whole source before any compact prefix.
		TCNodes parents; native->getDirectParents(parents);
		std::vector<const CBonusSystemNode *> pending(parents.begin(), parents.end());
		std::set<const CBonusSystemNode *> seen;
		while(!pending.empty())
		{
			const auto * parent = pending.back(); pending.pop_back();
			if(!seen.insert(parent).second) continue;
			if(seen.size() > MAXIMUM_EFFECT_RECORDS) return false;
			for(const auto & row : parent->getExportedBonusList())
			{
				if(source.searchBudget()) source.searchBudget()->require(battleSearch::WorkKind::Node);
				if(!row || Bonus::NTurns(row.get()) || row->source == BonusSource::SPELL_EFFECT
					|| newHorizonsFrozen::markerApplicationRound(*row)) return false;
			}
			TCNodes more; parent->getDirectParents(more);
			pending.insert(pending.end(), more.begin(), more.end());
		}
		// Exact raw native exporter roots, not effective records. A shared
		// exporter clock across recipients is unsupported before any plan prefix.
		for(const auto & row : native->getExportedBonusList())
			if(row && Bonus::NTurns(row.get()) && !clocks.insert(row.get()).second) return false;
		for(const auto & layer : layers)
			for(const auto * roots : {&layer.added, &layer.updated})
				for(const auto & row : *roots)
					if(row && Bonus::NTurns(row.get()) && !clocks.insert(row.get()).second) return false;
	}
	return true;
}
}
