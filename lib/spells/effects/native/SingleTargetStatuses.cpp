/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsShadowMagnitudes.h"
#include "../../RecipientSpellEffectPreview.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../scripting/ScriptTypeDescription.h"
#include "../../../texts/MetaString.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
namespace
{
class SingleStatus final : public UnitEffect
{
	bool doom;
	bool enabled(const Mechanics * mechanics) const
	{
		return mechanics->usesNewHorizonsMagicV3() && mechanics->getSpell()->getJsonKey()
			== (doom ? "new-horizons:doom" : "new-horizons:vampirism");
	}
	bool enemy(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		return unit && unit->alive() && unit->isValidTarget(false) && unit->unitSide() != mechanics->getCasterSide();
	}
	bool recipient(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		return doom ? enemy(mechanics, unit) && isReceptive(mechanics, unit)
			: unit && unit->alive() && unit->isValidTarget(false) && mechanics->ownerMatches(unit) && isReceptive(mechanics, unit);
	}
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		return doom ? enemy(mechanics, unit) : UnitEffect::isValidTarget(mechanics, unit);
	}
	void applyDoom(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit) const;
	void applyVampirism(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit) const;
public:
	explicit SingleStatus(bool doom) : doom(doom) {}
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(enabled(mechanics) && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit) { return recipient(mechanics, unit); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return enabled(mechanics) && target.size() == 1 && recipient(mechanics, target.front().unitValue) ? true : status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target &) const override
	{
		return enabled(mechanics) && aim.size() == 1 && aim.front().unitValue ? aim : Target{};
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!enabled(mechanics) || target.size() != 1 || !recipient(mechanics, target.front().unitValue)) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		if(doom) applyDoom(context, mechanics, *target.front().unitValue);
		else applyVampirism(context, mechanics, *target.front().unitValue);
	}
};

void SingleStatus::applyDoom(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit) const
{
	const std::string key = mechanics->getSpell()->getJsonKey();
	const int penalty = newHorizonsMagic::doomCripplingPenaltyPercent(mechanics->getEffectPower(),
		mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent());
	const int duration = mechanics->adjustEffectDuration(3);
	bool refreshed = false;
	for(const auto type : {BonusType::GENERAL_ATTACK_REDUCTION, BonusType::STACKS_SPEED, BonusType::STACKS_INITIATIVE, BonusType::MORALE})
	{
		const auto old = status::previous(unit, type, key);
		if(old.empty()) continue;
		refreshed = true;
		nativeEvents::removeBonuses(context, unit, old);
	}
	const auto add = [&](const char * type, int value, const char * valueType)
	{
		auto record = status::bonus(type, value, key, duration, {}, true);
		record["valueType"].String() = valueType;
		record["description"].String() = "New Horizons: Doom";
		status::add(context, unit, std::move(record));
	};
	add("GENERAL_ATTACK_REDUCTION", penalty, "ADDITIVE_VALUE");
	add("STACKS_SPEED", -penalty, "PERCENT_TO_ALL");
	if(!unit.getBonusesOfType(BonusType::STACKS_INITIATIVE_BASE)->empty()) add("STACKS_INITIATIVE", -penalty, "ADDITIVE_VALUE");
	add("MORALE", -3, "ADDITIVE_VALUE");
	if(context.server.describeChanges())
	{
		MetaString message;
		message.appendTextID(refreshed ? "new-horizons.combat.doom.refreshed" : "new-horizons.combat.doom.applied");
		message.replaceTextID(nativeEvents::creatureName(&unit, unit.getCount()));
		message.replaceNumber(duration);
		message.replaceNumber(penalty);
		nativeEvents::log(context, message);
	}
}

void SingleStatus::applyVampirism(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit) const
{
	const auto key = mechanics->getSpell()->getJsonKey();
	status::refresh(context, unit, BonusType::COMBAT_EVENT_TRIGGER, key, "core:vampirism");
	const auto * hero = mechanics->getHeroCaster();
	const bool nightFeeder = hero && hero->hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.nightFeeder");
	const int magnitude = newHorizonsMagic::vampirismHealBasisPoints(std::max(0, mechanics->getEffectPower()),
		mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent(), nightFeeder);
	auto record = status::bonus("COMBAT_EVENT_TRIGGER", mechanics->adjustMetamagicMagnitude(magnitude, &unit), key,
		3 + mechanics->getExtendSpellBonusRounds(), "core:vampirism");
	record["description"].String() = "New Horizons: Vampirism";
	status::add(context, unit, std::move(record));
}

class SoulChain final : public UnitEffect
{
	bool enemy(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		return unit && unit->alive() && unit->isValidTarget(false) && unit->unitSide() != mechanics->getCasterSide()
			&& !unit->isInvincible() && isReceptive(mechanics, unit);
	}
	bool valid(const Mechanics * mechanics, const Target & target) const
	{
		if(!mechanics->usesNewHorizonsMagicV3() || target.empty() || target.size() > 3) return false;
		std::unordered_set<uint32_t> seen;
		for(const auto & destination : target)
			if(!enemy(mechanics, destination.unitValue) || !seen.insert(destination.unitValue->unitId()).second) return false;
		return true;
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(mechanics->usesNewHorizonsMagicV3() && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit) { return enemy(mechanics, unit); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		return valid(mechanics, target) ? true : status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target &) const override
	{
		return mechanics->usesNewHorizonsMagicV3() && !aim.empty() && aim.size() <= 3 ? aim : Target{};
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!valid(mechanics, target)) return;
		const auto * primary = target.front().unitValue;
		const auto key = mechanics->getSpell()->getJsonKey();
		const int duration = newHorizonsMagic::scriptedRoundStatusDuration(*mechanics).value_or(0);
		server->recordResolvedSpellRecipient(primary->unitId());
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(size_t i = 1; i < target.size(); ++i)
		{
			const auto * unit = target[i].unitValue;
			status::refresh(context, *unit, BonusType::COMBAT_EVENT_TRIGGER, key, "core:soulChainStatus");
			auto record = status::bonus("COMBAT_EVENT_TRIGGER", mechanics->getRecipientSoulChainEchoBasisPoints(unit), key, duration, "core:soulChainStatus", true);
			record["addInfo"]["mdrPenetration"] = mechanics->getCapturedMdrPenetration();
			record["addInfo"]["damageMultiplierPercent"].Integer() = mechanics->getHeroLuckDamageMultiplierPercent();
			record["addInfo"]["primaryUnitId"].Integer() = primary->unitId();
			record["addInfo"]["casterSide"].Integer() = static_cast<int>(mechanics->getCasterSide());
			record["description"].String() = "New Horizons: Soul Chain";
			status::add(context, *unit, std::move(record));
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createSingleTargetStatusEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/doom") return std::make_shared<SingleStatus>(true);
	if(description.sourcePath == "spells/vampirism") return std::make_shared<SingleStatus>(false);
	if(description.sourcePath == "spells/soulChain") return std::make_shared<SoulChain>();
	return {};
}
}
