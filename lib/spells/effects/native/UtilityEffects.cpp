/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../CurePhysicalAffliction.h"
#include "../../NewHorizonsSorcery.h"
#include "../../CSpell.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../networkPacks/SetStackEffect.h"
#include "../../../battle/NewHorizonsSpecialtyProcs.h"
#include "../../../battle/NewHorizonsStun.h"
#include "../../../battle/NewHorizonsAttackControllers.h"
#include "../../../scripting/ScriptTypeDescription.h"
#include "../../../texts/MetaString.h"

namespace spells::effects::native
{
namespace
{
class Dispel final : public UnitEffect
{
	BonusList removable(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		const auto key = mechanics->getSpell()->getJsonKey();
		const auto bonuses = unit->getBonuses(Selector::all, "all");
		BonusList result;
		if(mechanics->isNewHorizonsResurrection()) return result;
		if(mechanics->isNewHorizonsCure())
		{
			if(!mechanics->getCurePhysicalAffliction().empty()) return result;
			const auto selected = mechanics->getCureAffliction();
			const auto source = selected == SpellID::NONE ? std::string{} : selected.toSpell()->getJsonKey();
			for(const auto & bonus : *bonuses)
				if(!source.empty() && bonus->source == BonusSource::SPELL_EFFECT && bonus->sid.toString() == source) result.push_back(bonus);
			return result;
		}
		const bool selective = mechanics->isSelectiveDispel();
		const bool nullifyingShot = newHorizonsSpecialtyProcs::isNullifyingShotCast(*mechanics);
		const bool friendly = selective && mechanics->ownerMatches(unit, true);
		for(const auto & bonus : *bonuses)
		{
			if(bonus->source != BonusSource::SPELL_EFFECT || bonus->sid.toString() == key) continue;
			const auto * spell = nativeEvents::spell(bonus->sid.toString());
			if(!spell || spell->isPersistent() || spell->isAdventure()) continue;
			if(mechanics->usesNewHorizonsTemporaryMagicDispel() && (!spell->isMagical() || !status::isTemporary(*bonus))) continue;
			if(nullifyingShot && !newHorizonsSpecialtyProcs::isPositiveTemporaryMagic(*bonus)) continue;
			if(selective)
			{
				if((friendly && spell->isNegative()) || (!friendly && spell->isPositive())) result.push_back(bonus);
			}
			else if((parameters["dispelPositive"].Bool() && spell->isPositive())
				|| (parameters["dispelNegative"].Bool() && spell->isNegative())
				|| (parameters["dispelNeutral"].Bool() && spell->isNeutral())) result.push_back(bonus);
		}
		return result;
	}
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(!unit->isValidTarget(false)) return false;
		if(mechanics->isNewHorizonsCure() && !mechanics->getCurePhysicalAffliction().empty())
			return mechanics->getCureAffliction() == SpellID::NONE && mechanics->battle() && mechanics->battle()->getBattle()
				&& curePhysical::present(mechanics->battle()->getBattle()->getMagicRules(), *unit, mechanics->getCurePhysicalAffliction());
		return !removable(mechanics, unit).empty();
	}
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const bool positiveOnly = parameters["dispelPositive"].Bool() && !parameters["dispelNegative"].Bool() && !parameters["dispelNeutral"].Bool();
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit) continue;
			if(mechanics->isNewHorizonsCure() && !mechanics->getCurePhysicalAffliction().empty())
			{
				const auto kind = mechanics->getCurePhysicalAffliction();
				if(kind == "frozen") nativeEvents::clearFrozen(context, *unit);
				else
				{
					if(kind != "graveDust" && kind != "stun" && kind != "cripple") throw std::invalid_argument("Unsupported physical Cure selection");
					if(!context.battle.getBattle() || context.battle.battleGetUnitByID(unit->unitId()) != unit)
						throw std::runtime_error("Grave Dust removal requires a unit in the given battle");
					const auto removed = kind == "cripple" ? newHorizonsAttackControllers::crippleRemoval(*unit) : kind == "stun" ? newHorizonsStun::removalPlan(*unit) : curePhysical::graveDustRemoval(*unit);
					if(!removed.empty())
					{
						SetStackEffect pack;
						pack.battleID = context.battle.getBattle()->getBattleID();
						pack.toRemove.emplace_back(unit->unitId(), removed);
						server->apply(pack);
					}
				}
			}
			const auto bonuses = removable(mechanics, unit);
			if(bonuses.empty()) continue;
			if(positiveOnly && server->describeChanges())
			{
				MetaString message;
				message.appendTextID("core.genrltxt.555");
				message.replaceTextID(nativeEvents::creatureName(unit, 0));
				nativeEvents::log(context, message);
			}
			nativeEvents::removeBonuses(context, *unit, bonuses);
		}
	}
};

