/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../battleAI/ProjectedTargetHitFixture.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include "../../lib/combatScripts/NativeCombatEvents.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/scripting/ScriptTypeDescription.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <array>
#include <tuple>

namespace
{
// This compares event adapters in paired hypothetical branches, not full battles.
// Ordinary/hostile chance calls delegate to HypotheticSpellServer's deterministic
// projection. The recorder's seeded RNG covers direct binomial/injury draws; it
// does not certify authoritative chance draws. Reroll allowances are compared
// separately because the delegate applies those state packets internally.
// Passive casts in both arms use the installed native spell factory: independent
// spell parity tests must establish the correctness of those downstream effects.
class EventRecorder final : public ServerCallback
{
	ServerCallback & delegate;
	CRandomGenerator random;
	template<typename Pack> void record(Pack & pack)
	{
		CMemorySerializer memory;
		CPackForClient * pointer = &pack;
		memory.oser & pointer;
		packets.emplace_back(typeid(pack).name(), memory.extractBuffer());
		delegate.apply(pack);
	}
public:
	std::vector<std::pair<std::string, std::vector<std::byte>>> packets;
	std::vector<std::tuple<std::string, uint32_t, uint32_t, int>> rolls;
	explicit EventRecorder(ServerCallback & delegate) : delegate(delegate), random(192837) {}
	std::vector<std::byte> rngState() const { return projectedHitTest::serialized(random); }
	void complain(const std::string & message) override { throw std::runtime_error(message); }
	bool describeChanges() const override { return true; }
	vstd::RNG * getRNG() override { return &random; }
	bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int chance) override
	{
		rolls.emplace_back("ordinary", actor.unitId(), actor.unitId(), chance);
		return delegate.rollCombatAbility(battle, actor, chance);
	}
	bool rollHostileCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor,
		const battle::Unit & recipient, int chance) override
	{
		rolls.emplace_back("hostile", actor.unitId(), recipient.unitId(), chance);
		return delegate.rollHostileCombatAbility(battle, actor, recipient, chance);
	}
	bool resolveAdverseCombatRoll(const BattleID & battle, BattleSide side, bool stochastic,
		bool adverse, const std::function<bool()> & draw) override
	{
		return delegate.resolveAdverseCombatRoll(battle, side, stochastic, adverse, draw);
	}
	void apply(CPackForClient & pack) override { record(pack); }
	void apply(BattleLogMessage & pack) override { record(pack); }
	void apply(BattleStackMoved & pack) override { record(pack); }
	void apply(BattleUnitsChanged & pack) override { record(pack); }
	void apply(SetStackEffect & pack) override { record(pack); }
	void apply(StacksInjured & pack) override { record(pack); }
	void apply(BattleObstaclesChanged & pack) override { record(pack); }
	void apply(CatapultAttack & pack) override { record(pack); }
};

struct Scenario
{
	std::string name;
	JsonNode parameters{};
	CombatEventType event = CombatEventType::AFTER_ATTACK;
	std::string method = "onAfterAttack";
	int repetitions = 1;
	bool ranged = false;
	std::optional<BattleHex> towerPosition{};
	bool projectedAdverseReroll = false;
};

