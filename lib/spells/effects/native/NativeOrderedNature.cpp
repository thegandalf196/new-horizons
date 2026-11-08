/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "Damage.h"
#include "../../CSpell.h"

namespace spells::effects::native
{
namespace
{
void applyMagic(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit, int64_t amount)
{
	const auto id = unit.unitId();
	const auto [damage, killed] = nativeEvents::damage(context, unit, amount, mechanics->getUnitCaster(), true);
	if(const auto * current = context.battle.battleGetUnitByID(id)) nativeEvents::clearFrozen(context, *current);
	if(damage > 0 && context.server.describeChanges())
		nativeEvents::spellDamageLog(context, *mechanics->getSpellId().toSpell(), &unit, damage, killed);
}

class Pandemonium final : public Damage
{
	static bool valid(const battle::Unit * unit)
	{ return unit && unit->alive() && !unit->isGhost() && !unit->isTurret() && unit->getPosition().isAvailable(); }
	bool isValidTarget(const Mechanics *, const battle::Unit * unit) const override { return valid(unit); }
	bool isReceptive(const Mechanics *, const battle::Unit * unit) const override { return valid(unit); }
	bool eligible(const Mechanics * mechanics, const battle::Unit * unit) const
	{ return valid(unit) && !unit->isInvincible() && Damage::isReceptive(mechanics, unit) && !mechanics->wouldResist(unit); }
	std::vector<std::pair<const battle::Unit *, int64_t>> capture(const Mechanics * mechanics, const Target & target) const
	{
		std::vector<std::pair<const battle::Unit *, int64_t>> result;
		for(const auto & destination : target)
			if(valid(destination.unitValue)) result.emplace_back(destination.unitValue, mechanics->getPandemoniumDebuffCount(destination.unitValue));
		return result;
	}
public:
	ResistanceShareKind resistanceShareKind() const override { return ResistanceShareKind::Pandemonium; }
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::UNKNOWN; }
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{ return !mechanics->getPandemoniumTargets().empty() || status::reject(mechanics, &problem); }
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target &) const override
	{ return applicableGeneral(problem, mechanics); }
	Target transformTarget(const Mechanics * mechanics, const Target &, const Target &) const override { return mechanics->getPandemoniumTargets(); }
	Target filterTarget(const Mechanics *, const Target & target) const override { return target; }
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		SpellEffectValue result;
		const auto recipients = capture(mechanics, target);
		for(const auto & [unit, count] : recipients)
			if(count > 0 && eligible(mechanics, unit))
			{
				auto copy = unit->acquireState();
				const auto beforeHealth = copy->getAvailableHealth(); const auto beforeCount = copy->getCount();
				auto amount = mechanics->getPandemoniumDamage(unit, count);
				copy->damage(amount);
				result.hpDelta += copy->getAvailableHealth() - beforeHealth; result.unitsDelta += copy->getCount() - beforeCount;
			}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		// Counts are captured for the entire set before any packet can remove an aura.
		const auto recipients = capture(mechanics, target);
		for(const auto & [unit, count] : recipients)
			if(count > 0 && eligible(mechanics, unit))
			{
				server->recordResolvedSpellRecipient(unit->unitId());
				applyMagic(context, mechanics, *unit, mechanics->getPandemoniumDamage(unit, count));
			}
	}
};

class NaturesWrath final : public Damage
{
	static bool valid(const battle::Unit * unit)
	{ return unit && unit->alive() && !unit->isGhost() && !unit->isTurret() && !unit->isTimeStopped() && unit->getPosition().isAvailable(); }
	const battle::Unit * first(const Mechanics * mechanics, const Target & target) const
	{
		if(target.size() != 1) return nullptr;
		if(target.front().unitValue) return target.front().unitValue;
		return target.front().hexValue.isAvailable() ? mechanics->battle()->battleGetUnitByPos(target.front().hexValue, true) : nullptr;
	}
	bool isValidTarget(const Mechanics *, const battle::Unit * unit) const override { return valid(unit); }
	bool isReceptive(const Mechanics *, const battle::Unit * unit) const override { return valid(unit); }
	bool eligible(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		if(!valid(unit) || unit->isInvincible()) return false;
		return mechanics->ownerMatches(unit, true) ? UnitEffect::isReceptive(mechanics, unit)
			: Damage::isReceptive(mechanics, unit) && !mechanics->wouldResist(unit);
	}
	int64_t healing(const Mechanics * mechanics, const battle::Unit * unit, size_t index) const
	{ return mechanics->applySpellBonus(mechanics->getNaturesWrathHopPower(static_cast<int32_t>(index)), unit); }
public:
	ResistanceShareKind resistanceShareKind() const override { return ResistanceShareKind::NaturesWrath; }
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::UNKNOWN; }
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{ return !mechanics->battle()->battleGetUnitsIf(valid).empty() || status::reject(mechanics, &problem); }
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{ return valid(first(mechanics, target)) || status::reject(mechanics, &problem); }
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		const auto * unit = first(mechanics, aim);
		if(!unit) unit = first(mechanics, target);
		return valid(unit) ? mechanics->getNaturesWrathRoute(unit) : Target{};
	}
	Target filterTarget(const Mechanics *, const Target & target) const override { return target; }
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		SpellEffectValue result;
		for(size_t index = 0; index < target.size(); ++index)
		{
			const auto * unit = target[index].unitValue;
			if(!eligible(mechanics, unit)) continue;
			auto copy = unit->acquireState();
			if(mechanics->ownerMatches(unit, true))
			{
				auto amount = healing(mechanics, unit, index);
				result.hpDelta += copy->heal(amount, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
			}
			else
			{
				const auto beforeHealth = copy->getAvailableHealth(); const auto beforeCount = copy->getCount();
				auto amount = mechanics->getNaturesWrathDamage(unit, static_cast<int32_t>(index));
				copy->damage(amount);
				result.hpDelta += copy->getAvailableHealth() - beforeHealth; result.unitsDelta += copy->getCount() - beforeCount;
			}
		}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		// Keep the captured route indices, including every blocked or resisted slot.
		for(size_t index = 0; index < target.size(); ++index)
		{
			const auto * unit = target[index].unitValue;
			if(!eligible(mechanics, unit)) continue;
			server->recordResolvedSpellRecipient(unit->unitId());
			if(mechanics->ownerMatches(unit, true))
			{
				const auto healed = nativeEvents::heal(context, *unit, healing(mechanics, unit, index), EHealLevel::HEAL, EHealPower::PERMANENT).first;
				if(healed <= 0 || !server->describeChanges()) continue;
				MetaString message;
				message.appendTextID("new-horizons.combat.naturesWrath.healed");
				message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount())); message.replaceNumber(healed);
				nativeEvents::log(context, message);
			}
			else applyMagic(context, mechanics, *unit, mechanics->getNaturesWrathDamage(unit, static_cast<int32_t>(index)));
		}
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createOrderedNatureEffect(const std::string & path)
{
	using namespace spells::effects::native;
	if(path == "spells/pandemonium") return std::make_shared<Pandemonium>();
	if(path == "spells/naturesWrath") return std::make_shared<NaturesWrath>();
	return {};
}
}
