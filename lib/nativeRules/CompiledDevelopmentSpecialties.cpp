/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledDevelopmentRules.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
namespace nativeRules
{
void CompiledDevelopmentRules::compileSpecialties(const JsonNode & rules)
{
	ancientOaths = newHorizonsHeroes::ancientOathsEnabled(rules);
	creatureLine = newHorizonsHeroes::creatureLineSpecialtyRules(rules);
	damageSpell = newHorizonsHeroes::damageSpellSpecialtyRules(rules);
	nonDamageSpell = newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules);
	skillSpecialty = newHorizonsHeroes::skillSpecialtyRules(rules);
	druid = newHorizonsHeroes::druidSpecialtyRules(rules);
	fortuneChild = newHorizonsHeroes::fortuneChildSpecialtyRules(rules);
	arcanePreparation = newHorizonsHeroes::arcanePreparationSpecialtyRules(rules);
	bottomlessMind = newHorizonsHeroes::bottomlessMindSpecialtyRules(rules);
	masterSpellward = newHorizonsHeroes::masterSpellwardSpecialtyRules(rules);
	individual = newHorizonsHeroes::individualSpecialtyRules(rules);
	manaFurnace = newHorizonsHeroes::manaFurnaceRules(rules);
	fieldPhysician = newHorizonsHeroes::fieldPhysicianRules(rules);
	for(size_t i = 0; i < schoolSpellcraft.size(); ++i)
		schoolSpellcraft[i] = newHorizonsHeroes::schoolSpellcraftRules(rules, newHorizonsHeroes::SchoolSpellcraftKind(i));
	for(size_t i = 0; i < recovery.size(); ++i)
		recovery[i] = newHorizonsHeroes::recoverySpecialtyRules(rules, newHorizonsHeroes::RecoverySpecialtyKind(i));
	for(size_t i = 0; i < learning.size(); ++i)
		learning[i] = newHorizonsHeroes::learningSpecialtyRules(rules, newHorizonsHeroes::LearningSpecialtyKind(i));
	const auto & ordinary = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	if(ordinary.isStruct())
	{
		for(const auto & [key, value] : ordinary.Struct())
		{
			ordinaryFields.insert(key);
			if(value.isNumber()) ordinaryValues.emplace(key, static_cast<int>(value.Integer()));
		}
		for(const auto & row : newHorizonsHeroes::detail::ordinaryCreatureSpecialtyRegistry())
		{
			const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), HeroTypeID::entityType(), row.hero, true);
			if(id && *id >= 0)
			{
				ordinaryRows.emplace(HeroTypeID(*id), &row);
				ordinaryHeroValues.emplace(HeroTypeID(*id), ordinaryValue(row.coefficient));
				const CreatureID family(CreatureID::decode(row.creature));
				if(family.hasValue()) ordinaryCreatures.emplace(HeroTypeID(*id), family);
			}
		}
	}
	defaultCreatureTable = rules.Struct().contains("defaultCreatureLineReplacements");
	capturedCreatureTarget = rules.Struct().contains("creatureLineSpecialtyTarget");
	if(defaultCreatureTable)
		for(const auto & [key, row] : rules["defaultCreatureLineReplacements"].Struct())
		{
			const HeroTypeID hero(HeroTypeID::decode(key));
			if(hero.hasValue()) defaultCreatures.emplace(hero, *newHorizonsHeroes::defaultCreatureLineTarget(rules, hero));
		}
	validatePilotCreatures(rules);
	const auto & prepared = rules["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"];
	if(prepared.isStruct())
		for(const auto & [key, row] : prepared.Struct())
		{
			const HeroTypeID id(HeroTypeID::decode(key));
			if(!id.hasValue()) throw std::runtime_error("Missing admitted specialty hero");
			creatureMechanics.emplace(id, prepareAdmittedPilotCreature(rules, id));
		}
}
const newHorizonsHeroes::detail::OrdinaryCreatureSpecialty * CompiledDevelopmentRules::ordinary(HeroTypeID id, bool prepared) const
{
	const auto found = ordinaryRows.find(id);
	return found == ordinaryRows.end() || (!prepared && !ordinaryFields.contains(found->second->coefficient)) ? nullptr : found->second;
}
std::optional<CreatureID> CompiledDevelopmentRules::ordinaryCreature(HeroTypeID id) const
{
	const auto found = ordinaryCreatures.find(id);
	return found == ordinaryCreatures.end() ? std::nullopt : std::optional(found->second);
}
std::optional<CreatureID> CompiledDevelopmentRules::defaultCreature(HeroTypeID hero) const
{
	const auto found = defaultCreatures.find(hero);
	return found == defaultCreatures.end() ? std::nullopt : std::optional(found->second);
}
int CompiledDevelopmentRules::ordinaryValue(std::string_view key) const
{
	const auto found = ordinaryValues.find(key);
	return found == ordinaryValues.end() ? 0 : found->second;
}
int CompiledDevelopmentRules::ordinaryValue(HeroTypeID hero) const
{
	const auto found = ordinaryHeroValues.find(hero);
	return found == ordinaryHeroValues.end() ? 0 : found->second;
}
std::shared_ptr<const PreparedPilotCreature> CompiledDevelopmentRules::preparedCreature(HeroTypeID hero) const
{
	const auto found = creatureMechanics.find(hero);
	return found == creatureMechanics.end() ? nullptr : found->second;
}
}
