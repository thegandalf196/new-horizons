/*
 * NewHorizonsGamblerTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "BattleStartSnapshotFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include "../../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto gamblerId = "new-horizons:luck.gambler";

JsonNode chanceCurve(int value)
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(value);
	return result;
}

class GamblerAdmissionServer final : public RecordingGameServer
{
public:
	size_t appliedPackets = 0;
	void applyPack(CPackForClient & pack) override
	{
		++appliedPackets;
		RecordingGameServer::applyPack(pack);
	}
};

class GamblerEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit GamblerEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsGamblerTest : public BattleTestFixture
{
protected:
	int goodLuckChance = 100;
	int badLuckChance = 100;
	bool captureZeroHeroLuckChance = false;
	bool captureLegacyWithoutHeroLuck = false;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, chanceCurve(goodLuckChance));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, chanceCurve(badLuckChance));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
		if(captureZeroHeroLuckChance || captureLegacyWithoutHeroLuck)
		{
			auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
			if(captureLegacyWithoutHeroLuck)
			{
				magic.Struct().erase("heroLuck");
				magic.Struct().erase("heroMorale");
			}
			else
			{
				for(const char * key : {"luckyDayChanceScalePercent", "erraticManaChanceScalePercent", "druidicLuckChanceScalePercent"})
					magic["heroLuck"][key].Integer() = 0;
			}
			magic.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
	}

	CStack * addStack(BattleSide side, const CreatureID & creature, const BattleHex & position, int32_t count)
	{
		auto * stack = BattleTestFixture::addStack(side, creature, position, count);
		const auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		if(hero && hero->hasActivePerk(luckSkillId, gamblerId))
		{
			// Advanced Luck is required to select Erratic Mana and normally contributes +2.
			// Cancel only that ordinary skill value so tests detect any retired creature Luck window.
			stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::LUCK, BonusSource::OTHER, -2, BonusSourceID()));
		}
		return stack;
	}

	void selectGambler(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		// Erratic Mana retains the Advanced Luck gambler ID. Establish its actual Basic Luck
		// prerequisite first, then accept Gambler through the ordinary offer.
		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, fortuneFavorId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, fortuneFavorId));
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, gamblerId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, gamblerId));
	}

	void acceptLuckPerkThroughOffer(CGHeroInstance * hero, const char * perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
			const auto selected = std::find_if(offer.begin(), offer.end(), [perkId](const auto & candidate)
			{
				return candidate.selection.skillId == luckSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(luckSkillId, perkId));
			return;
		}

		FAIL() << perkId << " never appeared in a legal Luck perk offer";
	}

	bool submit(CStack * attacker, BattleAction action)
	{
		battle()->activeStack = attacker->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(attacker), action);
	}

	bool melee(CStack * attacker, CStack * target)
	{
		return submit(attacker, BattleAction::makeMeleeAttack(
			attacker, target->getPosition(), attacker->getPosition()));
	}

	bool shoot(CStack * attacker, CStack * target)
	{
		return submit(attacker, BattleAction::makeShotAttack(attacker, target));
	}

	void activate(const CStack * stack, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = stack->unitId();
		pack.reason = reason;
		gameHandler->sendAndApply(pack);
	}

	const Bonus * gamblerPenalty(const CStack * stack) const
	{
		const auto bonuses = stack->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		}));
		return !bonuses || bonuses->empty() ? nullptr : bonuses->front().get();
	}
};
}

TEST_F(NewHorizonsGamblerTest, CurrentCapturedPolicyRejectsBattleStartBeforeDecodeInstallOrBroadcast)
{
	captureZeroHeroLuckChance = true;
	startGame();
	startBattle();
	ASSERT_TRUE(battle()->getMagicRules()["heroLuck"].isStruct());
	for(const char * key : {"luckyDayChanceScalePercent", "erraticManaChanceScalePercent", "druidicLuckChanceScalePercent"})
	{
		ASSERT_EQ(battle()->getMagicRules()["heroLuck"][key].Integer(), 0);
	}
	// Existing rule validation rejects present malformed blocks; only null is
	// the legacy absence marker. No invalid battle capture is manufactured here.
	JsonNode malformedArray;
	malformedArray.Vector().emplace_back(1);
	for(const auto & malformed : {JsonNode(1), malformedArray})
	{
		auto rules = battle()->getMagicRules();
		rules["heroLuck"] = malformed;
		EXPECT_THROW(newHorizonsHeroLuck::validateRules(rules), std::runtime_error);
	}
	{
		auto replica = std::make_shared<CGameState>();
		replica->preInit(LIBRARY);
		replica->loadFromMemory(gameState()->saveToMemory());
		BattleStart clean;
		clean.battleID = replica->nextBattleID;
		clean.info = battleStartFixture::snapshot(*battle(), replica.get());
		CMemorySerializer wire;
		wire.oser & clean;
		wire.iser.cb = replica.get();
		BattleStart decoded;
		ASSERT_NO_THROW(wire.iser & decoded);
		GamblerAdmissionServer recording;
		recording.gameState = replica;
		CGameHandler handler(recording, replica);
		ASSERT_NO_THROW(handler.sendAndApply(decoded));
		EXPECT_EQ(recording.appliedPackets, 1u);
		ASSERT_NE(replica->getBattle(clean.battleID), nullptr);
		EXPECT_FALSE(replica->getBattle(clean.battleID)->getSylvanLuckState(BattleSide::ATTACKER).gambler);
	}
	for(const bool usedHistory : {false, true})
	{
		auto replica = std::make_shared<CGameState>();
		replica->preInit(LIBRARY);
		replica->loadFromMemory(gameState()->saveToMemory());
		ASSERT_TRUE(replica->currentBattles.empty());
		const auto before = replica->saveToMemory();
		BattleStart outgoing;
		outgoing.battleID = replica->nextBattleID;
		outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
		// Explicit zero chance settings still select current policy; the marker is
		// the validated captured block, not a proc chance or the serialization epoch.
		ASSERT_EQ(outgoing.info->getMagicRules(), battle()->getMagicRules());
		auto & fortune = outgoing.info->getSide(BattleSide::ATTACKER).sylvanLuck;
		fortune.gambler = true;
		fortune.gamblerAttackUsedThisRound = usedHistory;
		CMemorySerializer wire;
		wire.oser & outgoing;
		wire.iser.cb = replica.get();
		BattleStart decoded;
		EXPECT_THROW(wire.iser & decoded, std::runtime_error);
		GameStatePackVisitor visitor(*replica);
		EXPECT_THROW(outgoing.visit(visitor), std::runtime_error);
		GamblerAdmissionServer recording;
		recording.gameState = replica;
		CGameHandler handler(recording, replica);
		EXPECT_THROW(handler.sendAndApply(outgoing), std::runtime_error);
		EXPECT_EQ(recording.appliedPackets, 0u);
		EXPECT_TRUE(replica->currentBattles.empty());
		EXPECT_EQ(replica->saveToMemory(), before);
	}
}

TEST_F(NewHorizonsGamblerTest, AbsentCapturedPolicyKeepsLegacyBattleStartAndRawCodecReadable)
{
	captureLegacyWithoutHeroLuck = true;
	startGame();
	startBattle();
	ASSERT_TRUE(battle()->getMagicRules()["heroLuck"].isNull());
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(gameState()->saveToMemory());
	BattleStart outgoing;
	outgoing.battleID = replica->nextBattleID;
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	// Historical captured rules remain a distinct compatibility context. This
	// constructed packet is not an ordinary supported mid-battle save protocol.
	ASSERT_EQ(outgoing.info->getMagicRules(), battle()->getMagicRules());
	outgoing.info->getSide(BattleSide::ATTACKER).sylvanLuck.gambler = true;
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart decoded;
	ASSERT_NO_THROW(wire.iser & decoded);
	GamblerAdmissionServer recording;
	recording.gameState = replica;
	CGameHandler handler(recording, replica);
	ASSERT_NO_THROW(handler.sendAndApply(decoded));
	EXPECT_EQ(recording.appliedPackets, 1u);
	const auto * restored = replica->getBattle(outgoing.battleID);
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->getSylvanLuckState(BattleSide::ATTACKER).gambler);
	EXPECT_EQ(restored->getSylvanLuckState(BattleSide::ATTACKER).chanceLuck(0, 42, false), 3);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gambler);
}

TEST_F(NewHorizonsGamblerTest, CurrentCapturedPolicyRejectsAttackInjectionAndRetainedHistoryBeforeDamage)
{
	startGame();
	startBattle();
	beginCombat();
	const auto attackers = battle()->getStacksIf([](const CStack * stack) { return stack->unitSide() == BattleSide::ATTACKER; });
	const auto defenders = battle()->getStacksIf([](const CStack * stack) { return stack->unitSide() == BattleSide::DEFENDER; });
	ASSERT_FALSE(attackers.empty());
	ASSERT_FALSE(defenders.empty());
	const auto * actor = attackers.front();
	const auto * target = defenders.front();
	BattleAttack outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.stackAttacking = actor->unitId();
	outgoing.fortuneSide = BattleSide::ATTACKER;
	outgoing.fortuneState = SylvanLuckState{};
	outgoing.fortuneState->gambler = true;
	BattleStackAttacked hit;
	hit.stackAttacked = target->unitId();
	hit.attackerID = actor->unitId();
	hit.damageAmount = 1;
	target->prepareAttacked(hit, gameHandler->getRandomGenerator(), false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_NE(hit.newState.data, target->acquireState()->save());
	outgoing.bsa.push_back(hit);
	CMemorySerializer wire;
	wire.oser & outgoing;
	BattleAttack decoded;
	wire.iser & decoded; // Raw legacy attack codec remains readable, even now.
	const auto actorBefore = actor->acquireState()->save();
	const auto targetBefore = target->acquireState()->save();
	const auto attacksBefore = server.attacks.size();
	GameStatePackVisitor visitor(*gameState());
	EXPECT_THROW(decoded.visit(visitor), std::runtime_error);
	EXPECT_THROW(gameHandler->sendAndApply(decoded), std::runtime_error);
	EXPECT_EQ(server.attacks.size(), attacksBefore);
	EXPECT_EQ(actor->acquireState()->save(), actorBefore);
	EXPECT_EQ(target->acquireState()->save(), targetBefore);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gambler);

	// Clean incoming state cannot launder contradictory retained current state.
	// This history-only raw shape is intentionally tested in-process, not
	// attributed to the reader policy: its raw codec already rejects it.
	decoded.fortuneState.reset();
	const auto original = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.gamblerAttackUsedThisRound = true;
	const auto contradictory = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_THROW(decoded.visit(visitor), std::runtime_error);
	EXPECT_THROW(gameHandler->sendAndApply(decoded), std::runtime_error);
	EXPECT_EQ(battle()->getSylvanLuckState(BattleSide::ATTACKER), contradictory);
	EXPECT_EQ(server.attacks.size(), attacksBefore);
	EXPECT_EQ(target->acquireState()->save(), targetBefore);
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck = original;
	SylvanLuckState legitimate;
	legitimate.serendipity = legitimate.secondChance = legitimate.chainOfFortune = true;
	EXPECT_NO_THROW(battle()->validateCapturedGamblerPolicy(&legitimate));
}

TEST(GamblerRulesTest, SideWindowAndAttackPacketRoundTripKeepLegacyDefaultsAndRejectDowngrade)
{
	SylvanLuckState state;
	state.gambler = true;
	EXPECT_TRUE(state.gamblerAttackAvailable());
	EXPECT_EQ(state.chanceLuck(0, 42, false), 3);
	EXPECT_FALSE(state.recordStrike(42, false, false));
	EXPECT_TRUE(state.gamblerAttackUsedThisRound)
		<< "The first executed attack consumes the side window even without a positive Luck result";
	EXPECT_FALSE(state.gamblerAttackAvailable());
	EXPECT_EQ(state.chanceLuck(0, 42, false), 0);

	CMemorySerializer stateWire;
	stateWire.oser & state;
	SylvanLuckState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, state);

	BattleAttack packet;
	packet.battleID = BattleID(0);
	packet.stackAttacking = 42;
	packet.fortuneSide = BattleSide::ATTACKER;
	packet.fortuneState = state;
	CMemorySerializer packetWire;
	packetWire.oser & packet;
	BattleAttack restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.fortuneSide, BattleSide::ATTACKER);
	ASSERT_TRUE(restoredPacket.fortuneState);
	EXPECT_EQ(*restoredPacket.fortuneState, state);

	CMemorySerializer rejectedStateDowngrade;
	rejectedStateDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	EXPECT_THROW(rejectedStateDowngrade.oser & state, std::runtime_error);
	EXPECT_TRUE(rejectedStateDowngrade.extractBuffer().empty());

	CMemorySerializer rejectedPacketDowngrade;
	rejectedPacketDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	EXPECT_THROW(rejectedPacketDowngrade.oser & packet, std::runtime_error);

	SylvanLuckState legacy;
	legacy.serendipity = true;
	CMemorySerializer legacyWire;
	legacyWire.oser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	legacyWire.oser & legacy;
	SylvanLuckState restoredLegacy;
	restoredLegacy.gambler = true;
	restoredLegacy.gamblerAttackUsedThisRound = true;
	legacyWire.iser & restoredLegacy;
	EXPECT_TRUE(restoredLegacy.serendipity);
	EXPECT_FALSE(restoredLegacy.gambler);
	EXPECT_FALSE(restoredLegacy.gamblerAttackUsedThisRound);

	BattleAttack legacyPacket;
	legacyPacket.battleID = BattleID(0);
	legacyPacket.stackAttacking = 42;
	legacyPacket.fortuneSide = BattleSide::ATTACKER;
	legacyPacket.fortuneState = legacy;
	CMemorySerializer legacyPacketWire;
	legacyPacketWire.oser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	legacyPacketWire.iser.version = ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE;
	legacyPacketWire.oser & legacyPacket;
	BattleAttack restoredLegacyPacket;
	legacyPacketWire.iser & restoredLegacyPacket;
	ASSERT_TRUE(restoredLegacyPacket.fortuneState);
	EXPECT_FALSE(restoredLegacyPacket.fortuneState->gambler)
		<< "Pre-Gambler strike packets decode with an inert default";

	SylvanLuckState invalid;
	invalid.gamblerAttackUsedThisRound = true;
	CMemorySerializer invalidWire;
	invalidWire.oser & invalid;
	SylvanLuckState decodedInvalid;
	EXPECT_THROW(invalidWire.iser & decodedInvalid, std::runtime_error);
}

TEST_F(NewHorizonsGamblerTest, SelectedErraticManaLeavesBothCreatureWindowsInertAcrossRounds)
{
	startGame();
	selectGambler(attackerSideHero);
	selectGambler(defenderSideHero);
	startBattle();
	auto * firstAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * secondAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 2), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	blockRetaliation(firstAttacker);
	blockRetaliation(secondAttacker);
	blockRetaliation(defender);
	beginCombat();

	ASSERT_EQ(battle()->battleGetAttackLuck(firstAttacker, defender, false), 0);
	ASSERT_TRUE(melee(firstAttacker, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	if(server.attacks.back().fortuneState)
	{
		EXPECT_FALSE(server.attacks.back().fortuneState->gambler);
		EXPECT_FALSE(server.attacks.back().fortuneState->gamblerAttackUsedThisRound);
	}
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).gamblerAttackUsedThisRound);
	EXPECT_EQ(gamblerPenalty(firstAttacker), nullptr);

	server.attacks.clear();
	EXPECT_EQ(battle()->battleGetAttackLuck(secondAttacker, defender, false), 0);
	ASSERT_TRUE(melee(secondAttacker, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	EXPECT_EQ(gamblerPenalty(secondAttacker), nullptr);

	server.attacks.clear();
	EXPECT_EQ(battle()->battleGetAttackLuck(defender, firstAttacker, false), 0);
	ASSERT_TRUE(melee(defender, firstAttacker));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).gamblerAttackUsedThisRound);

	const auto roundBefore = battle()->getRound();
	endRound();
	EXPECT_EQ(battle()->getRound(), roundBefore + 1);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).gamblerAttackUsedThisRound);

	server.attacks.clear();
	EXPECT_EQ(battle()->battleGetAttackLuck(secondAttacker, defender, false), 0);
	ASSERT_TRUE(melee(secondAttacker, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
}

TEST_F(NewHorizonsGamblerTest, ControlledCreatureCannotAcquireARetiredLuckWindow)
{
	startGame();
	selectGambler(attackerSideHero);
	selectGambler(defenderSideHero);
	startBattle();
	auto * hypnotized = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * formerAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	blockRetaliation(hypnotized);
	blockRetaliation(formerAlly);
	const auto hypnotize = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	hypnotized->addNewBonus(hypnotize);

	ASSERT_EQ(battle()->battleGetOwner(hypnotized), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetAttackLuck(hypnotized, formerAlly, false), 0);
	ASSERT_TRUE(submit(hypnotized, BattleAction::makeMeleeAttack(
		hypnotized, formerAlly->getPosition(), hypnotized->getPosition())));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	if(server.attacks.back().fortuneState)
	{
		EXPECT_EQ(server.attacks.back().fortuneSide, BattleSide::DEFENDER);
		EXPECT_FALSE(server.attacks.back().fortuneState->gambler);
	}
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).gamblerAttackUsedThisRound);
}

TEST_F(NewHorizonsGamblerTest, DoubleShotAndActivationTransitionsNeverCreateRetiredPenalty)
{
	goodLuckChance = 0;
	badLuckChance = 100;
	startGame();
	selectGambler(attackerSideHero);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(rightHex + 5), 1000);
	blockRetaliation(shooter);
	blockRetaliation(target);
	beginCombat();

	ASSERT_EQ(battle()->battleGetAttackLuck(shooter, target, true), 0);
	ASSERT_TRUE(shoot(shooter, target));
	ASSERT_EQ(server.attacks.size(), 2u) << "Marksman resolves both arrows through the native attack path";
	EXPECT_FALSE(server.attacks[0].lucky());
	EXPECT_FALSE(server.attacks[0].unlucky());
	EXPECT_FALSE(server.attacks[1].unlucky());
	ASSERT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), 0);

	activate(shooter, BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
	activate(shooter, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
	activate(shooter, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);

	const auto roundBefore = battle()->getRound();
	endRound();
	ASSERT_EQ(battle()->getRound(), roundBefore + 1);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
	server.attacks.clear();
	EXPECT_EQ(battle()->battleGetAttackLuck(shooter, target, true), 0);
	ASSERT_TRUE(shoot(shooter, target));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_FALSE(server.attacks[0].unlucky());
	EXPECT_FALSE(server.attacks[1].unlucky());
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);

	HeroOrderState secondWind;
	secondWind.command = HeroCommand::SECOND_WIND;
	secondWind.issuedRound = battle()->battleGetRound();
	secondWind.secondWindActive = true;
	secondWind.primaryTargetUnitId = shooter->unitId();
	battle()->getSide(BattleSide::ATTACKER).upsertOrder(secondWind);
	activate(shooter, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
}

TEST_F(NewHorizonsGamblerTest, NoLuckAndControlChangesNeverCreateRetiredHistoryOrPenalty)
{
	startGame();
	selectGambler(attackerSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	const auto noLuck = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID());
	attacker->addNewBonus(noLuck);
	beginCombat();

	EXPECT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), 0);
	ASSERT_TRUE(melee(attacker, defender));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_FALSE(server.attacks.back().lucky());
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_EQ(gamblerPenalty(attacker), nullptr);

	attacker->removeBonus(noLuck);
	EXPECT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), 0);
	const auto hypnotize = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	attacker->addNewBonus(hypnotize);
	EXPECT_EQ(battle()->battleGetOwner(attacker), PlayerColor(1));
	EXPECT_EQ(gamblerPenalty(attacker), nullptr);
	EXPECT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), 0);
	attacker->removeBonus(hypnotize);
	EXPECT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), 0);
}

TEST_F(NewHorizonsGamblerTest, DetachedDoubleShotLeavesLiveParentAndSiblingRetiredStateInert)
{
	goodLuckChance = 50;
	badLuckChance = 50;
	startGame();
	selectGambler(attackerSideHero);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(rightHex + 5), 1000);
	blockRetaliation(shooter);
	blockRetaliation(target);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	GamblerEnvironment environment(gameState());
	auto root = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, root);
	auto projectedShooter = branch->getForUpdate(shooter->unitId());
	auto projectedTarget = branch->getForUpdate(target->unitId());
	ASSERT_NE(projectedShooter, nullptr);
	ASSERT_NE(projectedTarget, nullptr);
	BattleAttackInfo projectedShot(projectedShooter.get(), projectedTarget.get(), 0, true);
	EXPECT_EQ(branch->battleGetAttackLuck(projectedShooter.get(), projectedTarget.get(), true), 0);
	EXPECT_FALSE(branch->fortuneStrikeIsCertain(projectedShot));

	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(projectedShot, shooter->getPosition(), cache, branch);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 2u) << "The expected attack includes both Marksman arrows";
	EXPECT_EQ(prediction.effectPreview, nullptr)
		<< "Erratic Mana alone must not create a retired creature-fortune replay branch";
	EXPECT_FALSE(root->getSylvanLuckState(BattleSide::ATTACKER).gambler);
	EXPECT_FALSE(root->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(branch->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.empty());
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);

	auto sibling = std::make_shared<HypotheticBattle>(&environment, root);
	auto siblingShooter = sibling->getForUpdate(shooter->unitId());
	auto siblingTarget = sibling->getForUpdate(target->unitId());
	ASSERT_NE(siblingShooter, nullptr);
	ASSERT_NE(siblingTarget, nullptr);
	EXPECT_EQ(sibling->battleGetAttackLuck(siblingShooter.get(), siblingTarget.get(), true), 0);

	// Even a guaranteed positive chance curve cannot create a retired +3 window at zero Luck.
	battle()->luckRollRules.goodChance.assign(10, 100);
	auto certainRoot = std::make_shared<HypotheticBattle>(&environment, callback);
	auto certainBranch = std::make_shared<HypotheticBattle>(&environment, certainRoot);
	auto certainShooter = certainBranch->getForUpdate(shooter->unitId());
	auto certainTarget = certainBranch->getForUpdate(target->unitId());
	ASSERT_NE(certainShooter, nullptr);
	ASSERT_NE(certainTarget, nullptr);
	BattleAttackInfo certainShot(certainShooter.get(), certainTarget.get(), 0, true);
	ASSERT_EQ(certainBranch->battleGetAttackLuck(certainShooter.get(), certainTarget.get(), true), 0);
	ASSERT_FALSE(certainBranch->fortuneStrikeIsCertain(certainShot));
	DamageCache certainCache;
	const auto certainPrediction = AttackPossibility::evaluate(
		certainShot, shooter->getPosition(), certainCache, certainBranch);
	ASSERT_EQ(certainPrediction.fortuneStrikes.size(), 2u);
	EXPECT_EQ(certainPrediction.effectPreview, nullptr);
	EXPECT_FALSE(certainRoot->getSylvanLuckState(BattleSide::ATTACKER).gambler);
	EXPECT_FALSE(certainRoot->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_FALSE(certainBranch->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackUsedThisRound);
	EXPECT_TRUE(certainBranch->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.empty());
	EXPECT_EQ(gamblerPenalty(shooter), nullptr);
}
