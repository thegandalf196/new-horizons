/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedPilotRules.h"
#include "MechanicsSourceAdapters.h"
#include "../json/JsonNode.h"
#include "../CSkillHandler.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../CCreatureHandler.h"
#include <algorithm>
#include <string_view>
namespace nativeRules
{
namespace
{
void require(bool value)
{
	if(!value) throw std::runtime_error("Invalid captured native pilot mechanics");
}
void fields(const JsonNode & row, std::initializer_list<std::string_view> allowed)
{
	require(row.isStruct());
	for(const auto & [key, value] : row.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end());
}
void equal(const JsonNode & value, std::string_view expected)
{
	require(value.isString() && value.String() == expected);
}
void integer(const JsonNode & value, int low, int high)
{
	require(value.getType() == JsonNode::JsonType::DATA_INTEGER && value.Integer() >= low && value.Integer() <= high);
}
const JsonNode & operation(const JsonNode & mechanics)
{
	fields(mechanics, {"version", "operations"});
	integer(mechanics["version"], 1, 1);
	require(mechanics["operations"].isVector() && mechanics["operations"].Vector().size() == 1);
	return mechanics["operations"].Vector().front();
}
const JsonNode * fragmentOperation(const JsonNode & mechanics)
{
	fields(mechanics, {"version", "operations"});
	if(mechanics.Struct().contains("version")) integer(mechanics["version"], 1, 1);
	if(!mechanics.Struct().contains("operations")) return nullptr;
	require(mechanics["operations"].isVector() && mechanics["operations"].Vector().size() == 1);
	return &mechanics["operations"].Vector().front();
}
void passive(const JsonNode & row, std::string_view type, std::string_view mode, bool family, bool adjacent)
{
	fields(row, {"operation", "source", "target", "stage", "lifetime", "type", "value", "valueMode",
		"family", "includeUpgrades", "aura"});
	equal(row["operation"], "bonus");
	equal(row["source"], family ? "heroSpecialty" : "skillRank");
	equal(row["target"], adjacent ? "adjacentArmy" : family ? "creatureFamily" : "army");
	equal(row["stage"], adjacent ? "spatialQuery" : "attachment");
	equal(row["lifetime"], "permanent");
	equal(row["type"], type);
	equal(row["valueMode"], mode);
	integer(row["value"], -100000, 100000);
	require(row.Struct().contains("family") == family && row.Struct().contains("includeUpgrades") == family);
	require(row.Struct().contains("aura") == adjacent);
	if(family) require(row["includeUpgrades"].isBool() && row["includeUpgrades"].Bool());
	if(adjacent) equal(row["aura"], "directMagicProtection");
}
struct CreatureSpec
{
	std::string_view hero, creature, coefficient, type, mode;
	bool adjacent;
};
constexpr std::array creatureSpecs{
	CreatureSpec{"core:fiona", "core:hellHound", "packPursuitSpeed", "STACKS_SPEED", "ADDITIVE_VALUE", false},
	CreatureSpec{"core:clancy", "core:unicorn", "resistanceAuraPoints", "SPELL_RESISTANCE_AURA", "ADDITIVE_VALUE", false},
	CreatureSpec{"core:pasis", "new-horizons:wisp", "prismaticScreenReductionPercent", "SPELL_DAMAGE_REDUCTION", "BASE_NUMBER", true}};
const JsonNode & creatures(const JsonNode & development)
{
	return development["additionalSpecialties"]["preparedCreatureSpecialties"];
}
void timedShape(const JsonNode & row, bool sanctuary)
{
	fields(row, {"operation", "source", "target", "stage", "lifetime"});
	equal(row["operation"], sanctuary ? "sanctuaryKeeper" : "creatureSuppression");
	equal(row["source"], "spellAddition");
	equal(row["target"], "spellRecipient");
	equal(row["stage"], "castApplication");
	equal(row["lifetime"], sanctuary ? "parentStatus" : "rounds");
}
}
static void validatePilotPerksImpl(const JsonNode & rules, bool supported, bool fragments)
{
	if(!rules.isStruct() || !rules["skills"].isStruct()) return;
	for(const auto & [skill, definition] : rules["skills"].Struct())
	{
		if(!definition.isStruct()) continue;
		if(definition["ranks"].isStruct())
		{
			for(const auto & [rank, row] : definition["ranks"].Struct())
				if(row.isStruct() && row.Struct().contains("mechanics"))
				{
					require(supported && (skill == "new-horizons:discipline" || skill == "new-horizons:luck"));
					const auto * payload = fragments ? fragmentOperation(row["mechanics"]) : &operation(row["mechanics"]);
					require(rank == "basic" || rank == "advanced" || rank == "expert");
					if(payload) passive(*payload, skill == "new-horizons:luck" ? "LUCK" : "MORALE", "BASE_NUMBER", false, false);
				}
		}
		if(definition["perks"].isVector())
		{
			for(const auto & row : definition["perks"].Vector())
				if(row.isStruct() && row.Struct().contains("mechanics"))
				{
					require(row["id"].isString());
					const auto & id = row["id"].String();
					const bool sanctuary = skill == "new-horizons:lightMagic" && id == "new-horizons:lightMagic.sanctuaryKeeper";
					require(supported && (sanctuary || (skill == "new-horizons:chaosMagic" && id == "new-horizons:chaosMagic.mindbreaker")));
					const auto * payload = fragments ? fragmentOperation(row["mechanics"]) : &operation(row["mechanics"]);
					if(payload) timedShape(*payload, sanctuary);
				}
		}
	}
}
static void validatePilotCreaturesImpl(const JsonNode & development, bool supported, bool fragments)
{
	if(!development.isStruct() || !development["additionalSpecialties"].isStruct()
		|| !development["additionalSpecialties"].Struct().contains("preparedCreatureSpecialties")) return;
	require(supported);
	const auto & block = creatures(development);
	fields(block, {"version", "heroes"});
	if(!fragments || block.Struct().contains("version")) integer(block["version"], 1, 1);
	if(fragments && !block.Struct().contains("heroes")) return;
	require(block["heroes"].isStruct() && (fragments || !block["heroes"].Struct().empty()));
	for(const auto & [hero, payload] : block["heroes"].Struct())
	{
		const auto spec = std::find_if(creatureSpecs.begin(), creatureSpecs.end(), [&](const auto & item) { return item.hero == hero; });
		require(spec != creatureSpecs.end());
		fields(payload, {"mechanics"});
		if(fragments && !payload.Struct().contains("mechanics")) continue;
		const auto * row = fragments ? fragmentOperation(payload["mechanics"]) : &operation(payload["mechanics"]);
		if(row)
		{
			passive(*row, spec->type, spec->mode, true, spec->adjacent);
			equal((*row)["family"], spec->creature);
		}
		const auto & legacy = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		require(legacy.isNull() || legacy.isStruct());
		require(!legacy.isStruct() || !legacy.Struct().contains(std::string(spec->coefficient)));
	}
}
void validatePilotPerks(const JsonNode & rules, bool supported)
{
	validatePilotPerksImpl(rules, supported, false);
}
void validatePilotCreatures(const JsonNode & development, bool supported)
{
	validatePilotCreaturesImpl(development, supported, false);
}
void validatePilotOverrides(const JsonNode & overrides, bool supported)
{
	validatePilotPerksImpl(overrides["heroes"]["newHorizonsPerks"], supported, true);
	const auto & development = overrides["heroes"]["newHorizons"];
	const auto & additional = development["additionalSpecialties"];
	if(additional.isStruct() && additional.Struct().contains("preparedCreatureSpecialties")
		&& additional.Struct().contains("version")) integer(additional["version"], 1, 1);
	validatePilotCreaturesImpl(development, supported, true);
}
void validatePilotRankRow(const std::string & key, const std::string & rank, const JsonNode & row, PreparedPilotPerks * collector)
{
	if(!row.Struct().contains("mechanics")) return;
	require(key == "new-horizons:discipline" || key == "new-horizons:luck");
	passive(operation(row["mechanics"]), key == "new-horizons:luck" ? "LUCK" : "MORALE", "BASE_NUMBER", false, false);
	if(!collector) return;
	const SecondarySkill skill(SecondarySkill::decode(key));
	require(skill.hasValue() && skill.toSkill());
	const int level = rank == "basic" ? 1 : rank == "advanced" ? 2 : 3;
	const auto prepared = prepareSourceReplacement(row, {SourceKind::SkillRank, BonusSourceID(skill), "", SpellID()}, {});
	for(auto & bonus : prepared->persistentReplacement())
	{
		bonus->description.appendTextID(skill.toSkill()->getNameTextID());
		bonus->description.appendRawString(" %+d");
		collector->ranks[{skill, level}].push_back(std::move(bonus));
	}
}
void validatePilotPerkRow(const std::string & key, const JsonNode & row, PreparedPilotPerks * collector)
{
	if(!row.Struct().contains("mechanics")) return;
	const bool sanctuary = key == "new-horizons:lightMagic" && row["id"].String() == "new-horizons:lightMagic.sanctuaryKeeper";
	require(sanctuary || (key == "new-horizons:chaosMagic" && row["id"].String() == "new-horizons:chaosMagic.mindbreaker"));
	timedShape(operation(row["mechanics"]), sanctuary);
	if(!collector) return;
	const SpellID spell = sanctuary ? SpellID(SpellID::decode("new-horizons:sanctuary")) : SpellID(SpellID::FORGETFULNESS);
	collector->timed[sanctuary ? 0 : 1] = prepareTimedSource(row, spell,
		sanctuary ? TimedSourceSlot::Sanctuary : TimedSourceSlot::Forgetfulness);
}
std::shared_ptr<const PreparedPilotPerks> preparePilotPerks(const JsonNode & rules)
{
	return newHorizonsHeroes::preparePilotPerkRules(rules);
}
std::optional<std::vector<std::shared_ptr<Bonus>>> PreparedPilotPerks::rank(SecondarySkill skill, int level) const
{
	const auto found = ranks.find({skill, level});
	if(found == ranks.end()) return {};
	std::vector<std::shared_ptr<Bonus>> result;
	for(const auto & bonus : found->second) result.push_back(std::make_shared<Bonus>(*bonus));
	return result;
}
const PreparedMechanics * PreparedPilotPerks::addition(TimedSourceSlot slot) const
{
	require(slot == TimedSourceSlot::Sanctuary || slot == TimedSourceSlot::Forgetfulness);
	const auto & result = timed[slot == TimedSourceSlot::Sanctuary ? 0 : 1];
	return result ? &*result : nullptr;
}
bool hasPreparedCreatureRow(const JsonNode & development, std::string_view hero)
{
	const auto & block = creatures(development);
	return block.isStruct() && block["heroes"].isStruct() && block["heroes"].Struct().contains(std::string(hero));
}
std::shared_ptr<const PreparedPilotCreature> preparePilotCreature(const JsonNode & development, HeroTypeID hero)
{
	validatePilotCreatures(development);
	if(!hero.hasValue()) return {};
	const auto key = HeroTypeID::encode(hero.getNum());
	if(!hasPreparedCreatureRow(development, key)) return {};
	const auto & row = creatures(development)["heroes"][key];
	const auto resolver = [](const std::string & name) -> const CCreature &
	{
		const CreatureID id(CreatureID::decode(name));
		require(id.hasValue() && id.toCreature());
		return *id.toCreature();
	};
	const auto prepared = prepareSourceReplacement(row, {SourceKind::HeroSpecialty, BonusSourceID(hero),
		"new-horizons:ordinary-creature-specialty:" + key, SpellID()}, resolver);
	return std::make_shared<PreparedPilotCreature>(*prepared->persistentReplacement().front());
}
}
