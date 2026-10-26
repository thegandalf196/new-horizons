/*
 * HeroCommandRevisionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/SideInBattle.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"

namespace
{
JsonNode revisedRules()
{
	const JsonNode config(JsonPath::builtin("config/newHorizonsCombat"));
	return config["combat"]["heroCommands"];
}
HeroOrderState vanguard(int round, int attack, int defense, int64_t leadership)
{
	HeroOrderState order;
	order.command = HeroCommand::VANGUARD;
	order.issuedRound = round;
	order.revision = heroCommands::nominalRevisedOrderSnapshot(order.command, round,
		attack, defense, leadership, revisedRules()["commands"]["vanguard"]);
	order.revision->recipientUnitIds = {1, 4};
	return order;
}
}

TEST(HeroCommandRevision, CurrentRosterDoesNotReinterpretHistoricalIds)
{
	const auto rules = revisedRules();
	ASSERT_NO_THROW(heroCommands::validateRules(rules));
	EXPECT_EQ(static_cast<int>(HeroCommand::PROTECT), 10);
	EXPECT_EQ(static_cast<int>(HeroCommand::FLANK), 11);
	EXPECT_EQ(static_cast<int>(HeroCommand::FEINT), 13);
	EXPECT_EQ(static_cast<int>(HeroCommand::VANGUARD), 14);
	EXPECT_TRUE(heroCommands::isActive(HeroCommand::PROTECT));
	EXPECT_FALSE(heroCommands::isIssuable(HeroCommand::PROTECT));
	EXPECT_FALSE(heroCommands::isIssuable(HeroCommand::FLANK));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::PROTECT));
	EXPECT_FALSE(heroCommands::supportedByRules(rules, HeroCommand::FLANK));
	for(const auto command : heroCommands::CANONICAL_COMMANDS)
		EXPECT_TRUE(heroCommands::supportedByRules(rules, command));
}

TEST(HeroCommandRevision, NominalVanguardUsesMinimumRatingAndExactStepBoundaries)
{
	EXPECT_EQ(vanguard(2, 74, 999, 0).revision->vanguardStatBonus, 1);
	EXPECT_EQ(vanguard(2, 75, 999, 0).revision->vanguardStatBonus, 2);
	EXPECT_EQ(vanguard(2, 999, 149, 0).revision->vanguardStatBonus, 2);
	EXPECT_EQ(vanguard(2, 999, 150, 0).revision->vanguardStatBonus, 3);
	EXPECT_EQ(vanguard(2, 999, 999, 0).revision->vanguardStatBonus, 3);
	EXPECT_EQ(vanguard(2, 0, 999, 0).revision->vanguardInitiativePercent, 5);
	EXPECT_EQ(vanguard(2, 200, 999, 0).revision->vanguardInitiativePercent, 15);
	EXPECT_EQ(vanguard(2, 260, 999, 0).revision->vanguardInitiativePercent, 18);
	EXPECT_EQ(vanguard(2, 999, 999, 0).revision->vanguardInitiativePercent, 18);
}

TEST(HeroCommandRevision, NominalDurationIncludesIssuingRoundAndClampsAtThree)
{
	for(const auto leadership : {0, 749, 750, 1499})
		EXPECT_EQ(vanguard(7, 0, 0, leadership).revision->expiresAfterRound, 7);
	EXPECT_EQ(vanguard(7, 0, 0, 1500).revision->expiresAfterRound, 8);
	EXPECT_EQ(vanguard(7, 0, 0, 2250).revision->expiresAfterRound, 9);
	EXPECT_EQ(vanguard(7, 0, 0, 99999).revision->expiresAfterRound, 9);
	const auto order = vanguard(7, 0, 0, 2250);
	EXPECT_FALSE(order.scheduledFor(1, 6));
	EXPECT_TRUE(order.scheduledFor(1, 7));
	EXPECT_TRUE(order.scheduledFor(4, 9));
	EXPECT_FALSE(order.scheduledFor(2, 8));
	EXPECT_FALSE(order.hasScheduledRecipients(10));
}

TEST(HeroCommandRevision, NominalFeintCapsAtSixtyAndLastsOneRound)
{
	const auto definition = revisedRules()["commands"]["feint"];
	EXPECT_EQ(heroCommands::nominalRevisedOrderSnapshot(HeroCommand::FEINT, 3, 0, 0, 0, definition).feintReductionPercent, 15);
	EXPECT_EQ(heroCommands::nominalRevisedOrderSnapshot(HeroCommand::FEINT, 3, 0, 0, 1000, definition).feintReductionPercent, 30);
	EXPECT_EQ(heroCommands::nominalRevisedOrderSnapshot(HeroCommand::FEINT, 3, 0, 0, 3000, definition).feintReductionPercent, 60);
	EXPECT_EQ(heroCommands::nominalRevisedOrderSnapshot(HeroCommand::FEINT, 3, 0, 0, 99999, definition).expiresAfterRound, 3);
}

TEST(HeroCommandRevision, SameOrderRefreshReplacesSnapshotAndKeepsOtherOrders)
{
	SideInBattle side(nullptr);
	side.upsertOrder(vanguard(4, 150, 150, 2250));
	HeroOrderState charge;
	charge.command = HeroCommand::CHARGE;
	charge.issuedRound = 5;
	side.upsertOrder(charge);
	side.upsertOrder(vanguard(5, 0, 0, 0));
	ASSERT_EQ(side.orderStates.size(), 2);
	EXPECT_EQ(side.findOrder(HeroCommand::VANGUARD)->revision->vanguardStatBonus, 1);
	EXPECT_EQ(side.findOrder(HeroCommand::VANGUARD)->revision->expiresAfterRound, 5);
	EXPECT_TRUE(side.findOrder(HeroCommand::CHARGE));
}

TEST(HeroCommandRevision, SnapshotRoundTripAndLossyDownsaveRejection)
{
	auto source = vanguard(4, 150, 200, 2250);
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & source);
	HeroOrderState restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored, source);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	HeroOrderState historical;
	historical.command = HeroCommand::PROTECT;
	historical.issuedRound = 4;
	ASSERT_NO_THROW(old.oser & historical);
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_FALSE(restored.revision);
	EXPECT_EQ(restored.command, HeroCommand::PROTECT);
}

TEST(HeroCommandRevision, ProfileGateAndMalformedSnapshotFailClosed)
{
	const auto rules = revisedRules();
	EXPECT_THROW(heroCommands::validateRevisedRulesSerialization(rules, false), std::runtime_error);
	EXPECT_NO_THROW(heroCommands::validateRevisedRulesSerialization(rules, true));
	auto historical = rules;
	historical["rulesetVersion"].Integer() = 3;
	EXPECT_NO_THROW(heroCommands::validateRevisedRulesSerialization(historical, false));
	auto order = vanguard(4, 150, 150, 2250);
	order.revision->recipientUnitIds = {4, 1};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order = vanguard(4, 150, 150, 2250);
	order.revision->expiresAfterRound = 3;
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order = vanguard(4, 150, 150, 2250);
	order.ironWillLifetime = true;
	order.divineDisciplineRecipientUnitIds = {1, 4};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
}

TEST(HeroCommandRevision, EfficiencyAffectsDerivedTermsAndUsesMinimumAfterIndividualAdjustment)
{
	const auto rules = revisedRules();
	const auto & definition = rules["commands"]["vanguard"];
	const auto base = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 70, 100, 1500, definition, 100, 100, 100);
	const auto command = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 70, 100, 1500, definition, 110, 100, 100);
	EXPECT_EQ(base.vanguardStatBonus, 1);
	EXPECT_EQ(command.vanguardStatBonus, 2);
	const auto minimum = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 200, 100, 1500, definition, 200, 100, 200);
	EXPECT_EQ(minimum.vanguardStatBonus, 2);
	EXPECT_EQ(minimum.vanguardInitiativePercent, 10);
	EXPECT_EQ(minimum.expiresAfterRound, base.expiresAfterRound);
	const auto & feint = rules["commands"]["feint"];
	EXPECT_EQ(heroCommands::scaledRevisedOrderSnapshot(HeroCommand::FEINT, 4, 0, 0, 1000, feint, 100, 100, 130).feintReductionPercent, 35);
	EXPECT_EQ(heroCommands::scaledRevisedOrderSnapshot(HeroCommand::FEINT, 4, 0, 0, 1000, feint, 100, 100, 155).feintReductionPercent, 38);
	EXPECT_EQ(heroCommands::scaledRevisedOrderSnapshot(HeroCommand::FEINT, 4, 0, 0, 1000, feint, 100, 100, 230).feintReductionPercent, 50);
}

TEST(HeroCommandRevision, LuckyDayFinalizationCapsOnceAndDoesNotScaleCompanionOrDuration)
{
	HeroOrderState order;
	order.command = HeroCommand::FEINT;
	order.issuedRound = 4;
	order.revision = heroCommands::scaledRevisedOrderSnapshot(order.command, 4, 0, 0, 1000,
		revisedRules()["commands"]["feint"], 100, 100, 130);
	order.heroLuck.emplace();
	order.heroLuck->heroValue = 1;
	order.heroLuck->diceSize = 100;
	order.heroLuck->baseThreshold = 4;
	order.heroLuck->draw = 0;
	order.heroLuck->mark(newHorizonsHeroLuck::Proc::LuckyDay);
	JsonNode magic;
	magic["heroLuck"]["orderMultiplierPercent"].Integer() = 200;
	heroCommands::finalizeRevisedOrderSnapshot(order, magic);
	EXPECT_EQ(order.revision->feintReductionPercent, 60);
	EXPECT_TRUE(order.revision->potencyFinalized);
	order.revision = heroCommands::scaledRevisedOrderSnapshot(order.command, 4, 0, 0, 0,
		revisedRules()["commands"]["feint"], 100, 100, 100);
	order.revision->feintCompanionPercent = 5;
	heroCommands::finalizeRevisedOrderSnapshot(order, magic);
	EXPECT_EQ(order.revision->feintReductionPercent, 35);
	heroCommands::finalizeRevisedOrderSnapshot(order, magic);
	EXPECT_EQ(order.revision->feintReductionPercent, 35);
	order.command = HeroCommand::VANGUARD;
	order.revision = heroCommands::scaledRevisedOrderSnapshot(order.command, 4, 300, 300, 1500,
		revisedRules()["commands"]["vanguard"], 100, 100, 250);
	order.revision->recipientUnitIds = {1};
	EXPECT_EQ(order.revision->vanguardStatBonus, 5); // No early cap before Luck.
	magic["heroLuck"]["orderMultiplierPercent"].Integer() = 50;
	heroCommands::finalizeRevisedOrderSnapshot(order, magic);
	EXPECT_EQ(order.revision->vanguardStatBonus, 2);
	EXPECT_EQ(order.revision->vanguardInitiativePercent, 10);
	EXPECT_EQ(order.revision->expiresAfterRound, 5);
}

TEST(HeroCommandRevision, CrownVariantSnapshotRoundTripsAndRejectsMissingProvenance)
{
	HeroOrderState order;
	order.command = HeroCommand::FEINT;
	order.issuedRound = 4;
	const auto definition = revisedRules()["commands"]["feint"];
	order.revision = heroCommands::scaledRevisedOrderSnapshot(order.command, 4, 0, 0, 1000,
		definition, 100, 100, 100);
	const auto crown = heroCommands::scaledRevisedOrderSnapshot(order.command, 4, 0, 0, 1000,
		heroCommands::crownRevisedOrderDefinition(definition, order.command), 100, 100, 100);
	order.revision->crown = heroCommands::CrownRevisedOrderPotency{crown.feintReductionPercent, 0, 0};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order.crownAndAltarRecipientUnitIds = {1};
	heroCommands::finalizeRevisedOrderSnapshot(order, JsonNode());
	EXPECT_EQ(heroCommands::revisedFeintReductionFor(order, 1), 33);
	EXPECT_EQ(heroCommands::revisedFeintReductionFor(order, 2), 30);
	ASSERT_NO_THROW(order.validateShape());
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(memory.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(memory.iser & restored);
	EXPECT_EQ(restored, order);
	order.revision->crown->feintReductionPercent = 61;
	EXPECT_THROW(order.validateShape(), std::runtime_error);
}

TEST(HeroCommandRevision, CrownVanguardOnlyBoostsDerivedTermsAndKeepsDuration)
{
	const auto definition = revisedRules()["commands"]["vanguard"];
	const auto crownDefinition = heroCommands::crownRevisedOrderDefinition(definition, HeroCommand::VANGUARD);
	const auto baseline = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 140, 140, 1500, definition, 100, 100, 100);
	const auto crowned = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 140, 140, 1500, crownDefinition, 100, 100, 100);
	EXPECT_EQ(baseline.vanguardStatBonus, 2);
	EXPECT_EQ(crowned.vanguardStatBonus, 3);
	EXPECT_EQ(baseline.vanguardInitiativePercent, 12);
	EXPECT_EQ(crowned.vanguardInitiativePercent, 13);
	EXPECT_EQ(crowned.expiresAfterRound, baseline.expiresAfterRound);
	const auto zero = heroCommands::scaledRevisedOrderSnapshot(HeroCommand::VANGUARD, 4, 0, 0, 1500, crownDefinition, 100, 100, 100);
	EXPECT_EQ(zero.vanguardStatBonus, 1);
	EXPECT_EQ(zero.vanguardInitiativePercent, 5);
	auto order = vanguard(4, 140, 140, 1500);
	order.revision->crown = heroCommands::CrownRevisedOrderPotency{0, crowned.vanguardStatBonus, crowned.vanguardInitiativePercent};
	order.crownAndAltarRecipientUnitIds = {1};
	ASSERT_NO_THROW(order.validateShape());
	order.crownAndAltarRecipientUnitIds = {2}; // Existing unit, but outside original Vanguard recipients.
	EXPECT_THROW(order.validateShape(), std::runtime_error);
}

class HeroCommandRevisionEligibilityTest : public HeroCommandFixture {};

TEST_F(HeroCommandRevisionEligibilityTest, IssuedVanguardIncludesNonLivingElementalButPreservesMoraleImmunity)
{
	useRevisedOrders = true;
	prepareCommands();
	auto * elemental = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(70), 10);
	auto * living = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(73), 10);
	auto * undead = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(76), 10);
	ASSERT_TRUE(elemental->hasBonusOfType(BonusType::NON_LIVING));
	EXPECT_TRUE(heroCommands::isVanguardCreature(elemental));
	EXPECT_TRUE(heroCommands::isVanguardCreature(living));
	EXPECT_FALSE(heroCommands::isVanguardCreature(undead));
	ASSERT_TRUE(issue(HeroCommand::VANGUARD));
	EXPECT_TRUE(heroCommands::activeVanguardBonuses(elemental));
	EXPECT_TRUE(heroCommands::activeVanguardBonuses(living));
	EXPECT_FALSE(heroCommands::activeVanguardBonuses(undead));
	EXPECT_EQ(elemental->moraleVal(), 0);
}

TEST_F(HeroCommandRevisionEligibilityTest, NativeVanguardGrantsUseOnlyCapturedCrownIntersection)
{
	useRevisedOrders = true;
	prepareCommands();
	auto * crowned = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 10);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(73), 10);
	auto order = vanguard(battle()->getRound(), 140, 140, 1500);
	order.revision->recipientUnitIds = {crowned->unitId(), ordinary->unitId()};
	std::sort(order.revision->recipientUnitIds.begin(), order.revision->recipientUnitIds.end());
	order.crownAndAltarRecipientUnitIds = {crowned->unitId()};
	order.revision->crown = heroCommands::CrownRevisedOrderPotency{0, 3, 13};
	heroCommands::finalizeRevisedOrderSnapshot(order, battle()->getMagicRules());
	ASSERT_NO_THROW(order.validateShape());
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, order));
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(crowned));
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(ordinary));
	EXPECT_EQ(heroCommands::activeVanguardBonuses(crowned)->statBonus, 3);
	EXPECT_EQ(heroCommands::activeVanguardBonuses(ordinary)->statBonus, 2);
	EXPECT_EQ(heroCommands::activeVanguardBonuses(crowned)->initiativePercent, 13);
	EXPECT_EQ(heroCommands::activeVanguardBonuses(ordinary)->initiativePercent, 12);
}
