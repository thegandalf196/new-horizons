/*
 * NewHorizonsRecruitmentTrainingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include <sstream>
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/TerrainHandler.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/JsonSerializer.h"
#include "../../../lib/serializer/JsonDeserializer.h"
#include "../../../server/queries/VisitQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
using newHorizonsTraining::Receipt;
class TrainingEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TrainingEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
struct TrainingPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature != Version::NEW_HORIZONS_RECRUITMENT_TRAINING; }
	template <typename T> TrainingPrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached ordinary payload");
	}
};
template <typename T> void rejectsBeforePrefix(T & object)
{
	TrainingPrefixProbe probe;
	EXPECT_THROW(object.serialize(probe), std::runtime_error);
	EXPECT_EQ(probe.fields, 0);
}
}

class NewHorizonsRecruitmentTrainingTest : public HeroCommandFixture
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
		for(const auto id : {newHorizonsTraining::DRILL_SERGEANT, newHorizonsTraining::FIELD_INSTRUCTOR,
			newHorizonsTraining::REINFORCEMENT_DRILL})
		{
			bool found = false;
			for(const auto & entry : rules["skills"][newHorizonsTraining::SKILL]["perks"].Vector())
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
		const SecondarySkill skill(SecondarySkill::decode(newHorizonsTraining::SKILL));
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [this](const std::string & skillId) { return attackerSideHero->getPerkSkillRank(skillId); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsTraining::SKILL, perk));
					return;
				}
		}
		FAIL() << "No legal public offer for " << perk;
	}
	void prepare(const char * advanced = newHorizonsTraining::FIELD_INSTRUCTOR, bool drill = true)
	{
		startGame();
		auto leadership = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LEADERSHIP,
			BonusSource::OTHER, 100000, BonusSourceID());
		attackerSideHero->addNewBonus(leadership);
		if(drill)
			select(newHorizonsTraining::DRILL_SERGEANT, MasteryLevel::BASIC);
		else if(advanced)
			select("new-horizons:recruitment.volunteerNetwork", MasteryLevel::BASIC);
		if(advanced)
			select(advanced, MasteryLevel::ADVANCED);
	}
	CGTownInstance * townForRecruitment(CreatureID creature, int count)
	{
		const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::TOWN, MapObjectSubID(0));
		const auto templates = handler->getTemplates();
		if(templates.empty()) return nullptr;
		auto instance = handler->create(gameState().get(), templates.front());
		auto * town = dynamic_cast<CGTownInstance *>(instance.get());
		if(!town) return nullptr;
		town->CGObjectInstance::setOwner(attackerSideHero->getOwner());
		town->setAnchorPos({20, 20, 0});
		town->creatures = {{static_cast<uint32_t>(count), {creature}}};
		town->setVisitingHero(attackerSideHero);
		map()->generateUniqueInstanceName(instance.get());
		map()->addNewObject(std::move(instance));
		return town;
	}
	CGDwelling * externalForRecruitment(CreatureID creature, int count)
	{
		const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::CREATURE_GENERATOR1, MapObjectSubID(56));
		const auto templates = handler->getTemplates();
		if(templates.empty()) return nullptr;
		auto instance = handler->create(gameState().get(), templates.front());
		auto * dwelling = dynamic_cast<CGDwelling *>(instance.get());
		if(!dwelling) return nullptr;
		dwelling->setOwner(attackerSideHero->getOwner());
		dwelling->setAnchorPos({20, 20, 0});
		dwelling->creatures = {{static_cast<uint32_t>(count), {creature}}};
		dwelling->clearSlots();
		map()->generateUniqueInstanceName(instance.get());
		map()->addNewObject(std::move(instance));
		return dwelling;
	}
	void paidRecruit(CreatureID creature, int count = 1)
	{
		auto * town = townForRecruitment(creature, count);
		ASSERT_NE(town, nullptr);
		gameHandler->giveResource(PlayerColor(0), EGameResID::GOLD, 100000);
		ASSERT_TRUE(gameHandler->recruitCreatures(town->id, attackerSideHero->id, creature, count, 0, PlayerColor(0)));
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
	CStack * original(SlotID slot)
	{
		for(auto * unit : battle()->getStacksIf([slot](const CStack * stack)
			{ return stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == slot; }))
			return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}
	void completeVictory()
	{
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto query = gameHandler->queries->topQuery(player);
			if(query && query->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
		}
	}
	BattleField validBattlefield()
	{
		return BattleField(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", "core:sand_shore"));
	}
	CreatureID creature(const char * key) { return CreatureID(CreatureID::decode(key)); }
};

TEST_F(NewHorizonsRecruitmentTrainingTest, PaidRecruitmentArmsWholeExistingStackAndFirstBattleConsumesDrill)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 4));
	const int baseAttack = attackerSideHero->getStackPtr(SlotID(0))->getAttack(false);
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	auto * strategic = attackerSideHero->getStackPtr(SlotID(0));
	ASSERT_NE(strategic, nullptr);
	EXPECT_EQ(strategic->getCount(), 5);
	EXPECT_TRUE(strategic->getTrainingReceipt().fieldPending);
	EXPECT_EQ(strategic->getTrainingReceipt().drillDeadline, gameState()->getCalendar().getCurrentDay() + 6);
	EXPECT_EQ(strategic->getAttack(false), baseAttack);
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * unit = original(SlotID(0));
	ASSERT_NE(unit, nullptr);
	EXPECT_TRUE(unit->hasBonus(CSelector([](const Bonus * bonus) { return bonus->stacking == "new-horizons:drillSergeant"; })));
	EXPECT_EQ(strategic->getTrainingReceipt().drillDeadline, -1);
	EXPECT_FALSE(strategic->getTrainingReceipt().fieldTrained);
	EXPECT_FALSE(unit->hasBonus(CSelector([](const Bonus * bonus)
		{ return newHorizonsTraining::isTrainingBonus(bonus) && bonus->stacking == "new-horizons:fieldInstructor"; })));
	const int nativeAttack = unit->valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::ATTACK)).And(Selector::sourceTypeSel(BonusSource::TERRAIN_NATIVE)));
	EXPECT_EQ(unit->getAttack(false), baseAttack + nativeAttack);
	ASSERT_NO_FATAL_FAILURE(completeVictory());
	EXPECT_TRUE(strategic->getTrainingReceipt().fieldTrained);
	EXPECT_FALSE(strategic->getTrainingReceipt().fieldPending);
	EXPECT_EQ(strategic->getAttack(false), baseAttack + 1);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, ActualFreeExternalAcceptanceArmsTrainingWithoutGold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	auto * dwelling = externalForRecruitment(pikeman, 2);
	ASSERT_NE(dwelling, nullptr);
	const auto resources = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto priorCount = attackerSideHero->getStackCount(attackerSideHero->getSlotFor(pikeman));
	// Public accepted-answer boundary dispatches to CGDwelling's real free
	// producer; do not call the new bridge directly or forge addToSlot provenance.
	const IObjectInterface * interface = dwelling;
	interface->blockingDialogAnswered(*gameHandler, attackerSideHero, 1);
	EXPECT_EQ(dwelling->creatures[0].first, 0u);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resources);
	const auto * stack = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(pikeman));
	ASSERT_NE(stack, nullptr);
	EXPECT_EQ(stack->getCount(), priorCount + 2);
	EXPECT_TRUE(stack->getTrainingReceipt().fieldPending);
	EXPECT_GE(stack->getTrainingReceipt().drillDeadline, 0);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, GenericArmyAdditionAndFailedPaidRecruitDoNotArm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 2));
	ASSERT_TRUE(gameHandler->addToSlot(StackLocation(attackerSideHero->id, SlotID(0)), pikeman.toCreature(), 1));
	EXPECT_TRUE(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt().empty());
	auto * town = townForRecruitment(pikeman, 0);
	ASSERT_NE(town, nullptr);
	EXPECT_FALSE(gameHandler->recruitCreatures(town->id, attackerSideHero->id, pikeman, 1, 0, PlayerColor(0)));
	EXPECT_TRUE(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt().empty());
}

TEST_F(NewHorizonsRecruitmentTrainingTest, DrillWindowInclusiveSeventhDayAndUndeadImmunityStillConsumes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(nullptr));
	const auto skeleton = creature("core:skeleton");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 1));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(skeleton));
	const int deadline = attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt().drillDeadline;
	gameState()->day = deadline;
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * unit = original(SlotID(0));
	ASSERT_NE(unit, nullptr);
	EXPECT_TRUE(unit->hasBonus(CSelector([](const Bonus * bonus) { return bonus->stacking == "new-horizons:drillSergeant"; })));
	EXPECT_EQ(unit->moraleVal(), 0);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt().drillDeadline, -1);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, ExpiredDrillWindowConsumedWithoutCombatBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare(nullptr));
	const auto pikeman = creature("core:pikeman");
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	auto * strategic = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(pikeman));
	gameState()->day = strategic->getTrainingReceipt().drillDeadline + 1;
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	EXPECT_FALSE(original(attackerSideHero->getSlotFor(pikeman))->hasBonus(CSelector(newHorizonsTraining::isTrainingBonus)));
	EXPECT_EQ(strategic->getTrainingReceipt().drillDeadline, -1);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, ReinforcementLowestOriginalSlotIncludesChampionAndIsFlatRoundOneOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare(newHorizonsTraining::REINFORCEMENT_DRILL, false));
	const auto angel = creature("core:angel");
	const auto pikeman = creature("core:pikeman");
	attackerSideHero->clearSlots();
	ASSERT_NO_FATAL_FAILURE(paidRecruit(angel));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	auto * first = attackerSideHero->getStackPtr(SlotID(0));
	auto * second = attackerSideHero->getStackPtr(SlotID(1));
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ASSERT_TRUE(first->getTrainingReceipt().reinforcementPending);
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * unit = original(SlotID(0));
	ASSERT_NE(unit, nullptr);
	const CSelector reinforcement([](const Bonus * bonus)
	{
		return newHorizonsTraining::isTrainingBonus(bonus)
			&& bonus->stacking == "new-horizons:reinforcementDrill";
	});
	const auto diagnostic = [&](const char * boundary)
	{
		std::ostringstream out;
		out << boundary << " round=" << battle()->getRound()
			<< " initiative=" << unit->getInitiative()
			<< " strategic=" << first->getInitiative()
			<< " timeStopped=" << unit->isTimeStopped();
		const SpellID spellLock(SpellID::decode("new-horizons:spellLock"));
		out << " spellLock=" << unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock)));
		const auto append = [&](const char * kind, const BonusList & bonuses)
		{
			out << ' ' << kind << '[';
			for(const auto & bonus : bonuses)
				out << "{type=" << static_cast<int>(bonus->type)
					<< ",source=" << static_cast<int>(bonus->source)
					<< ",stacking=" << bonus->stacking << ",value=" << bonus->val
					<< ",duration=" << static_cast<int>(bonus->duration)
					<< ",turns=" << bonus->turnsRemain << '}';
			out << ']';
		};
		append("local", unit->getExportedBonusList());
		append("effective", *unit->getBonuses(Selector::all));
		append("unstacked", *unit->getUnstackedBonuses(Selector::all));
		out << " reinforcementExported=" << std::count_if(unit->getExportedBonusList().begin(),
			unit->getExportedBonusList().end(), [](const auto & bonus)
			{
				return newHorizonsTraining::isTrainingBonus(bonus.get())
					&& bonus->stacking == "new-horizons:reinforcementDrill";
			}) << " reinforcementUnstacked=" << unit->getUnstackedBonuses(reinforcement)->size();
		return out.str();
	};
	SCOPED_TRACE(diagnostic("entry"));
	const auto entry = unit->getBonuses(reinforcement);
	ASSERT_EQ(entry->size(), 1);
	EXPECT_EQ(entry->front()->val, 2);
	EXPECT_EQ(entry->front()->turnsRemain, 1);
	EXPECT_EQ(battle()->getRound(), 1);
	TrainingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle ordinaryBattle(&environment, callback);
	ordinaryBattle.removeUnitBonus(unit->unitId(), {*entry->front()});
	const int ordinary = ordinaryBattle.battleGetUnitByID(unit->unitId())->getInitiative();
	EXPECT_EQ(unit->getInitiative(), ordinary + 2);
	EXPECT_EQ(unit->getMovementRange(), first->getMovementRange());
	EXPECT_FALSE(first->getTrainingReceipt().reinforcementPending);
	EXPECT_TRUE(second->getTrainingReceipt().reinforcementPending);
	EXPECT_EQ(attackerSideHero->getTrainingDrillLastWeek(), 0);
	advanceRound();
	SCOPED_TRACE(diagnostic("after first boundary"));
	EXPECT_EQ(battle()->getRound(), 2);
	EXPECT_FALSE(unit->hasBonus(reinforcement));
	EXPECT_TRUE(unit->getUnstackedBonuses(reinforcement)->empty());
	advanceRound();
	SCOPED_TRACE(diagnostic("after second boundary"));
	EXPECT_EQ(battle()->getRound(), 3);
	EXPECT_FALSE(unit->hasBonus(reinforcement));
	EXPECT_TRUE(unit->getUnstackedBonuses(reinforcement)->empty());
	EXPECT_EQ(unit->getInitiative(), ordinary);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, SameHeroSplitMergeReorderPreservesNonstackingReceipts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 4));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	const auto receipt = attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt();
	RebalanceStacks split;
	split.srcArmy = split.dstArmy = attackerSideHero->id;
	split.srcSlot = SlotID(0);
	split.dstSlot = SlotID(2);
	split.count = 2;
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(split.srcArmy, split.srcSlot),
		StackLocation(split.dstArmy, split.dstSlot), split.count));
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(2))->getTrainingReceipt(), receipt);
	RebalanceStacks merge = split;
	merge.srcSlot = SlotID(2);
	merge.dstSlot = SlotID(0);
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(merge.srcArmy, merge.srcSlot),
		StackLocation(merge.dstArmy, merge.dstSlot), merge.count));
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt(), receipt);
	RebalanceStacks reorder = split;
	reorder.count = attackerSideHero->getStackCount(SlotID(0));
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(reorder.srcArmy, reorder.srcSlot),
		StackLocation(reorder.dstArmy, reorder.dstSlot), reorder.count));
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(2))->getTrainingReceipt(), receipt);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, PartialRealArmyTransferPermanentlyClearsResidentTrainingButDrillTravels)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), pikeman, 4));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	defenderSideHero->setOwner(PlayerColor(0));
	const auto before = attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt();
	RebalanceStacks move;
	move.srcArmy = attackerSideHero->id;
	move.dstArmy = defenderSideHero->id;
	move.srcSlot = SlotID(0);
	move.dstSlot = SlotID(2);
	move.count = 2;
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(move.srcArmy, move.srcSlot),
		StackLocation(move.dstArmy, move.dstSlot), move.count));
	const auto moved = defenderSideHero->getStackPtr(SlotID(2))->getTrainingReceipt();
	EXPECT_EQ(moved.drillDeadline, before.drillDeadline);
	EXPECT_FALSE(moved.fieldPending);
	EXPECT_EQ(moved.residentRecruiter, ObjectInstanceID::NONE);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt(), before);
	std::swap(move.srcArmy, move.dstArmy);
	std::swap(move.srcSlot, move.dstSlot);
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(move.srcArmy, move.srcSlot),
		StackLocation(move.dstArmy, move.dstSlot), move.count));
	// OR merge preserves the recipient's own prior eligibility; the returned
	// portion does not itself regain residence training.
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->getTrainingReceipt(), before);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, StaleTypedRecruitAndDuplicateBatchRejectBeforeAnyMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	auto * stack = attackerSideHero->getStackPtr(SlotID(0));
	const int count = stack->getCount();
	RecruitTrainedStack pack;
	pack.change = {attackerSideHero->id, SlotID(0), stack->getCreatureID(), count + 1, 1, {},
		newHorizonsTraining::afterRecruitment(*attackerSideHero, stack->getCreatureID(),
			gameState()->getCalendar().getCurrentDay(), {})};
	EXPECT_THROW(gameHandler->sendAndApply(pack), std::runtime_error);
	EXPECT_EQ(stack->getCount(), count);
	EXPECT_TRUE(stack->getTrainingReceipt().empty());
	newHorizonsTraining::Batch batch;
	batch.stacks = {pack.change, pack.change};
	EXPECT_THROW(newHorizonsTraining::validateBatch(*gameState(), batch), std::runtime_error);
	EXPECT_EQ(stack->getCount(), count);
	for(const auto invalid : {CreatureID(-2), CreatureID(static_cast<int32_t>(LIBRARY->creh->objects.size()))})
	{
		pack.change = {attackerSideHero->id, SlotID(6), invalid, 0, 1, {}, {}};
		EXPECT_THROW(gameHandler->sendAndApply(pack), std::runtime_error);
		EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(6)), nullptr);
		EXPECT_EQ(stack->getCount(), count);
		EXPECT_TRUE(stack->getTrainingReceipt().empty());
	}
}

TEST_F(NewHorizonsRecruitmentTrainingTest, DetachedRoundOneStatsAndNestedExpiryPreserveParentSiblingAndLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare(newHorizonsTraining::REINFORCEMENT_DRILL));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(creature("core:pikeman")));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	auto * live = original(attackerSideHero->getSlotFor(creature("core:pikeman")));
	ASSERT_NE(live, nullptr);
	TrainingEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentUnit = parent->getForUpdate(live->unitId());
	const auto childUnit = child->getForUpdate(live->unitId());
	const auto siblingUnit = sibling->getForUpdate(live->unitId());
	EXPECT_EQ(parentUnit->getInitiative(), live->getInitiative());
	EXPECT_EQ(parentUnit->moraleVal(), live->moraleVal());
	child->nextRound();
	EXPECT_LT(childUnit->getInitiative(), parentUnit->getInitiative());
	EXPECT_EQ(siblingUnit->getInitiative(), parentUnit->getInitiative());
	EXPECT_EQ(live->getInitiative(), parentUnit->getInitiative());
}

TEST_F(NewHorizonsRecruitmentTrainingTest, CurrentReceiptRoundTripAndOldOuterWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(paidRecruit(creature("core:pikeman")));
	auto * stack = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(creature("core:pikeman")));
	Receipt saved = stack->getTrainingReceipt();
	CMemorySerializer memory;
	saved.serialize(memory.oser);
	Receipt restored;
	restored.serialize(memory.iser);
	EXPECT_EQ(restored, saved);
	rejectsBeforePrefix(*stack);
	rejectsBeforePrefix(*attackerSideHero);
	rejectsBeforePrefix(*map());
	rejectsBeforePrefix(*gameState());
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	rejectsBeforePrefix(lobby);
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	rejectsBeforePrefix(*battle());
	BattleStart start;
	start.info = BattleInfo::setupBattle(gameState().get(), {4,4,0},
		gameState()->getTile({4,4,0})->getTerrainID(), validBattlefield(),
		{attackerSideHero, defenderSideHero}, {attackerSideHero, defenderSideHero},
		BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero), nullptr);
	rejectsBeforePrefix(start);
}

TEST(NewHorizonsRecruitmentTrainingProtocolTest, PlainLegacyReceiptDefaultsAndMalformedResidenceReject)
{
	Receipt plain;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	plain.serialize(memory.oser);
	EXPECT_TRUE(memory.extractBuffer().empty());
	CMemorySerializer older(std::vector<std::byte>{});
	older.iser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	Receipt restored;
	restored.drillDeadline = 7;
	restored.serialize(older.iser);
	EXPECT_TRUE(restored.empty());
	restored.fieldTrained = true;
	EXPECT_THROW(restored.validate(), std::runtime_error);
}

TEST(NewHorizonsRecruitmentTrainingProtocolTest, MergeUsesLaterDeadlineAndOrFlagsWithoutAdditiveTraining)
{
	Receipt first{7, ObjectInstanceID(3), true, false, false};
	Receipt second{9, ObjectInstanceID(3), false, true, false};
	first.merge(second);
	EXPECT_EQ(first.drillDeadline, 9);
	EXPECT_TRUE(first.fieldPending);
	EXPECT_TRUE(first.fieldTrained);
	const auto completed = newHorizonsTraining::afterCompletedBattle(first, ObjectInstanceID(3));
	EXPECT_TRUE(completed.fieldTrained);
	EXPECT_FALSE(completed.fieldPending);
	EXPECT_EQ(completed.drillDeadline, 9);
	first.crossedArmyBoundary();
	EXPECT_EQ(first.drillDeadline, 9);
	EXPECT_FALSE(first.fieldTrained);
	EXPECT_EQ(first.residentRecruiter, ObjectInstanceID::NONE);
}

TEST(NewHorizonsRecruitmentTrainingProtocolTest, DirectAndCompositeCombatBonusPayloadsRejectOldWriterBeforePrefix)
{
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::SECONDARY_SKILL, 1,
		BonusSourceID(SecondarySkill(SecondarySkill::decode(newHorizonsTraining::SKILL))));
	bonus.stacking = "new-horizons:drillSergeant";
	rejectsBeforePrefix(bonus);
	for(int operation = 0; operation < 3; ++operation)
	{
		SetStackEffect packet;
		auto * changes = operation == 0 ? &packet.toAdd : operation == 1 ? &packet.toUpdate : &packet.toRemove;
		changes->push_back({0, {bonus}});
		rejectsBeforePrefix(packet);
	}
	UnitChanges change;
	change.data["bonuses"].Vector().push_back(bonus.toJsonNode());
	rejectsBeforePrefix(change);
	BattleUnitsChanged units;
	units.changedStacks.push_back(change);
	rejectsBeforePrefix(units);
	StacksInjured injuries;
	BattleStackAttacked hit;
	hit.newState = change;
	injuries.stacks.push_back(hit);
	rejectsBeforePrefix(injuries);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, UnselectedAndChampionDoNotAcquireCoreEliteTraining)
{
	ASSERT_NO_FATAL_FAILURE(prepare(nullptr, false));
	const auto angel = creature("core:angel");
	ASSERT_NO_FATAL_FAILURE(paidRecruit(angel));
	EXPECT_TRUE(attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(angel))->getTrainingReceipt().empty());
	ASSERT_NO_FATAL_FAILURE(select(newHorizonsTraining::DRILL_SERGEANT, MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(select(newHorizonsTraining::FIELD_INSTRUCTOR, MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(angel));
	EXPECT_TRUE(attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(angel))->getTrainingReceipt().empty());
}

TEST_F(NewHorizonsRecruitmentTrainingTest, ReinforcementOncePerAbsoluteWeekAndNextWeekAvailableAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(newHorizonsTraining::REINFORCEMENT_DRILL, false));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(creature("core:pikeman")));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	EXPECT_EQ(attackerSideHero->getTrainingDrillLastWeek(), 0);
	ASSERT_NO_FATAL_FAILURE(completeVictory());
	// A fresh positive recruitment rearms the whole stack, but not the weekly
	// hero use. Reinforcement capture is deterministic and never rolls RNG.
	ASSERT_NO_FATAL_FAILURE(paidRecruit(creature("core:pikeman")));
	auto * stack = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(creature("core:pikeman")));
	ASSERT_TRUE(stack->getTrainingReceipt().reinforcementPending);
	auto * opponent = externalForRecruitment(creature("core:pikeman"), 0);
	ASSERT_NE(opponent, nullptr);
	opponent->setOwner(PlayerColor(1));
	ASSERT_TRUE(opponent->setCreature(SlotID(0), creature("core:pikeman"), 1));
	BattleSideArray<const CArmedInstance *> armies{attackerSideHero, opponent};
	BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, nullptr};
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, opponent);
	auto model = BattleInfo::setupBattle(gameState().get(), {4,4,0},
		gameState()->getTile({4,4,0})->getTerrainID(), validBattlefield(), armies, heroes, layout, nullptr);
	EXPECT_TRUE(newHorizonsTraining::captureEntry(*model, 1, 0).weeks.empty());
	gameState()->day = 8;
	const auto next = newHorizonsTraining::captureEntry(*model, 8, 1);
	ASSERT_EQ(next.weeks.size(), 1u);
	EXPECT_EQ(next.weeks.front().previous, 0);
	EXPECT_EQ(next.weeks.front().next, 1);
	EXPECT_TRUE(stack->getTrainingReceipt().reinforcementPending); // forecast cannot consume
}

TEST_F(NewHorizonsRecruitmentTrainingTest, CompletedSurrenderKeepsOriginalSurvivorTraining)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	for(int actions = 0; actions < 10 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitSide() != BattleSide::ATTACKER; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	const int cost = battle()->battleGetSurrenderCost(PlayerColor(0));
	ASSERT_GE(cost, 0);
	gameHandler->giveResource(PlayerColor(0), EGameResID::GOLD, cost);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeSurrender(BattleSide::ATTACKER)));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto query = gameHandler->queries->topQuery(player);
		if(query && query->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
	}
	const auto * survivor = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(pikeman));
	ASSERT_NE(survivor, nullptr);
	EXPECT_TRUE(survivor->getTrainingReceipt().fieldTrained);
	EXPECT_FALSE(survivor->getTrainingReceipt().fieldPending);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, CompletedBattleWithOpposingRetreatTrainsSurvivingRecruiterArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	for(int actions = 0; actions < 10 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_TRUE(battle()->battleCanFlee(PlayerColor(1)));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeRetreat(BattleSide::DEFENDER)));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto query = gameHandler->queries->topQuery(player);
		if(query && query->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
	}
	const auto * survivor = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(pikeman));
	ASSERT_NE(survivor, nullptr);
	EXPECT_TRUE(survivor->getTrainingReceipt().fieldTrained);
}

TEST_F(NewHorizonsRecruitmentTrainingTest, ReplayRestoresOriginalOpeningBonusesWithoutSpendingAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(newHorizonsTraining::REINFORCEMENT_DRILL));
	ASSERT_NO_FATAL_FAILURE(paidRecruit(creature("core:pikeman")));
	ASSERT_NO_FATAL_FAILURE(acceptedBattle());
	const auto snapshot = battle()->trainingEntrySnapshot;
	ASSERT_FALSE(snapshot.empty());
	const auto location = battle()->getLocation();
	// Empty valid field tiles must retain the ordinary terrain fallback.
	const int3 emptyTile(4, 4, 0);
	const auto & emptyTerrain = gameState()->getMap().getTile(emptyTile);
	ASSERT_TRUE(emptyTerrain.visitableObjects.empty());
	ASSERT_FALSE(gameState()->getMap().isCoastalTile(emptyTile));
	const auto & terrainFields = emptyTerrain.getTerrain()->battleFields;
	ASSERT_FALSE(terrainFields.empty());
	const auto selectedField = gameState()->battleGetBattlefieldType(emptyTile, gameHandler->getRandomGenerator());
	EXPECT_NE(selectedField, BattleField::NONE);
	EXPECT_TRUE(vstd::contains(terrainFields, selectedField));
	const auto week = attackerSideHero->getTrainingDrillLastWeek();
	advanceRound();
	advanceRound();
	const auto layout = BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero);
	gameHandler->battles->restartBattle(BattleID(0), attackerSideHero, defenderSideHero, location,
		attackerSideHero, defenderSideHero, layout, nullptr);
	auto * replay = gameState()->getBattle(attackerSideHero->getOwner());
	ASSERT_NE(replay, nullptr);
	EXPECT_EQ(replay->trainingEntrySnapshot, snapshot);
	EXPECT_EQ(attackerSideHero->getTrainingDrillLastWeek(), week);
	for(auto * unit : replay->getStacksIf([](const CStack * stack)
		{ return stack->unitSide() == BattleSide::ATTACKER && stack->base; }))
	{
		EXPECT_TRUE(unit->hasBonus(CSelector([](const Bonus * bonus)
			{ return bonus->stacking == "new-horizons:drillSergeant"; })));
		EXPECT_TRUE(unit->hasBonus(CSelector([](const Bonus * bonus)
			{ return bonus->stacking == "new-horizons:reinforcementDrill"; })));
	}
}

TEST_F(NewHorizonsRecruitmentTrainingTest, StrategicStackBinaryAndJsonPreserveTrainedReceiptWithoutDuplicateAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	ASSERT_NO_FATAL_FAILURE(paidRecruit(pikeman));
	auto receipt = attackerSideHero->getStackPtr(attackerSideHero->getSlotFor(pikeman))->getTrainingReceipt();
	receipt = newHorizonsTraining::afterCompletedBattle(receipt, attackerSideHero->id);
	CStackInstance saved(gameState().get(), pikeman, 3);
	saved.setTrainingReceipt(receipt);
	CMemorySerializer binary;
	binary.iser.cb = gameState().get();
	saved.serialize(binary.oser);
	CStackInstance restored(gameState().get(), pikeman, 1);
	restored.serialize(binary.iser);
	EXPECT_EQ(restored.getCount(), 3);
	EXPECT_EQ(restored.getTrainingReceipt(), receipt);
	EXPECT_EQ(restored.getAttack(false), saved.getAttack(false));
	EXPECT_EQ(restored.getBonuses(CSelector(newHorizonsTraining::isTrainingBonus))->size(), 1u);
	JsonNode data;
	JsonSerializer writer(nullptr, data);
	saved.serializeJson(writer);
	CStackInstance fromJson(gameState().get(), pikeman, 1);
	JsonDeserializer reader(nullptr, data);
	fromJson.serializeJson(reader);
	EXPECT_EQ(fromJson.getCount(), 3);
	EXPECT_EQ(fromJson.getTrainingReceipt(), receipt);
	EXPECT_EQ(fromJson.getAttack(false), saved.getAttack(false));
	EXPECT_EQ(fromJson.getBonuses(CSelector(newHorizonsTraining::isTrainingBonus))->size(), 1u);
	auto malformed = data;
	malformed["training"]["fieldPending"].Integer() = 1;
	JsonDeserializer invalid(nullptr, malformed);
	EXPECT_THROW(fromJson.serializeJson(invalid), std::runtime_error);
	EXPECT_EQ(fromJson.getTrainingReceipt(), receipt);
}
