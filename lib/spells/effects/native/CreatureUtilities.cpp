/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include <vcmi/spells/Spell.h>
#include "../../../battle/CUnitState.h"
#include "../../../GameLibrary.h"
#include "../../../CCreatureHandler.h"
#include "../../../scripting/ScriptTypeDescription.h"
#include "../../../texts/MetaString.h"

namespace spells::effects::native
{
namespace
{
class CabirRepair final : public UnitEffect
{
	static bool repairSpell(const Mechanics * mechanics) { return mechanics->getSpell()->getJsonKey() == "new-horizons:cabirRepair"; }
	static bool survivingCaster(const Mechanics * mechanics)
	{
		const auto * caster = mechanics->getUnitCaster();
		return caster && caster->alive() && caster->getCount() > 0;
	}
	static EHealLevel healLevel(const Mechanics * mechanics)
	{
		const auto * caster = mechanics->getUnitCaster();
		return caster && caster->getPhantomInitialIntegrity() > 0 ? EHealLevel::HEAL : EHealLevel::RESURRECT;
	}
	static int64_t repairPool(const Mechanics * mechanics, const battle::Unit * unit)
	{
		return std::max<int64_t>(0, mechanics->applySpellBonus(mechanics->getEffectValue(), unit));
	}
	bool targetValid(const Mechanics * mechanics, const Target & target) const
	{
		return repairSpell(mechanics) && target.size() == 1 && isValidTarget(mechanics, target.front().unitValue)
			&& isReceptive(mechanics, target.front().unitValue);
	}
protected:
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(!repairSpell(mechanics) || !survivingCaster(mechanics) || !unit || !unit->alive()
			|| !unit->isValidTarget(false) || unit->getCount() <= 0 || !mechanics->ownerMatches(unit)
			|| unit->isGhost() || unit->isSummoned() || unit->isClone() || unit->getPhantomInitialIntegrity() > 0) return false;
		const auto * creature = unit->creatureId().toEntity(LIBRARY);
		if(!creature) return false;
		const auto & key = creature->getJsonKey();
		if(key != "core:stoneGargoyle" && key != "core:obsidianGargoyle" && key != "core:ironGolem" && key != "core:stoneGolem") return false;
		const auto level = healLevel(mechanics);
		if(level == EHealLevel::RESURRECT)
		{
			const int64_t creatureHealth = static_cast<int64_t>(unit->getCount() - 1) * unit->getMaxHealth() + unit->getFirstHPleft();
			if(unit->getTotalHealth() - creatureHealth - static_cast<int64_t>(unit->getUnusableRemains()) * unit->getMaxHealth() <= 0) return false;
		}
		auto copy = unit->acquireState();
		auto pool = repairPool(mechanics, unit);
		const auto result = copy->heal(pool, level, EHealPower::PERMANENT);
		return result.healedHealthPoints > 0 || result.resurrectedCount > 0;
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(!repairSpell(mechanics) || !survivingCaster(mechanics)) return status::reject(mechanics, &problem);
		if(mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
		{
			return isValidTarget(mechanics, unit) && isReceptive(mechanics, unit);
		}).empty()) return status::reject(mechanics, &problem);
		return true;
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return targetValid(mechanics, target) || status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target &) const override
	{
		return repairSpell(mechanics) && aim.size() == 1 && aim.front().unitValue ? aim : Target{};
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		SpellEffectValue result;
		if(!repairSpell(mechanics) || !survivingCaster(mechanics)) return result;
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !isValidTarget(mechanics, unit) || !isReceptive(mechanics, unit)) continue;
			auto copy = unit->acquireState();
			auto pool = repairPool(mechanics, unit);
			const auto healed = copy->heal(pool, healLevel(mechanics), EHealPower::PERMANENT);
			result.hpDelta += healed.healedHealthPoints;
			result.unitsDelta += healed.resurrectedCount;
			result.unitType = unit->creatureId().toEntity(LIBRARY);
		}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!targetValid(mechanics, target)) return;
		const auto * unit = target.front().unitValue;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const auto [healed, restored] = nativeEvents::heal(context, *unit, repairPool(mechanics, unit), healLevel(mechanics), EHealPower::PERMANENT);
		MetaString message;
		if(restored > 0)
		{
			message.appendTextID(restored == 1 ? "core.genrltxt.117" : "core.genrltxt.116");
			message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
			message.replaceNumber(restored);
		}
		else if(healed > 0)
		{
			message.appendTextID("core.genrltxt.414");
			message.replaceTextID(nativeEvents::creatureName(mechanics->getUnitCaster(), 1));
			message.replaceTextID(nativeEvents::creatureName(unit, 1));
			message.replaceNumber(healed);
		}
		else return;
		nativeEvents::log(context, message);
	}
};

class GenieLuck final : public UnitEffect
{
protected:
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		const auto & key = mechanics->getSpell()->getJsonKey();
		const auto * caster = mechanics->getUnitCaster();
		return (key == "new-horizons:bestowLuck" || key == "new-horizons:turnOfFate") && caster && caster->alive()
			&& unit && unit->alive() && unit->isValidTarget(false);
	}
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto & key = mechanics->getSpell()->getJsonKey();
		if(key != "new-horizons:bestowLuck" && key != "new-horizons:turnOfFate") return;
		const bool negative = key == "new-horizons:turnOfFate";
		const std::string family = negative ? "core:misfortune" : "core:fortune";
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!isValidTarget(mechanics, unit) || !isReceptive(mechanics, unit) || !mechanics->ownerMatches(unit)) continue;
			BonusList previous;
			const auto bonuses = unit->getBonuses(Selector::all, "all");
			for(const auto & bonus : *bonuses)
				if(bonus->source == BonusSource::SPELL_EFFECT && (bonus->sid.toString() == "core:fortune" || bonus->sid.toString() == "core:misfortune")) previous.push_back(bonus);
			if(!previous.empty()) nativeEvents::removeBonuses(context, *unit, previous);
			auto record = status::bonus("LUCK", negative ? -3 : 3, family, 5, {}, negative);
			record["stacking"].String() = "new-horizons:wheelOfFortune";
			status::add(context, *unit, std::move(record));
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createCreatureUtilityEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/cabirRepair") return std::make_shared<CabirRepair>();
	if(description.sourcePath == "spells/genieLuck") return std::make_shared<GenieLuck>();
	return {};
}
}
