/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MechanicsAuthoring.h"
#include "NativeRulePilots.h"
#include "../CCreatureHandler.h"
#include "../json/JsonNode.h"
#include "../bonuses/Limiters.h"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
namespace nativeRules
{
namespace
{
void require(bool valid)
{
	if(!valid) throw std::invalid_argument("Unsupported native mechanics version1 record");
}
void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct());
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end());
}
std::string text(const JsonNode & node)
{
	require(node.isString());
	return node.String();
}
int integer(const JsonNode & node, int lower, int upper)
{
	require(node.getType() == JsonNode::JsonType::DATA_INTEGER);
	require(node.Integer() >= lower && node.Integer() <= upper);
	return static_cast<int>(node.Integer());
}
template<typename T>
T named(const JsonNode & value, std::initializer_list<std::pair<std::string_view, T>> names)
{
	const auto name = text(value);
	for(const auto & [key, result] : names)
		if(key == name) return result;
	throw std::invalid_argument("Unknown native mechanics enum value");
}
BonusRuleContract contract(const JsonNode & row, SourceKind source)
{
	BonusRuleContract result;
	result.source = named<SourceKind>(row["source"], {
		std::pair<std::string_view, SourceKind>{"skillRank", SourceKind::SkillRank},
		{"heroSpecialty", SourceKind::HeroSpecialty}, {"spellAddition", SourceKind::SpellAddition}});
	require(result.source == source);
	result.target = named<TargetDomain>(row["target"], {
		std::pair<std::string_view, TargetDomain>{"army", TargetDomain::Army},
		{"creatureFamily", TargetDomain::CreatureFamily}, {"adjacentArmy", TargetDomain::AdjacentArmy},
		{"spellRecipient", TargetDomain::SpellRecipient}});
	result.stage = named<EvaluationStage>(row["stage"], {
		std::pair<std::string_view, EvaluationStage>{"attachment", EvaluationStage::Attachment},
		{"spatialQuery", EvaluationStage::SpatialQuery}, {"castApplication", EvaluationStage::CastApplication}});
	result.lifetime = named<LifetimePolicy>(row["lifetime"], {
		std::pair<std::string_view, LifetimePolicy>{"permanent", LifetimePolicy::Permanent},
		{"rounds", LifetimePolicy::Rounds}, {"parentStatus", LifetimePolicy::ParentStatus}});
	return result;
}
PreparedBonusRule passive(const JsonNode & row, const BonusRuleContract & policy,
	const ResolvedAuthoringSource & source, const FamilyResolver & resolver)
{
	fields(row, {"operation", "source", "target", "stage", "lifetime", "type", "value", "valueMode",
		"primarySkill", "family", "includeUpgrades", "aura"});
	require(policy.source != SourceKind::SpellAddition);
	require(policy.source != SourceKind::HeroSpecialty || !source.stacking.empty());
	const auto type = named<BonusType>(row["type"], {
		std::pair<std::string_view, BonusType>{"MORALE", BonusType::MORALE}, {"LUCK", BonusType::LUCK},
		{"STACKS_SPEED", BonusType::STACKS_SPEED}, {"PRIMARY_SKILL", BonusType::PRIMARY_SKILL},
		{"SPELL_RESISTANCE_AURA", BonusType::SPELL_RESISTANCE_AURA},
		{"PERCENTAGE_DAMAGE_BOOST", BonusType::PERCENTAGE_DAMAGE_BOOST},
		{"GENERAL_ATTACK_REDUCTION", BonusType::GENERAL_ATTACK_REDUCTION},
		{"SPELL_DAMAGE_REDUCTION", BonusType::SPELL_DAMAGE_REDUCTION}});
	Bonus bonus(BonusDuration::PERMANENT, type,
		policy.source == SourceKind::SkillRank ? BonusSource::SECONDARY_SKILL : BonusSource::HERO_SPECIAL,
		integer(row["value"], -100000, 100000), source.identity);
	bonus.stacking = source.stacking;
	if(type == BonusType::PERCENTAGE_DAMAGE_BOOST)
		bonus.subtype = BonusSubtypeID(BonusCustomSubtype::damageTypeAll);
	if(type == BonusType::SPELL_DAMAGE_REDUCTION)
		bonus.subtype = BonusSubtypeID(SpellSchool::ANY);
	bonus.valType = named<BonusValueType>(row["valueMode"], {
		std::pair<std::string_view, BonusValueType>{"BASE_NUMBER", BonusValueType::BASE_NUMBER},
		{"ADDITIVE_VALUE", BonusValueType::ADDITIVE_VALUE}, {"PERCENT_TO_BASE", BonusValueType::PERCENT_TO_BASE}});
	require(row.Struct().contains("primarySkill") == (type == BonusType::PRIMARY_SKILL));
	if(type == BonusType::PRIMARY_SKILL)
		bonus.subtype = BonusSubtypeID(named<PrimarySkill>(row["primarySkill"], {
			std::pair<std::string_view, PrimarySkill>{"attack", PrimarySkill::ATTACK}, {"defense", PrimarySkill::DEFENSE}}));
	const bool family = policy.source == SourceKind::HeroSpecialty;
	require(row.Struct().contains("family") == family && row.Struct().contains("includeUpgrades") == family);
	const bool adjacent = policy.target == TargetDomain::AdjacentArmy;
	require(row.Struct().contains("aura") == adjacent);
	if(family)
	{
		require(row["includeUpgrades"].isBool() && static_cast<bool>(resolver));
		const auto identity = text(row["family"]);
		const auto separator = identity.find(':');
		const auto valid = [](char c, bool scope)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '_' || (scope && c == '-');
		};
		require(separator != std::string::npos && separator > 0 && separator + 1 < identity.size());
		for(size_t i = 0; i < identity.size(); ++i)
			if(i != separator) require(valid(identity[i], i < separator));
		const auto & creature = resolver(identity);
		require(creature.getId().hasValue());
		bonus.limiter = std::make_shared<CCreatureTypeLimiter>(creature, row["includeUpgrades"].Bool());
		if(adjacent)
		{
			const auto aura = named<AuraPolicy>(row["aura"], {
				std::pair<std::string_view, AuraPolicy>{"friendlyDefense", AuraPolicy::FriendlyDefense},
				{"nativeUndeadDefense", AuraPolicy::NativeUndeadDefense}, {"enemyMeleeReduction", AuraPolicy::EnemyMeleeReduction},
				{"friendlyMeleeDamage", AuraPolicy::FriendlyMeleeDamage}, {"directMagicProtection", AuraPolicy::DirectMagicProtection}});
			require(row["includeUpgrades"].Bool()); // native aura adapters include upgrades
			require((aura == AuraPolicy::FriendlyDefense || aura == AuraPolicy::NativeUndeadDefense)
				? type == BonusType::PRIMARY_SKILL && bonus.subtype == BonusSubtypeID(PrimarySkill::DEFENSE)
				: aura == AuraPolicy::EnemyMeleeReduction ? type == BonusType::GENERAL_ATTACK_REDUCTION
				: aura == AuraPolicy::FriendlyMeleeDamage ? type == BonusType::PERCENTAGE_DAMAGE_BOOST
				: type == BonusType::SPELL_DAMAGE_REDUCTION);
			configureAdjacentSpecialtyBonus(bonus, creature.getId(), aura);
		}
	}
	return PreparedBonusRule(policy, bonus);
}
PreparedBonusRule suppression(bool enhanced)
{
	const auto prepared = creatureSuppressionTemplate(enhanced);
	auto bonus = prepared.preparedBonus();
	bonus.statusIdentity = "core:forgetfulness";
	return PreparedBonusRule(prepared.policy(), bonus);
}
}
PreparedMechanics::PreparedMechanics(ResolvedAuthoringSource source, std::vector<Operation> ordered)
	: source(std::move(source)), ordered(std::move(ordered)) {}
