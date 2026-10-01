/*
 * NewHorizonsSecondChanceTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto secondChanceId = "new-horizons:luck.secondChance";

JsonNode certainLuckChance()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

class SecondChanceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SecondChanceEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSecondChanceTest : public BattleTestFixture
{
	JsonNode oldBadLuck;

protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		oldBadLuck = LIBRARY->settingsHandler->getValue(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		LIBRARY->settingsHandler->addOverride(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuckChance());
		startGame();
	}

	void TearDown() override
	{
		LIBRARY->settingsHandler->addOverride(EGameSettings::COMBAT_BAD_LUCK_CHANCE, oldBadLuck);
		BattleTestFixture::TearDown();
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuckChance());
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuckChance());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	void selectSecondChance(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({luckSkillId, secondChanceId});
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, secondChanceId));
	}

	static void luck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
			BonusSource::OTHER, value, BonusSourceID()));
	}

	BattleAttack * attackBy(uint32_t unitId)
	{
		const auto found = std::find_if(server.attacks.begin(), server.attacks.end(), [unitId](const auto & hit)
		{
			return hit.stackAttacking == unitId;
		});
		return found == server.attacks.end() ? nullptr : &*found;
	}

	size_t secondChanceLogCount() const
	{
		return static_cast<size_t>(std::count_if(server.battleLogLines.begin(), server.battleLogLines.end(), [](const auto & line)
		{
			return line.find("Second Chance suppresses a negative Luck trigger") != std::string::npos;
		}));
	}
};
}

TEST_F(NewHorizonsSecondChanceTest, SuppressesOnlyFirstBadStrikePerSideAndNeverResetsAtRoundBoundary)
{
	selectSecondChance(attackerSideHero);
	selectSecondChance(defenderSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	blockRetaliation(defender);
	luck(attacker, -20);
	forceMaximumDamage(attacker);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::DEFENDER).secondChance);
	ASSERT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), -10);

	const auto normalRange = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	auto * first = attackBy(attacker->unitId());
	ASSERT_NE(first, nullptr);
	ASSERT_FALSE(first->bsa.empty());
	EXPECT_FALSE(first->unlucky());
	EXPECT_EQ(first->bsa.front().damageAmount, normalRange.max);
	ASSERT_TRUE(first->fortuneState);
	EXPECT_TRUE(first->fortuneState->secondChanceUsed);
	EXPECT_EQ(secondChanceLogCount(), 1u);
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).secondChanceUsed)
		<< "The other army's independent first negative-Luck allowance must remain available";

	server.attacks.clear();
	auto badLuck = BattleAttackInfo(attacker, defender, 0, false);
	badLuck.unluckyStrike = true;
	const auto unluckyRange = battle()->calculateDmgRange(badLuck).damage;
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	auto * second = attackBy(attacker->unitId());
	ASSERT_NE(second, nullptr);
	ASSERT_FALSE(second->bsa.empty());
	EXPECT_TRUE(second->unlucky());
	EXPECT_EQ(second->bsa.front().damageAmount, unluckyRange.max);
	EXPECT_EQ(secondChanceLogCount(), 1u) << "The message is emitted only for the suppressed first trigger";

	battle()->nextRound();
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	auto * nextRound = attackBy(attacker->unitId());
	ASSERT_NE(nextRound, nullptr);
	EXPECT_TRUE(nextRound->unlucky());
	EXPECT_TRUE(nextRound->fortuneState->secondChanceUsed);

	// The defender's first negative physical strike is still independently suppressed.
	luck(defender, -20);
	server.attacks.clear();
	ASSERT_TRUE(attack(defender, attacker->getPosition()));
	auto * otherSideFirst = attackBy(defender->unitId());
	ASSERT_NE(otherSideFirst, nullptr);
	EXPECT_FALSE(otherSideFirst->unlucky());
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::DEFENDER).secondChanceUsed);
}

TEST(SylvanLuckRulesTest, SecondChanceAndStrikePacketRoundTripWithLegacyDefaultsAndValidation)
{
	SylvanLuckState spent;
	spent.secondChance = true;
	spent.secondChanceUsed = true;
	CMemorySerializer stateWire;
	stateWire.oser & spent;
	SylvanLuckState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, spent);

	BattleAttack packet;
	packet.battleID = BattleID(0);
	packet.fortuneSide = BattleSide::ATTACKER;
	packet.fortuneState = spent;
	CMemorySerializer packetWire;
	packetWire.oser & packet;
	BattleAttack restoredPacket;
	packetWire.iser & restoredPacket;
	EXPECT_EQ(restoredPacket.fortuneSide, BattleSide::ATTACKER);
	ASSERT_TRUE(restoredPacket.fortuneState);
	EXPECT_EQ(*restoredPacket.fortuneState, spent);

	CMemorySerializer rejectedDowngrade;
	rejectedDowngrade.oser.version = ESerializationVersion::NEW_HORIZONS_PERFECT_MOMENT;
	EXPECT_THROW(rejectedDowngrade.oser & spent, std::runtime_error);
	EXPECT_TRUE(rejectedDowngrade.extractBuffer().empty());

	SylvanLuckState legacyValue;
	legacyValue.serendipity = true;
	CMemorySerializer legacyWire;
	legacyWire.oser.version = legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_PERFECT_MOMENT;
	legacyWire.oser & legacyValue;
	SylvanLuckState restoredLegacy;
	restoredLegacy.secondChance = true;
	restoredLegacy.secondChanceUsed = true;
	legacyWire.iser & restoredLegacy;
	EXPECT_TRUE(restoredLegacy.serendipity);
	EXPECT_FALSE(restoredLegacy.secondChance);
	EXPECT_FALSE(restoredLegacy.secondChanceUsed);

	SylvanLuckState invalid;
	invalid.secondChanceUsed = true;
	CMemorySerializer invalidWire;
	invalidWire.oser & invalid;
	SylvanLuckState decodedInvalid;
	EXPECT_THROW(invalidWire.iser & decodedInvalid, std::runtime_error);
}

TEST_F(NewHorizonsSecondChanceTest, DetachedDoubleShotProjectionConsumesOnlyTheSelectedBranch)
{
	selectSecondChance(attackerSideHero);
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(shooter, -20);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);

	auto environment = std::make_shared<SecondChanceEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto projectedShooter = branch->getForUpdate(shooter->unitId());
	auto projectedTarget = branch->getForUpdate(target->unitId());
	BattleAttackInfo attack(projectedShooter.get(), projectedTarget.get(), 0, true);
	ASSERT_TRUE(branch->fortuneStrikeIsCertain(attack));

	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, branch);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 2u) << "Marksman contributes its two physical shots";
	ASSERT_FALSE(prediction.fortuneStrikes[0].hits.empty());
	ASSERT_FALSE(prediction.fortuneStrikes[1].hits.empty());

	const auto averageDamage = [](const DamageRange & range)
	{
		return range.min + (range.max - range.min) / 2;
	};
	const auto expectedNormal = averageDamage(branch->calculateDmgRange(attack).damage);
	auto unluckyAttack = attack;
	unluckyAttack.unluckyStrike = true;
	const auto expectedUnlucky = averageDamage(branch->calculateDmgRange(unluckyAttack).damage);
	EXPECT_EQ(prediction.fortuneStrikes[0].hits.front().second, expectedNormal)
		<< "The first certain negative trigger should consume Second Chance as a normal hit";
	EXPECT_EQ(prediction.fortuneStrikes[1].hits.front().second, expectedUnlucky)
		<< "The second shot should retain its negative-Luck damage";

	BattleExchangeVariant committed;
	auto selected = std::make_shared<HypotheticBattle>(environment.get(), parent);
	committed.trackAttack(prediction, selected, cache);
	EXPECT_TRUE(selected->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_FALSE(parent->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
}

TEST_F(NewHorizonsSecondChanceTest, DetachedRetaliationUsesDefenderSideSecondChanceWithoutMutatingLiveState)
{
	selectSecondChance(attackerSideHero);
	selectSecondChance(defenderSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	luck(attacker, -20);
	luck(defender, -20);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::DEFENDER).secondChance);

	auto environment = std::make_shared<SecondChanceEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto projectedAttacker = branch->getForUpdate(attacker->unitId());
	auto projectedDefender = branch->getForUpdate(defender->unitId());
	BattleAttackInfo melee(projectedAttacker.get(), projectedDefender.get(), 0, false);
	ASSERT_TRUE(branch->fortuneStrikeIsCertain(melee));
	ASSERT_TRUE(branch->fortuneStrikeIsCertain(BattleAttackInfo(projectedDefender.get(), projectedAttacker.get(), 0, false)));

	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(melee, attacker->getPosition(), cache, branch);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 2u)
		<< "The expected exchange contains the declared melee hit and its retaliation";
	EXPECT_FALSE(prediction.fortuneStrikes.front().retaliation);
	EXPECT_TRUE(prediction.fortuneStrikes.back().retaliation);
	ASSERT_FALSE(prediction.fortuneStrikes.front().hits.empty());
	ASSERT_FALSE(prediction.fortuneStrikes.back().hits.empty());
	ASSERT_EQ(prediction.fortuneStrikes.front().hits.front().first, defender->unitId());
	ASSERT_EQ(prediction.fortuneStrikes.back().hits.front().first, attacker->unitId());

	const auto averageDamage = [](const DamageRange & range)
	{
		return range.min + (range.max - range.min) / 2;
	};
	EXPECT_EQ(prediction.fortuneStrikes.front().hits.front().second,
		averageDamage(branch->calculateDmgRange(melee).damage));
	auto expectedAfterHit = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto expectedRetaliator = expectedAfterHit->getForUpdate(defender->unitId());
	auto expectedRetaliationTarget = expectedAfterHit->getForUpdate(attacker->unitId());
	auto primaryDamage = prediction.fortuneStrikes.front().hits.front().second;
	ASSERT_EQ(prediction.fortuneStrikes.front().damageProvenance, battle::DamageProvenance::PHYSICAL_CREATURE);
	expectedRetaliator->damage(primaryDamage, false, prediction.fortuneStrikes.front().damageProvenance);
	BattleAttackInfo retaliation(expectedRetaliator.get(), expectedRetaliationTarget.get(), 0, false);
	retaliation.retaliation = true;
	EXPECT_EQ(prediction.fortuneStrikes.back().hits.front().second,
		averageDamage(expectedAfterHit->calculateDmgRange(retaliation).damage))
		<< "The defender's first certain negative retaliation is suppressed by its own token";

	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_TRUE(prediction.effectPreview->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_TRUE(prediction.effectPreview->getSylvanLuckState(BattleSide::DEFENDER).secondChanceUsed);
	EXPECT_FALSE(parent->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_FALSE(parent->getSylvanLuckState(BattleSide::DEFENDER).secondChanceUsed);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::DEFENDER).secondChanceUsed);
}

TEST_F(NewHorizonsSecondChanceTest, ForcedPositivePerfectMomentDoesNotSpendNegativeLuckAllowance)
{
	selectSecondChance(attackerSideHero);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	luck(attacker, -20);
	auto environment = std::make_shared<SecondChanceEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle branch(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.perfectMoment = true;
	fortune.secondChance = true;
	branch.setSylvanLuckState(BattleSide::ATTACKER, fortune);

	const auto consumedMoment = [&branch]
	{
		auto current = branch.getSylvanLuckState(BattleSide::ATTACKER);
		EXPECT_TRUE(current.consumePerfectMoment());
		branch.setSylvanLuckState(BattleSide::ATTACKER, current);
	};
	consumedMoment();
	auto projectedAttacker = branch.getForUpdate(attacker->unitId());
	auto projectedDefender = branch.getForUpdate(defender->unitId());
	BattleAttackInfo guaranteed(projectedAttacker.get(), projectedDefender.get(), 0, false);
	guaranteed.luckyStrike = true;
	ASSERT_LT(branch.battleGetAttackLuck(projectedAttacker.get(), projectedDefender.get(), false), 0);
	ASSERT_TRUE(branch.fortuneStrikeIsCertain(guaranteed));
	branch.projectFortuneStrike(guaranteed, {{defender->unitId(), 100}}, projectedAttacker.get(), false);
	EXPECT_TRUE(branch.getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
	EXPECT_FALSE(branch.getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed)
		<< "The guaranteed positive trigger cannot also count as negative Luck";
}

TEST_F(NewHorizonsSecondChanceTest, IneligibleBallistaAndSpellLikeShotsDoNotSpendTheAllowance)
{
	selectSecondChance(attackerSideHero);
	startBattle();
	auto * ballista = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(leftHex), 1);
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(leftHex - 2), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(ballista, -20);
	luck(magog, -20);
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(magog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	EXPECT_FALSE(newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(ballista, true));
	EXPECT_FALSE(newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(magog, false));
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);

	const auto submitShot = [this, target](CStack * shooter)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	};

	ASSERT_TRUE(submitShot(ballista));
	auto * ballistaAttack = attackBy(ballista->unitId());
	ASSERT_NE(ballistaAttack, nullptr);
	EXPECT_TRUE(ballistaAttack->unlucky());
	ASSERT_TRUE(ballistaAttack->fortuneState);
	EXPECT_FALSE(ballistaAttack->fortuneState->secondChanceUsed);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);

	server.attacks.clear();
	ASSERT_TRUE(submitShot(magog));
	auto * spellLikeAttack = attackBy(magog->unitId());
	ASSERT_NE(spellLikeAttack, nullptr);
	EXPECT_TRUE(spellLikeAttack->spellLike());
	EXPECT_TRUE(spellLikeAttack->unlucky());
	ASSERT_TRUE(spellLikeAttack->fortuneState);
	EXPECT_FALSE(spellLikeAttack->fortuneState->secondChanceUsed);
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
}
