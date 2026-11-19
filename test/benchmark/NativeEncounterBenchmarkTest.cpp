/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEncounterBenchmarkSupport.h"
#include "NativeEncounterBenchmarkInputs.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/filesystem/ResourcePath.h"
#ifdef NH_PERF_EXPERIMENTS
#include "SearchWorkXml.h"
#endif
#include "../SpellPointTestUtils.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/battle/AutocombatPreferences.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include <array>

namespace
{
using namespace nativeBenchmark;
battleSearch::Profile selectedProfile()
{
	const char * value = std::getenv("NH_BENCHMARK_POLICY");
	if(!value || std::string_view(value) == "Full") return battleSearch::Profile::Full;
	if(std::string_view(value) == "Fast") return battleSearch::Profile::BeamOneTwoActors;
	throw std::invalid_argument("Expected explicit Full or Fast benchmark policy");
}
class DISABLED_NativeEncounterBenchmarkTest : public TinyMapGameTest
{
protected:
	LogScope logging;
	EncounterServer server;
	std::unique_ptr<CGameHandler> handler;
	std::shared_ptr<EncounterEnvironment> environment;
	std::array<std::shared_ptr<EncounterCallback>, 2> callbacks;
	std::array<std::unique_ptr<CBattleAI>, 2> ais;
	std::vector<BattleAction> submitted;
	JsonNode initial;
	uint64_t callbackUs = 0;
	uint64_t callbacksMade = 0;
	int maximumRound = 0;
	const int seed = benchmarkSeed();

	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(seed); }
	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
		// Keep a real result query open: this workload ends at result receipt,
		// before acknowledgment, progression decisions and adventure continuation.
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
	void TearDown() override
	{
		for(auto & ai : ais) ai.reset();
		for(auto & callback : callbacks) callback.reset();
		environment.reset();
		handler.reset();
		server.gameState.reset();
		TinyMapGameTest::TearDown();
	}
	std::shared_ptr<CArmedInstance> plainArmy(const Army & roster, PlayerColor owner)
	{
		auto army = std::make_shared<CArmedInstance>(gameState().get());
		army->tempOwner = owner;
		army->pos = int3(4, 4, 0);
		army->appearance = std::make_shared<ObjectTemplate>();
		army->instanceName = owner == PlayerColor(0) ? "benchmarkLeft" : "benchmarkRight";
		for(size_t slot = 0; slot < roster.size(); ++slot)
			army->setCreature(SlotID(slot), roster[slot].first, roster[slot].second);
		map()->addNewObject(army);
		army->attachToBonusSystem(*gameState());
		return army;
	}
	void grantSpells(CGHeroInstance * hero, bool sorcery)
	{
		if(!hero->getArt(ArtifactPosition::SPELLBOOK))
		{
			NewArtifact book;
			book.artHolder = hero->id;
			book.artId = ArtifactID::SPELLBOOK;
			book.pos = ArtifactPosition::SPELLBOOK;
			handler->sendAndApply(book);
		}
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, sorcery ? 80 : 24, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 17, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, sorcery ? 80 : 24);
		ASSERT_GE(hero->manaLimit(), sorcery ? 80 : 24);
		ASSERT_EQ(hero->getNormalSpellPoints(), sorcery ? 80 : 24);
		ASSERT_EQ(hero->getBufferSpellPoints(), 0);
		std::vector<SpellID> spells{SpellID::CURE, SpellID::ICE_BOLT};
		if(sorcery)
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
				3, ChangeValueMode::ABSOLUTE);
			for(const auto * name : {"new-horizons:focusMagic", "new-horizons:forcedRecall", "new-horizons:forceLance"})
				spells.emplace_back(SpellID::decode(name));
		}
		for(const auto spell : spells)
		{
			ASSERT_TRUE(spell.hasValue());
			hero->addSpellToSpellbook(spell);
			initial["spells"].Vector().emplace_back(SpellID::encode(spell.getNum()));
		}
	}
	void arrangeRuntimeArmy(CArmedInstance & army, const Army & expected)
	{
		ASSERT_LE(expected.size(), 7u);
		army.clearSlots();
		for(size_t slot = 0; slot < expected.size(); ++slot)
		{
			ASSERT_NE(expected[slot].first.toCreature(), nullptr);
			ASSERT_GT(expected[slot].second, 0);
			army.setCreature(SlotID(slot), expected[slot].first, expected[slot].second);
		}
		ASSERT_EQ(army.Slots().size(), expected.size());
		for(size_t slot = 0; slot < expected.size(); ++slot)
		{
			const auto * stack = army.getStackPtr(SlotID(slot));
			ASSERT_NE(stack, nullptr);
			ASSERT_EQ(stack->getId(), expected[slot].first);
			ASSERT_EQ(stack->getCount(), expected[slot].second);
		}
	}
	void verifyStartingHero(const CGHeroInstance & hero)
	{
		const JsonNode canonical(JsonPath::builtin("config/newHorizonsHeroes"));
		ASSERT_EQ(gameState()->getHeroDevelopmentRules(), canonical);
		const auto & starts = canonical["startingSkills"];
		ASSERT_EQ(starts["startingPerksPolicyVersion"].Integer(), 4);
		const auto identity = HeroTypeID::encode(hero.getHeroTypeID().getNum());
		const auto & expected = starts["startingPerkProfiles"][identity].Vector();
		ASSERT_EQ(expected.size(), 1u) << identity;
		ASSERT_EQ(hero.getPerkState().getSelected().size(), 1u) << identity;
		EXPECT_TRUE(hero.getPerkState().hasSelection(expected.front()["skill"].String(), expected.front()["perk"].String()));
		ASSERT_EQ(hero.secSkills.size(), 2u);
		for(const auto & entry : starts["startingDevelopmentProfiles"][identity]["skills"].Vector())
			EXPECT_EQ(hero.getSecSkillLevel(SecondarySkill(SecondarySkill::decode(entry["skill"].String()))), 1);
		for(const auto & [skill, rank] : hero.secSkills) EXPECT_EQ(rank, 1);
		ASSERT_NE(hero.getArt(ArtifactPosition::SPELLBOOK), nullptr);
	}
	void verifyBenchmarkPreset()
	{
		const auto & active = LIBRARY->modh->getActiveMods();
		for(const std::string name : {"core", "vcmi", "vcmi-test", "new-horizons"})
			ASSERT_NE(std::find(active.begin(), active.end(), name), active.end()) << name;
		ASSERT_EQ(active.size(), 4u) << "Encounter benchmarks require the isolated curated test preset";
		for(const auto & mod : active) initial["activeMods"].Vector().emplace_back(mod);
	}
	void prepare(const Army & left, const Army & right, const char * heroName, bool options, bool sorcery = false, bool neutralDefender = false, bool nativeHeroDefaults = false, const char * defenderHeroName = nullptr)
	{
		ASSERT_NO_FATAL_FAILURE(verifyBenchmarkPreset());
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Owned native encounter benchmark")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({15, 15, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
			.town({25, 25, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
		if(heroName)
		{
			// SOD cannot encode runtime-added creature IDs. Load a valid body,
			// then arrange and verify the actual benchmark army before battle.
			const Army placeholder{{troop("core:pikeman"), 1}};
			builder.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(heroName)), PlayerColor(0)).heroGarrison(placeholder);
			if(neutralDefender)
			{
				ASSERT_EQ(right.size(), 1u);
				builder.monster({7, 7, 0}, right.front().first, right.front().second);
			}
			else
				builder.hero({7, 7, 0}, defenderHeroName ? HeroTypeID(HeroTypeID::decode(defenderHeroName)) : HeroTypeID(1), PlayerColor(1)).heroGarrison(placeholder);
		}
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder), EMapDifficulty::NORMAL));
		server.gameState = gameState();
		handler = std::make_unique<CGameHandler>(server, gameState());
		handler->randomizer->setSeed(seed);
		CRandomGenerator::getDefault().setSeed(seed);
		CGHeroInstance * leftHero = heroName ? findHeroByOwner(PlayerColor(0)) : nullptr;
		CGHeroInstance * rightHero = heroName && !neutralDefender ? findHeroByOwner(PlayerColor(1)) : nullptr;
		std::shared_ptr<CArmedInstance> leftPlain, rightPlain;
		const auto * neutral = neutralDefender ? findFirst<CGCreature>() : nullptr;
		if(neutralDefender) ASSERT_NE(neutral, nullptr);
		if(heroName)
		{
			ASSERT_NE(leftHero, nullptr);
			if(!neutralDefender) ASSERT_NE(rightHero, nullptr);
			ASSERT_NO_FATAL_FAILURE(arrangeRuntimeArmy(*leftHero, left));
			if(rightHero) ASSERT_NO_FATAL_FAILURE(arrangeRuntimeArmy(*rightHero, right));
			// Assert the actual authored creation ledger before external rank grants.
			ASSERT_NO_FATAL_FAILURE(verifyStartingHero(*leftHero));
			if(rightHero) ASSERT_NO_FATAL_FAILURE(verifyStartingHero(*rightHero));
			if(options && !nativeHeroDefaults) ASSERT_NO_FATAL_FAILURE(grantSpells(leftHero, sorcery));
			initial["attackerHero"] = heroEvidence(*leftHero);
			if(rightHero) initial["defenderHero"] = heroEvidence(*rightHero);
		}
		else
		{
			leftPlain = plainArmy(left, PlayerColor(0));
			rightPlain = plainArmy(right, PlayerColor(1));
		}
		const CArmedInstance * leftArmy = leftHero ? leftHero : leftPlain.get();
		const CArmedInstance * rightArmy = neutral ? static_cast<const CArmedInstance *>(neutral)
			: rightHero ? rightHero : rightPlain.get();
		initial["attackerArmy"] = armyEvidence(*leftArmy);
		initial["defenderArmy"] = armyEvidence(*rightArmy);
		initial["defenderProvenance"].String() = neutral ? "CGCreature" : heroName ? "CGHeroInstance" : "CArmedInstance";
		BattleLayout layout;
		constexpr std::array<int, 7> rows{0, 2, 4, 5, 6, 8, 10};
		for(size_t slot = 0; slot < 7; ++slot)
		{
			layout.units[BattleSide::ATTACKER][slot] = BattleHex(2, rows[slot]);
			layout.units[BattleSide::DEFENDER][slot] = BattleHex(13, rows[slot]);
		}
		layout.obstaclesAllowed = false;
		layout.tacticsAllowed = false;
		handler->battles->startBattle(leftArmy, rightArmy, int3(4, 4, 0), leftHero, rightHero, layout, nullptr);
		const auto * current = gameState()->getBattle(BattleID(0));
		ASSERT_NE(current, nullptr);
		initial["terrain"].Integer() = current->getTerrainType().getNum();
		initial["battlefield"].Integer() = current->getBattlefieldType().getNum();
		initial["obstacles"].Integer() = current->obstacles.size();
		for(const auto * unit : current->battleGetAllUnits(false))
			initial["unitStates"].Vector().push_back(unit->acquireState()->save());
		environment = std::make_shared<EncounterEnvironment>(gameState());
		for(size_t index = 0; index < 2; ++index)
		{
			const auto side = index == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
			// Battle admission normalizes unflaggable map-army owners to Neutral.
			// The AI callback must use that fighting-side identity.
			callbacks[index] = std::make_shared<EncounterCallback>(current->getSidePlayer(side));
			callbacks[index]->onBattleStarted(current);
			ASSERT_EQ(callbacks[index]->getBattle(BattleID(0))->battleGetMySide(), side);
			ais[index] = std::make_unique<CBattleAI>(nullptr, nullptr, selectedProfile());
			AutocombatPreferences preferences;
			preferences.enableSpellsUsage = options;
			preferences.enableTacticsUsage = false;
			ais[index]->initBattleInterface(environment, callbacks[index], preferences);
			ais[index]->battleStart(BattleID(0), leftArmy, rightArmy, int3(4, 4, 0), leftHero, rightHero,
				index == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER, false);
		}
	}
	void runEncounter()
	{
		RecordProperty("initialEvidence", initial.toString());
		RecordProperty("authorityRngBefore", hex(bytes(*handler->randomizer)));
		RecordProperty("aiRngBefore", hex(bytes(CRandomGenerator::getDefault())));
		const auto initialPacketCount = server.packets;
		const auto started = Clock::now();
		while(!server.result && callbacksMade < 512)
		{
			auto * current = gameState()->getBattle(BattleID(0));
			ASSERT_NE(current, nullptr);
			maximumRound = std::max(maximumRound, current->getRound());
			ASSERT_LE(maximumRound, 64) << "Bounded fixture failed to finish";
			const auto * actor = current->battleActiveUnit();
			ASSERT_NE(actor, nullptr) << "No active unit; deployment/flow prerequisite unresolved";
			const auto player = current->battleGetActionController(actor);
			const auto index = player == current->getSide(BattleSide::ATTACKER).color ? size_t{0} : size_t{1};
			ASSERT_TRUE(player == current->getSide(BattleSide::ATTACKER).color
				|| player == current->getSide(BattleSide::DEFENDER).color);
			for(auto & ai : ais) ai->battleNewRound(BattleID(0));
			callbacks[index]->pending.clear();
			const auto authorityBefore = bytes(*handler->randomizer);
			const auto aiBefore = bytes(CRandomGenerator::getDefault());
			const auto callbackStart = Clock::now();
			ais[index]->activeStack(BattleID(0), current->getStack(actor->unitId()));
			callbackUs += microseconds(callbackStart);
			++callbacksMade;
			ASSERT_EQ(bytes(*handler->randomizer), authorityBefore);
			ASSERT_EQ(bytes(CRandomGenerator::getDefault()), aiBefore);
			ASSERT_EQ(callbacks[index]->pending.size(), 1u);
			const auto action = callbacks[index]->pending.front();
			submitted.push_back(action);
			// Apply only after activeStack returns: forecasts never reenter live
			// authoritative mutation from inside their submission callback.
			ASSERT_TRUE(handler->battles->makePlayerBattleAction(BattleID(0), player, action));
		}
		RecordProperty("encounterUs", std::to_string(microseconds(started)));
		ASSERT_TRUE(server.result) << "512-callback budget reached; not a successful whole fight";
		RecordProperty("callbackUs", std::to_string(callbackUs));
		RecordProperty("callbacks", std::to_string(callbacksMade));
		RecordProperty("rounds", std::to_string(maximumRound));
		RecordProperty("packets", std::to_string(server.packets - initialPacketCount));
		RecordProperty("submittedActionsHex", hex(bytes(submitted)));
		RecordProperty("terminalUnits", server.terminalUnits.toString());
		RecordProperty("winner", std::to_string(static_cast<int>(server.result->winner)));
		JsonNode outcome, actions;
		outcome["winner"].Integer() = static_cast<int>(server.result->winner);
		outcome["resultType"].Integer() = static_cast<int>(server.result->result);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const std::string name = side == BattleSide::ATTACKER ? "attacker" : "defender";
			outcome[name]["experience"].Integer() = server.result->exp[side];
			for(const auto & [creature, killed] : server.result->casualties[side])
				outcome[name]["casualties"][creature.toCreature()->getJsonKey()].Integer() = killed;
		}
		for(const auto & action : submitted) actions.Vector().emplace_back(action.toString());
		RecordProperty("outcome", outcome.toString());
		RecordProperty("submittedActions", actions.toString());
		RecordProperty("casualtiesHex", hex(bytes(server.result->casualties)));
		RecordProperty("experienceHex", hex(bytes(server.result->exp)));
		RecordProperty("authorityRngAfter", hex(bytes(*handler->randomizer)));
		RecordProperty("aiRngAfter", hex(bytes(CRandomGenerator::getDefault())));
		RecordProperty("seed", std::to_string(seed));
		RecordProperty("policy", selectedProfile() == battleSearch::Profile::Full ? "Full" : "Fast/BeamOneTwoActors");
		RecordProperty("scope", "Authoritative encounter to result receipt; includes callback RNG witnesses and packet observers; excludes init, acknowledgment, progression and adventure continuation");
		for(size_t index = 0; index < 2; ++index)
		{
			const auto timing = ais[index]->getTimingSummary();
			ASSERT_TRUE(timing);
			const auto prefix = std::string("side") + std::to_string(index);
#ifdef NH_PERF_EXPERIMENTS
			nativeBenchmark::recordSearchWorkDiagnostics(prefix, *timing);
#endif
			RecordProperty(prefix + "MovementExchanges", std::to_string(timing->movementExchangeBody.calls));
			RecordProperty(prefix + "NormalExchanges", std::to_string(timing->bestTargetNormalBody.calls));
			RecordProperty(prefix + "WaitedExchanges", std::to_string(timing->bestTargetWaitedBody.calls));
			RecordProperty(prefix + "TargetProbes", std::to_string(timing->movementExchangeTargetProbes.calls
				+ timing->bestTargetNormalTargetProbes.calls + timing->bestTargetWaitedTargetProbes.calls));
			ais[index]->battleEnd(BattleID(0), &*server.result, QueryID::NONE);
			callbacks[index]->onBattleEnded(BattleID(0));
		}
	}
};

