/*
 * NewHorizonsElementalMemoryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr std::string_view SKILL = "new-horizons:elementalRebirth";
constexpr std::string_view MEMORY = "new-horizons:elementalRebirth.elementalMemory";

class MemoryEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MemoryEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsElementalMemoryTest : public HeroCommandFixture
{
protected:
	CStack * source = nullptr;
	CStack * enemy = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped-active New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(const auto & entry : perks["skills"][std::string(SKILL)]["perks"].Vector())
			if(entry["id"].String() == MEMORY)
			{
				ASSERT_EQ(entry["effect"]["status"].String(), "active");
				found = true;
			}
		ASSERT_TRUE(found);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(std::string_view perk)
	{
		const auto lookup = [this](const std::string & id) { return defenderSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = defenderSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t i = 0; i < offers.size(); ++i)
				if(offers[i].selection.skillId == SKILL && offers[i].selection.perkId == perk)
				{
					gameHandler->levelUpHero(defenderSideHero, offers, i, seed, false);
					ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(SKILL), std::string(perk)));
					return;
				}
		}
		FAIL() << "No legal Elemental Rebirth offer " << perk;
	}
	void prepare(bool memory = true, bool chain = false)
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL)));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(memory)
			ASSERT_NO_FATAL_FAILURE(select(MEMORY));
		if(chain)
		{
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:elementalRebirth.rebirthChain"));
		}
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 101));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSlot() == SlotID(0))
			{
				if(unit->unitSide() == BattleSide::DEFENDER)
					source = battle()->getStack(unit->unitId(), false);
				else
					enemy = battle()->getStack(unit->unitId(), false);
			}
		ASSERT_NE(source, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}
	void modifier(CStack * unit, BonusType type, int value)
	{
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{
			Bonus(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID())});
		gameHandler->sendAndApply(effect);
	}
	void aura(BonusType type, int value)
	{
		defenderSideHero->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, value, BonusSourceID()));
	}
	CStack * destroy(CStack * unit)
	{
		const auto id = battle()->battleNextUnitId();
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.attackerID = enemy->unitId();
		hit.stackAttacked = unit->unitId();
		hit.damageAmount = unit->getAvailableHealth();
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), unit->acquireState());
		gameHandler->sendAndApply(packet);
		return battle()->getStack(id, false);
	}
	int memoryValue(const battle::Unit & unit, BonusType type) const
	{
		const auto modifiers = unit.getAllBonuses(Selector::type()(type).And(CSelector([](const Bonus * bonus)
		{ return bonus && newHorizonsElementalRebirth::isElementalMemoryBonus(*bonus); })));
		return modifiers->totalValue();
	}
	void nextRound()
	{
		BattleNextRound round;
		round.battleID = BattleID(0);
		gameHandler->sendAndApply(round);
	}
};
}

TEST_F(NewHorizonsElementalMemoryTest, PublicSelectionActualSpawnCopiesPositiveModifiersAndPreservesImmunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsElementalRebirth::activeProfile(defenderSideHero)->elementalMemory);
	modifier(source, BonusType::MORALE, 4);
	modifier(source, BonusType::LUCK, 4);
	const auto morale = source->valOfBonuses(BonusType::MORALE);
	const auto luck = source->valOfBonuses(BonusType::LUCK);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	EXPECT_GT(memoryValue(*reborn, BonusType::MORALE), 0);
	EXPECT_GT(memoryValue(*reborn, BonusType::LUCK), 0);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::MORALE), morale);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::LUCK), luck);
	EXPECT_TRUE(reborn->unaffectedByMorale());
	EXPECT_EQ(reborn->moraleVal(), 0);
	EXPECT_LE(reborn->luckVal(), static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE).size()));
}

TEST_F(NewHorizonsElementalMemoryTest, ExistingSharedAuraIsNotAddedAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	aura(BonusType::MORALE, 3);
	aura(BonusType::LUCK, 3);
	const auto morale = source->valOfBonuses(BonusType::MORALE);
	const auto luck = source->valOfBonuses(BonusType::LUCK);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	EXPECT_EQ(memoryValue(*reborn, BonusType::LUCK), 0);
	EXPECT_EQ(memoryValue(*reborn, BonusType::MORALE), 0);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::MORALE), morale);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::LUCK), luck);
}

TEST_F(NewHorizonsElementalMemoryTest, NegativeNetMoraleIsNotInheritedWhilePositiveLuckIs)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	modifier(source, BonusType::MORALE, 4);
	modifier(source, BonusType::MORALE, -100);
	modifier(source, BonusType::LUCK, 4);
	const auto snapshot = newHorizonsElementalRebirth::captureDeathSource(*source, defenderSideHero);
	ASSERT_TRUE(snapshot);
	EXPECT_EQ(snapshot->positiveMoraleModifier, 0);
	EXPECT_GT(snapshot->positiveLuckModifier, 0);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	EXPECT_EQ(memoryValue(*reborn, BonusType::MORALE), 0);
	EXPECT_GT(memoryValue(*reborn, BonusType::LUCK), 0);
}

TEST_F(NewHorizonsElementalMemoryTest, UnselectedSameRankHasNoMemoryCaptureOrSpawnBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	modifier(source, BonusType::MORALE, 4);
	modifier(source, BonusType::LUCK, 4);
	const auto snapshot = newHorizonsElementalRebirth::captureDeathSource(*source, defenderSideHero);
	ASSERT_TRUE(snapshot);
	EXPECT_FALSE(snapshot->profile.elementalMemory);
	EXPECT_EQ(snapshot->positiveMoraleModifier, 0);
	EXPECT_EQ(snapshot->positiveLuckModifier, 0);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	EXPECT_EQ(memoryValue(*reborn, BonusType::MORALE), 0);
	EXPECT_EQ(memoryValue(*reborn, BonusType::LUCK), 0);
}

TEST_F(NewHorizonsElementalMemoryTest, NewbornSpecificNegativeAuraIsPreservedWithoutCompensation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	aura(BonusType::LUCK, 2);
	modifier(source, BonusType::LUCK, 4);
	for(const auto * id : {"core:airElemental", "core:waterElemental", "core:fireElemental", "core:earthElemental", "core:magicElemental"})
	{
		auto negative = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK,
			BonusSource::OTHER, -3, BonusSourceID());
		negative->limiter = std::make_shared<CCreatureTypeLimiter>(*creatureByName(id).toCreature(), false);
		defenderSideHero->addNewBonus(negative);
	}
	ASSERT_EQ(source->valOfBonuses(BonusType::LUCK), 6);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	EXPECT_EQ(memoryValue(*reborn, BonusType::LUCK), 4);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::LUCK), 3); // +2 shared +4 Memory -3 newborn aura
	nextRound();
	EXPECT_EQ(memoryValue(*reborn, BonusType::LUCK), 0);
	EXPECT_EQ(reborn->valOfBonuses(BonusType::LUCK), -1);
}

TEST_F(NewHorizonsElementalMemoryTest, LiveRoundExpiryAndOrdinaryBonusPersistence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	modifier(source, BonusType::MORALE, 4);
	modifier(source, BonusType::LUCK, 4);
	auto * reborn = destroy(source);
	ASSERT_NE(reborn, nullptr);
	CMemorySerializer memory;
	ASSERT_NO_THROW(reborn->serialize(memory.oser));
	CStack decoded;
	ASSERT_NO_THROW(decoded.serialize(memory.iser));
	decoded.localInit(battle());
	EXPECT_EQ(memoryValue(decoded, BonusType::LUCK), memoryValue(*reborn, BonusType::LUCK));
	const auto saved = decoded.getBonusesOfType(BonusType::LUCK);
	EXPECT_TRUE(std::any_of(saved->begin(), saved->end(), [](const auto & bonus)
	{ return newHorizonsElementalRebirth::isElementalMemoryBonus(*bonus) && bonus->turnsRemain == 1; }));
	decoded.detachFromAll();
	nextRound();
	EXPECT_EQ(memoryValue(*reborn, BonusType::MORALE), 0);
	EXPECT_EQ(memoryValue(*reborn, BonusType::LUCK), 0);
}

TEST_F(NewHorizonsElementalMemoryTest, MaterializedChildrenKeepMemoryUntilTheirOwnRoundAfterParentExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	modifier(source, BonusType::MORALE, 4);
	modifier(source, BonusType::LUCK, 4);
	MemoryEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedSource = parent->getForUpdate(source->unitId());
	const auto captured = parent->captureElementalRebirthSource(*projectedSource);
	ASSERT_TRUE(captured);
	auto damage = projectedSource->getAvailableHealth();
	projectedSource->damage(damage);
	const auto id = parent->projectElementalRebirth(projectedSource.get(), *captured, true, false, false);
	ASSERT_TRUE(id);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto parentUnit = parent->getForUpdate(*id);
	auto childUnit = child->getForUpdate(*id);
	auto siblingUnit = sibling->getForUpdate(*id);
	ASSERT_GT(memoryValue(*childUnit, BonusType::LUCK), 0);
	const auto pending = memoryValue(*parentUnit, BonusType::LUCK);
	const auto pendingMorale = memoryValue(*parentUnit, BonusType::MORALE);
	// Both children are materialized while Memory is active. The parent can
	// advance independently without retroactively rewriting these snapshots.
	parent->nextRound();
	EXPECT_EQ(memoryValue(*parentUnit, BonusType::LUCK), 0);
	EXPECT_EQ(memoryValue(*parentUnit, BonusType::MORALE), 0);
	EXPECT_EQ(memoryValue(*childUnit, BonusType::LUCK), pending);
	EXPECT_EQ(memoryValue(*childUnit, BonusType::MORALE), pendingMorale);
	EXPECT_EQ(memoryValue(*siblingUnit, BonusType::LUCK), pending);
	EXPECT_EQ(memoryValue(*siblingUnit, BonusType::MORALE), pendingMorale);
	child->nextRound();
	EXPECT_EQ(memoryValue(*childUnit, BonusType::LUCK), 0);
	EXPECT_EQ(memoryValue(*childUnit, BonusType::MORALE), 0);
	EXPECT_EQ(memoryValue(*parentUnit, BonusType::LUCK), 0);
	EXPECT_EQ(memoryValue(*siblingUnit, BonusType::LUCK), pending);
	sibling->nextRound();
	EXPECT_EQ(memoryValue(*siblingUnit, BonusType::LUCK), 0);
	EXPECT_EQ(memoryValue(*siblingUnit, BonusType::MORALE), 0);
	EXPECT_TRUE(source->alive());
	EXPECT_EQ(memoryValue(*source, BonusType::LUCK), 0);
	EXPECT_EQ(battle()->battleGetUnitByID(*id), nullptr);
}

TEST_F(NewHorizonsElementalMemoryTest, ChainCapturesCurrentFirstGenerationModifiers)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	modifier(source, BonusType::LUCK, 4);
	auto * first = destroy(source);
	ASSERT_NE(first, nullptr);
	modifier(first, BonusType::LUCK, 2);
	const auto current = first->valOfBonuses(BonusType::LUCK);
	auto * second = destroy(first);
	ASSERT_NE(second, nullptr);
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_EQ(second->valOfBonuses(BonusType::LUCK), current);
	EXPECT_TRUE(second->unaffectedByMorale());
	nextRound();
	EXPECT_EQ(memoryValue(*second, BonusType::LUCK), 0);
}