class NativeCombatEventsParityTest : public ProjectedTargetHitFixture
{
protected:
	void prepareTower(const BattleHex & position, uint32_t & actorID, uint32_t & victimID)
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		auto * town = towns.front();
		// Construct the siege fixture before battle setup. Both base and upgraded
		// buildings exercise the Lua rule that counts a building only once.
		for(const auto building : {BuildingID::CITADEL, BuildingID::CASTLE,
			BuildingID::TOWN_HALL, BuildingID::CITY_HALL, BuildingID::MARKETPLACE, BuildingID::TAVERN})
			town->addBuilding(BuildingID(building));
		startBattle(town);
		const auto towers = battle()->battleGetStacksIf([&](const CStack * stack)
			{ return stack->isTurret() && stack->getPosition() == position; });
		ASSERT_EQ(towers.size(), 1u);
		actorID = towers.front()->unitId();
		victimID = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 100)->unitId();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetChild();
	}

	void compare(const Scenario & scenario)
	{
		uint32_t actorID = 0, victimID = 0;
		if(scenario.towerPosition)
			ASSERT_NO_FATAL_FAILURE(prepareTower(*scenario.towerPosition, actorID, victimID));
		else
		{
			ASSERT_NO_FATAL_FAILURE(prepare());
			actorID = braced->unitId();
			victimID = incoming->unitId();
		}
		scripting::LuaModule host;
		const std::vector<ScriptSource> patches = scenario.name == "deathStare"
			? std::vector<ScriptSource>{{"core", "combat/deathStareCommander"}} : std::vector<ScriptSource>{};
		scripting::LuaScriptInstance script(host, "core", ScriptPath::builtinTODO("combat/" + scenario.name).addPrefix("SCRIPTS/"), patches);
		auto implementation = nativeEvents::create("combat/" + scenario.name);
		ASSERT_NE(implementation, nullptr);
		const auto liveState = gameState()->saveToMemory();
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const std::array<AdverseCombatRerollState, 2> parentRerolls{
			child->getAdverseCombatRerollState(BattleSide::ATTACKER),
			child->getAdverseCombatRerollState(BattleSide::DEFENDER)};
		using Result = std::tuple<std::vector<std::pair<std::string, std::vector<std::byte>>>,
			std::vector<std::tuple<std::string, uint32_t, uint32_t, int>>, std::vector<std::byte>,
			std::vector<JsonNode>, std::array<AdverseCombatRerollState, 2>>;
		std::optional<Result> expected;
		for(bool native : {false, true})
		{
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
			auto actor = branch->getForUpdate(actorID);
			auto victim = branch->getForUpdate(victimID);
			if(!scenario.towerPosition) actor->position = BattleHex(93);
			victim->position = BattleHex(94);
			const auto beforeActor = actor->getAvailableHealth(), beforeVictim = victim->getAvailableHealth();
			int64_t hurtActor = scenario.towerPosition ? 0 : beforeActor / 4;
			int64_t hurtVictim = std::min<int64_t>(40, beforeVictim / 4);
			actor->damage(hurtActor);
			victim->damage(hurtVictim);
			if(scenario.projectedAdverseReroll)
				for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
					branch->setAdverseCombatRerollState(side, {true, false});
			CombatEventPayload payload;
			payload.ranged = scenario.ranged;
			const auto * hit = scenario.event == CombatEventType::AFTER_ATTACKED ? actor.get() : victim.get();
			AttackedTarget target;
			target.unit = hit;
			target.damage = hurtVictim;
			target.killed = 2;
			target.damageBeforeDefense = 300;
			target.healthBeforeAttack = scenario.event == CombatEventType::AFTER_ATTACKED ? beforeActor : beforeVictim;
			payload.targets.push_back(target);
			if(scenario.name == "focusMagic") payload.targets.push_back(payload.targets.front());
			EventRecorder recorder(*branch->getServerCallback());
			if(native)
			{
				ASSERT_TRUE(implementation->handlesEvent(*branch, scenario.event));
				for(int repeat = 0; repeat < scenario.repetitions; ++repeat)
					implementation->run(&recorder, *branch, scenario.event, actor.get(), victim.get(), scenario.parameters, payload);
			}
			else
			{
				scripting::LuaContext context(&script, environment.get());
				context.initialize();
				ASSERT_TRUE(context.hasFunction(scenario.method));
				for(int repeat = 0; repeat < scenario.repetitions; ++repeat)
					context.callMethod<void>(scenario.method, scenario.parameters,
						static_cast<ServerCallback *>(&recorder), static_cast<const CBattleInfoCallback *>(branch.get()),
						static_cast<const battle::Unit *>(actor.get()), static_cast<const battle::Unit *>(victim.get()), payload);
			}
			std::vector<JsonNode> states;
			for(const auto * unit : branch->battleGetAllUnits(false)) states.push_back(unit->acquire()->save());
			const std::array<AdverseCombatRerollState, 2> rerolls{
				branch->getAdverseCombatRerollState(BattleSide::ATTACKER),
				branch->getAdverseCombatRerollState(BattleSide::DEFENDER)};
			if(scenario.projectedAdverseReroll)
			{
				EXPECT_FALSE(rerolls[0].used);
				EXPECT_TRUE(rerolls[1].used) << "Must exercise the recipient's projected reroll";
			}
			if(scenario.towerPosition)
			{
				ASSERT_EQ(recorder.packets.size(), 2u) << "Both tower damage bonuses must be emitted";
				for(const auto & packet : recorder.packets) EXPECT_EQ(packet.first, typeid(SetStackEffect).name());
			}
			const Result result{recorder.packets, recorder.rolls, recorder.rngState(), states, rerolls};
			if(!expected) expected = result;
			else EXPECT_EQ(result, *expected) << scenario.name;
			EXPECT_EQ(gameState()->saveToMemory(), liveState);
			EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
			EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
			EXPECT_EQ(child->getAdverseCombatRerollState(BattleSide::ATTACKER), parentRerolls[0]);
			EXPECT_EQ(child->getAdverseCombatRerollState(BattleSide::DEFENDER), parentRerolls[1]);
		}
	}
};
}

