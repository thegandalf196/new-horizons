/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <array>

namespace
{
class LeadershipEfficiencyBattleTest : public BattleTestFixture
{
protected:
	static CreatureID id(const char * key) { return CreatureID(CreatureID::decode(key)); }
	void originalMixedArmy()
	{
		attackerSideHero->clearSlots();
		attackerSideHero->setCreature(SlotID(0), id("core:pikeman"), 10);
		attackerSideHero->setCreature(SlotID(1), id("core:monk"), 10);
		attackerSideHero->setCreature(SlotID(2), id("core:angel"), 1);
	}
};
}

TEST_F(LeadershipEfficiencyBattleTest, AuthorityPerksUseActualCategoryFactionAndIntrinsicShooter)
{
	using namespace newHorizonsHeroes;
	startGame();
	ASSERT_TRUE(usesLeadershipEfficiency(attackerSideHero->getCapabilityRules()));
	const LeadershipComposition composition{true, true, true};
	struct Example { const char * perk; const char * creature; int tier; int percent; };
	const std::array<Example, 10> examples = {{{"massedRanks", "core:pikeman", 0, 25},
		{"veteranCadre", "core:monk", 0, 25}, {"familiarStandards", "core:pikeman", 0, 20},
		{"foreignLegion", "core:centaur", 0, 30}, {"officerCorps", "core:pikeman", 1, 25},
		{"missileCommand", "core:archer", 1, 25}, {"regimentalOrganization", "core:pikeman", 1, 25},
		{"combinedArms", "core:pikeman", 1, 35}, {"grandMarshal", "core:pikeman", 2, 25},
		{"masterOfLegions", "core:pikeman", 2, 50}}};
	for(const auto & example : examples)
	{
		SCOPED_TRACE(example.perk);
		LeadershipModifiers projected;
		projected.authorityRank = 3;
		projected.flatLeadership = 0;
		projected.authorityPerks[example.tier] = example.perk;
		const auto value = leadershipStackReadout(attackerSideHero, id(example.creature), 1, &composition, nullptr, &projected);
		EXPECT_EQ(value.effectiveLeadership, leadershipWithBonusPercent(value.baseLeadership, 45 + example.percent));
		EXPECT_DOUBLE_EQ(value.efficiency, 1.0); // Extra capacity is not an independent damage bonus.
	}
	LeadershipModifiers rank;
	rank.flatLeadership = 0;
	for(const int value : {0, 1, 2, 3})
	{
		rank.authorityRank = value;
		const auto view = leadershipStackReadout(attackerSideHero, id("core:pikeman"), 1, &composition, nullptr, &rank);
		EXPECT_EQ(view.effectiveLeadership, leadershipWithBonusPercent(view.baseLeadership, value * 15));
	}
}

TEST_F(LeadershipEfficiencyBattleTest, NecromancyRequirementsRoundOnceAndFlatModifiersRemainIndependent)
{
	using namespace newHorizonsHeroes;
	startGame();
	LeadershipModifiers modifiers;
	modifiers.flatLeadership = 0;
	const auto skeleton = id("core:skeleton");
	const auto base = leadershipStackReadout(attackerSideHero, skeleton, 100, nullptr, nullptr, &modifiers);
	const std::array<int, 4> remaining = {100, 50, 40, 30};
	for(int rank = 0; rank <= 3; ++rank)
	{
		modifiers.necromancyRank = rank;
		const auto value = leadershipStackReadout(attackerSideHero, skeleton, 100, nullptr, nullptr, &modifiers);
		EXPECT_EQ(value.requirement, (base.requirement * remaining[rank] + 99) / 100);
	}
	modifiers.lordOfTheDead = true;
	modifiers.authorityRank = 3;
	const auto lord = leadershipStackReadout(attackerSideHero, skeleton, 1000, nullptr, nullptr, &modifiers);
	EXPECT_EQ(lord.requirement, std::max<int64_t>(1, (base.requirement * 10 + 99) / 100));
	modifiers.flatLeadership = -100;
	const auto lowered = leadershipStackReadout(attackerSideHero, skeleton, 1000, nullptr, nullptr, &modifiers);
	EXPECT_EQ(lowered.baseLeadership, base.baseLeadership - 100);
	EXPECT_LT(lowered.effectiveLeadership, lord.effectiveLeadership);
}

