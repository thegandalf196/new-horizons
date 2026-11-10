/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsWarfare.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <limits>

TEST(WarfareState, OnlySuccessfulOrderConsumptionRecordsBattleHistory)
{
	using Ledger = HeroActionAllowanceState;
	Ledger live;
	live.resetForRound(1);
	const auto initial = live;
	EXPECT_FALSE(live.consumeAllowance(999, Ledger::ActionKind::ORDER, 1));
	EXPECT_EQ(live, initial);
	auto preview = live;
	ASSERT_TRUE(preview.consumeAllowance(preview.grants.front().id, Ledger::ActionKind::ORDER, 1));
	EXPECT_TRUE(preview.orderIssuedThisCombat);
	EXPECT_EQ(live, initial);
	ASSERT_TRUE(live.consumeAllowance(live.grants.front().id, Ledger::ActionKind::SPELL, 1));
	EXPECT_FALSE(live.orderIssuedThisCombat);
	live.resetForRound(2);
	ASSERT_TRUE(live.consumeAllowance(live.grants.front().id, Ledger::ActionKind::ORDER, 2));
	EXPECT_TRUE(live.orderIssuedThisCombat);
	live.resetForRound(3);
	live.resetForRound(3);
	EXPECT_TRUE(live.orderIssuedThisCombat);
	EXPECT_EQ(live.remainingCounts(3).heroActions, 1u);
}

TEST(WarfareState, CapturedPolicyRequiresExplicitBoundedParameters)
{
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	rules["warfarePolicyVersion"].Integer() = 1;
	ASSERT_NO_THROW(newHorizonsWarfare::validatePolicy(rules));
	const auto valid = rules;
	rules["skills"]["new-horizons:command"]["ranks"]["basic"]["effect"]["parameters"]["orderEfficiencyPercent"].Float() = 110.5;
	EXPECT_THROW(newHorizonsWarfare::validatePolicy(rules), std::runtime_error);
	rules = valid;
	for(auto & perk : rules["skills"]["new-horizons:command"]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:command.crisisCommand")
			perk["effect"]["parameters"]["attackAndSpellReductionPercent"].Integer() = 101;
	EXPECT_THROW(newHorizonsWarfare::validatePolicy(rules), std::runtime_error);
}

TEST(WarfareState, BattleHistorySurvivesCodecAndRejectsLossyWriterBeforeBytes)
{
	HeroActionAllowanceState saved;
	saved.resetForRound(1);
	ASSERT_TRUE(saved.consumeAllowance(saved.grants.front().id, HeroActionAllowanceState::ActionKind::ORDER, 1));
	CMemorySerializer current;
	current.oser & saved;
	HeroActionAllowanceState restored;
	current.iser & restored;
	EXPECT_EQ(restored, saved);
	const auto old = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY) - 1);
	CMemorySerializer unsupported;
	unsupported.oser.version = old;
	EXPECT_THROW(saved.serialize(unsupported.oser), std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty());
}

TEST(WarfareState, OlderLedgerReadClearsReusedHistoryWithoutInventingPastOrders)
{
	const auto old = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY) - 1);
	HeroActionAllowanceState saved;
	saved.resetForRound(2);
	CMemorySerializer wire;
	wire.oser.version = old;
	wire.iser.version = old;
	wire.oser & saved;
	auto reused = saved;
	reused.orderIssuedThisCombat = true;
	wire.iser & reused;
	EXPECT_FALSE(reused.orderIssuedThisCombat);
	EXPECT_EQ(reused, saved);
}

TEST(WarfareState, IssuanceSnapshotCannotBeRewrittenByProgressUpdates)
{
	HeroOrderState original;
	original.command = HeroCommand::CHARGE;
	original.issuedRound = 1;
	original.battlePlanEfficiencyBonusPercent = 20;
	original.heroicSpiritEfficiencyBonusPercent = 25;
	auto progress = original;
	progress.consumedUnitIds = {0};
	EXPECT_TRUE(heroCommands::sameIssuance(original, progress));
	progress.battlePlanEfficiencyBonusPercent = 0;
	EXPECT_FALSE(heroCommands::sameIssuance(original, progress));
	progress = original;
	progress.heroicSpiritEfficiencyBonusPercent = 0;
	EXPECT_FALSE(heroCommands::sameIssuance(original, progress));
	CMemorySerializer current;
	current.oser & original;
	HeroOrderState restored;
	current.iser & restored;
	EXPECT_EQ(restored, original);
}

TEST(WarfareState, OrderAndEnclosingUpdateRejectUnsupportedSnapshotBeforePrefixes)
{
	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 1;
	order.battlePlanEfficiencyBonusPercent = 20;
	const auto old = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY) - 1);
	CMemorySerializer direct;
	direct.oser.version = old;
	EXPECT_THROW(order.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleHeroOrderStateChanged update;
	update.side = BattleSide::ATTACKER;
	update.state = order;
	update.states = std::vector<HeroOrderState>{order};
	CMemorySerializer enclosing;
	enclosing.oser.version = old;
	EXPECT_THROW(update.serialize(enclosing.oser), std::runtime_error);
	EXPECT_TRUE(enclosing.extractBuffer().empty());
}

TEST(WarfareState, IndependentReductionIsBoundedAndOverflowSafe)
{
	using newHorizonsWarfare::reduceDamage;
	EXPECT_EQ(reduceDamage(40, 20), 32);
	EXPECT_EQ(reduceDamage(4, 20), 3);
	EXPECT_EQ(reduceDamage(100, 1000), 0);
	EXPECT_EQ(reduceDamage(100, -1), 100);
	const auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(reduceDamage(maximum, 20), maximum / 5 * 4 + maximum % 5 * 4 / 5);
}

TEST(WarfareState, NativeDamageComposesIndependentFactorsAndKeepsDefenseOutOfReflectionBasis)
{
	nativeDamage::Facts facts;
	facts.attacker.minDamage = facts.attacker.maxDamage = 100;
	facts.attacker.count = 10;
	facts.defender.maxHealth = facts.defender.firstHealth = 100;
	facts.defender.count = 100;
	for(bool shooting : {false, true})
		for(bool physical : {false, true})
		{
			DamageAttackInfo info;
			info.shooting = shooting;
			info.physicalDamage = physical;
			info.sylvanDefenseMultipliers = {40};
			info.commandBeneficiaryDamageMultiplierPercent = 80;
			info.heroOrderFinalDamageMultipliers = {110};
			const auto damage = nativeDamage::calculate(info, facts);
			EXPECT_EQ(damage.damage.min, 352);
			EXPECT_EQ(damage.damage.max, 352);
			EXPECT_EQ(damage.kills.min, 3);
			EXPECT_EQ(damage.damageBeforeDefense.min, 1100);
			facts.defender.values[static_cast<size_t>(BonusType::DAMAGE_RECEIVED_CAP)] = 200;
			EXPECT_EQ(nativeDamage::calculate(info, facts).damage.max, 200);
			facts.defender.values[static_cast<size_t>(BonusType::DAMAGE_RECEIVED_CAP)] = 0;
		}
}
