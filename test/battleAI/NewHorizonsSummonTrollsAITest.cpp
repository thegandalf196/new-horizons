/*
 * NewHorizonsSummonTrollsAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include <set>

namespace
{
constexpr auto summonTrollsKey = "new-horizons:summonTrolls";

SpellID summonTrollsSpell()
{
	return SpellID(SpellID::decode(std::string(summonTrollsKey)));
}

class SummonTrollsEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SummonTrollsEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class SummonTrollsCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	SummonTrollsCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::vector<const battle::Unit *> natureTrollSummons(const CBattleInfoCallback & battle)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->unitType() || unit->unitType()->getJsonKey() != "core:troll"
			|| !unit->isSummoned())
			continue;

		const auto unitState = unit->acquireState();
		if(unitState && unitState->natureSummoned)
			result.push_back(unit);
	}
	return result;
}
}

class NewHorizonsSummonTrollsAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<SummonTrollsEnvironment> environment;
	std::shared_ptr<SummonTrollsCallback> callback;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare(const int spellPower = 100, const bool withElixirOfLife = false)
	{
		useCommands = true;
		startGame();

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = summonTrollsSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		if(withElixirOfLife)
			giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:elixirOfLife")), ArtifactPosition::MISC1);
		const auto natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
		ASSERT_GE(natureMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::NONE,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		enemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:peasant"), BattleHex(14, 5), 1);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);

		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -active->getMovementRange();
		active->addNewBonus(std::make_shared<Bonus>(immobilized));

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<SummonTrollsCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<SummonTrollsEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsSummonTrollsAITest, SelectsLegalHexProjectsExactTrollPoolAndSubmitsThatDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = summonTrollsSpell();
	const auto * summonTrolls = spell.toSpell();
	ASSERT_NE(summonTrolls, nullptr);
	ASSERT_TRUE(useCommands);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(summonTrolls), 10);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto unitCountBefore = battle()->battleGetAllUnits(false).size();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	EXPECT_TRUE(natureTrollSummons(*battle()).empty());
	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, summonTrolls);
	const auto targetMechanics = summonTrolls->battleMechanics(&targetPreview);
	ASSERT_EQ(targetMechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::LOCATION}));
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_EQ(viableTargets.size(), 12u)
		<< "Only a deterministic, board-wide shortlist should enter full AI projection";

	size_t legalLocationCount = 0;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable())
			continue;
		const spells::Target candidate{spells::Destination(hex)};
		if(targetMechanics->canBeCastAt(candidate))
			++legalLocationCount;
	}
	ASSERT_GT(legalLocationCount, viableTargets.size())
		<< "The test battle should exercise the bounded shortlist rather than a tiny board";

	std::set<int> legalLocations;
	for(const auto & target : viableTargets)
	{
		ASSERT_EQ(target.size(), 1u);
		ASSERT_EQ(target.front().unitValue, nullptr);
		EXPECT_TRUE(target.front().hexValue.isAvailable());
		EXPECT_TRUE(targetMechanics->canBeCastAt(target));
		EXPECT_TRUE(legalLocations.insert(target.front().hexValue.toInt()).second)
			<< "Each legal empty placement should be enumerated once";
	}

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, spell);
	const auto selectedTarget = action.getTarget(battle());
	ASSERT_EQ(selectedTarget.size(), 1u);
	ASSERT_EQ(selectedTarget.front().unitValue, nullptr);
	const auto selectedHex = selectedTarget.front().hexValue;
	ASSERT_TRUE(std::any_of(viableTargets.begin(), viableTargets.end(), [&](const spells::Target & target)
	{
		return target.front().hexValue == selectedHex;
	})) << "The submitted destination must be one of the shared-legality candidates";

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, summonTrolls);
	const auto projectedMechanics = summonTrolls->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->canBeCastAt(selectedTarget));
	projectedMechanics->castEval(projected.getServerCallback(), selectedTarget);
	const auto projectedTrolls = natureTrollSummons(projected);
	ASSERT_EQ(projectedTrolls.size(), 1u);
	const auto projectedTroll = projectedTrolls.front();
	const int64_t expectedPool = 100
		+ 5LL * attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) / 2;
	const auto trollHitPoints = static_cast<int64_t>(projectedTroll->getMaxHealth());
	ASSERT_GT(trollHitPoints, 0);
	const auto expectedCount = (expectedPool + trollHitPoints - 1) / trollHitPoints;
	EXPECT_EQ(projectedTroll->getCount(), expectedCount);
	EXPECT_EQ(projectedTroll->getAvailableHealth(), expectedPool)
		<< "The final Troll must be wounded so the summoned stack has exactly the previewed pool";
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore)
		<< "Projected Trolls must stay in the detached battle";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "AI preview must not spend Mana before command validation";
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto resolvedTrolls = natureTrollSummons(*battle());
	ASSERT_EQ(resolvedTrolls.size(), 1u);
	EXPECT_EQ(resolvedTrolls.front()->getCount(), expectedCount);
	EXPECT_EQ(resolvedTrolls.front()->getAvailableHealth(), expectedPool)
		<< "The AI projection and the authoritative cast must resolve the same HP pool";
	EXPECT_EQ(resolvedTrolls.front()->getPosition(), selectedHex);
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
}

TEST_F(NewHorizonsSummonTrollsAITest, ElixirHealthInheritanceMatchesProjectedAndResolvedTrollCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare(57, true));
	const auto spell = summonTrollsSpell();
	const auto * summonTrolls = spell.toSpell();
	ASSERT_NE(summonTrolls, nullptr);
	ASSERT_TRUE(useCommands);
	EXPECT_EQ(attackerSideHero->getEffectPowerDivisor(summonTrolls), 10);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 57);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, spell);
	const auto selectedTarget = action.getTarget(battle());
	ASSERT_EQ(selectedTarget.size(), 1u);
	ASSERT_EQ(selectedTarget.front().unitValue, nullptr);
	ASSERT_TRUE(selectedTarget.front().hexValue.isAvailable());

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, summonTrolls);
	const auto projectedMechanics = summonTrolls->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->canBeCastAt(selectedTarget));
	projectedMechanics->castEval(projected.getServerCallback(), selectedTarget);
	const auto projectedTrolls = natureTrollSummons(projected);
	ASSERT_EQ(projectedTrolls.size(), 1u);
	EXPECT_EQ(projectedTrolls.front()->getMaxHealth(), 54)
		<< "Elixir of Life must flow through the summoned Troll's hypothetical army provenance";
	EXPECT_EQ(projectedTrolls.front()->getCount(), 5);
	EXPECT_EQ(projectedTrolls.front()->getAvailableHealth(), 242)
		<< "The forecast must retain the wounded final Troll and exact aggregate pool";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto resolvedTrolls = natureTrollSummons(*battle());
	ASSERT_EQ(resolvedTrolls.size(), 1u);
	EXPECT_EQ(resolvedTrolls.front()->getMaxHealth(), 54);
	EXPECT_EQ(resolvedTrolls.front()->getCount(), 5);
	EXPECT_EQ(resolvedTrolls.front()->getAvailableHealth(), 242)
		<< "Submitted AI action and authoritative Elixir-adjusted Troll HP must agree";
	EXPECT_EQ(resolvedTrolls.front()->getPosition(), selectedTarget.front().hexValue);
}
