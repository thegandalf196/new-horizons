/*
 * NewHorizonsMercenaryCombatTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/JsonSerializer.h"
#include "../../../lib/serializer/JsonDeserializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
using newHorizonsTraining::Receipt;
struct CohortPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature != Version::NEW_HORIZONS_DIPLOMACY_COHORTS; }
	template <typename T> CohortPrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached ordinary payload");
	}
};
template <typename T> void rejectsBeforePrefix(T & object)
{
	CohortPrefixProbe probe;
	EXPECT_THROW(object.serialize(probe), std::runtime_error);
	EXPECT_EQ(probe.fields, 0);
}
class CohortEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CohortEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}
class NewHorizonsMercenaryCombatTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons profile";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto id : {newHorizonsTraining::MERCENARY_CAPTAIN, newHorizonsTraining::LOYAL_MERCENARIES})
		{
			bool found = false;
			for(const auto & entry : rules["skills"][newHorizonsTraining::DIPLOMACY]["perks"].Vector())
				if(entry["id"].String() == id)
				{
					EXPECT_EQ(entry["effect"]["status"].String(), "active");
					found = true;
				}
			EXPECT_TRUE(found);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	void select(const char * perk, int rank)
	{
		const SecondarySkill skill(SecondarySkill::decode(newHorizonsTraining::DIPLOMACY));
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsTraining::DIPLOMACY, perk));
					return;
				}
		}
		FAIL() << "No legal public offer for " << perk;
	}
	void prepare(bool captain = true, bool loyal = false)
	{
		startGame();
		if(captain) select(newHorizonsTraining::MERCENARY_CAPTAIN, MasteryLevel::BASIC);
		else if(loyal)
		{
			select("new-horizons:diplomacy.envoy", MasteryLevel::BASIC); // Legal, combat-inert prerequisite.
			ASSERT_FALSE(attackerSideHero->hasActivePerk(newHorizonsTraining::DIPLOMACY, newHorizonsTraining::MERCENARY_CAPTAIN));
		}
		if(loyal) select(newHorizonsTraining::LOYAL_MERCENARIES, MasteryLevel::ADVANCED);
	}
	CStackInstance * cohort(SlotID slot = SlotID(0))
	{
		auto * stack = attackerSideHero->getStackPtr(slot);
		Receipt receipt = stack->getTrainingReceipt();
		receipt.admittedThroughDiplomacy(attackerSideHero->id);
		stack->setTrainingReceipt(receipt);
		attackerSideHero->armyChanged();
		return stack;
	}
	void acceptedBattle()
	{
		gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
		ASSERT_NE(battle(), nullptr);
		if(battle()->tacticDistance > 0)
		{
			const auto side = battle()->tacticsSide;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
				BattleAction::makeEndOFTacticPhase(side)));
		}
	}
	CStack * original(SlotID slot = SlotID(0))
	{
		for(const auto * unit : battle()->getStacksIf([slot](const CStack * stack)
			{ return stack->unitSide() == BattleSide::ATTACKER && stack->base && stack->unitSlot() == slot; }))
			return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}
	double factionMorale() const
	{
		const auto bonus = attackerSideHero->getExportedBonusList().getFirst(
			Selector::sourceType()(BonusSource::ARMY).And(Selector::type()(BonusType::MORALE))
				.And(CSelector([](const Bonus * b) { return b->sid != BonusSourceID(BonusCustomSource::undeadMoraleDebuff); })));
		return bonus ? bonus->val : 0;
	}
	void mixedArmy()
	{
		attackerSideHero->clearSlots();
		for(const auto & [slot, id] : std::vector<std::pair<int,const char *>>{{0,"core:pikeman"},{1,"core:centaur"},{2,"core:gremlin"}})
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(slot), CreatureID(CreatureID::decode(id)), 1));
	}
};
TEST_F(NewHorizonsMercenaryCombatTest, ActualAcceptedOpeningGrantsNonadditiveCaptainAndConsumesOne)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * strategic = cohort();
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * live = original();
	ASSERT_NE(live, nullptr);
	EXPECT_TRUE(live->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
	EXPECT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 2);
	EXPECT_EQ(live->getAllBonuses(CSelector(newHorizonsTraining::isMercenaryBonus))->size(), 1u);
	EXPECT_EQ(battle()->trainingEntrySnapshot.stacks.front().previous.mercenaryOrigins.front().combatsRemaining, 3);
}
TEST_F(NewHorizonsMercenaryCombatTest, ActualOpeningWithoutCaptainStillConsumesAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * strategic = cohort();
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	EXPECT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 2);
	EXPECT_FALSE(original()->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
}
TEST_F(NewHorizonsMercenaryCombatTest, ExhaustedAllowanceAndLateAcquisitionDoNotReset)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * strategic = cohort();
	auto receipt = strategic->getTrainingReceipt();
	for(int battleIndex = 0; battleIndex < 3; ++battleIndex)
		receipt = newHorizonsTraining::afterBattleEntry(receipt, 1, false, attackerSideHero->id);
	strategic->setTrainingReceipt(receipt);
	ASSERT_NO_FATAL_FAILURE(select(newHorizonsTraining::MERCENARY_CAPTAIN, MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	EXPECT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 0);
	EXPECT_FALSE(original()->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
	EXPECT_TRUE(strategic->getTrainingReceipt().recruitedBy(attackerSideHero->id));
}
TEST_F(NewHorizonsMercenaryCombatTest, LoyalRelievesOnlyNegativeFactionMixAndSpentOriginsPersist)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_NO_FATAL_FAILURE(mixedArmy());
	EXPECT_EQ(factionMorale(), -1);
	auto * strategic = cohort(SlotID(2));
	auto receipt = strategic->getTrainingReceipt();
	receipt.mercenaryOrigins.front().combatsRemaining = 0;
	strategic->setTrainingReceipt(receipt);
	attackerSideHero->armyChanged();
	EXPECT_EQ(factionMorale(), 0);
	cohort(SlotID(1));
	EXPECT_EQ(factionMorale(), 0) << "Excluding mercenaries does not fabricate unity";
}
TEST_F(NewHorizonsMercenaryCombatTest, LoyalPreservesTrueUnityAndUndeadPenalty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 1));
	cohort();
	EXPECT_EQ(factionMorale(), 1);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), CreatureID(CreatureID::decode("core:skeleton")), 1));
	cohort(SlotID(1));
	EXPECT_EQ(factionMorale(), 0);
	EXPECT_TRUE(attackerSideHero->hasBonus(Selector::source(BonusSource::ARMY, BonusCustomSource::undeadMoraleDebuff)));
}
TEST_F(NewHorizonsMercenaryCombatTest, ForeignOriginDoesNotQualifyForEitherPerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	ASSERT_NO_FATAL_FAILURE(mixedArmy());
	auto * strategic = attackerSideHero->getStackPtr(SlotID(2));
	Receipt receipt;
	receipt.admittedThroughDiplomacy(defenderSideHero->id);
	strategic->setTrainingReceipt(receipt);
	attackerSideHero->armyChanged();
	EXPECT_EQ(factionMorale(), -1);
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	EXPECT_FALSE(original(SlotID(2))->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
	EXPECT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 3);
}
TEST_F(NewHorizonsMercenaryCombatTest, DetachedCaptainSnapshotAndNestedBranchesPreserveLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	cohort();
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * live = original();
	CohortEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	EXPECT_EQ(child->getForUpdate(live->unitId())->moraleVal(), live->moraleVal());
	EXPECT_TRUE(child->getForUpdate(live->unitId())->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
	EXPECT_EQ(parent->getForUpdate(live->unitId())->moraleVal(), sibling->getForUpdate(live->unitId())->moraleVal());
	EXPECT_TRUE(live->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
}
TEST_F(NewHorizonsMercenaryCombatTest, CurrentStrategicJsonAndReceiptBinaryRoundTrip)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * strategic = cohort();
	CMemorySerializer memory;
	auto saved = strategic->getTrainingReceipt();
	saved.serialize(memory.oser);
	Receipt restored;
	restored.serialize(memory.iser);
	EXPECT_EQ(restored, saved);
	JsonNode data;
	JsonSerializer writer(nullptr, data);
	strategic->serializeJson(writer);
	CStackInstance fromJson(gameState().get());
	JsonDeserializer reader(nullptr, data);
	fromJson.serializeJson(reader);
	EXPECT_EQ(fromJson.getTrainingReceipt(), saved);
}
TEST_F(NewHorizonsMercenaryCombatTest, AllStrategicAndBattleOuterOldWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * strategic = cohort();
	rejectsBeforePrefix(*strategic);
	rejectsBeforePrefix(*attackerSideHero);
	rejectsBeforePrefix(*map());
	rejectsBeforePrefix(*gameState());
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	rejectsBeforePrefix(lobby);
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	rejectsBeforePrefix(*battle());
	BattleStart start;
	start.info = BattleInfo::setupBattle(gameState().get(), {4,4,0}, gameState()->getTile({4,4,0})->getTerrainID(),
		BattleField(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", "core:sand_shore")),
		{attackerSideHero, defenderSideHero}, {attackerSideHero, defenderSideHero},
		BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero), nullptr);
	rejectsBeforePrefix(start);
}
TEST(NewHorizonsMercenaryProtocolTest, OriginUnionMaxSplitCopyPauseAndThreeEntries)
{
	Receipt first;
	first.admittedThroughDiplomacy(ObjectInstanceID(2));
	Receipt copy = first;
	copy = newHorizonsTraining::afterBattleEntry(copy, 1, false, ObjectInstanceID(9));
	EXPECT_EQ(copy, first);
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		copy = newHorizonsTraining::afterBattleEntry(copy, 1, false, ObjectInstanceID(2));
		EXPECT_EQ(copy.mercenaryOrigins.front().combatsRemaining, remaining);
	}
	copy.admittedThroughDiplomacy(ObjectInstanceID(2));
	EXPECT_EQ(copy.mercenaryOrigins.front().combatsRemaining, 0);
	Receipt second;
	second.admittedThroughDiplomacy(ObjectInstanceID(7));
	copy.merge(second);
	copy.merge(first);
	ASSERT_EQ(copy.mercenaryOrigins.size(), 2u);
	EXPECT_EQ(copy.mercenaryOrigins[0].combatsRemaining, 3);
	copy.crossedArmyBoundary();
	EXPECT_TRUE(copy.recruitedBy(ObjectInstanceID(2)));
	EXPECT_TRUE(copy.recruitedBy(ObjectInstanceID(7)));
}
TEST(NewHorizonsMercenaryProtocolTest, MalformedOriginsRejectAndLegacyReaderClearsOnlyOrigins)
{
	Receipt invalid;
	invalid.mercenaryOrigins = {{ObjectInstanceID(1),4}};
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	invalid.mercenaryOrigins = {{ObjectInstanceID(1),1},{ObjectInstanceID(1),2}};
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	invalid.mercenaryOrigins = {{ObjectInstanceID(2),1},{ObjectInstanceID(1),2}};
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	Receipt plain;
	plain.drillDeadline = 9;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_TRAINING;
	plain.serialize(memory.oser);
	Receipt restored;
	restored.admittedThroughDiplomacy(ObjectInstanceID(4));
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_TRAINING;
	restored.serialize(memory.iser);
	EXPECT_EQ(restored, plain);
}
TEST(NewHorizonsMercenaryProtocolTest, MovementBonusAndCompositePacketOldPrefixesReject)
{
	RebalanceStacks move;
	move.srcArmy = ObjectInstanceID(1); move.dstArmy = ObjectInstanceID(2);
	move.srcSlot = SlotID(0); move.dstSlot = SlotID(0); move.count = 1;
	move.diplomacyRecruiter = move.dstArmy;
	rejectsBeforePrefix(move);
	SwapStacks swap;
	swap.srcArmy = move.srcArmy; swap.dstArmy = move.dstArmy;
	swap.srcSlot = move.srcSlot; swap.dstSlot = move.dstSlot;
	swap.diplomacyRecruiter = move.dstArmy;
	rejectsBeforePrefix(swap);
	BulkRebalanceStacks bulk;
	bulk.moves = {move};
	rejectsBeforePrefix(bulk);
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::SECONDARY_SKILL, 1,
		BonusSourceID(SecondarySkill(SecondarySkill::decode(newHorizonsTraining::DIPLOMACY))));
	bonus.stacking = newHorizonsTraining::MERCENARY_BONUS;
	rejectsBeforePrefix(bonus);
	SetStackEffect effect;
	effect.toAdd = {{1, {bonus}}};
	rejectsBeforePrefix(effect);
}

TEST_F(NewHorizonsMercenaryCombatTest, ActualRestartReusesOpeningReceiptWithoutSecondConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * strategic = cohort();
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	const auto snapshot = battle()->trainingEntrySnapshot;
	ASSERT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 2);
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
	gameHandler->battles->restartBattle(BattleID(0), attackerSideHero, defenderSideHero, {4,4,0},
		attackerSideHero, defenderSideHero, layout, nullptr);
	auto * replay = gameState()->getBattle(attackerSideHero->getOwner());
	ASSERT_NE(replay, nullptr);
	EXPECT_EQ(replay->trainingEntrySnapshot, snapshot);
	EXPECT_EQ(strategic->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 2);
	for(const auto * unit : replay->getStacksIf([](const CStack * stack)
		{ return stack->unitSide() == BattleSide::ATTACKER && stack->base; }))
		EXPECT_TRUE(unit->hasBonus(CSelector(newHorizonsTraining::isMercenaryBonus)));
}
TEST_F(NewHorizonsMercenaryCombatTest, MalformedCurrentJsonRejectsBeforeStrategicCountMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * strategic = cohort();
	JsonNode data;
	JsonSerializer writer(nullptr, data);
	strategic->serializeJson(writer);
	data["training"]["mercenaryOrigins"].Vector().front()["combatsRemaining"].Integer() = 4;
	CStackInstance restored(gameState().get(), strategic->getCreatureID(), 7);
	JsonDeserializer reader(nullptr, data);
	EXPECT_THROW(restored.serializeJson(reader), std::runtime_error);
	EXPECT_EQ(restored.getCount(), 7);
	EXPECT_TRUE(restored.getTrainingReceipt().empty());
}