class SpellLock final : public UnitEffect
{
	bool isValidTarget(const Mechanics *, const battle::Unit * unit) const override { return unit && unit->isValidTarget(false); }
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto * hero = mechanics->getHeroCaster();
		const int turns = mechanics->adjustEffectDuration(newHorizonsSorcery::spellLockDurationBasisPoints(mechanics->getEffectPower(),
			hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.spellbinder"),
			mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent()));
		const std::string key = "new-horizons:spellLock";
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !unit->alive()) continue;
			const bool friendly = context.battle.battleGetOwner(unit) == mechanics->getCasterColor();
			const auto bonuses = unit->getBonuses(Selector::all, "all");
			BonusList removable;
			for(const auto & bonus : *bonuses)
			{
				if(bonus->source != BonusSource::SPELL_EFFECT || bonus->sid.toString() == key) continue;
				const auto * source = nativeEvents::spell(bonus->sid.toString());
				if(source && !source->isAdventure() && source->isMagical()
					&& (friendly ? source->isNegative() : source->isPositive())) removable.push_back(bonus);
			}
			if(!removable.empty()) nativeEvents::removeBonuses(context, *unit, removable);
			status::add(context, *unit, status::bonus("MAGIC_RESISTANCE", 100, key, turns, {}, !friendly));
			auto marker = status::bonus("NONE", friendly ? turns : -turns, key, turns, {}, !friendly);
			marker.Struct().erase("stacking");
			marker["hidden"].Bool() = true;
			marker["description"].String() = friendly ? "Spell Lock: preserve beneficial magic and freeze its duration"
				: "Spell Lock: preserve hostile magic and freeze its duration";
			status::add(context, *unit, std::move(marker));
			if(server->describeChanges())
			{
				MetaString message;
				message.appendTextID(friendly ? "new-horizons.combat.spellLock.preserveBeneficial" : "new-horizons.combat.spellLock.preserveHostile");
				message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
				message.replaceNumber(mechanics->getRecipientSpellLockDuration(unit));
				nativeEvents::log(context, message);
			}
		}
	}
};

BonusList chosenBonuses(const battle::Unit & unit)
{
	const auto all = unit.getBonusesFrom(BonusSource::SPELL_EFFECT);
	BonusList result;
	for(const auto & bonus : *all) if(bonus->sid.toString() == "new-horizons:theChosen") result.push_back(bonus);
	return result;
}

class TheChosen final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(!unit || !unit->isValidTarget(false) || !unit->isLiving() || unit->hasBonusOfType(BonusType::MECHANICAL)
			|| !mechanics->ownerMatches(unit) || unit->isTimeStopped()) return false;
		return mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * previous)
		{
			return previous->unitSide() == unit->unitSide() && previous->isTimeStopped() && !chosenBonuses(*previous).empty();
		}).empty();
	}
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto * unit = target.empty() ? nullptr : target.front().unitValue;
		if(!isValidTarget(mechanics, unit) || !isReceptive(mechanics, unit)) return;
		const int turns = mechanics->getTheChosenDuration();
		const int jousting = mechanics->getTheChosenJoustingPercent();
		if(turns <= 0 || jousting <= 0) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto * previous : mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * other) { return other->unitSide() == unit->unitSide(); }))
		{
			const auto old = chosenBonuses(*previous);
			if(!old.empty()) nativeEvents::removeBonuses(context, *previous, old);
		}
		for(const auto * type : {"ADDITIONAL_ATTACK", "UNLIMITED_RETALIATIONS", "FLYING", "JOUSTING"})
		{
			auto record = status::bonus(type, std::string_view(type) == "JOUSTING" ? jousting : 1, "new-horizons:theChosen", turns);
			record.Struct().erase("stacking");
			record["valueType"].String() = "INDEPENDENT_MAX";
			status::add(context, *unit, std::move(record));
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createUtilityEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/dispel") return std::make_shared<Dispel>();
	if(description.sourcePath == "spells/spellLock") return std::make_shared<SpellLock>();
	if(description.sourcePath == "spells/theChosen") return std::make_shared<TheChosen>();
	return {};
}
}
