/*
 * NewHorizonsPhoenixSparkTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CStack.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>

namespace
{
class SparkEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SparkEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class SparkCallback final : public CBattleCallback
{
public:
	explicit SparkCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
};
}

class NewHorizonsPhoenixSparkTest : public HeroCommandFixture
{
protected:
	static constexpr auto skillKey = "new-horizons:elementalRebirth";
	static constexpr auto sparkKey = "new-horizons:elementalRebirth.phoenixSpark";
	CStack * source = nullptr;
	CStack * other = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(const auto & perk : perks["skills"][skillKey]["perks"].Vector())
			if(perk["id"].String() == sparkKey)
			{
				ASSERT_EQ(perk["effect"]["status"].String(), "active");
				found = true;
			}
		ASSERT_TRUE(found);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	static CreatureID creature(const char * key) { return CreatureID(CreatureID::decode(key)); }
	void prepare(bool selected = true, bool greater = false, const char * sourceKey = "core:angel")
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill skill(SecondarySkill::decode(skillKey));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({skillKey, "new-horizons:elementalRebirth.primalBurst"});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({skillKey, greater
			? "new-horizons:elementalRebirth.greaterEssence" : "new-horizons:elementalRebirth.rebirthChain"});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
			defenderSideHero->applyPerkSelection({skillKey, sparkKey});
		ASSERT_EQ(defenderSideHero->hasActivePerk(skillKey, sparkKey), selected);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature(sourceKey), 3));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creature("core:angel"), 4));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 1000));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
				other = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				enemy = battle()->getStack(unit->unitId(), false);
		}
		ASSERT_NE(source, nullptr);
		ASSERT_NE(other, nullptr);
		ASSERT_NE(enemy, nullptr);
		const auto phoenix = creature("core:phoenix");
		ASSERT_TRUE(phoenix.toCreature()->isDoubleWide());
		// Original single-wide Champions start at the defender's x15 edge.
		// A defender Phoenix needs the adjacent x16 tail, which is unavailable.
		ASSERT_FALSE(battle()->getAccessibility(source).accessible(source->getPosition(), true, source->unitSide()));
		const BattleHex sourcePosition(source->getPosition().toInt() - 1);
		const BattleHex otherPosition(other->getPosition().toInt() - 1);
		ASSERT_TRUE(battle()->getAccessibility(source).accessible(sourcePosition, source));
		battle()->moveUnit(source->unitId(), sourcePosition);
		ASSERT_TRUE(battle()->getAccessibility(other).accessible(otherPosition, other));
		battle()->moveUnit(other->unitId(), otherPosition);
		ASSERT_TRUE(battle()->getAccessibility(source).accessible(sourcePosition, true, source->unitSide()));
		ASSERT_TRUE(battle()->getAccessibility(other).accessible(otherPosition, true, other->unitSide()));
		// These are still the original strategic stacks, not synthetic summons.
		ASSERT_EQ(source->base, defenderSideHero->getStackPtr(SlotID(0)));
		ASSERT_EQ(other->base, defenderSideHero->getStackPtr(SlotID(1)));
		beginCombat();
	}
	void hit(CStack * target, int64_t damage)
	{
		BattleStackAttacked entry;
		entry.attackerID = enemy->unitId();
		entry.stackAttacked = target->unitId();
		entry.damageAmount = damage;
		CStack::prepareAttacked(entry, gameHandler->getRandomGenerator(), target->acquireState());
		StacksInjured pack;
		pack.battleID = BattleID(0);
		pack.stacks.push_back(std::move(entry));
		gameHandler->sendAndApply(pack);
	}
	CStack * summonedAt(BattleHex position) const
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->alive() && unit->isSummoned() && unit->getPosition() == position)
				return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}
	auto capture(CStack * target) const
	{
		return newHorizonsElementalRebirth::captureDeathSource(*target, defenderSideHero,
			battle()->getRebirthChainUsed(target->unitSide()), &battle()->getCreatureCategoryRules(),
			battle()->getPhoenixSparkUsed(target->unitSide()));
	}
	void deathWithoutReaction(CStack * target)
	{
		auto state = target->acquireState();
		auto damage = state->getAvailableHealth();
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		update.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(update);
	}
};

TEST_F(NewHorizonsPhoenixSparkTest, ActualChampionDeathUsesFrozenBattleStartQuarterAndTemporaryPhoenix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maximum = source->getBattleStartMaximumAggregateHP();
	hit(source, 1);
	hit(source, source->getAvailableHealth());
	const auto * phoenix = summonedAt(source->getPosition());
	ASSERT_NE(phoenix, nullptr);
	EXPECT_EQ(phoenix->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_EQ(phoenix->getAvailableHealth(), maximum / 4);
	EXPECT_TRUE(phoenix->isSummoned());
	EXPECT_TRUE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*phoenix, defenderSideHero,
		false, &battle()->getCreatureCategoryRules(), true));
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsPhoenixSparkTest, GreaterEssenceDoesNotIncreaseExplicitQuarter)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	const auto maximum = source->getBattleStartMaximumAggregateHP();
	hit(source, source->getAvailableHealth());
	ASSERT_NE(summonedAt(source->getPosition()), nullptr);
	EXPECT_EQ(summonedAt(source->getPosition())->getAvailableHealth(), maximum / 4);
}

TEST_F(NewHorizonsPhoenixSparkTest, PhoenixSubstitutionRetainsActualPrimalBurstHook)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bool positioned = false;
	for(const auto & hex : source->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, true)
			&& hex != battle::Unit::occupiedHex(source->getPosition(), true, source->unitSide()))
		{
			battle()->moveUnit(enemy->unitId(), hex);
			positioned = true;
			break;
		}
	ASSERT_TRUE(positioned);
	const auto before = enemy->getAvailableHealth();
	const auto budget = newHorizonsElementalRebirth::primalBurstDamageBudget(
		source->getBattleStartMaximumAggregateHP() / 4);
	hit(source, source->getAvailableHealth());
	ASSERT_NE(summonedAt(source->getPosition()), nullptr);
	EXPECT_EQ(before - enemy->getAvailableHealth(), budget);
}

TEST_F(NewHorizonsPhoenixSparkTest, SecondChampionDeathUsesOrdinaryExpertRebirthNotAnotherSpark)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	hit(source, source->getAvailableHealth());
	hit(other, other->getAvailableHealth());
	const auto * first = summonedAt(source->getPosition());
	const auto * second = summonedAt(other->getPosition());
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(first->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_NE(second->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_EQ(second->getAvailableHealth(), other->getBattleStartMaximumAggregateHP() / 2);
	EXPECT_TRUE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsPhoenixSparkTest, NoChampionDeathRetainsOrdinaryRebirthAndUnusedSpark)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, "core:peasant"));
	ASSERT_TRUE(capture(source));
	EXPECT_FALSE(capture(source)->phoenixSpark);
	hit(source, source->getAvailableHealth());
	ASSERT_NE(summonedAt(source->getPosition()), nullptr);
	EXPECT_NE(summonedAt(source->getPosition())->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsPhoenixSparkTest, RankWithoutSelectedPerkRetainsOrdinaryChampionRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	hit(source, source->getAvailableHealth());
	ASSERT_NE(summonedAt(source->getPosition()), nullptr);
	EXPECT_NE(summonedAt(source->getPosition())->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_EQ(summonedAt(source->getPosition())->getAvailableHealth(),
		source->getBattleStartMaximumAggregateHP() / 2);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsPhoenixSparkTest, ReceiptRejectsLivingWrongCategoryAndDuplicateBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, "core:peasant"));
	auto clone = other->acquireState();
	clone->cloned = true;
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*clone, defenderSideHero,
		false, &battle()->getCreatureCategoryRules(), false));
	auto phantom = other->acquireState();
	phantom->summoned = true;
	phantom->natureSummoned = false;
	phantom->cloned = false;
	phantom->initializePhantomProfile(100, 2);
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*phantom, defenderSideHero,
		false, &battle()->getCreatureCategoryRules(), false));
	BattleUnitsChanged receipt;
	receipt.battleID = BattleID(0);
	receipt.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{
		BattleSide::DEFENDER, other->unitId()};
	EXPECT_THROW(gameHandler->sendAndApply(receipt), std::runtime_error);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	deathWithoutReaction(source);
	receipt.phoenixSparkConsumption->sourceUnitId = source->unitId();
	EXPECT_THROW(gameHandler->sendAndApply(receipt), std::runtime_error);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	deathWithoutReaction(other);
	receipt.phoenixSparkConsumption->sourceUnitId = other->unitId();
	ASSERT_NO_THROW(gameHandler->sendAndApply(receipt));
	EXPECT_TRUE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_THROW(gameHandler->sendAndApply(receipt), std::runtime_error);
}

TEST_F(NewHorizonsPhoenixSparkTest, BlockedPlacementDoesNotShiftFirstReceiptToAnotherChampion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto first = capture(source);
	ASSERT_TRUE(first);
	ASSERT_TRUE(first->phoenixSpark);
	deathWithoutReaction(source);
	BattleUnitsChanged receipt;
	receipt.battleID = BattleID(0);
	receipt.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{
		BattleSide::DEFENDER, source->unitId()};
	gameHandler->sendAndApply(receipt);
	auto blocked = battle()->getAccessibility();
	blocked[source->getPosition().toInt()] = EAccessibility::OBSTACLE;
	EXPECT_TRUE(newHorizonsElementalRebirth::legalCandidatePool(*battle(), blocked, *first).empty());
	const auto next = capture(other);
	ASSERT_TRUE(next);
	EXPECT_FALSE(next->phoenixSpark);
	hit(other, other->getAvailableHealth());
	ASSERT_NE(summonedAt(other->getPosition()), nullptr);
	EXPECT_NE(summonedAt(other->getPosition())->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
}

TEST_F(NewHorizonsPhoenixSparkTest, DetachedActualDeathMatchesLiveAndForkedOnceReceiptIsIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SparkEnvironment environment(gameState());
	auto callback = std::make_shared<SparkCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
	callback->onBattleStarted(battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto snapshot = child->captureElementalRebirthSource(*child->battleGetUnitByID(source->unitId()));
	ASSERT_TRUE(snapshot);
	ASSERT_TRUE(snapshot->phoenixSpark);
	auto doomed = child->getForUpdate(source->unitId());
	auto damage = doomed->getAvailableHealth();
	doomed->damage(damage);
	const auto output = child->projectElementalRebirth(doomed.get(), *snapshot, true, false, false);
	ASSERT_TRUE(output);
	const auto * phoenix = child->battleGetUnitByID(*output);
	ASSERT_NE(phoenix, nullptr);
	EXPECT_EQ(phoenix->creatureId(), CreatureID(CreatureID::decode("core:phoenix")));
	EXPECT_EQ(phoenix->getAvailableHealth(), snapshot->battleStartMaximumAggregateHP / 4);
	EXPECT_TRUE(child->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(parent->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(sibling->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	hit(source, source->getAvailableHealth());
	ASSERT_NE(summonedAt(source->getPosition()), nullptr);
	EXPECT_EQ(summonedAt(source->getPosition())->getAvailableHealth(), phoenix->getAvailableHealth());
	auto grandchild = std::make_shared<HypotheticBattle>(&environment, child);
	EXPECT_TRUE(grandchild->getPhoenixSparkUsed(BattleSide::DEFENDER));
	const auto next = grandchild->captureElementalRebirthSource(*grandchild->battleGetUnitByID(other->unitId()));
	ASSERT_TRUE(next);
	EXPECT_FALSE(next->phoenixSpark);
}

TEST(NewHorizonsPhoenixSparkProtocolTest, CurrentReceiptRoundTripsAndOldWritersRejectBeforePrefixes)
{
	SideInBattle side(nullptr);
	side.phoenixSparkUsed = true;
	CMemorySerializer current;
	current.oser & side;
	SideInBattle restored(nullptr);
	current.iser & restored;
	EXPECT_TRUE(restored.phoenixSparkUsed);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(old.oser & side, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	side.phoenixSparkUsed = false;
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	legacy.oser & side;
	restored.phoenixSparkUsed = true;
	legacy.iser & restored;
	EXPECT_FALSE(restored.phoenixSparkUsed);
	CGameState callback;
	callback.preInit(LIBRARY);
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = std::make_unique<BattleInfo>(&callback);
	start.info->setPhoenixSparkUsed(BattleSide::DEFENDER, true);
	CMemorySerializer outer;
	outer.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(outer.oser & start, std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	BattleUnitsChanged receipt;
	receipt.battleID = BattleID(0);
	receipt.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{BattleSide::DEFENDER, 7};
	CMemorySerializer packet;
	packet.oser & receipt;
	BattleUnitsChanged packetCopy;
	packet.iser & packetCopy;
	ASSERT_TRUE(packetCopy.phoenixSparkConsumption);
	EXPECT_EQ(packetCopy.phoenixSparkConsumption->sourceUnitId, 7u);
	CMemorySerializer packetOld;
	packetOld.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(packetOld.oser & receipt, std::runtime_error);
	EXPECT_TRUE(packetOld.extractBuffer().empty());
	receipt.changedStacks.emplace_back(7, UnitChanges::EOperation::REMOVE);
	EXPECT_THROW(receipt.validatePhoenixSparkShape(), std::runtime_error);
}
