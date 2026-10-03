/*
 * NewHorizonsRealityWarpPlannerTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"

namespace
{
using newHorizonsRealityWarp::EffectBundle;

EffectBundle effectBundle(SpellID spell, BonusType type, int value, int turnsRemaining, PlayerColor caster)
{
	EffectBundle result;
	result.spell = spell;
	result.transferable = true;

	Bonus bonus(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, BonusSourceID(spell));
	bonus.turnsRemain = turnsRemaining;
	bonus.spellCasterOwner = caster;
	result.bonuses.push_back(bonus);
	return result;
}

const Bonus & onlyBonus(const EffectBundle & bundle)
{
	EXPECT_EQ(bundle.bonuses.size(), 1);
	return bundle.bonuses.front();
}
}

TEST(NewHorizonsRealityWarpPlannerTest, ExchangesCompleteBundlesWithExactSidecarsAndDurations)
{
	const auto regeneration = effectBundle(SpellID(SpellID::HASTE), BonusType::HP_REGENERATION, 3, 4, PlayerColor(0));
	const auto guardianSpirit = effectBundle(SpellID(SpellID::SHIELD), BonusType::GENERAL_DAMAGE_REDUCTION, 5, 2, PlayerColor(1));
	const auto hydraVitality = effectBundle(SpellID(SpellID::PRAYER), BonusType::STACK_HEALTH, 7, 6, PlayerColor(0));

	auto firstRegeneration = regeneration;
	firstRegeneration.regeneration = newHorizonsRealityWarp::RegenerationPayload{125000, 4500001};
	auto firstHydra = hydraVitality;
	firstHydra.capacityRegeneration = newHorizonsRealityWarp::CapacityRegenerationPayload{7};
	auto secondGuardian = guardianSpirit;
	secondGuardian.guardianSpirit = newHorizonsRealityWarp::GuardianSpiritPayload{8123, 3};

	const std::vector<EffectBundle> firstSnapshot{firstRegeneration, firstHydra};
	const std::vector<EffectBundle> secondSnapshot{secondGuardian};
	const auto plan = newHorizonsRealityWarp::planExchange(firstSnapshot, PlayerColor(0),
		secondSnapshot, PlayerColor(1), [](const EffectBundle &, newHorizonsRealityWarp::RecipientSide, PlayerColor)
		{
			return true;
		});

	ASSERT_EQ(plan.first.size(), 1);
	EXPECT_EQ(plan.first.front().spell, guardianSpirit.spell);
	ASSERT_TRUE(plan.first.front().guardianSpirit.has_value());
	EXPECT_EQ(plan.first.front().guardianSpirit->hitPointPool, 8123);
	EXPECT_EQ(plan.first.front().guardianSpirit->roundsRemaining, 3);
	EXPECT_EQ(onlyBonus(plan.first.front()).turnsRemain, 2);

	ASSERT_EQ(plan.second.size(), 2);
	EXPECT_EQ(plan.second[0].spell, regeneration.spell);
	ASSERT_TRUE(plan.second[0].regeneration.has_value());
	EXPECT_EQ(plan.second[0].regeneration->rateMillionths, 125000);
	EXPECT_EQ(plan.second[0].regeneration->pendingMicroHealth, 4500001);
	EXPECT_EQ(onlyBonus(plan.second[0]).turnsRemain, 4);
	EXPECT_EQ(plan.second[1].spell, hydraVitality.spell);
	ASSERT_TRUE(plan.second[1].capacityRegeneration.has_value());
	EXPECT_EQ(plan.second[1].capacityRegeneration->remainderTenths, 7);
	EXPECT_EQ(onlyBonus(plan.second[1]).turnsRemain, 6);
}

TEST(NewHorizonsRealityWarpPlannerTest, PlanningLeavesOriginalEffectSnapshotsUnchanged)
{
	auto source = effectBundle(SpellID(SpellID::HASTE), BonusType::MORALE, -2, 3, PlayerColor(0));
	source.bonuses.front().appliedByEnemy = false;
	source.regeneration = newHorizonsRealityWarp::RegenerationPayload{8765, 123456};
	const auto plan = newHorizonsRealityWarp::planExchange({source}, PlayerColor(0), {}, PlayerColor(1),
		[](const EffectBundle &, newHorizonsRealityWarp::RecipientSide, PlayerColor)
		{
			return true;
		});

	ASSERT_EQ(plan.second.size(), 1);
	EXPECT_TRUE(plan.second.front().bonuses.front().appliedByEnemy)
		<< "Known caster hostility is recalculated only on the detached recipient copy";
	EXPECT_FALSE(source.bonuses.front().appliedByEnemy);
	EXPECT_EQ(source.bonuses.front().spellCasterOwner, PlayerColor(0));
	ASSERT_TRUE(source.regeneration.has_value());
	EXPECT_EQ(source.regeneration->rateMillionths, 8765);
	EXPECT_EQ(source.regeneration->pendingMicroHealth, 123456);
	EXPECT_EQ(source.bonuses.front().turnsRemain, 3);
}

TEST(NewHorizonsRealityWarpPlannerTest, KeepsExcludedAndIllegalBundlesAtTheirOriginalEndpoint)
{
	auto excluded = effectBundle(SpellID(SpellID::MIRTH), BonusType::MORALE, 1, 3, PlayerColor(0));
	excluded.transferable = false;
	const auto illegalForRecipient = effectBundle(SpellID(SpellID::SLOW), BonusType::MORALE, -2, 4, PlayerColor(0));
	int legalityChecks = 0;

	const auto plan = newHorizonsRealityWarp::planExchange(
		{excluded, illegalForRecipient}, PlayerColor(0), {}, PlayerColor(1),
		[&legalityChecks](const EffectBundle & bundle, newHorizonsRealityWarp::RecipientSide destination, PlayerColor)
		{
			++legalityChecks;
			return destination == newHorizonsRealityWarp::RecipientSide::SECOND
				&& bundle.spell != SpellID(SpellID::SLOW);
		});

	ASSERT_EQ(plan.first.size(), 2);
	EXPECT_EQ(plan.first[0].spell, excluded.spell);
	EXPECT_EQ(plan.first[1].spell, illegalForRecipient.spell);
	EXPECT_TRUE(plan.second.empty());
	EXPECT_EQ(legalityChecks, 1) << "Explicitly excluded bundles are not offered to recipient eligibility";
}

TEST(NewHorizonsRealityWarpPlannerTest, KeepsDuplicateSpellBundlesAndTheirIndependentTimers)
{
	const auto firstDuplicate = effectBundle(SpellID(SpellID::HASTE), BonusType::MORALE, 2, 2, PlayerColor(0));
	const auto secondDuplicate = effectBundle(SpellID(SpellID::HASTE), BonusType::MORALE, 4, 5, PlayerColor(0));
	const auto plan = newHorizonsRealityWarp::planExchange({firstDuplicate, secondDuplicate},
		PlayerColor(0), {}, PlayerColor(1),
		[](const EffectBundle &, newHorizonsRealityWarp::RecipientSide, PlayerColor)
		{
			return true;
		});

	ASSERT_TRUE(plan.first.empty());
	ASSERT_EQ(plan.second.size(), 2);
	EXPECT_EQ(plan.second[0].spell, firstDuplicate.spell);
	EXPECT_EQ(plan.second[1].spell, secondDuplicate.spell);
	EXPECT_EQ(onlyBonus(plan.second[0]).val, 2);
	EXPECT_EQ(onlyBonus(plan.second[0]).turnsRemain, 2);
	EXPECT_EQ(onlyBonus(plan.second[1]).val, 4);
	EXPECT_EQ(onlyBonus(plan.second[1]).turnsRemain, 5);
}

TEST(NewHorizonsRealityWarpPlannerTest, RecalculatesKnownCasterHostilityAndPreservesUnknownLegacyHostility)
{
	auto effects = effectBundle(SpellID(SpellID::SORROW), BonusType::MORALE, -2, 3, PlayerColor(0));
	effects.bonuses.front().appliedByEnemy = false;
	Bonus friendlyAtDestination(BonusDuration::N_TURNS, BonusType::MORALE,
		BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(SpellID::SORROW)));
	friendlyAtDestination.turnsRemain = 2;
	friendlyAtDestination.spellCasterOwner = PlayerColor(1);
	effects.bonuses.push_back(friendlyAtDestination);
	Bonus legacyUnknown(BonusDuration::N_TURNS, BonusType::MORALE,
		BonusSource::SPELL_EFFECT, -3, BonusSourceID(SpellID(SpellID::SORROW)));
	legacyUnknown.turnsRemain = 1;
	legacyUnknown.appliedByEnemy = true;
	effects.bonuses.push_back(legacyUnknown);

	const auto plan = newHorizonsRealityWarp::planExchange({effects}, PlayerColor(0), {}, PlayerColor(1),
		[](const EffectBundle &, newHorizonsRealityWarp::RecipientSide, PlayerColor)
		{
			return true;
		});

	ASSERT_EQ(plan.second.size(), 1);
	ASSERT_EQ(plan.second.front().bonuses.size(), 3);
	EXPECT_EQ(plan.second.front().bonuses[0].spellCasterOwner, PlayerColor(0));
	EXPECT_TRUE(plan.second.front().bonuses[0].appliedByEnemy);
	EXPECT_EQ(plan.second.front().bonuses[1].spellCasterOwner, PlayerColor(1));
	EXPECT_FALSE(plan.second.front().bonuses[1].appliedByEnemy);
	EXPECT_EQ(plan.second.front().bonuses[2].spellCasterOwner, PlayerColor::CANNOT_DETERMINE);
	EXPECT_TRUE(plan.second.front().bonuses[2].appliedByEnemy);

	Bonus copied(effects.bonuses.front(), BonusSourceID(SpellID(SpellID::HASTE)));
	EXPECT_EQ(copied.spellCasterOwner, PlayerColor(0));
	EXPECT_EQ(copied.sid, BonusSourceID(SpellID(SpellID::HASTE)));
	EXPECT_EQ(effects.bonuses.front().spellCasterOwner, PlayerColor(0));
	EXPECT_FALSE(effects.bonuses.front().appliedByEnemy)
		<< "Planning must not rewrite the source snapshot's target-relative hostility";
}

TEST(NewHorizonsRealityWarpPlannerTest, BonusCasterOwnerSerializationIsVersionedAndRejectsLossyDownsave)
{
	Bonus source(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::SPELL_EFFECT,
		-3, BonusSourceID(SpellID(SpellID::SORROW)));
	source.turnsRemain = 2;
	source.appliedByEnemy = true;
	source.spellCasterOwner = PlayerColor(1);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & source;
	Bonus currentCopy;
	current.iser & currentCopy;
	EXPECT_EQ(currentCopy.spellCasterOwner, PlayerColor(1));
	EXPECT_TRUE(currentCopy.appliedByEnemy);
	EXPECT_EQ(currentCopy.turnsRemain, 2);

	Bonus legacySource(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::SPELL_EFFECT,
		-1, BonusSourceID(SpellID(SpellID::SORROW)));
	legacySource.turnsRemain = 4;
	legacySource.appliedByEnemy = true;
	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_PACT_STATE;
	previous.iser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_PACT_STATE;
	previous.oser & legacySource;
	Bonus previousCopy;
	previous.iser & previousCopy;
	EXPECT_EQ(previousCopy.spellCasterOwner, PlayerColor::CANNOT_DETERMINE);
	EXPECT_TRUE(previousCopy.appliedByEnemy);
	EXPECT_EQ(previousCopy.turnsRemain, 4);

	CMemorySerializer unsupportedDownsave;
	unsupportedDownsave.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_PACT_STATE;
	EXPECT_THROW(unsupportedDownsave.oser & source, std::runtime_error);
	EXPECT_TRUE(unsupportedDownsave.extractBuffer().empty());
}

namespace
{
JsonNode hypnotizedBonusConfig()
{
	JsonNode result;
	result["type"].String() = "HYPNOTIZED";
	result.setModScope("core", false);
	return result;
}
}

TEST(NewHorizonsRealityWarpPlannerTest, HypnotizeHealthCeilingSurvivesParserJsonAndCurrentWireRoundTrip)
{
	constexpr int64_t capturedHealthCeiling = 9007199254740993LL;
	auto config = hypnotizedBonusConfig();
	config["addInfo"]["maximumTargetHealth"].Integer() = capturedHealthCeiling;
	config["addInfo"]["opaqueLegacyField"].String() = "preserved";

	const auto parsed = JsonUtils::parseBonus(config);
	ASSERT_NE(parsed, nullptr);
	ASSERT_EQ(parsed->type, BonusType::HYPNOTIZED);
	ASSERT_NE(parsed->parameters, nullptr);
	const auto parsedInfo = parsed->parameters->toCustom<JsonNode>();
	ASSERT_EQ(parsedInfo["maximumTargetHealth"].getType(), JsonNode::JsonType::DATA_INTEGER);
	EXPECT_EQ(parsedInfo["maximumTargetHealth"].Integer(), capturedHealthCeiling);
	EXPECT_EQ(parsedInfo["opaqueLegacyField"].String(), "preserved");

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & *parsed;
	Bonus restored;
	wire.iser & restored;
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>()["maximumTargetHealth"].Integer(), capturedHealthCeiling);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>()["opaqueLegacyField"].String(), "preserved");

	const auto jsonRoundTrip = JsonUtils::parseBonus(restored.toJsonNode());
	ASSERT_NE(jsonRoundTrip, nullptr);
	ASSERT_NE(jsonRoundTrip->parameters, nullptr);
	EXPECT_EQ(jsonRoundTrip->parameters->toCustom<JsonNode>()["maximumTargetHealth"].Integer(), capturedHealthCeiling);
	EXPECT_EQ(jsonRoundTrip->parameters->toCustom<JsonNode>()["opaqueLegacyField"].String(), "preserved");
}

TEST(NewHorizonsRealityWarpPlannerTest, HypnotizeParserRejectsMalformedCapturedHealthCeilings)
{
	auto floatingCeiling = hypnotizedBonusConfig();
	floatingCeiling["addInfo"]["maximumTargetHealth"].Float() = 9007199254740992.0;
	EXPECT_THROW(JsonUtils::parseBonus(floatingCeiling), std::runtime_error);

	auto negativeCeiling = hypnotizedBonusConfig();
	negativeCeiling["addInfo"]["maximumTargetHealth"].Integer() = -1;
	EXPECT_THROW(JsonUtils::parseBonus(negativeCeiling), std::runtime_error);

	auto missingCeiling = hypnotizedBonusConfig();
	missingCeiling["addInfo"]["opaqueLegacyField"].String() = "preserved";
	EXPECT_THROW(JsonUtils::parseBonus(missingCeiling), std::runtime_error);
}

TEST(NewHorizonsRealityWarpPlannerTest, LegacyHypnotizeWithoutHealthCeilingRemainsReadable)
{
	const auto legacy = JsonUtils::parseBonus(hypnotizedBonusConfig());
	ASSERT_NE(legacy, nullptr);
	ASSERT_EQ(legacy->type, BonusType::HYPNOTIZED);
	EXPECT_EQ(legacy->parameters, nullptr);

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & *legacy;
	Bonus restored;
	wire.iser & restored;
	EXPECT_EQ(restored.parameters, nullptr);
}
