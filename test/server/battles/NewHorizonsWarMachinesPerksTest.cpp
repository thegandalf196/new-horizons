/*
 * NewHorizonsWarMachinesPerksTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsWarMachines.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../luascript/StdInc.h"
#include "../../../luascript/api/callback/ServerCallback.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/Effect.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/BattleAI.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/callback/CBattleCallback.h"
#endif

namespace
{
constexpr auto SKILL = "new-horizons:warMachines";
constexpr auto PRECISION = "new-horizons:warMachines.precisionBombardment";
constexpr auto BREACH = "new-horizons:warMachines.breachmaker";
constexpr auto WORKSHOP = "new-horizons:warMachines.fieldWorkshop";
}

TEST(NewHorizonsBreachmakerGeometryTest, GateHasPhysicalNeighborsNotEnumNeighbors)
{
	const auto result = newHorizonsWarMachines::overflow(EWallPart::GATE, 1, 102, [](EWallPart part)
	{
		return part == EWallPart::BELOW_GATE ? 40 : part == EWallPart::OVER_GATE ? 30 : 1;
	});
	EXPECT_EQ(result.part, EWallPart::OVER_GATE);
	EXPECT_EQ(result.damage, 50);
}

TEST(NewHorizonsBreachmakerGeometryTest, TiesUseOuterOrderAndSkipDeadNeighbors)
{
	auto result = newHorizonsWarMachines::overflow(EWallPart::BOTTOM_WALL, 100, 103,
		[](EWallPart) { return 20; });
	EXPECT_EQ(result.part, EWallPart::BOTTOM_TOWER);
	EXPECT_EQ(result.damage, 1);
	result = newHorizonsWarMachines::overflow(EWallPart::BOTTOM_WALL, 100, 200, [](EWallPart part)
	{
		return part == EWallPart::BOTTOM_TOWER ? 0 : 20;
	});
	EXPECT_EQ(result.part, EWallPart::BELOW_GATE);
}

TEST(NewHorizonsBreachmakerGeometryTest, TowersKeepAndZeroExcessNeverCarry)
{
	for(const auto part : {EWallPart::KEEP, EWallPart::BOTTOM_TOWER, EWallPart::UPPER_TOWER})
		EXPECT_EQ(newHorizonsWarMachines::overflow(part, 1, 400, [](EWallPart) { return 1; }).damage, 0);
	for(const auto damage : {99, 100, 101})
		EXPECT_EQ(newHorizonsWarMachines::overflow(EWallPart::GATE, 100, damage,
			[](EWallPart) { return 1; }).damage, 0);
}

class NewHorizonsWarMachinesPerksTest : public HeroCommandFixture
{
protected:
	CStack * tent = nullptr;
	CStack * catapult = nullptr;

	void choose(CGHeroInstance * hero, const char * perk)
	{
		const auto rank = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto choice = std::ranges::find_if(offer, [perk](const auto & entry)
			{
				return entry.selection.skillId == SKILL && entry.selection.perkId == perk;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(SKILL, perk));
			return;
		}
		FAIL() << "No ordinary active-registry offer for " << perk;
	}

	void acquire(CGHeroInstance * hero, bool workshop)
	{
		const SecondarySkill skill(SecondarySkill::decode(SKILL));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(choose(hero, workshop ? "new-horizons:warMachines.surgeon" : PRECISION));
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(choose(hero, workshop ? WORKSHOP : BREACH));
		if(workshop)
		{
			hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		}
	}

	void prepare(bool selected = true)
	{
		startGame(true);
		if(selected)
		{
			ASSERT_NO_FATAL_FAILURE(acquire(defenderSideHero, true));
			ASSERT_NO_FATAL_FAILURE(acquire(attackerSideHero, false));
		}
		else
		{
			const SecondarySkill skill(SecondarySkill::decode(SKILL));
			for(auto * hero : {attackerSideHero, defenderSideHero})
				hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		}
		giveArtifact(defenderSideHero, ArtifactID::FIRST_AID_TENT, ArtifactPosition::MACH3);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		for(const auto building : {BuildingID::FORT, BuildingID::CITADEL, BuildingID::CASTLE})
			if(!towns.front()->hasBuilt(building))
				ASSERT_TRUE(gameHandler->buildStructure(towns.front()->id, building, true));
		startBattle(towns.front());
		for(const auto & stack : battle()->stacks)
		{
			if(stack->isFirstAidTent() && stack->unitSide() == BattleSide::DEFENDER)
				tent = stack.get();
			if(stack->isCatapult())
				catapult = stack.get();
		}
		ASSERT_NE(tent, nullptr);
		ASSERT_NE(catapult, nullptr);
		ASSERT_GT(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 0);
		// Structure-only healing must survive automatic-turn admission.
		battle()->setWallStructuralHP(EWallPart::BOTTOM_WALL, 100);
		beginCombat();
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0);
		packet.stack = unit->unitId();
		packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}

	bool repair(EWallPart part)
	{
		activate(tent);
		BattleAction action;
		action.actionType = EActionType::STACK_HEAL;
		action.stackNumber = tent->unitId();
		action.side = BattleSide::DEFENDER;
		action.aimToHex(battle()->wallPartToBattleHex(part));
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action);
	}

	BattleStructureRepaired packet(EWallPart part)
	{
		const auto preview = battle()->battleGetFirstAidStructureRepairPreview(tent, part);
		BattleStructureRepaired result;
		result.battleID = BattleID(0);
		result.part = part;
		result.healerID = tent->unitId();
		result.expectedHP = preview.expectedHP;
		result.replacementHP = preview.replacementHP;
		return result;
	}
};

TEST_F(NewHorizonsWarMachinesPerksTest, PaidStructureRepairMatchesPreviewAndCapsMissingHP)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->setWallStructuralHP(EWallPart::BOTTOM_WALL, 290);
	const auto preview = battle()->battleGetFirstAidStructureRepairPreview(tent, EWallPart::BOTTOM_WALL);
	ASSERT_EQ(preview.repairedHP(), 10);
	ASSERT_TRUE(repair(EWallPart::BOTTOM_WALL));
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 300);
}

TEST_F(NewHorizonsWarMachinesPerksTest, TowerStructureRepairPrecedesShooterAtSameHex)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto part = EWallPart::BOTTOM_TOWER;
	battle()->setWallStructuralHP(part, 10);
	const auto * shooter = battle()->battleGetUnitByPos(battle()->getTowerShooterHex(part), false);
	ASSERT_NE(shooter, nullptr);
	const auto health = shooter->getAvailableHealth();
	const auto preview = battle()->battleGetFirstAidStructureRepairPreview(tent, part);
	ASSERT_GT(preview.repairedHP(), 0);
	ASSERT_TRUE(repair(part));
	EXPECT_EQ(battle()->getWallStructuralHP(part), preview.replacementHP);
	EXPECT_EQ(shooter->getAvailableHealth(), health);
}

TEST_F(NewHorizonsWarMachinesPerksTest, FullDestroyedEnemyAndInactiveStructuresAreIneligible)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_EQ(battle()->battleGetFirstAidStructureRepairPreview(tent, EWallPart::BOTTOM_WALL).repairedHP(), 0);
	EXPECT_EQ(battle()->battleGetFirstAidStructureRepairPreview(catapult, EWallPart::BOTTOM_WALL).repairedHP(), 0);
	ASSERT_NO_FATAL_FAILURE(acquire(defenderSideHero, true));
	battle()->setWallStructuralHP(EWallPart::BOTTOM_WALL, 0);
	EXPECT_EQ(battle()->battleGetFirstAidStructureRepairPreview(tent, EWallPart::BOTTOM_WALL).repairedHP(), 0);
	battle()->setWallStructuralHP(EWallPart::GATE, 450);
	EXPECT_EQ(battle()->battleGetFirstAidStructureRepairPreview(tent, EWallPart::GATE).repairedHP(), 0);
}

TEST_F(NewHorizonsWarMachinesPerksTest, StaleAndWrongControllerPacketsCannotMutate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(tent);
	auto changed = packet(EWallPart::BOTTOM_WALL);
	BattleStatePackVisitor visitor(*battle());
	changed.expectedHP += 1;
	EXPECT_THROW(changed.visitTyped(visitor), std::runtime_error);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 100);
	changed = packet(EWallPart::BOTTOM_WALL);
	changed.healerID = catapult->unitId();
	EXPECT_THROW(changed.visitTyped(visitor), std::runtime_error);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 100);
}

TEST_F(NewHorizonsWarMachinesPerksTest, PaidMachineRepairCapsSurvivorHPWithoutCleaningAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * machine = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(8, 5), 1);
	ASSERT_NE(machine, nullptr);
	auto state = machine->acquireState();
	int64_t wound = 200;
	state->damage(wound);
	BattleUnitsChanged wounded;
	wounded.battleID = BattleID(0);
	UnitChanges update(machine->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	wounded.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(wounded);
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::OTHER, 0, BonusSourceID());
	JsonNode parameters;
	parameters["kind"].String() = "disease";
	marker->parameters = std::make_shared<BonusParameters>(parameters);
	machine->addNewBonus(marker);
	ASSERT_TRUE(battle()->battleCanRepairWarMachine(tent, machine));
	const auto preview = battle()->battleGetFirstAidHealingPreview(tent, machine);
	EXPECT_EQ(preview.restoredHP, 0);
	EXPECT_EQ(preview.survivorHealedHP, 200);
	activate(tent);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeal(tent, machine)));
	EXPECT_EQ(machine->getAvailableHealth(), machine->getMaxHealth());
	EXPECT_EQ(machine->getCount(), 1);
	EXPECT_TRUE(machine->hasBonusOfType(BonusType::PHYSICAL_AFFLICTION));
}

TEST_F(NewHorizonsWarMachinesPerksTest, ReducedOutputRepairUsesSameOutputAndCannotRebuild)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(tent);
	ReducedExtraActivationState reduced;
	reduced.enabled = true;
	reduced.used = true;
	reduced.activeUnitId = tent->unitId();
	reduced.outputPercent = 50;
	battle()->setReducedExtraActivationState(BattleSide::DEFENDER, reduced);
	const auto output = battle()->battleGetFirstAidHealingOutput(tent);
	const auto preview = battle()->battleGetFirstAidStructureRepairPreview(tent, EWallPart::BOTTOM_WALL);
	ASSERT_EQ(preview.repairedHP(), std::min<int64_t>(200, output));
	BattleStatePackVisitor visitor(*battle());
	auto changed = packet(EWallPart::BOTTOM_WALL);
	changed.visitTyped(visitor);
	EXPECT_EQ(battle()->getWallStructuralHP(changed.part), 100 + output);
	auto * dead = addStack(BattleSide::DEFENDER, CreatureID::AMMO_CART, BattleHex(9, 5), 1);
	ASSERT_NE(dead, nullptr);
	auto state = dead->acquireState();
	int64_t lethal = state->getAvailableHealth();
	state->damage(lethal);
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	UnitChanges update(dead->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	killed.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(killed);
	EXPECT_FALSE(battle()->battleCanRepairWarMachine(tent, dead));
	EXPECT_EQ(battle()->battleGetFirstAidHealingPreview(tent, dead).totalHealedHP(), 0);
}

TEST_F(NewHorizonsWarMachinesPerksTest, PacketRoundTripAndOldWriterRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto changed = packet(EWallPart::BOTTOM_WALL);
	CMemorySerializer wire;
	wire.oser & changed;
	BattleStructureRepaired restored;
	wire.iser & restored;
	EXPECT_EQ(restored.part, changed.part);
	EXPECT_EQ(restored.expectedHP, changed.expectedHP);
	EXPECT_EQ(restored.replacementHP, changed.replacementHP);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_WAR_MACHINES_REPAIR) - 1);
	EXPECT_THROW(old.oser & changed, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer malformed;
	changed.expectedHP = 0;
	EXPECT_THROW(malformed.oser & changed, std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}

TEST_F(NewHorizonsWarMachinesPerksTest, BreachCarryUsesFinalDamageOnlyOnceAndRemovesTower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->setWallStructuralHP(EWallPart::BOTTOM_WALL, 1);
	battle()->setWallStructuralHP(EWallPart::BOTTOM_TOWER, 1);
	battle()->setWallStructuralHP(EWallPart::BELOW_GATE, 200);
	const auto * tower = battle()->battleGetUnitByPos(battle()->getTowerShooterHex(EWallPart::BOTTOM_TOWER), false);
	ASSERT_NE(tower, nullptr);
	const auto towerID = tower->unitId();
	const auto raw = battle()->battleGetCatapultStructuralDamage(catapult, 1);
	const auto preview = battle()->battleGetBreachmakerPreview(catapult, EWallPart::BOTTOM_WALL, raw);
	ASSERT_EQ(preview.part, EWallPart::BOTTOM_TOWER);
	EXPECT_EQ(preview.damage, (raw - 1) / 2);
	scripting::api::ServerCallbackProxy::catapultAttack(*gameHandler->spellcastEnvironment(),
		*battle(), catapult, EWallPart::BOTTOM_WALL, 1);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), 0);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BOTTOM_TOWER), 0);
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::BELOW_GATE), 200);
	const auto * removedTower = battle()->battleGetUnitByID(towerID);
	ASSERT_NE(removedTower, nullptr);
	EXPECT_TRUE(removedTower->isGhost());
	EXPECT_FALSE(removedTower->alive());
	EXPECT_EQ(battle()->battleGetUnitByPos(battle()->getTowerShooterHex(EWallPart::BOTTOM_TOWER), false), nullptr);
}

TEST_F(NewHorizonsWarMachinesPerksTest, PrecisionActualActionNeverRedirectsOrRetargets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Remove Breachmaker's eligible excess by aiming at a tower; no overflow
	// is permitted from a tower, even if the same hero has both perks.
	const auto part = EWallPart::KEEP;
	battle()->setWallStructuralHP(part, 1);
	std::map<EWallPart, int32_t> before;
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
		before[static_cast<EWallPart>(i)] = battle()->getWallStructuralHP(static_cast<EWallPart>(i));
	activate(catapult);
	BattleAction action;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = catapult->unitId();
	action.actionType = EActionType::CATAPULT;
	action.aimToHex(battle()->wallPartToBattleHex(part));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	for(const auto & [other, hp] : before)
		if(other != part)
			EXPECT_EQ(battle()->getWallStructuralHP(other), hp);
}

TEST_F(NewHorizonsWarMachinesPerksTest, RegisteredPrecisionMissKeepsAccuracyAndQualityDrawsWithoutDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto part = EWallPart::KEEP;
	std::map<EWallPart, int32_t> before;
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
		before[static_cast<EWallPart>(i)] = battle()->getWallStructuralHP(static_cast<EWallPart>(i));
	const SpellID spellID(SpellID::decode("core:catapultShot"));
	ASSERT_NE(spellID, SpellID::NONE);
	spells::BattleCast cast(battle(), catapult, spells::Mode::SPELL_LIKE_ATTACK, spellID.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED); // Registered two-shot script profile.
	const auto mechanics = spellID.toSpell()->battleMechanics(&cast);
	::testing::StrictMock<ServerCallbackMock> authority;
	::testing::StrictMock<vstd::RNGMock> rng;
	EXPECT_CALL(authority, getRNG()).Times(4).WillRepeatedly(::testing::Return(&rng));
	EXPECT_CALL(rng, nextInt(0, 99)).Times(4).WillRepeatedly(::testing::Return(99));
	EXPECT_CALL(authority, apply(::testing::Matcher<CatapultAttack &>(::testing::_))).Times(2)
		.WillRepeatedly(::testing::Invoke([&](CatapultAttack & attack)
		{
			EXPECT_EQ(attack.attackedPart, part);
			EXPECT_EQ(attack.damageDealt, 0);
			EXPECT_EQ(attack.structuralDamage, 0);
			gameHandler->sendAndApply(attack);
		}));
	int effects = 0;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		++effects;
		effect.apply(&authority, mechanics.get(), {battle::Destination(battle()->wallPartToBattleHex(part))});
		return true;
	});
	ASSERT_EQ(effects, 1);
	for(const auto & [other, hp] : before)
		EXPECT_EQ(battle()->getWallStructuralHP(other), hp);
}

TEST_F(NewHorizonsWarMachinesPerksTest, RegisteredPrecisionDestroyedTargetStopsBeforeLaterDraws)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto part = EWallPart::KEEP;
	battle()->setWallStructuralHP(part, 1);
	std::map<EWallPart, int32_t> before;
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
		before[static_cast<EWallPart>(i)] = battle()->getWallStructuralHP(static_cast<EWallPart>(i));
	const SpellID spellID(SpellID::decode("core:catapultShot"));
	ASSERT_NE(spellID, SpellID::NONE);
	spells::BattleCast cast(battle(), catapult, spells::Mode::SPELL_LIKE_ATTACK, spellID.toSpell());
	cast.setSpellLevel(MasteryLevel::ADVANCED);
	const auto mechanics = spellID.toSpell()->battleMechanics(&cast);
	::testing::StrictMock<ServerCallbackMock> authority;
	::testing::StrictMock<vstd::RNGMock> rng;
	EXPECT_CALL(authority, getRNG()).Times(2).WillRepeatedly(::testing::Return(&rng));
	{
		::testing::InSequence sequence;
		EXPECT_CALL(rng, nextInt(0, 99)).WillOnce(::testing::Return(0)); // Accuracy succeeds.
		EXPECT_CALL(rng, nextInt(0, 99)).WillOnce(::testing::Return(99)); // Ordinary critical quality.
	}
	EXPECT_CALL(authority, apply(::testing::Matcher<CatapultAttack &>(::testing::_))).Times(1)
		.WillOnce(::testing::Invoke([&](CatapultAttack & attack)
		{
			EXPECT_EQ(attack.attackedPart, part);
			EXPECT_GT(attack.structuralDamage, 1);
			gameHandler->sendAndApply(attack);
		}));
	int effects = 0;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		++effects;
		effect.apply(&authority, mechanics.get(), {battle::Destination(battle()->wallPartToBattleHex(part))});
		return true;
	});
	ASSERT_EQ(effects, 1);
	EXPECT_EQ(battle()->getWallStructuralHP(part), 0);
	for(const auto & [other, hp] : before)
		if(other != part)
			EXPECT_EQ(battle()->getWallStructuralHP(other), hp);
}

#ifdef ENABLE_BATTLE_AI
namespace
{
class WorkshopEnvironment : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WorkshopEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(NewHorizonsWarMachinesPerksTest, DetachedRepairMatchesLiveWithoutMutatingLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(tent);
	WorkshopEnvironment environment(gameState());
	auto callback = std::make_shared<CBattleCallback>(std::nullopt, nullptr);
	callback->onBattleStarted(battle());
	HypotheticBattle projected(&environment, callback->getBattle(BattleID(0)));
	auto changed = packet(EWallPart::BOTTOM_WALL);
	BattleStatePackVisitor visitor(projected);
	changed.visitTyped(visitor);
	EXPECT_EQ(projected.getWallStructuralHP(changed.part), changed.replacementHP);
	EXPECT_EQ(battle()->getWallStructuralHP(changed.part), changed.expectedHP);
	ASSERT_TRUE(repair(changed.part));
	EXPECT_EQ(battle()->getWallStructuralHP(changed.part), projected.getWallStructuralHP(changed.part));
}

TEST_F(NewHorizonsWarMachinesPerksTest, ActualAIChoosesAndPaysStructureOnlyRepair)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(tent);
	auto environment = std::make_shared<WorkshopEnvironment>(gameState());
	auto callback = std::make_shared<CBattleCallback>(PlayerColor(1), nullptr);
	callback->onBattleStarted(battle());
	CBattleAI ai;
	ai.initBattleInterface(environment, callback);
	const auto action = ai.useHealingTent(BattleID(0), tent);
	ASSERT_EQ(action.actionType, EActionType::STACK_HEAL);
	const auto before = battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_GT(battle()->getWallStructuralHP(EWallPart::BOTTOM_WALL), before);
}
#endif