TEST_F(LeadershipEfficiencyBattleTest, CompositionIsCapturedBeforeSummonsAndSurvivesCasualties)
{
	using namespace newHorizonsHeroes;
	startGame();
	originalMixedArmy();
	startBattle();
	const auto captured = battle()->getLeadershipComposition(BattleSide::ATTACKER);
	ASSERT_TRUE(captured.captured);
	EXPECT_TRUE(captured.regimentalOrganization);
	EXPECT_TRUE(captured.combinedArms);
	auto * summon = addStack(BattleSide::ATTACKER, id("core:pikeman"), BattleHex(leftHex), 1000);
	const auto before = battle()->battleLeadershipReadout(summon);
	ASSERT_LT(before.efficiency, 1);
	const auto maximumHealth = summon->getMaxHealth();
	int64_t damage = maximumHealth * 500;
	summon->damage(damage);
	const auto after = battle()->battleLeadershipReadout(summon);
	EXPECT_EQ(after.count, 500);
	EXPECT_GT(after.efficiency, before.efficiency);
	EXPECT_LT(after.effectiveCount(), before.effectiveCount());
	EXPECT_EQ(summon->getMaxHealth(), maximumHealth);
	EXPECT_EQ(battle()->getLeadershipComposition(BattleSide::ATTACKER), captured);
	EXPECT_FALSE(attackerSideHero->getLeadershipSlotCapacity(id("core:angel")));
	EXPECT_DOUBLE_EQ(leadershipStackReadout(nullptr, id("core:angel"), 1000000).efficiency, 1);
}

TEST_F(LeadershipEfficiencyBattleTest, DamagePreviewScalesMeleeRangedRetaliationAndMagicalAttacksOnce)
{
	startGame();
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, id("core:archer"), BattleHex(leftHex), 1000);
	auto * target = addStack(BattleSide::DEFENDER, id("core:pikeman"), BattleHex(rightHex), 1000000);
	forceMaximumDamage(actor);
	const auto efficiency = battle()->battleLeadershipReadout(actor).efficiency;
	ASSERT_LT(efficiency, 1);
	const auto extra = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LEADERSHIP,
		BonusSource::OTHER, 1000000, BonusSourceID());
	for(int variant = 0; variant < 4; ++variant)
	{
		BattleAttackInfo info(actor, target, 0, variant == 1);
		info.retaliation = variant == 2;
		info.physicalDamage = variant != 3;
		const auto reduced = battle()->calculateDmgRange(info).damage;
		attackerSideHero->addNewBonus(extra);
		EXPECT_DOUBLE_EQ(battle()->battleLeadershipReadout(actor).efficiency, 1);
		const auto full = battle()->calculateDmgRange(info).damage;
		attackerSideHero->removeBonus(extra);
		EXPECT_NEAR(reduced.min, full.min * efficiency, 2);
		EXPECT_NEAR(reduced.max, full.max * efficiency, 2);
	}
	const auto health = target->getAvailableHealth();
	blockRetaliation(target);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto preview = battle()->calculateDmgRange(BattleAttackInfo(actor, target, 0, false)).damage;
	ASSERT_TRUE(attack(actor, BattleHex(rightHex)));
	const auto actual = health - target->getAvailableHealth();
	EXPECT_GE(actual, preview.min);
	EXPECT_LE(actual, preview.max);
}

TEST_F(LeadershipEfficiencyBattleTest, LargeAdventureStackEntersBattleWithoutDiscardingCreatures)
{
	startGame();
	attackerSideHero->setCreature(SlotID(0), id("core:pikeman"), 2000000);
	EXPECT_FALSE(attackerSideHero->getLeadershipSlotCapacity(id("core:pikeman")));
	startBattle();
	const auto units = battle()->battleGetUnitsIf([](const battle::Unit * unit)
		{ return unit->unitSide() == BattleSide::ATTACKER && unit->creatureId() == id("core:pikeman"); });
	ASSERT_EQ(units.size(), 1u);
	EXPECT_EQ(units.front()->getCount(), 2000000);
	EXPECT_LT(battle()->battleLeadershipReadout(units.front()).efficiency, 1);
}

TEST_F(LeadershipEfficiencyBattleTest, MachinesAndTurretsAreExcludedFromSharedReadouts)
{
	startGame();
	startBattle();
	for(const auto creature : {CreatureID(CreatureID::BALLISTA), CreatureID(CreatureID::ARROW_TOWERS)})
	{
		const auto adventure = newHorizonsHeroes::leadershipStackReadout(attackerSideHero, creature, 1000000);
		EXPECT_FALSE(adventure.active);
		EXPECT_DOUBLE_EQ(adventure.efficiency, 1);
	}
	auto * machine = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::BALLISTA), BattleHex(leftHex), 1000000);
	const auto projected = battle()->battleLeadershipReadout(machine);
	EXPECT_FALSE(projected.active);
	EXPECT_DOUBLE_EQ(projected.efficiency, 1);
}

TEST_F(LeadershipEfficiencyBattleTest, DownlevelLobbyEnvelopeRejectsCapturedRulesBeforeItsPrefix)
{
	startGame();
	LobbyStartGame packet;
	packet.initializedGameState = server.gameState;
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	EXPECT_THROW(packet.serialize(writer.oser), std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}
