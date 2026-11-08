/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "../../NewHorizonsSorceryRevision.h"
#include "../../NewHorizonsTrollSummonPower.h"
#include "../../NewHorizonsElementalConvergencePower.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
enum class SummonKind { LIVING_ARMOR, TROLLS, ELEMENTALS };
class TimedSummon final : public SummonEffect
{
	SummonKind kind;
	bool nature() const { return kind != SummonKind::LIVING_ARMOR; }
	bool conjurer(const Mechanics * mechanics) const
	{ return summon::perk(mechanics, "new-horizons:natureMagic", "new-horizons:natureMagic.elementalConjurer"); }
	const Creature * type(const Mechanics * mechanics) const
	{ return kind == SummonKind::ELEMENTALS ? mechanics->getElementalConvergenceCreature() : nativeEvents::creature(parameters["id"].String()); }
	int64_t livingParameter(const Mechanics * mechanics, const std::string & name) const
	{
		const auto * state = mechanics->battle()->getBattle();
		if(!state) return 0;
		const auto & config = state->getMagicRules()["spells"][mechanics->getSpell()->getJsonKey()]["livingArmor"];
		return config.isStruct() && config["version"].Integer() == 1 ? config[name].Integer() : 0;
	}
	int64_t pool(const Mechanics * mechanics) const
	{
		int64_t ordinary;
		if(kind == SummonKind::LIVING_ARMOR)
			ordinary = newHorizonsSorceryRevision::livingArmorHitPoints(livingParameter(mechanics, "baseHitPoints"),
				livingParameter(mechanics, "spellPowerCoefficient"), mechanics->getEffectPower(), mechanics->getEffectPowerDivisor(),
				mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent());
		else if(kind == SummonKind::TROLLS)
			ordinary = newHorizonsMagic::trollSummonHitPoints(mechanics->getEffectPower(), mechanics->getSpellPowerCoefficientBasisPoints(),
				mechanics->getEmpowerSpellBonusPercent(), summon::perk(mechanics, "new-horizons:natureMagic", "new-horizons:natureMagic.beastcaller"));
		else
			ordinary = newHorizonsMagic::elementalConvergenceHitPoints(mechanics->getEffectPower(), mechanics->getSpellPowerCoefficientBasisPoints(),
				mechanics->getEmpowerSpellBonusPercent(), conjurer(mechanics));
		return mechanics->adjustMetamagicMagnitude(ordinary, nullptr);
	}
	bool hasDurationAndPool(const Mechanics * mechanics) const
	{ return kind != SummonKind::LIVING_ARMOR || (pool(mechanics) > 0 && livingParameter(mechanics, "duration") > 0); }
	int64_t count(const Mechanics * mechanics, int64_t health) const
	{ return static_cast<int64_t>(std::ceil(static_cast<double>(health) / mechanics->getSummonedCreatureMaxHealth(type(mechanics), nature()))); }
public:
	explicit TimedSummon(SummonKind kind) : kind(kind) {}
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		const auto * creature = type(mechanics);
		const auto hex = creature ? mechanics->battle()->getAvailableHex(creature, mechanics->getCasterSide()) : BattleHex::INVALID;
		if(hasDurationAndPool(mechanics) && hex.isValid()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(hasDurationAndPool(mechanics) && summon::selectedHex(mechanics, type(mechanics), target).isValid()) return true;
		return status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		const auto & selected = target.size() != 1 && aim.size() == 1 ? aim : target;
		const auto hex = summon::selectedHex(mechanics, type(mechanics), selected);
		return hex.isValid() ? Target{Destination(hex)} : Target{};
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		if(!summon::selectedHex(mechanics, type(mechanics), target).isValid()) return {};
		const auto health = pool(mechanics);
		return summon::value(health, count(mechanics, health), type(mechanics));
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto position = summon::selectedHex(mechanics, type(mechanics), target);
		if(!position.isValid() || (kind == SummonKind::LIVING_ARMOR && livingParameter(mechanics, "duration") <= 0)) return;
		const auto health = pool(mechanics), amount = count(mechanics, health);
		if(health <= 0 || amount <= 0) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		battle::UnitInfo info;
		info.count = amount; info.type = type(mechanics)->getId(); info.side = mechanics->getCasterSide();
		info.position = position; info.summoned = true; info.natureSummoned = nature();
		if(kind == SummonKind::LIVING_ARMOR)
			info.summonRoundsRemaining = mechanics->adjustMetamagicDuration(mechanics->adjustEffectDuration(livingParameter(mechanics, "duration")), nullptr);
		const auto * unit = nativeEvents::addUnit(context, info);
		if(!unit) return;
		auto excess = unit->getAvailableHealth() - health;
		if(excess > 0)
		{
			auto copy = unit->acquireState();
			copy->damage(excess);
			nativeEvents::changeUnit(context, *copy);
		}
		if(kind == SummonKind::ELEMENTALS && conjurer(mechanics))
		{
			Bonus bonus(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE_FLAT, BonusSource::SPELL_EFFECT, 2, BonusSourceID(mechanics->getSpellId()));
			bonus.turnsRemain = 1;
			nativeEvents::addBonus(context, *unit, bonus, false);
		}
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createTimedSummonEffect(const std::string & path)
{
	using namespace spells::effects::native;
	if(path == "spells/timedSummon") return std::make_shared<TimedSummon>(SummonKind::LIVING_ARMOR);
	if(path == "spells/summonTrolls") return std::make_shared<TimedSummon>(SummonKind::TROLLS);
	if(path == "spells/elementalConvergence") return std::make_shared<TimedSummon>(SummonKind::ELEMENTALS);
	return {};
}
}
