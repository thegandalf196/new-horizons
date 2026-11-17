/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "FlatDamageInputs.h"
#include "HypotheticBattle.h"
#include "HeroActionAdmission.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"

FlatDamageInputs::FlatDamageInputs(const HypotheticBattle & battle, bool shooting)
	: source(battle), shooting(shooting)
{}

bool FlatDamageInputs::prepare(const battle::Unit & actor, std::span<const battle::Unit * const> targets)
{
	units.clear();
	rangedUnits.clear();
	try
	{
		const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
		if(!calculator || calculator->forecastImplementation(source) != DamageForecastImplementation::CANONICAL)
			return false;
		heroActionProjection::CanonicalSourceAdmission admission(actor, source);
		for(const auto * target : targets)
			if(!target || !admission.admits(*target)) return false;
		std::map<const battle::Unit *, nativeDamage::PreparedUnit> prepared;
		const auto add = [&](const battle::Unit & unit)
		{
			if(prepared.contains(&unit)) return true;
			auto facts = nativeDamage::prepareUnit(unit, shooting);
			if(!facts) return false;
			prepared.emplace(&unit, std::move(*facts));
			return true;
		};
		if(!add(actor)) return false;
		for(const auto * target : targets)
			if(!add(*target)) return false;
		units = std::move(prepared);
		return true;
	}
	catch(const std::exception &)
	{
		return false; // Optional preparation never changes reference rejection.
	}
}

bool FlatDamageInputs::prepareRoster(HypotheticBattle & branch)
{
	units.clear();
	rangedUnits.clear();
	if(&branch != &source) return false;
	try
	{
		const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
		if(!calculator || calculator->forecastImplementation(source) != DamageForecastImplementation::CANONICAL)
			return false;
		battle::Units roster;
		for(const auto * unit : source.battleGetAllUnits(false))
			if(unit->alive()) roster.push_back(branch.getForUpdate(unit->unitId()).get());
		if(roster.size() < 2) return false;
		heroActionProjection::CanonicalSourceAdmission admission(*roster.front(), source);
		if(!admission.admits(*roster.back())) return false;
		std::map<const battle::Unit *, nativeDamage::PreparedUnit> primary, secondary;
		for(const auto * unit : roster)
		{
			if(auto facts = nativeDamage::prepareUnit(*unit, shooting))
				primary.emplace(unit, std::move(*facts));
			if(auto facts = nativeDamage::prepareUnit(*unit, !shooting))
				secondary.emplace(unit, std::move(*facts));
		}
		units = std::move(primary);
		rangedUnits = std::move(secondary);
		return !units.empty() || !rangedUnits.empty();
	}
	catch(const std::exception &) { return false; }
}

const nativeDamage::PreparedUnit * FlatDamageInputs::find(const battle::Unit & unit, bool shot) const
{
	if(source.battleGetUnitByID(unit.unitId()) != &unit)
		return nullptr;
	const auto & inputs = shot == shooting ? units : rangedUnits;
	const auto found = inputs.find(&unit);
	return found == inputs.end() ? nullptr : &found->second;
}

FlatDamageInputs::Scope::Scope(HypotheticBattle & battle, const FlatDamageInputs * inputs)
	: battle(battle), previous(battle.flatDamageInputs)
{
	// Nested/foreign branches cannot borrow the enclosing numeric frame.
	battle.flatDamageInputs = inputs && &inputs->source == &battle ? inputs : nullptr;
}
FlatDamageInputs::Scope::~Scope() { battle.flatDamageInputs = previous; }

const nativeDamage::PreparedUnit * HypotheticBattle::preparedUnitDamageFacts(const battle::Unit & unit, bool shooting) const
{
	return flatDamageInputs ? flatDamageInputs->find(unit, shooting) : nullptr;
}