TEST_F(DISABLED_NativeEncounterBenchmarkTest, NoHeroFourteenStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rangedArmy(), knightArmy(), nullptr, false));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, HeroOptionsFourteenStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rangedArmy(), knightArmy(), "core:orrin", true));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, CastleEncounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare({{troop("core:pikeman"), 14}, {troop("core:archer"), 9}},
		{{troop("core:halfling"), 29}}, "core:christian", true, false, true));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, NecropolisEncounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare({{troop("core:skeleton"), 24}, {troop("core:walkingDead"), 12}},
		{{troop("core:halfling"), 29}}, "core:straker", true, false, true));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, RampartEncounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare({{troop("core:centaur"), 18}, {troop("core:woodElf"), 9}},
		{{troop("core:halfling"), 29}}, "core:mephala", true, false, true));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, ConfluxAenainGnolls)
{
	ASSERT_NO_FATAL_FAILURE(prepare({{troop("new-horizons:wisp"), 14}, {troop("core:airElemental"), 2},
		{troop("core:waterElemental"), 2}}, {{troop("core:gnoll"), 24}}, "core:aenain", true, false, true, true));
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->Slots().size(), 3u);
	const auto * current = gameState()->getBattle(BattleID(0));
	ASSERT_NE(current, nullptr);
	const auto units = current->battleGetAllUnits(false);
	ASSERT_EQ(units.size(), 4u);
	const Army expected{{troop("new-horizons:wisp"), 14}, {troop("core:airElemental"), 2},
		{troop("core:waterElemental"), 2}};
	for(size_t slot = 0; slot < expected.size(); ++slot)
	{
		const auto found = std::ranges::find_if(units, [slot](const auto * unit)
		{
			return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(slot);
		});
		ASSERT_NE(found, units.end());
		ASSERT_EQ((*found)->creatureId(), expected[slot].first);
		ASSERT_EQ((*found)->getCount(), expected[slot].second);
	}
	const auto enemy = std::ranges::find_if(units, [](const auto * unit)
	{
		return unit->unitSide() == BattleSide::DEFENDER;
	});
	ASSERT_NE(enemy, units.end());
	ASSERT_EQ((*enemy)->creatureId(), troop("core:gnoll"));
	ASSERT_EQ((*enemy)->getCount(), 24);
	for(const auto & [slot, stack] : hero->Slots())
	{
		// Current Leadership reduces oversized-stack damage; it never imposes
		// an army admission cap. Record the real efficiency used by this fixture.
		EXPECT_FALSE(hero->getLeadershipSlotCapacity(stack->getId()));
		const auto readout = newHorizonsHeroes::leadershipStackReadout(hero,
			stack->getId(), stack->getCount());
		ASSERT_TRUE(readout.active);
		ASSERT_GE(readout.capacity, 1);
		ASSERT_GT(readout.efficiency, 0.0);
		ASSERT_LE(readout.efficiency, 1.0);
		auto & row = initial["leadership"][std::to_string(slot.getNum())];
		row["base"].Integer() = readout.baseLeadership;
		row["effective"].Integer() = readout.effectiveLeadership;
		row["requirement"].Integer() = readout.requirement;
		row["capacity"].Integer() = readout.capacity;
		row["efficiency"].Float() = readout.efficiency;
	}
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, SorceryEncounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare({{troop("core:gremlin"), 24}, {troop("core:stoneGargoyle"), 12}},
		{{troop("core:halfling"), 29}}, "core:neela", true, true, true));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorCastle)
{
	const auto army = mirrorCastleArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, nullptr, false));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorRampart)
{
	const auto army = mirrorRampartArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, nullptr, false));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorNecropolis)
{
	const auto army = mirrorNecropolisArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, nullptr, false));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorCastleHeroOptions)
{
	const auto army = mirrorCastleArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, "core:orrin", true, false, false, true, "core:orrin"));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorRampartHeroOptions)
{
	const auto army = mirrorRampartArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, "core:mephala", true, false, false, true, "core:mephala"));
	runEncounter();
}
TEST_F(DISABLED_NativeEncounterBenchmarkTest, MirrorNecropolisHeroOptions)
{
	const auto army = mirrorNecropolisArmy();
	ASSERT_NO_FATAL_FAILURE(prepare(army, army, "core:straker", true, false, false, true, "core:straker"));
	runEncounter();
}

}
