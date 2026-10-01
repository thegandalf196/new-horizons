/*
 * NewHorizonsTwistRuntimeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../include/vcmi/ServerCallback.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto secondChanceId = "new-horizons:luck.secondChance";
constexpr auto chainOfFortuneId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFateId = "new-horizons:luck.twistOfFate";

bool setTwistOfFateStatus(JsonNode & rules, std::string_view status)
{
	auto & perks = rules["skills"][luckSkillId]["perks"].Vector();
	const auto twist = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == twistOfFateId;
	});
	if(twist == perks.end())
		return false;
	(*twist)["effect"]["status"].String() = std::string(status);
	return true;
}

JsonNode badLuckCurve(int chance)
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(chance);
	return result;
}

class TwistRuntimeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit TwistRuntimeEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsTwistRuntimeTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setTwistOfFateStatus(perkRules, "active"))
			throw std::runtime_error("Missing Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, badLuckCurve(35));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_MORALE_CHANCE, badLuckCurve(35));
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptLuckPerkThroughOffer(CGHeroInstance * hero, const char * perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
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

	void selectTwistOfFate(CGHeroInstance * hero, const char * basicPerk = fortuneFavorId)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, basicPerk);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, basicPerk));
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, chainOfFortuneId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, chainOfFortuneId));
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, twistOfFateId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFateId));
	}

	void startTwistBattle(bool bothSides = false)
	{
		startGame();
		selectTwistOfFate(attackerSideHero);
		if(bothSides)
			selectTwistOfFate(defenderSideHero);
		startBattle();
		ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).enabled);
		EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).enabled, bothSides);
	}

	static void luck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

TEST_F(NewHorizonsTwistRuntimeTest, HostileChainLightningLazilyRerollsPrimaryResistanceDuringAuthoritativeCast)
{
	startTwistBattle(true);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CHAIN_LIGHTNING);
	setTestSpellPointTotal(attackerSideHero, 1000);

	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 20);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(target->magicResistance(), 1);
	auto * secondary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 20);
	ASSERT_NE(secondary, nullptr);
	beginCombat();

	// The prepass draws once for every on-field unit, including zero-resistance
	// units. Find a reproducible stream where this target's cached draw misses
	// and the immediate lazy draw resists at 1%.
	const auto units = battle()->battleGetAllUnits(false);
	const auto targetInList = std::ranges::find(units, target->unitId(), [](const auto * unit)
	{
		return unit->unitId();
	});
	ASSERT_NE(targetInList, units.end());
	const size_t targetIndex = static_cast<size_t>(std::distance(units.begin(), targetInList));
	std::optional<int> seed;
	for(int candidateSeed = 1; candidateSeed < 100000; ++candidateSeed)
	{
		CRandomGenerator candidate(candidateSeed);
		int targetFirstDraw = -1;
		for(size_t index = 0; index < units.size(); ++index)
		{
			const int draw = candidate.nextInt(0, 99);
			if(index == targetIndex)
				targetFirstDraw = draw;
		}
		const int lazyDraw = candidate.nextInt(0, 99);
		if(targetFirstDraw >= 1 && lazyDraw == 0)
		{
			seed = candidateSeed;
			break;
		}
	}
	ASSERT_TRUE(seed.has_value()) << "Could not find the fixed first-fail / lazy-resist draw sequence";
	gameHandler->randomizer->setSeed(*seed);

	const auto healthBefore = target->getAvailableHealth();
	const auto secondaryHealthBefore = secondary->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::CHAIN_LIGHTNING, target));

	EXPECT_EQ(target->getAvailableHealth(), healthBefore)
		<< "The actual chain consumer must use the final resistance result, not route through on the cached miss";
	EXPECT_EQ(secondary->getAvailableHealth(), secondaryHealthBefore)
		<< "A primary that resists during preparation must stop the chain before a second hop is selected";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used)
		<< "A hostile, eligible recipient's stochastic resistance spends its controller's one use";
}

TEST_F(NewHorizonsTwistRuntimeTest, NegativeLuckForecastSquaresOnlyTheAvailableTwistChance)
{
	startTwistBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 20);
	luck(attacker, -10);
	ASSERT_LT(battle()->battleGetAttackLuck(attacker, defender, false), 0);

	const auto rules = battle()->getLuckRollRules();
	ASSERT_FALSE(rules.badChance.empty());
	ASSERT_GT(rules.diceSize, 0);
	const double chance = static_cast<double>(rules.badChance.front()) / rules.diceSize;
	ASSERT_GT(chance, 0.0);
	ASSERT_LT(chance, 1.0);
	const BattleAttackInfo ordinary(attacker, defender, 0, false);
	const auto average = [](const DamageRange & range)
	{
		return range.min + (range.max - range.min) / 2;
	};
	const int64_t normal = average(battle()->calculateDmgRange(ordinary).damage);
	auto unluckyAttack = ordinary;
	unluckyAttack.unluckyStrike = true;
	const int64_t unlucky = average(battle()->calculateDmgRange(unluckyAttack).damage);
	const auto forecast = [normal, unlucky](double adverseChance)
	{
		return static_cast<int64_t>(normal * (1.0 - adverseChance) + unlucky * adverseChance);
	};

	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), forecast(chance * chance));
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());

	int draws = 0;
	const bool finalBadLuck = gameHandler->battles->resolveAdverseCombatRoll(BattleID(0),
		BattleSide::ATTACKER, true, true, [&draws]
		{
			++draws;
			return draws == 1;
		});
	EXPECT_FALSE(finalBadLuck);
	EXPECT_EQ(draws, 2);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), forecast(chance));
}

TEST_F(NewHorizonsTwistRuntimeTest, HypotheticalAdverseRollSpendsOnlyItsBranchAllowance)
{
	startTwistBattle();
	TwistRuntimeEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto root = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, root);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, root);
	int draws = 0;

	const bool finalResult = branch->getServerCallback()->resolveAdverseCombatRoll(BattleID(0),
		BattleSide::ATTACKER, true, true, [&draws]
		{
			++draws;
			return draws == 1;
		});

	EXPECT_FALSE(finalResult);
	EXPECT_EQ(draws, 2);
	EXPECT_TRUE(branch->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(root->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(sibling->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
}

TEST_F(NewHorizonsTwistRuntimeTest, ActualNegativeLuckSpendsTheAttackingArmysAllowance)
{
	startTwistBattle(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10000);
	// This case deliberately excludes retaliation: only the initiating negative
	// Luck roll is under test, not a possible adverse result from the other army.
	blockRetaliation(attacker);
	luck(attacker, -20);
	ASSERT_LT(battle()->battleGetAttackLuck(attacker, defender, false), 0);
	ASSERT_TRUE(gameHandler->randomizer->isBadLuckRollStochastic(10));
	for(int attempt = 0; attempt < 32
		&& !battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used; ++attempt)
		ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
}

TEST_F(NewHorizonsTwistRuntimeTest, SecondChanceSuppressesTheFirstBadStrikeBeforeTwistCanSpend)
{
	startGame();
	selectTwistOfFate(attackerSideHero, secondChanceId);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10000);
	blockRetaliation(attacker);
	luck(attacker, -20);
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChance);
	for(int attempt = 0; attempt < 32
		&& !battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed; ++attempt)
	{
		ASSERT_TRUE(attack(attacker, defender->getPosition()));
		EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available())
			<< "A suppressed adverse result cannot consume Twist of Fate";
	}
	ASSERT_TRUE(battle()->getSylvanLuckState(BattleSide::ATTACKER).secondChanceUsed);
	for(int attempt = 0; attempt < 32
		&& !battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used; ++attempt)
		ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used)
		<< "A subsequent unsuppressed negative trigger is eligible";
}

TEST_F(NewHorizonsTwistRuntimeTest, ActualNegativeMoraleSpendsOnlyTheActivatingArmysAllowance)
{
	startTwistBattle(true);
	auto * afflicted = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(leftHex), 10);
	afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, -20, BonusSourceID()));
	ASSERT_LT(afflicted->moraleVal(), 0);
	ASSERT_TRUE(gameHandler->randomizer->isBadMoraleRollStochastic(10));
	beginCombat();
	for(int round = 0; round < 32
		&& !battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used; ++round)
		endRound();
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
}

TEST_F(NewHorizonsTwistRuntimeTest, HostileDeathBlowSpendsTheRecipientsAllowanceNotTheActors)
{
	startTwistBattle(true);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10000);
	blockRetaliation(attacker);
	luck(attacker, -battle()->battleGetAttackLuck(attacker, defender, false));
	ASSERT_EQ(battle()->battleGetAttackLuck(attacker, defender, false), 0);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DOUBLE_DAMAGE_CHANCE, BonusSource::OTHER, 35, BonusSourceID()));
	for(int attempt = 0; attempt < 32
		&& !battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used; ++attempt)
		ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
}
