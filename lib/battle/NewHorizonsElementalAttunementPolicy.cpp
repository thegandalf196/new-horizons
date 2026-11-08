/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsElementalAttunement.h"
#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../spells/CSpell.h"
#include <stdexcept>

namespace newHorizonsElementalAttunement
{
void Profile::validate() const
{
	if(rank < 0 || rank > 3 || stoneboundBasisPoints < 0 || stoneboundBasisPoints > 1500
		|| stoneboundBasisPoints % 25 != 0 || (!stoneboundMagic && stoneboundBasisPoints != 0)
		|| (rank == 0 && (magmaWeapons || stormBolts || elementalConduit || waterVeil
			|| greaterEssence || stoneboundMagic || primalVigilance || elementalCataclysm))
		|| (rank < 2 && (greaterEssence || stoneboundMagic || primalVigilance || elementalCataclysm)))
		throw std::runtime_error("Invalid captured Elemental Attunement profile");
}

bool usesRules(const JsonNode & rules)
{
	if(!rules.isStruct()) return false;
	const auto found = rules.Struct().find(POLICY_KEY);
	return found != rules.Struct().end() && found->second.isStruct()
		&& found->second["version"].getType() == JsonNode::JsonType::DATA_INTEGER && found->second["version"].Integer() == 1;
}

bool usesRules(const CGHeroInstance * hero)
{
	return hero && usesRules(hero->getPerkState().rules);
}

void validatePolicy(const JsonNode & rules)
{
	if(!rules.isStruct()) return;
	if(!rules.Struct().contains(POLICY_KEY))
	{
		const auto & skills = rules["skills"];
		if(skills.isStruct() && skills.Struct().size() == newHorizonsHeroes::PERK_CANONICAL_SKILL_COUNT)
			throw std::runtime_error("A complete current perk registry requires Elemental Attunement policy version 1");
		return;
	}
	if(!usesRules(rules) || rules[POLICY_KEY].Struct().size() != 1)
		throw std::runtime_error("Unsupported captured Elemental Attunement policy");
}

void validatePolicySerialization(const JsonNode & rules, bool supported)
{
	validatePolicy(rules);
	if(usesRules(rules) && !supported)
		throw std::runtime_error("Cannot discard captured Elemental Attunement policy");
}

namespace
{
uint8_t tagMask(const JsonNode & tag)
{
	if(tag.isNull()) return 0;
	if(!tag.isString()) throw std::runtime_error("Elemental spell tag must be a string");
	const auto & name = tag.String();
	if(name == "air") return 1;
	if(name == "fire") return 2;
	if(name == "water") return 4;
	if(name == "earth") return 8;
	throw std::runtime_error("Unknown elemental spell tag");
}

const JsonNode & spellRow(const JsonNode & rules, SpellID spell)
{
	static const JsonNode absent;
	const auto * definition = spell.hasValue() ? spell.toSpell() : nullptr;
	return definition ? rules["spells"][definition->getJsonKey()] : absent;
}
}

void validateSpellMetadata(const JsonNode & rules)
{
	if(!rules["spells"].isStruct()) return;
	for(const auto & [id, row] : rules["spells"].Struct())
	{
		if(!row.isStruct()) throw std::runtime_error("Elemental spell definition must be an object");
		if(row.Struct().contains("elementalTag"))
		{
			if(row["elementalTag"].isNull()) throw std::runtime_error("Elemental spell tag cannot be null");
			(void)tagMask(row["elementalTag"]);
		}
		const auto area = row.Struct().find("elementalAreaDamage");
		if(area != row.Struct().end() && (!area->second.isBool()
			|| (area->second.Bool() && tagMask(row["elementalTag"]) == 0)))
			throw std::runtime_error("Elemental area damage requires a boolean and explicit elemental tag: " + id);
	}
}

bool hasSpellMetadata(const JsonNode & rules)
{
	if(!rules["spells"].isStruct()) return false;
	for(const auto & [id, row] : rules["spells"].Struct())
		if(row.Struct().contains("elementalTag") || row.Struct().contains("elementalAreaDamage")) return true;
	return false;
}

void validateSpellMetadataSerialization(const JsonNode & rules, bool supported)
{
	validateSpellMetadata(rules);
	if(!supported && hasSpellMetadata(rules))
		throw std::runtime_error("Cannot discard captured elemental spell metadata");
}

uint8_t spellElementMask(const JsonNode & rules, SpellID spell)
{
	return tagMask(spellRow(rules, spell)["elementalTag"]);
}

bool elementalAreaDamage(const JsonNode & rules, SpellID spell)
{
	const auto & row = spellRow(rules, spell);
	return tagMask(row["elementalTag"]) != 0 && row["elementalAreaDamage"].isBool()
		&& row["elementalAreaDamage"].Bool();
}
}
