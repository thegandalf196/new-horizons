/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Heal.h"
#include "../../ISpellMechanics.h"
#include "../../../battle/CUnitState.h"
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../GameLibrary.h"
#include "../../../CCreatureHandler.h"
#include "../../../texts/MetaString.h"

namespace spells::effects::native
{
namespace
{
bool phantomCaster(const Mechanics * mechanics)
{
	const auto * caster = mechanics->getUnitCaster();
	return caster && caster->getPhantomInitialIntegrity() > 0;
}
bool ineligible(const Mechanics * mechanics, const battle::Unit * unit)
{
	return mechanics->isNewHorizonsResurrection() && (unit->isSummoned() || unit->isClone() || unit->getPhantomInitialIntegrity() > 0);
}
}

EHealLevel Heal::configuredLevel() const
{
	const auto & value = parameters["healLevel"].String();
	return value == "resurrect" ? EHealLevel::RESURRECT : value == "overHeal" ? EHealLevel::OVERHEAL : EHealLevel::HEAL;
}

EHealLevel Heal::level(const Mechanics * mechanics) const
{
	return phantomCaster(mechanics) ? EHealLevel::HEAL : configuredLevel();
}

EHealPower Heal::power(const Mechanics * mechanics) const
{
	if(mechanics->isNewHorizonsResurrection() || (phantomCaster(mechanics) && configuredLevel() != EHealLevel::HEAL)) return EHealPower::PERMANENT;
	return parameters["healPower"].String() == "oneBattle" ? EHealPower::ONE_BATTLE : EHealPower::PERMANENT;
}

int64_t Heal::minimumFullUnits(const Mechanics * mechanics) const
{
	if(mechanics->isNewHorizonsResurrection() || (phantomCaster(mechanics) && configuredLevel() != EHealLevel::HEAL)) return 0;
	return parameters["minFullUnits"].Integer();
}

int64_t Heal::amount(const Mechanics * mechanics, const battle::Unit * unit) const
{
	const int64_t amount = mechanics->applySpellBonus(mechanics->getRecipientEffectValue(unit), unit);
	const auto * hero = mechanics->getHeroCaster();
	if(!mechanics->isNewHorizonsResurrection() || !hero
		|| !hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.miracleWorker")) return amount;
	const auto wounds = unit->healingCapacity(amount, EHealLevel::HEAL, EHealPower::PERMANENT);
	const auto casualties = std::max<int64_t>(0, amount - wounds);
	return wounds + casualties + casualties / 4;
}

bool Heal::isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const
{
	if(ineligible(mechanics, unit)) return false;
	const auto healingLevel = level(mechanics);
	if(!unit->isValidTarget(healingLevel != EHealLevel::HEAL)) return false;
	int64_t injuries = unit->getTotalHealth() - unit->getAvailableHealth();
	if(healingLevel == EHealLevel::RESURRECT) injuries -= static_cast<int64_t>(unit->getUnusableRemains()) * unit->getMaxHealth();
	if(injuries <= 0) return false;
	const auto minimum = minimumFullUnits(mechanics);
	if(minimum > 0 && std::min(amount(mechanics, unit), injuries) < minimum * unit->getMaxHealth()) return false;
	if(unit->isDead())
		for(const auto & hex : unit->getHexes())
			if(!mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * other)
			{
				return other != unit && other->isValidTarget(false) && other->coversPos(hex);
			}).empty()) return false;
	return true;
}

SpellEffectValue Heal::getHealthChange(const Mechanics * mechanics, const Target & target) const
{
	SpellEffectValue result;
	const bool phantom = phantomCaster(mechanics);
	const auto healingLevel = level(mechanics);
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!unit || (phantom && unit->isDead()) || ineligible(mechanics, unit)) continue;
		auto copy = unit->acquireState();
		auto healing = amount(mechanics, unit);
		const auto healed = copy->heal(healing, healingLevel, power(mechanics));
		result.hpDelta += healed.healedHealthPoints;
		result.unitsDelta += healed.resurrectedCount;
		result.unitType = unit->creatureId().toEntity(LIBRARY);
	}
	return result;
}

void Heal::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const bool unitCaster = mechanics->getHeroCaster() == nullptr;
	const bool phantom = phantomCaster(mechanics);
	const auto healingLevel = level(mechanics);
	const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!unit || (phantom && unit->isDead()) || ineligible(mechanics, unit)) continue;
		const auto [healed, resurrected] = nativeEvents::heal(context, *unit, amount(mechanics, unit), healingLevel, power(mechanics));
		MetaString message;
		if(resurrected > 0)
		{
			message.appendTextID(resurrected == 1 ? "core.genrltxt.117" : "core.genrltxt.116");
			message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
			message.replaceNumber(resurrected);
		}
		else if(healed > 0 && unitCaster)
		{
			message.appendTextID("core.genrltxt.414");
			message.replaceTextID(nativeEvents::creatureName(mechanics->getUnitCaster(), 1));
			message.replaceTextID(nativeEvents::creatureName(unit, 1));
			message.replaceNumber(healed);
		}
		else continue;
		nativeEvents::log(context, message);
	}
}
}