PreparedMechanics prepareMechanics(const JsonNode & mechanics, ResolvedAuthoringSource source,
	const FamilyResolver & resolveFamily)
{
	fields(mechanics, {"version", "operations"});
	integer(mechanics["version"], 1, 1);
	require(mechanics["operations"].isVector());
	const auto & rows = mechanics["operations"].Vector();
	require(!rows.empty() && rows.size() <= 8);
	std::vector<PreparedMechanics::Operation> result;
	for(const auto & row : rows)
	{
		fields(row, {"operation", "source", "target", "stage", "lifetime", "type", "value", "valueMode",
			"primarySkill", "family", "includeUpgrades", "aura"});
		const auto policy = contract(row, source.kind);
		const auto operation = text(row["operation"]);
		using Kind = PreparedMechanics::Operation::Kind;
		if(operation == "bonus")
			result.push_back({Kind::Bonus, passive(row, policy, source, resolveFamily), {}});
		else
		{
			fields(row, {"operation", "source", "target", "stage", "lifetime"});
			require(source.kind == SourceKind::SpellAddition && source.spell.hasValue()
				&& source.identity == BonusSourceID(source.spell) && result.empty());
			require((operation == "sanctuaryKeeper" && source.timedSlot == TimedSourceSlot::Sanctuary)
				|| (operation == "creatureSuppression" && source.timedSlot == TimedSourceSlot::Forgetfulness));
			auto ordinary = operation == "sanctuaryKeeper" ? sanctuaryKeeperTemplate() : suppression(false);
			require(operation == "sanctuaryKeeper" || operation == "creatureSuppression");
			const auto & expected = ordinary.policy();
			require(policy.source == expected.source && policy.target == expected.target
				&& policy.stage == expected.stage && policy.lifetime == expected.lifetime);
			result.push_back({operation == "sanctuaryKeeper" ? Kind::SanctuaryKeeper : Kind::CreatureSuppression,
				std::move(ordinary), operation == "creatureSuppression" ? std::optional(suppression(true)) : std::nullopt});
		}
	}
	return PreparedMechanics(std::move(source), std::move(result));
}
std::vector<std::shared_ptr<Bonus>> PreparedMechanics::persistentReplacement() const
{
	require(source.kind != SourceKind::SpellAddition);
	std::vector<std::shared_ptr<Bonus>> result;
	for(const auto & operation : ordered)
		result.push_back(operation.ordinary.attach());
	return result;
}
std::vector<Bonus> PreparedMechanics::timedAdditions(const TimedAdditionFacts & facts) const
{
	require(source.kind == SourceKind::SpellAddition && facts.spell == source.spell);
	std::vector<Bonus> result;
	if(!facts.nativeMagic) return result;
	for(const auto & operation : ordered)
	{
		if(operation.kind == Operation::Kind::SanctuaryKeeper)
		{
			if(facts.sanctuaryKeeper) result.push_back(operation.ordinary.bindSpell(facts.spell));
		}
		else
			result.push_back((facts.mindbreaker ? *operation.enhanced : operation.ordinary).bindSpell(facts.spell, facts.rounds));
	}
	return result;
}
std::string_view PreparedMechanics::timedOrderingKey() const
{
	require(source.kind == SourceKind::SpellAddition);
	return source.timedSlot == TimedSourceSlot::Sanctuary ? "sanctuaryKeeperMorale" : "creatureAbilitySuppression";
}
std::optional<PreparedMechanics> prepareSourceReplacement(const JsonNode & capturedRow,
	ResolvedAuthoringSource source, const FamilyResolver & resolveFamily)
{
	require(capturedRow.isStruct());
	if(!capturedRow.Struct().contains("mechanics")) return {};
	return prepareMechanics(capturedRow["mechanics"], std::move(source), resolveFamily);
}
}
