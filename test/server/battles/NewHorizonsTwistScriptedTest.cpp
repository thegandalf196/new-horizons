/*
 * NewHorizonsTwistScriptedTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/combatScripts/ICombatEventScript.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";
constexpr auto chainOfFortuneId = "new-horizons:luck.chainOfFortune";
constexpr auto twistOfFateId = "new-horizons:luck.twistOfFate";

bool activateTwistOfFateInMapRules(JsonNode & rules)
{
	auto & perks = rules["skills"][luckSkillId]["perks"].Vector();
	const auto twist = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == twistOfFateId;
	});
	if(twist == perks.end())
		return false;
	(*twist)["effect"]["status"].String() = "active";
	return true;
}

struct BinomialSequence
{
	int seed;
	int first;
	int final;
	int cap;
};

std::optional<BinomialSequence> findPositiveDistinctBinomialRedraw(int trials, double probability, int cap)
{
	for(int seed = 1; seed < 100000; ++seed)
	{
		CRandomGenerator expected(seed);
		const int first = std::min(expected.nextBinomialInt(trials, probability), cap);
		const int final = std::min(expected.nextBinomialInt(trials, probability), cap);
		if(first > 0 && final > 0 && first != final)
			return BinomialSequence{seed, first, final, cap};
	}
	return std::nullopt;
}

std::optional<int> findAbilitySeedWith(bool firstResult, bool finalResult, int chance)
{
	for(int seed = 1; seed < 100000; ++seed)
	{
		CRandomGenerator global(seed);
		RandomGeneratorWithBias expected(global.nextInt());
		const bool first = expected.roll(chance, 100, 0);
		const bool final = expected.roll(chance, 100, 0);
		if(first == firstResult && final == finalResult)
			return seed;
	}
	return std::nullopt;
}

class NewHorizonsTwistScriptedTest : public BattleTestFixture
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
		if(!activateTwistOfFateInMapRules(perkRules))
			throw std::runtime_error("Missing Twist of Fate from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::COMBAT_ABILITY_BIAS, JsonNode(0));
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

	void selectTwistOfFate(CGHeroInstance * hero)
	{
		const int decodedLuck = SecondarySkill::decode(luckSkillId);
		ASSERT_GE(decodedLuck, 0);
		const auto luck = SecondarySkill(decodedLuck);

		hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, fortuneFavorId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, fortuneFavorId));
		hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, chainOfFortuneId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, chainOfFortuneId));
		hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptLuckPerkThroughOffer(hero, twistOfFateId);
		ASSERT_TRUE(hero->hasActivePerk(luckSkillId, twistOfFateId));
	}

	void startTwistBattle()
	{
		startGame();
		selectTwistOfFate(attackerSideHero);
		selectTwistOfFate(defenderSideHero);
		startBattle();
		ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).enabled);
		ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).enabled);
	}

	std::shared_ptr<Bonus> attachCombatScript(CStack * unit, std::string_view scriptName,
		int value, const JsonNode & scriptParameters)
	{
		const auto identifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "script", std::string(scriptName));
		EXPECT_TRUE(identifier.has_value()) << scriptName;
		if(!identifier)
			return {};

		const ScriptID id(*identifier);
		const auto & description = LIBRARY->scriptTypes()->getById(id);
		EXPECT_EQ(description.kind, ScriptKind::COMBAT_EVENT) << scriptName;
		EXPECT_NE(description.combatEventScript, nullptr) << scriptName;
		if(!description.combatEventScript)
			return {};

		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(id));
		bonus->parameters = std::make_shared<BonusParameters>(scriptParameters);
		unit->addNewBonus(bonus);
		return bonus;
	}

	void runCombatScript(const std::shared_ptr<Bonus> & bonus, CStack * actor, CStack * recipient)
	{
		ASSERT_NE(bonus, nullptr);
		const auto scriptId = bonus->subtype.as<ScriptID>();
		const auto & description = LIBRARY->scriptTypes()->getById(scriptId);
		ASSERT_NE(description.combatEventScript, nullptr);

		JsonNode parameters;
		if(bonus->parameters)
			parameters = bonus->parameters->toCustom<JsonNode>();
		parameters["val"].Integer() = bonus->val;

		CombatEventPayload payload;
		description.combatEventScript->run(gameHandler->spellcastEnvironment(), *battle(),
			CombatEventType::AFTER_ATTACK, actor, recipient, parameters, payload);
	}

	size_t twistFeedbackCount() const
	{
		return static_cast<size_t>(std::count_if(server.battleLogLines.begin(), server.battleLogLines.end(),
			[](const std::string & line)
			{
				return line.find("Twist of Fate") != std::string::npos;
			}));
	}
};
}

TEST_F(NewHorizonsTwistScriptedTest, DestructionRerollsAHostileSuccessForTheRecipientAndTheRedrawIsFinal)
{
	startTwistBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(recipient, nullptr);

	JsonNode parameters;
	parameters["killBy"].String() = "count";
	parameters["amount"].Integer() = 1;
	const auto script = attachCombatScript(actor, "destruction", 50, parameters);
	ASSERT_NE(script, nullptr);

	// Pin the actor's existing biased sequence to success then failure. The first
	// hostile result spends Twist; the final redraw therefore leaves the target intact.
	const auto seed = findAbilitySeedWith(true, false, 50);
	ASSERT_TRUE(seed.has_value());
	gameHandler->randomizer->setSeed(*seed);
	const auto recipientCountBefore = recipient->getCount();
	runCombatScript(script, actor, recipient);

	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	EXPECT_EQ(recipient->getCount(), recipientCountBefore)
		<< "The final failed redraw must suppress the initial successful destruction proc";
	EXPECT_EQ(twistFeedbackCount(), 1u);

	// A later scripted success in the same combat gets its ordinary single draw,
	// with no second Twist transition or feedback.
	runCombatScript(script, actor, recipient);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
	EXPECT_EQ(twistFeedbackCount(), 1u);
}

TEST_F(NewHorizonsTwistScriptedTest, DeathStareRerollsTheWholePositiveCappedBinomialCount)
{
	startTwistBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1000);
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100000);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(recipient, nullptr);

	constexpr int baseChance = 99;
	const int chanceBasisPoints = actor->favorableCreatureAbilityChanceBasisPoints(baseChance);
	ASSERT_GT(chanceBasisPoints, 0);
	ASSERT_LT(chanceBasisPoints, 10000);
	const double probability = static_cast<double>(chanceBasisPoints) / 10000.0;
	const int cap = static_cast<int>(std::ceil(actor->getCount() * probability));
	const auto sequence = findPositiveDistinctBinomialRedraw(actor->getCount(), probability, cap);
	ASSERT_TRUE(sequence.has_value()) << "Could not find a deterministic positive count and distinct final redraw";
	ASSERT_GT(sequence->first, 0);
	ASSERT_GT(sequence->final, 0);
	ASSERT_NE(sequence->first, sequence->final);

	JsonNode parameters;
	parameters["situation"].String() = "melee";
	const auto script = attachCombatScript(actor, "deathStare", baseChance, parameters);
	ASSERT_NE(script, nullptr);
	gameHandler->randomizer->setSeed(sequence->seed);
	runCombatScript(script, actor, recipient);

	const auto casts = server.castsOf(SpellID::DEATH_STARE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().killed, static_cast<uint32_t>(sequence->final))
		<< "The script must use the second capped binomial sample, not merely reroll whether the first was positive";
	EXPECT_LE(casts.front().killed, static_cast<uint32_t>(sequence->cap));
	EXPECT_GT(casts.front().killed, 0u);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	EXPECT_EQ(twistFeedbackCount(), 1u);
}

TEST_F(NewHorizonsTwistScriptedTest, ImmuneDeathStareTargetDoesNotSpendTheRecipientAllowance)
{
	startTwistBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1000);
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(rightHex), 100000);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(recipient, nullptr);

	constexpr int baseChance = 99;
	const double probability = static_cast<double>(actor->favorableCreatureAbilityChanceBasisPoints(baseChance)) / 10000.0;
	const int cap = static_cast<int>(std::ceil(actor->getCount() * probability));
	const auto sequence = findPositiveDistinctBinomialRedraw(actor->getCount(), probability, cap);
	ASSERT_TRUE(sequence.has_value());
	ASSERT_GT(sequence->first, 0);

	JsonNode parameters;
	parameters["situation"].String() = "melee";
	const auto script = attachCombatScript(actor, "deathStare", baseChance, parameters);
	ASSERT_NE(script, nullptr);
	gameHandler->randomizer->setSeed(sequence->seed);
	const auto recipientCountBefore = recipient->getCount();
	runCombatScript(script, actor, recipient);

	const auto casts = server.castsOf(SpellID::DEATH_STARE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().killed, 0u);
	EXPECT_EQ(recipient->getCount(), recipientCountBefore);
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available())
		<< "The immune recipient's positive raw binomial result bypasses Twist and remains immune";
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::ATTACKER).available());
	EXPECT_EQ(twistFeedbackCount(), 0u);
}