TEST_F(NativeCombatEventsParityTest, LifeDrainMatchesPacketsStateAndRecorderRng)
{
	Scenario scenario{"lifeDrain"}; scenario.parameters["val"].Integer() = 100;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, SoulStealMatchesPermanentOverheal)
{
	Scenario scenario{"soulSteal"}; scenario.parameters["val"].Integer() = 2; scenario.parameters["permanent"].Bool() = true;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, VampirismMatchesSurvivingOnlyHeal)
{
	Scenario scenario{"vampirism"}; scenario.parameters["val"].Integer() = 4000;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, FireShieldMatchesReflectionProvenanceAndAnimation)
{
	Scenario scenario{"fireShield"}; scenario.parameters["val"].Integer() = 20;
	scenario.event = CombatEventType::AFTER_ATTACKED; scenario.method = "onAfterAttacked";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, HexOfPainMatchesCasterSideAndReflection)
{
	Scenario scenario{"hexOfPain"}; scenario.parameters["val"].Integer() = 15;
	scenario.parameters["damageSharePercent"].Integer() = 20; scenario.parameters["casterSide"].Integer() = 1;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, ShadowGiftMatchesSpellTypedDamage)
{
	Scenario scenario{"shadowGift"}; scenario.parameters["val"].Integer() = 1750; scenario.parameters["casterSide"].Integer() = 0;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, FocusMagicMatchesFirstDoubleMarkRefreshCapAndDuplicateHits)
{
	Scenario scenario{"focusMagic"}; scenario.parameters["val"].Integer() = 1500;
	scenario.parameters["beneficiarySide"].Integer() = 0; scenario.parameters["arcaneAcquisition"].Bool() = true;
	scenario.ranged = true; scenario.repetitions = 5;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, DestructionPreservesHostileRollAndInjury)
{
	Scenario scenario{"destruction"}; scenario.parameters["val"].Integer() = 100;
	scenario.parameters["killBy"].String() = "count"; scenario.parameters["amount"].Integer() = 1;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, InertDestructionPreservesOrdinaryChanceRequest)
{
	Scenario scenario{"destruction"}; scenario.parameters["val"].Integer() = 42;
	scenario.parameters["killBy"].String() = "count"; scenario.parameters["amount"].Integer() = 0;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, DestructionMatchesProjectedAdverseRerollExpenditure)
{
	Scenario scenario{"destruction"}; scenario.parameters["val"].Integer() = 75;
	scenario.parameters["killBy"].String() = "count"; scenario.parameters["amount"].Integer() = 1;
	scenario.projectedAdverseReroll = true;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, TransmutationPreservesRemovalReplacementAndRefresh)
{
	Scenario scenario{"transmutation"}; scenario.parameters["val"].Integer() = 100;
	scenario.parameters["creature"].String() = "core:woodElf"; scenario.parameters["transmuteBy"].String() = "health";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, DeathStarePreservesBinomialAndPassiveCast)
{
	Scenario scenario{"deathStare"}; scenario.parameters["val"].Integer() = 10;
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, CommanderStarePreservesPatchedLevelRatio)
{
	Scenario scenario{"deathStare"}; scenario.parameters["val"].Integer() = 2; scenario.parameters["situation"].String() = "commander";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, EnchantmentPreservesPassiveEffects)
{
	Scenario scenario{"enchanted"}; scenario.parameters["spell"].String() = "core:bless";
	scenario.parameters["level"].Integer() = 2; scenario.parameters["duration"].Integer() = 3;
	scenario.event = CombatEventType::BATTLE_START; scenario.method = "onBattleStart";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, RoundEnchantmentPreservesMassiveRefresh)
{
	Scenario scenario{"enchanted"}; scenario.parameters["spell"].String() = "core:bless";
	scenario.parameters["level"].Integer() = 2; scenario.parameters["duration"].Integer() = 3;
	scenario.parameters["massive"].Bool() = true; scenario.repetitions = 2;
	scenario.event = CombatEventType::ROUND_START; scenario.method = "onRoundStart";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, KeepDamageMatchesDefaultBuildingAndUpgradeCounting)
{
	Scenario scenario{"arrowTowerDamage"};
	scenario.event = CombatEventType::BATTLE_SETUP; scenario.method = "onBattleSetup";
	scenario.towerPosition = BattleHex(BattleHex::CASTLE_CENTRAL_TOWER);
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, LesserTowerDamageMatchesCustomFractionalParameters)
{
	Scenario scenario{"arrowTowerDamage"};
	scenario.parameters["keepBase"].Float() = 13.5;
	scenario.parameters["towerBase"].Float() = 7.25;
	scenario.parameters["perBuilding"].Float() = 1.5;
	scenario.event = CombatEventType::BATTLE_SETUP; scenario.method = "onBattleSetup";
	scenario.towerPosition = BattleHex(BattleHex::CASTLE_BOTTOM_TOWER);
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, GuardiansPreserveOrderedPlacement)
{
	Scenario scenario{"summonGuardians"}; scenario.parameters["creature"].String() = "core:griffin";
	scenario.parameters["val"].Integer() = 50; scenario.event = CombatEventType::BATTLE_START; scenario.method = "onBattleStart";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
TEST_F(NativeCombatEventsParityTest, DoubleWideGuardiansPreserveOrderedPlacement)
{
	Scenario scenario{"summonGuardians"}; scenario.parameters["creature"].String() = "core:cavalier";
	scenario.parameters["val"].Integer() = 50; scenario.event = CombatEventType::BATTLE_START; scenario.method = "onBattleStart";
	ASSERT_NO_FATAL_FAILURE(compare(scenario));
}
