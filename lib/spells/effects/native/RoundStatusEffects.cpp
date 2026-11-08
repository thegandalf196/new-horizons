/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../RecipientSpellEffectPreview.h"
#include "../../NewHorizonsTimedMagnitudes.h"
#include "../../NewHorizonsMetamagicEffects.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../scripting/ScriptTypeDescription.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
namespace
{
enum class Kind { HEX, PLAGUE, SHADOW, FRAILTY, ATTACH, FOCUS };

class RoundStatus final : public UnitEffect
{
	Kind kind;
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		if(kind != Kind::FOCUS) return UnitEffect::isValidTarget(mechanics, unit);
		return unit && unit->alive() && unit->isValidTarget(false) && unit->isShooter()
			&& mechanics->battle()->playerToSide(mechanics->battle()->battleGetOwner(unit)) == mechanics->getCasterSide();
	}
	void frailty(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit & unit, const std::string & key) const
	{
		const auto old = status::previous(unit, BonusType::PRIMARY_SKILL, key, "defence");
		const auto plan = mechanics->getRecipientFrailtyPlan(&unit);
		if(!old.empty()) nativeEvents::removeBonuses(context, unit, old);
		auto record = status::bonus("PRIMARY_SKILL", -plan.defenseLoss, key, 0, "defence", true);
		record["duration"].String() = "ONE_BATTLE";
		record["addInfo"].Integer() = plan.totalBasisPoints;
		status::add(context, unit, std::move(record));
	}
public:
	explicit RoundStatus(Kind kind) : kind(kind) {}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override;
};

struct RoundPlan
{
	std::string event;
	int64_t magnitude = 0;
	int duration = 0;
	bool debuff = false;
	JsonNode extra;
};

RoundPlan plan(const Mechanics * mechanics, Kind kind, const JsonNode & parameters)
{
	RoundPlan result;
	if(kind == Kind::HEX)
	{
		result.event = "core:hexOfPain";
		result.magnitude = mechanics->getHexOfPainFlatDamage();
		result.duration = newHorizonsMagic::scriptedRoundStatusDuration(*mechanics).value_or(0);
		result.debuff = true;
		result.extra["casterSide"].Integer() = static_cast<int>(mechanics->getCasterSide());
	}
	else if(kind == Kind::PLAGUE)
	{
		result.event = "core:plagueStatus";
		result.magnitude = mechanics->getPlagueTickDamage();
		result.duration = mechanics->adjustEffectDuration(3);
		result.debuff = true;
		const auto units = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit) { return unit->unitSide() == mechanics->getCasterSide(); });
		result.extra["sourceUnitId"].Integer() = units.empty() ? -1 : static_cast<int64_t>(units.front()->unitId());
	}
	else if(kind == Kind::SHADOW)
	{
		result.event = "core:shadowGift";
		result.magnitude = mechanics->getShadowGiftDamageBonusBasisPoints();
		result.duration = 3 + mechanics->getExtendSpellBonusRounds();
		result.extra["casterSide"].Integer() = static_cast<int>(mechanics->getCasterSide());
	}
	else if(kind == Kind::FOCUS)
	{
		result.event = "core:focusMagic";
		const auto * hero = mechanics->getHeroCaster();
		result.extra["beneficiarySide"].Integer() = static_cast<int>(mechanics->getCasterSide());
		result.extra["arcaneAcquisition"].Bool() = mechanics->isMetamagicFollowup() && hero
			&& hero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.arcaneAcquisition");
	}
	else if(kind == Kind::ATTACH)
	{
		result.event = parameters["eventScript"].String();
		result.magnitude = static_cast<int64_t>(parameters["eventValue"].Float());
		result.extra = parameters["eventParameters"];
	}
	return result;
}

void RoundStatus::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const auto key = mechanics->getSpell()->getJsonKey();
	if(kind == Kind::SHADOW)
	{
		const auto percent = mechanics->getShadowGiftSacrificePercent();
		if((percent != 10 && percent != 20 && percent != 30) || mechanics->getShadowGiftSacrificeCostBasisPoints() <= 0
			|| mechanics->getShadowGiftDamageBonusBasisPoints() <= 0) return;
	}
	const auto prepared = plan(mechanics, kind, parameters);
	const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!unit || !unit->alive()) continue;
		if((kind == Kind::HEX || kind == Kind::SHADOW) && !mechanics->ownerMatches(unit)) continue;
		if(kind == Kind::FOCUS && !isValidTarget(mechanics, unit)) continue;
		if(kind != Kind::ATTACH && !isReceptive(mechanics, unit)) continue;
		if(kind == Kind::FRAILTY) { frailty(context, mechanics, *unit, key); continue; }
		if(kind != Kind::ATTACH) status::refresh(context, *unit, BonusType::COMBAT_EVENT_TRIGGER, key, prepared.event);
		const auto magnitude = kind == Kind::ATTACH ? prepared.magnitude : kind == Kind::FOCUS
			? mechanics->getRecipientArcaneBreachMarkBasisPoints(unit) : kind == Kind::PLAGUE
			? newHorizonsMetamagic::outgoingDamageMagnitude(prepared.magnitude, mechanics->getMetamagicCastProfile(), unit)
			: mechanics->adjustMetamagicMagnitude(prepared.magnitude, unit);
		const int duration = kind == Kind::FOCUS ? mechanics->adjustEffectDuration(3) : kind == Kind::ATTACH
			? mechanics->getEffectDuration() : prepared.duration;
		auto record = status::bonus("COMBAT_EVENT_TRIGGER", magnitude, key, duration, prepared.event, prepared.debuff);
		record["addInfo"] = prepared.extra;
		if(kind == Kind::HEX || kind == Kind::SHADOW)
			record["addInfo"]["mdrPenetration"] = mechanics->getCapturedMdrPenetration();
		if(kind == Kind::HEX) record["addInfo"]["damageSharePercent"].Integer() = mechanics->getRecipientHexOfPainDamageSharePercent(unit);
		if(kind == Kind::PLAGUE)
		{
			record["addInfo"]["mdrPenetration"] = mechanics->getCapturedMdrPenetration();
			if(const auto * cast = mechanics->getMetamagicCastProfile(); cast && cast->factionSpellDamageBonus > 0)
				record["addInfo"]["mdrPenetration"]["factionSpellDamageCapturedInMagnitude"].Bool() = true;
			record["addInfo"]["casterSide"].Integer() = static_cast<int>(mechanics->getCasterSide());
			record["addInfo"]["spreadAttempts"].Integer() = 0;
			record["addInfo"]["propagationLimit"].Integer() = mechanics->getPlaguePropagationLimit();
			record["addInfo"]["lastProcessedRound"].Integer() = mechanics->battle()->battleGetRound() - 1;
			record["description"].String() = "New Horizons: Plague";
		}
		status::add(context, *unit, std::move(record));
	}
}
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createStatusEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	const auto & path = description.sourcePath;
	if(path == "spells/hexOfPain") return std::make_shared<RoundStatus>(Kind::HEX);
	if(path == "spells/plague") return std::make_shared<RoundStatus>(Kind::PLAGUE);
	if(path == "spells/shadowGift") return std::make_shared<RoundStatus>(Kind::SHADOW);
	if(path == "spells/frailty") return std::make_shared<RoundStatus>(Kind::FRAILTY);
	if(path == "spells/attachCombatScript") return std::make_shared<RoundStatus>(Kind::ATTACH);
	if(path == "spells/focusMagic") return std::make_shared<RoundStatus>(Kind::FOCUS);
	return {};
}
}
