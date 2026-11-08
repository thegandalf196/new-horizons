/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeScriptFactory.h"
#include "../combatScripts/NativeCombatEvents.h"
#include "../combatScripts/NativeDamageCalculator.h"
#include "../spells/effects/native/NativeSpellFactory.h"

namespace
{
void requirePatches(const ScriptTypeDescription & description, std::initializer_list<std::string_view> expected)
{
	if(description.patches.size() != expected.size())
		throw std::runtime_error("Unsupported native rule patch chain for " + description.scriptId);
	size_t index = 0;
	for(const auto path : expected)
	{
		const auto & actual = description.patches[index++];
		if(actual.first != "core" || actual.second != path)
			throw std::runtime_error("Unsupported native rule patch chain for " + description.scriptId);
	}
}
}

void NativeScriptFactory::initialize(const ScriptTypeDescription & description)
{
	if(description.kind == ScriptKind::DAMAGE_CALCULATOR)
	{
		if(description.sourcePath != "damage/damageCalculator")
			throw std::runtime_error("Unknown native damage rules: " + description.sourcePath);
		requirePatches(description, {"damage/physicalProtection", "damage/siegeWeapon", "damage/magicElemental",
			"damage/psychicElemental", "damage/vulnerableFromBack", "damage/hatesTrait", "damage/revenge",
			"damage/enemyAttackReduction", "damage/damageReceivedCap"});
	}
	else if(description.kind == ScriptKind::COMBAT_EVENT)
	{
		if(description.sourcePath == "combat/deathStare")
			requirePatches(description, {"combat/deathStareCommander"});
		else requirePatches(description, {});
		if(!nativeEvents::create(description.sourcePath))
			throw std::runtime_error("Unknown native combat event: " + description.sourcePath);
	}
	else if(description.kind == ScriptKind::SPELL_EFFECT)
	{
		if(!nativeSpells::create(description))
			throw std::runtime_error("Unknown native spell effect: " + description.sourcePath);
	}
	else throw std::runtime_error("Unknown native rule kind for " + description.scriptId);
	descriptions.insert_or_assign(description.scriptId, description);
}

std::shared_ptr<ICombatEventScript> NativeScriptFactory::createCombatEventScript(const std::string & scriptId) const
{
	return nativeEvents::create(descriptions.at(scriptId).sourcePath);
}

std::shared_ptr<IDamageCalculatorScript> NativeScriptFactory::createDamageCalculatorScript(const std::string & scriptId) const
{
	const auto & description = descriptions.at(scriptId);
	if(description.kind != ScriptKind::DAMAGE_CALCULATOR)
		throw std::runtime_error("Rule is not a damage calculator: " + scriptId);
	return std::make_shared<NativeDamageCalculator>();
}

std::shared_ptr<spells::effects::Effect> NativeScriptFactory::createSpellEffect(const std::string & scriptId) const
{
	return nativeSpells::create(descriptions.at(scriptId));
}
