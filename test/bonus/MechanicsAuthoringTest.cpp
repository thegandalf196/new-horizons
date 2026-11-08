/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/MechanicsAuthoring.h"
#include "../../lib/nativeRules/NativeRulePilots.h"
#include "../../lib/nativeRules/MechanicsSourceAdapters.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <cstring>
namespace
{
using namespace nativeRules;
JsonNode read(const char * data)
{
	return JsonNode(data, std::strlen(data), "mechanics-authoring-test");
}
JsonNode skill()
{
	return read(R"({"version":1,"operations":[{"operation":"bonus","source":"skillRank","target":"army",
		"stage":"attachment","lifetime":"permanent","type":"MORALE","value":2,"valueMode":"ADDITIVE_VALUE"}]})");
}
ResolvedAuthoringSource skillSource()
{
	return {SourceKind::SkillRank, BonusSourceID(SecondarySkill(SecondarySkill::LEADERSHIP)), "existing:leadership", SpellID()};
}
ResolvedAuthoringSource specialtySource()
{
	return {SourceKind::HeroSpecialty, BonusSourceID(HeroTypeID(0)), "existing:specialty", SpellID()};
}
JsonNode timed(const char * operation, const char * lifetime)
{
	auto node = read(R"({"version":1,"operations":[{"source":"spellAddition","target":"spellRecipient","stage":"castApplication"}]})");
	node["operations"].Vector()[0]["operation"].String() = operation;
	node["operations"].Vector()[0]["lifetime"].String() = lifetime;
	return node;
}
}
TEST(MechanicsAuthoring, CapturedAbsenceAloneSelectsLegacyAndPresentInvalidNeverFallsBack)
{
	JsonNode row;
	row.Struct();
	EXPECT_FALSE(prepareSourceReplacement(row, skillSource(), {}));
	row["mechanics"] = JsonNode();
	EXPECT_THROW(prepareSourceReplacement(row, skillSource(), {}), std::invalid_argument);
	row["mechanics"] = skill();
	ASSERT_TRUE(prepareSourceReplacement(row, skillSource(), {}));
}
TEST(MechanicsAuthoring, PersistentReplacementIsOwnedImmutableAndDoesNotResolveOnQueries)
{
	auto definition = skill();
	int resolutions = 0;
	const auto prepared = prepareMechanics(definition, skillSource(), [&](const std::string &) -> const CCreature &
	{
		++resolutions;
		throw std::runtime_error("No skill family resolution is allowed");
	});
	definition["operations"].Vector()[0]["value"].Integer() = 77;
	for(int query = 0; query < 100; ++query)
	{
		auto replacement = prepared.persistentReplacement();
		ASSERT_EQ(replacement.size(), 1);
		EXPECT_EQ(replacement[0]->val, 2);
		EXPECT_EQ(replacement[0]->source, BonusSource::SECONDARY_SKILL);
		replacement[0]->val = 99;
	}
	EXPECT_EQ(resolutions, 0);
}
TEST(MechanicsAuthoring, UnknownFieldsOperationSourceAndAllContractDimensionsReject)
{
	for(const auto & [field, value] : {std::pair{"operation", "invokeController"}, {"source", "heroSpecialty"},
		{"target", "spellRecipient"}, {"stage", "castApplication"}, {"lifetime", "rounds"}, {"type", "NONE"},
		{"valueMode", "invented"}, {"expression", "actor.health"}})
	{
		auto invalid = skill();
		invalid["operations"].Vector()[0][field].String() = value;
		EXPECT_THROW(prepareMechanics(invalid, skillSource(), {}), std::invalid_argument) << field;
	}
	for(const int version : {0, 2})
	{
		auto invalid = skill();
		invalid["version"].Integer() = version;
		EXPECT_THROW(prepareMechanics(invalid, skillSource(), {}), std::invalid_argument);
	}
	auto invalid = skill();
	invalid["operations"].Vector()[0]["value"].Float() = 1.5;
	EXPECT_THROW(prepareMechanics(invalid, skillSource(), {}), std::invalid_argument);
}
TEST(MechanicsAuthoring, FamilyResolvesExactlyOnceBeforeImmutableAttachment)
{
	auto definition = skill();
	auto & row = definition["operations"].Vector()[0];
	row["source"].String() = "heroSpecialty";
	row["target"].String() = "creatureFamily";
	row["type"].String() = "STACKS_SPEED";
	row["value"].Integer() = 5;
	row["family"].String() = "core:hellHound";
	row["includeUpgrades"].Bool() = true;
	int resolutions = 0;
	const auto prepared = prepareMechanics(definition, specialtySource(), [&](const std::string & id) -> const CCreature &
	{
		++resolutions;
		EXPECT_EQ(id, "core:hellHound");
		return *CreatureID(CreatureID::decode(id)).toCreature();
	});
	for(int query = 0; query < 100; ++query)
	{
		const auto output = prepared.persistentReplacement();
		ASSERT_EQ(output.size(), 1);
		EXPECT_EQ(output[0]->val, 5);
		const auto * family = dynamic_cast<const CCreatureTypeLimiter *>(output[0]->limiter.get());
		ASSERT_NE(family, nullptr);
		EXPECT_TRUE(family->includeUpgrades);
	}
	EXPECT_EQ(resolutions, 1);
}
TEST(MechanicsAuthoring, BundleLimitsAndAtomicLateInvalidRecordRejectWholePreparation)
{
	auto definition = skill();
	auto & rows = definition["operations"].Vector();
	const auto first = rows.front();
	rows.resize(8, first);
	EXPECT_NO_THROW(prepareMechanics(definition, skillSource(), {}));
	rows.push_back(first);
	EXPECT_THROW(prepareMechanics(definition, skillSource(), {}), std::invalid_argument);
	rows.resize(2);
	rows.back()["lifetime"].String() = "rounds";
	EXPECT_THROW(prepareMechanics(definition, skillSource(), {}), std::invalid_argument);
	rows.clear();
	EXPECT_THROW(prepareMechanics(definition, skillSource(), {}), std::invalid_argument);
	rows.push_back(JsonNode(4));
	EXPECT_THROW(prepareMechanics(definition, skillSource(), {}), std::invalid_argument);
}
TEST(MechanicsAuthoring, SanctuaryRetainsExactSameSourceParentStatusAndEligibility)
{
	const SpellID spell(40);
	const auto prepared = prepareMechanics(timed("sanctuaryKeeper", "parentStatus"),
		{SourceKind::SpellAddition, BonusSourceID(spell), "", spell, TimedSourceSlot::Sanctuary}, {});
	EXPECT_TRUE(prepared.timedAdditions({spell, 0, false, false, true}).empty());
	EXPECT_TRUE(prepared.timedAdditions({spell, 0, true, false, false}).empty());
	const auto output = prepared.timedAdditions({spell, 0, true, false, true});
	ASSERT_EQ(output.size(), 1);
	EXPECT_EQ(output[0].val, 2);
	EXPECT_EQ(output[0].duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(prepared.timedOrderingKey(), "sanctuaryKeeperMorale");
	const auto * parent = dynamic_cast<const HasAnotherBonusLimiter *>(output[0].limiter.get());
	ASSERT_NE(parent, nullptr);
	EXPECT_EQ(parent->type, BonusType::SANCTIFIED);
	EXPECT_EQ(parent->sid, BonusSourceID(spell));
	EXPECT_TRUE(parent->isSourceIDRelevant);
	EXPECT_THROW(prepared.persistentReplacement(), std::invalid_argument);
	EXPECT_THROW(prepared.timedAdditions({SpellID(41), 0, true, false, true}), std::invalid_argument);
}
TEST(MechanicsAuthoring, MindbreakerChoosesExistingCategoricalTemplateAndNativeDuration)
{
	const SpellID spell(SpellID::FORGETFULNESS);
	const auto prepared = prepareMechanics(timed("creatureSuppression", "rounds"),
		{SourceKind::SpellAddition, BonusSourceID(spell), "", spell, TimedSourceSlot::Forgetfulness}, {});
	for(bool enhanced : {false, true})
	{
		const auto output = prepared.timedAdditions({spell, 3, false, enhanced, true});
		ASSERT_EQ(output.size(), 1);
		EXPECT_EQ(output[0].type, BonusType::CREATURE_ABILITY_SUPPRESSION);
		EXPECT_EQ(output[0].val, enhanced ? 2 : 1);
		EXPECT_EQ(output[0].turnsRemain, 3);
		EXPECT_EQ(output[0].source, BonusSource::SPELL_EFFECT);
		EXPECT_EQ(output[0].statusIdentity, "core:forgetfulness");
	}
	EXPECT_EQ(prepared.timedOrderingKey(), "creatureAbilitySuppression");
	JsonNode capture;
	capture["mechanics"] = timed("creatureSuppression", "rounds");
	EXPECT_TRUE(prepareTimedSource(capture, spell, TimedSourceSlot::Forgetfulness));
	EXPECT_THROW(prepareTimedSource(capture, SpellID(SpellID::SLOW), TimedSourceSlot::Forgetfulness), std::invalid_argument);
	auto & rows = capture["mechanics"]["operations"].Vector();
	rows.push_back(rows.front());
	EXPECT_THROW(prepareTimedSource(capture, spell, TimedSourceSlot::Forgetfulness), std::invalid_argument);
	EXPECT_THROW(prepared.timedAdditions({spell, 0, false, false, true}), std::invalid_argument);
	EXPECT_THROW(prepareMechanics(timed("sanctuaryKeeper", "parentStatus"),
		(ResolvedAuthoringSource{SourceKind::SpellAddition, BonusSourceID(spell), "", spell, TimedSourceSlot::Forgetfulness}), {}), std::invalid_argument);
}
TEST(MechanicsAuthoring, RankAdapterPreservesLegacyDeferredIdentityAndNeverDoubleApplies)
{
	const SecondarySkill source(SecondarySkill::LEADERSHIP);
	auto original = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::SECONDARY_SKILL, 2, BonusSourceID(source));
	int calls = 0;
	const auto legacy = [&] { ++calls; return std::vector{original}; };
	JsonNode capture;
	capture.Struct();
	const auto old = rankEffectsOrLegacy(capture, source, legacy);
	ASSERT_EQ(old.size(), 1);
	EXPECT_EQ(old[0].get(), original.get());
	EXPECT_EQ(calls, 1);
	// A deferred existing loader callback still writes the actual installed object.
	original->subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	EXPECT_EQ(old[0]->subtype, original->subtype);
	capture["mechanics"] = skill();
	const auto replacement = rankEffectsOrLegacy(capture, source, legacy);
	ASSERT_EQ(replacement.size(), 1);
	EXPECT_EQ(calls, 1);
	EXPECT_NE(replacement[0].get(), original.get());
	EXPECT_EQ(replacement[0]->stacking, "");
	capture["mechanics"]["version"].Integer() = 2;
	EXPECT_THROW(rankEffectsOrLegacy(capture, source, legacy), std::invalid_argument);
	EXPECT_EQ(calls, 1);
}
TEST(MechanicsAuthoring, AuraMatchesNativePasisTemplateAndRejectsUnsupportedMatrix)
{
	auto definition = skill();
	auto & row = definition["operations"].Vector()[0];
	row["source"].String() = "heroSpecialty";
	row["target"].String() = "adjacentArmy";
	row["stage"].String() = "spatialQuery";
	row["type"].String() = "SPELL_DAMAGE_REDUCTION";
	row["valueMode"].String() = "BASE_NUMBER";
	row["value"].Integer() = 15;
	row["family"].String() = "new-horizons:wisp";
	row["includeUpgrades"].Bool() = true;
	row["aura"].String() = "directMagicProtection";
	const auto & creature = *CreatureID(CreatureID::decode("new-horizons:wisp")).toCreature();
	const auto resolver = [&](const std::string &) -> const CCreature & { return creature; };
	const auto output = prepareMechanics(definition, specialtySource(), resolver).persistentReplacement();
	ASSERT_EQ(output.size(), 1);
	Bonus expected(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::HERO_SPECIAL, 15,
		specialtySource().identity);
	expected.stacking = specialtySource().stacking;
	expected.valType = BonusValueType::BASE_NUMBER;
	expected.limiter = std::make_shared<CCreatureTypeLimiter>(creature, true);
	configureAdjacentSpecialtyBonus(expected, creature.getId(), AuraPolicy::DirectMagicProtection);
	CMemorySerializer authoredWire;
	authoredWire.oser & *output[0];
	CMemorySerializer nativeWire;
	nativeWire.oser & expected;
	EXPECT_EQ(authoredWire.extractBuffer(), nativeWire.extractBuffer());
	row["type"].String() = "MORALE";
	EXPECT_THROW(prepareMechanics(definition, specialtySource(), resolver), std::invalid_argument);
}
TEST(MechanicsAuthoring, AuthoredBonusOrderIsRetainedAndNumericRepresentationIsStrict)
{
	auto definition = skill();
	auto second = definition["operations"].Vector().front();
	second["type"].String() = "LUCK";
	second["value"].Integer() = 3;
	definition["operations"].Vector().push_back(second);
	const auto output = prepareMechanics(definition, skillSource(), {}).persistentReplacement();
	ASSERT_EQ(output.size(), 2);
	EXPECT_EQ(output[0]->type, BonusType::MORALE);
	EXPECT_EQ(output[1]->type, BonusType::LUCK);
	definition["version"].Float() = 1.0;
	EXPECT_THROW(prepareMechanics(definition, skillSource(), {}), std::invalid_argument);
}
