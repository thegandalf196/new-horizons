/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <stdexcept>

namespace
{
class LeadershipEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit LeadershipEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class LeadershipProjectionTest : public BattleTestFixture, public ::testing::WithParamInterface<const char *>
{
protected:
	std::unique_ptr<LeadershipEnvironment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<InitialExportBattleSubject> seed;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;
	uint32_t pikeId = 0, angelId = 0, enemyId = 0;

	void prepare(bool ownedOriginalRoster = true)
	{
		startGame();
		const SecondarySkill authority(SecondarySkill::decode("new-horizons:authority"));
		attackerSideHero->setSecSkillLevel(authority, 1, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:authority", "new-horizons:authority.massedRanks"});
		attackerSideHero->setSecSkillLevel(authority, 2, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:authority", GetParam()});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:authority", GetParam()));
		attackerSideHero->clearSlots(); // Before the battle owns any originating stack pointers.
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 1000));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), CreatureID(CreatureID::decode("core:griffin")), 100));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(2), CreatureID(CreatureID::decode("core:angel")), 10));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSide() == BattleSide::ATTACKER)
			{
				if(unit->unitSlot() == SlotID(0)) pikeId = unit->unitId();
				if(unit->unitSlot() == SlotID(2)) angelId = unit->unitId();
			}
		enemyId = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(100), 1000)->unitId();
		environment = std::make_unique<LeadershipEnvironment>(gameState());
		live = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		seed = InitialExportBattleSubject::capture(live, *battle());
		if(ownedOriginalRoster)
			parent = std::make_shared<HypotheticBattle>(environment.get(), seed);
		else
			parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};

TEST_P(LeadershipProjectionTest, SelectedCompositionPerkSurvivesCasualtiesAndTemporaryDuplicateLines)
{
	// Summons use BattleAI's ordinary mutable-roster source. The separate
	// explicit InitialExport recipe owns only its captured original roster.
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_FALSE(parent->initialExportSubject());
	const auto original = battle()->getLeadershipComposition(BattleSide::ATTACKER);
	ASSERT_TRUE(original.captured);
	ASSERT_TRUE(original.regimentalOrganization);
	ASSERT_TRUE(original.combinedArms);
	const auto before = live->battleLeadershipReadout(live->battleGetUnitByID(pikeId));
	auto withoutPerk = newHorizonsHeroes::captureLeadershipModifiers(*attackerSideHero);
	withoutPerk.authorityPerks[1].clear();
	const auto noCompositionPerk = newHorizonsHeroes::leadershipStackReadout(attackerSideHero,
		live->battleGetUnitByID(pikeId)->creatureId(), before.count, &original, nullptr, &withoutPerk);
	ASSERT_GT(before.capacity, noCompositionPerk.capacity);
	int64_t damage = child->battleGetUnitByID(angelId)->getAvailableHealth();
	child->getForUpdate(angelId)->damage(damage); // No remaining Champion; captured eligibility survives.
	battle::UnitInfo summon;
	summon.id = child->nextUnitId();
	summon.count = 1;
	summon.type = CreatureID(CreatureID::decode("core:pikeman"));
	summon.side = BattleSide::ATTACKER;
	summon.position = BattleHex(70);
	summon.summoned = true; // Duplicate line must not invalidate Regimental Organization.
	JsonNode data;
	summon.save(data);
	child->addUnit(summon.id, data);
	const auto arrival = child->battleLeadershipReadout(child->battleGetUnitByID(summon.id));
	EXPECT_EQ(arrival.capacity, before.capacity);
	EXPECT_EQ(arrival.effectiveLeadership, before.effectiveLeadership);
	EXPECT_DOUBLE_EQ(arrival.efficiency, 1.0);
	EXPECT_THROW(seed->occurrence(summon.id), std::invalid_argument);
	EXPECT_EQ(child->battleLeadershipReadout(child->battleGetUnitByID(pikeId)).capacity, before.capacity);
	damage = child->battleGetUnitByID(pikeId)->getMaxHealth() * 500;
	child->getForUpdate(pikeId)->damage(damage);
	const auto after = child->battleLeadershipReadout(child->battleGetUnitByID(pikeId));
	EXPECT_EQ(after.count, 500);
	EXPECT_GT(after.efficiency, before.efficiency);
	EXPECT_EQ(after.capacity, before.capacity);
	auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
	const auto descendantSummon = grandchild->getForUpdate(summon.id);
	ASSERT_EQ(descendantSummon->getCount(), 1);
	EXPECT_EQ(grandchild->battleLeadershipReadout(descendantSummon.get()).capacity, before.capacity);
	ASSERT_TRUE(descendantSummon->health.appendTemporaryCreatures(4));
	EXPECT_EQ(descendantSummon->getCount(), 5);
	EXPECT_EQ(child->battleGetUnitByID(summon.id)->getCount(), 1);
	damage = grandchild->battleGetUnitByID(pikeId)->getMaxHealth() * 100;
	grandchild->getForUpdate(pikeId)->damage(damage);
	EXPECT_EQ(grandchild->battleGetUnitByID(pikeId)->getCount(), 400);
	EXPECT_EQ(child->battleGetUnitByID(pikeId)->getCount(), 500);
	for(const auto * branch : {parent.get(), child.get(), sibling.get(), grandchild.get()})
		EXPECT_EQ(branch->getLeadershipComposition(BattleSide::ATTACKER), original);
	for(const auto * branch : {parent.get(), sibling.get()})
	{
		EXPECT_EQ(branch->battleGetUnitByID(pikeId)->getCount(), 1000);
		EXPECT_EQ(branch->battleGetUnitByID(summon.id), nullptr);
	}
	EXPECT_EQ(live->battleGetUnitByID(pikeId)->getCount(), 1000);
	EXPECT_EQ(live->battleGetUnitByID(summon.id), nullptr);
	EXPECT_EQ(battle()->getLeadershipComposition(BattleSide::ATTACKER), original);
}

TEST_P(LeadershipProjectionTest, ProjectedHeroLeadershipChangesOnlyChildCapacityAndAttackDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto readout = [&](const CBattleInfoCallback & branch)
		{ return branch.battleLeadershipReadout(branch.battleGetUnitByID(pikeId)); };
	const auto damage = [&](const CBattleInfoCallback & branch)
		{ return branch.calculateDmgRange(BattleAttackInfo(branch.battleGetUnitByID(pikeId),
			branch.battleGetUnitByID(enemyId), 0, false)).damage.min; };
	const auto before = readout(*parent);
	const auto originalDamage = damage(*parent);
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::LEADERSHIP, BonusSource::OTHER, 10000, BonusSourceID());
	bonus.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	child->addUnitBonus(pikeId, {bonus}); // Originating export reaches the branch hero rating.
	EXPECT_GT(readout(*child).capacity, before.capacity);
	EXPECT_GT(readout(*child).efficiency, before.efficiency);
	EXPECT_GT(damage(*child), originalDamage);
	for(const auto * branch : {parent.get(), sibling.get()})
	{
		EXPECT_EQ(readout(*branch).capacity, before.capacity);
		EXPECT_DOUBLE_EQ(readout(*branch).efficiency, before.efficiency);
		EXPECT_EQ(damage(*branch), originalDamage);
	}
	EXPECT_EQ(readout(*live).capacity, before.capacity);
	EXPECT_EQ(damage(*live), originalDamage);
}

INSTANTIATE_TEST_SUITE_P(AuthorityAdvancedPerks, LeadershipProjectionTest, ::testing::Values(
	"new-horizons:authority.regimentalOrganization", "new-horizons:authority.combinedArms"));
