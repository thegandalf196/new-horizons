/*
 * NewHorizonsForgetfulnessAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view CHAOS_MAGIC_SKILL = "new-horizons:chaosMagic";
constexpr std::string_view MINDBREAKER_PERK = "new-horizons:chaosMagic.mindbreaker";

SpellID forgetfulnessSpell()
{
	return SpellID(SpellID::FORGETFULNESS);
}

class ForgetfulnessAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ForgetfulnessAIEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsForgetfulnessAITest : public HeroCommandFixture
{
protected:
	CStack * casterStack = nullptr;
	CStack * afflicted = nullptr;
	std::vector<CStack *> adjacentOpponents;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill chaosMagic() const
	{
		const int decoded = SecondarySkill::decode(std::string(CHAOS_MAGIC_SKILL));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void configureHeroes(const bool mindbreaker)
	{
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(forgetfulnessSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 160, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		attackerSideHero->setSecSkillLevel(chaosMagic(),
			mindbreaker ? MasteryLevel::ADVANCED : MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
	}

	void selectMindbreakerThroughLegalOffers()
	{
		const auto skill = chaosMagic();
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [this](const std::string & skillId)
		{
			return attackerSideHero->getPerkSkillRank(skillId);
		};
		const auto choose = [&](const std::string_view wantedPerk, const int requiredRank)
		{
			for(uint64_t seed = 0; seed < 4096; ++seed)
			{
				const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
				const auto selected = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
				{
					return offer.selection.skillId == CHAOS_MAGIC_SKILL
						&& (wantedPerk.empty() || offer.selection.perkId == wantedPerk)
						&& offer.requiredRank == requiredRank;
				});
				if(selected == offers.end())
					continue;
				gameHandler->levelUpHero(attackerSideHero, offers,
					static_cast<size_t>(std::distance(offers.begin(), selected)), seed, false);
				return true;
			}
			return false;
		};

		ASSERT_TRUE(choose({}, MasteryLevel::BASIC));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(choose(MINDBREAKER_PERK, MasteryLevel::ADVANCED));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(
			std::string(CHAOS_MAGIC_SKILL), std::string(MINDBREAKER_PERK)));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void prepare(const std::string & targetCreature, const bool mindbreaker = false,
		const bool surroundTarget = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		configureHeroes(mindbreaker);
		if(mindbreaker)
			selectMindbreakerThroughLegalOffers();
		ASSERT_NO_FATAL_FAILURE(startBattle());
		removeDeployedUnits();

		casterStack = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000);
		afflicted = addStack(BattleSide::DEFENDER, creatureByName(targetCreature), BattleHex(12, 5), 100);
		ASSERT_NE(casterStack, nullptr);
		ASSERT_NE(afflicted, nullptr);

		if(surroundTarget)
		{
			std::vector<BattleHex> emptyAdjacent;
			for(const auto hex : afflicted->getSurroundingHexes(afflicted->getPosition()))
				if(hex.isAvailable())
					emptyAdjacent.push_back(hex);
			ASSERT_GE(emptyAdjacent.size(), 3u);
			for(size_t index = 0; index < 3; ++index)
			{
				auto * enemy = addStack(BattleSide::ATTACKER,
					creatureByName("core:peasant"), emptyAdjacent[index], 100);
				ASSERT_NE(enemy, nullptr);
				adjacentOpponents.push_back(enemy);
			}
		}
		else if(targetCreature == "core:medusa")
		{
			auto * enemy = addStack(BattleSide::ATTACKER,
				creatureByName("core:peasant"), BattleHex(11, 5), 1000);
			ASSERT_NE(enemy, nullptr);
			adjacentOpponents.push_back(enemy);
		}
		else if(targetCreature == "core:ogreMage")
		{
			auto * ally = addStack(BattleSide::DEFENDER,
				creatureByName("core:ogre"), BattleHex(14, 5), 100);
			ASSERT_NE(ally, nullptr);
		}
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}

	bool issue(const battle::Unit * actor, const BattleAction & action)
	{
		return actor && gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(actor->unitSide()), action);
	}

	bool activate(const battle::Unit * wanted)
	{
		const auto maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t attempt = 0; attempt < maximumActions; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == wanted->unitId())
				return true;
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool castForgetfulness()
	{
		if(!activate(casterStack))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = forgetfulnessSpell();
		action.aimToUnit(afflicted);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::shared_ptr<ForgetfulnessAIEnvironment> environment() const
	{
		return std::make_shared<ForgetfulnessAIEnvironment>(gameState());
	}

	std::shared_ptr<CPlayerBattleCallback> callback(const PlayerColor player = PlayerColor(1)) const
	{
		return std::make_shared<CPlayerBattleCallback>(battle(), player);
	}

	std::shared_ptr<HypotheticBattle> projection(const ForgetfulnessAIEnvironment & env,
		const std::shared_ptr<CPlayerBattleCallback> & view) const
	{
		return std::make_shared<HypotheticBattle>(&env, view);
	}

	struct ActionSummary
	{
		int64_t bestValue = 0;
		size_t widestAttack = 0;
	};

	ActionSummary evaluateActions(const ForgetfulnessAIEnvironment & env,
		const std::shared_ptr<CPlayerBattleCallback> & view, const battle::Unit * source) const
	{
		const auto model = projection(env, view);
		const auto * projected = model->battleGetUnitByID(source->unitId());
		DamageCache damage;
		PotentialTargets targets(projected, damage, model);
		ActionSummary result;
		result.bestValue = targets.bestActionValue();
		for(const auto & attack : targets.possibleAttacks)
			result.widestAttack = std::max(result.widestAttack, attack.affectedUnits.size());
		return result;
	}
};
}

TEST_F(NewHorizonsForgetfulnessAITest,
	LiveSuppressionRemovesCreatureSpellForecastAndNestedProjectionDispelRestoresTheCaster)
{
	prepare("core:ogreMage");
	ASSERT_TRUE(afflicted->canCast());
	ASSERT_FALSE(afflicted->getBonusesOfType(BonusType::SPELLCASTER)->empty());
	ASSERT_TRUE(castForgetfulness());
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 1);
	EXPECT_FALSE(afflicted->canCast());
	EXPECT_TRUE(afflicted->getBonusesOfType(BonusType::SPELLCASTER)->empty());

	auto env = environment();
	auto view = callback();
	auto evaluatorCallback = std::make_shared<CBattleCallback>(PlayerColor(1), nullptr);
	evaluatorCallback->onBattleStarted(battle());
	BattleEvaluator suppressedAI(env, evaluatorCallback, afflicted, PlayerColor(1), BattleID(0),
		BattleSide::DEFENDER, 1.0f, 2);
	EXPECT_FALSE(suppressedAI.findBestCreatureSpell(afflicted).has_value())
		<< "BattleAI's creature-spell selector must see the live caster as unavailable";

	const auto markerSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(forgetfulnessSpell())).And(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION));
	const auto parent = projection(*env, view);
	const auto * parentMage = parent->battleGetUnitByID(afflicted->unitId());
	ASSERT_NE(parentMage, nullptr);
	EXPECT_FALSE(parentMage->canCast());
	EXPECT_TRUE(parentMage->getBonusesOfType(BonusType::SPELLCASTER)->empty());
	EXPECT_TRUE(parentMage->getAllBonuses(Selector::type()(BonusType::SPELLCASTER))->empty());
	const auto baselineCaster = parentMage->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::SPELLCASTER), {}, true);
	ASSERT_FALSE(baselineCaster->empty());

	const auto child = std::make_shared<HypotheticBattle>(env.get(), parent);
	child->getForUpdate(afflicted->unitId())->removeUnitBonus(markerSelector);
	const auto * restoredMage = child->battleGetUnitByID(afflicted->unitId());
	ASSERT_NE(restoredMage, nullptr);
	EXPECT_TRUE(restoredMage->canCast());
	EXPECT_FALSE(restoredMage->getAllBonuses(Selector::type()(BonusType::SPELLCASTER))->empty());
	EXPECT_FALSE(parentMage->canCast());
	EXPECT_FALSE(afflicted->canCast());
}

TEST_F(NewHorizonsForgetfulnessAITest,
	ActualForgetfulnessChangesBattleAIValueAndTargetsForHydraSpecialAttacks)
{
	prepare("core:hydra", false, true);
	auto env = environment();
	auto view = callback();
	const auto before = evaluateActions(*env, view, afflicted);
	ASSERT_GE(before.widestAttack, 2u)
		<< "The unfiltered AI projection should plan Hydra's native all-adjacent strike";
	ASSERT_GT(before.bestValue, 0);

	ASSERT_TRUE(castForgetfulness());
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 1);
	const auto after = evaluateActions(*env, view, afflicted);
	EXPECT_EQ(after.widestAttack, 1u)
		<< "After the real cast, hypothetical attacks must no longer include Hydra's adjacent targets";
	EXPECT_LT(after.bestValue, before.bestValue)
		<< "The AI should value the lost native attack mode, not merely a ranged-damage penalty";
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT))
		<< "Evaluating detached actions must preserve the live suppression";
	const auto native = afflicted->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::ATTACKS_ALL_ADJACENT), {}, true);
	ASSERT_NE(native, nullptr);
	EXPECT_FALSE(native->empty())
		<< "The underlying creature ability remains available for restoration";
}

TEST_F(NewHorizonsForgetfulnessAITest,
	MindbreakerChangesBattleAIValueForTheNativeNoMeleePenaltyPassive)
{
	prepare("core:medusa", true);
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	ASSERT_FALSE(battle()->battleCanShoot(afflicted, adjacentOpponents.front()->getPosition()));
	auto env = environment();
	auto view = callback();
	const auto before = evaluateActions(*env, view, afflicted);
	ASSERT_GT(before.bestValue, 0);
	ASSERT_TRUE(castForgetfulness());
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 2);
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	const auto after = evaluateActions(*env, view, afflicted);
	EXPECT_LT(after.bestValue, before.bestValue)
		<< "Mindbreaker must reduce actual melee valuation when it suppresses a passive offensive bonus";
	EXPECT_TRUE(afflicted->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::NO_MELEE_PENALTY), {}, true)->size() > 0);
}

TEST_F(NewHorizonsForgetfulnessAITest,
	MindbreakerInvalidatesWarmedRetaliationCachesWithoutMutatingParentOrLiveStack)
{
	prepare("core:griffin", true);
	Bonus unlimited(BonusDuration::PERMANENT, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(afflicted->creatureId()));
	afflicted->addNewBonus(std::make_shared<Bonus>(unlimited));
	ASSERT_EQ(afflicted->counterAttacks.total(), 2);
	ASSERT_FALSE(afflicted->counterAttacks.isLimited());
	ASSERT_TRUE(castForgetfulness());
	EXPECT_EQ(afflicted->counterAttacks.total(), 1)
		<< "Mindbreaker must not retain a warmed extra-retaliation allowance while active";
	EXPECT_TRUE(afflicted->counterAttacks.isLimited())
		<< "The native unlimited-retaliation passive is suppressed at Mindbreaker level";

	auto env = environment();
	auto view = callback();
	const auto parent = projection(*env, view);
	const auto parentUnit = parent->getForUpdate(afflicted->unitId());
	EXPECT_EQ(parentUnit->counterAttacks.total(), 1);
	EXPECT_TRUE(parentUnit->counterAttacks.isLimited());
	const auto child = std::make_shared<HypotheticBattle>(env.get(), parent);
	child->getForUpdate(afflicted->unitId())->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(forgetfulnessSpell())).And(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION)));
	const auto restored = child->getForUpdate(afflicted->unitId());
	EXPECT_EQ(restored->counterAttacks.total(), 2)
		<< "Removing the marker in a same-round branch restores the warmed ordinary retaliation cap";
	EXPECT_FALSE(restored->counterAttacks.isLimited());
	EXPECT_EQ(parentUnit->counterAttacks.total(), 1);
	EXPECT_TRUE(parentUnit->counterAttacks.isLimited());
	EXPECT_EQ(afflicted->counterAttacks.total(), 1);
	EXPECT_TRUE(afflicted->counterAttacks.isLimited());
}
