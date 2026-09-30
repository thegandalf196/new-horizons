/*
 * NewHorizonsVerdantPrisonAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <map>
#include <set>

namespace
{
constexpr auto verdantPrisonKey = "new-horizons:verdantPrison";
constexpr int survivingEnemyPeasantCount = 20;

SpellID verdantPrisonSpell()
{
	return SpellID(SpellID::decode(std::string(verdantPrisonKey)));
}

class VerdantPrisonEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit VerdantPrisonEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class VerdantPrisonCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	VerdantPrisonCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::vector<const battle::Unit *> natureDendroidSummons(const CBattleInfoCallback & battle)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->unitType() || unit->unitType()->getJsonKey() != "core:dendroidGuard"
			|| !unit->isSummoned())
			continue;

		const auto unitState = unit->acquireState();
		if(unitState && unitState->natureSummoned)
			result.push_back(unit);
	}
	return result;
}

std::set<int> unitPositions(const std::vector<const battle::Unit *> & units)
{
	std::set<int> result;
	for(const auto * unit : units)
		result.insert(unit->getPosition().toInt());
	return result;
}

std::map<int, int64_t> unitHealthByPosition(const std::vector<const battle::Unit *> & units)
{
	std::map<int, int64_t> result;
	for(const auto * unit : units)
		result.emplace(unit->getPosition().toInt(), unit->getAvailableHealth());
	return result;
}
}

class NewHorizonsVerdantPrisonAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	CStack * openRingEnemy = nullptr;
	std::vector<CStack *> ringBlockers;
	std::shared_ptr<VerdantPrisonEnvironment> environment;
	std::shared_ptr<VerdantPrisonCallback> callback;

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

	void prepare(const int availableRingHexes = 4, const bool addOpenRingTarget = false)
	{
		useCommands = true;
		startGame();

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = verdantPrisonSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 101, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
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
			creatureByName("core:peasant"), BattleHex(14, 3), survivingEnemyPeasantCount);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);

		const auto ring = enemy->getSurroundingHexes().toVector();
		ASSERT_EQ(ring.size(), 6u);
		ASSERT_LE(availableRingHexes, ring.size());
		for(size_t index = availableRingHexes; index < ring.size(); ++index)
		{
			auto * blocker = addStack(BattleSide::ATTACKER,
				creatureByName("core:pikeman"), ring[index], 1);
			ASSERT_NE(blocker, nullptr);
			ringBlockers.push_back(blocker);
		}

		if(addOpenRingTarget)
		{
			openRingEnemy = addStack(BattleSide::DEFENDER,
				creatureByName("core:peasant"), BattleHex(14, 8), survivingEnemyPeasantCount);
			ASSERT_NE(openRingEnemy, nullptr);
		}

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

		callback = std::make_shared<VerdantPrisonCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<VerdantPrisonEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsVerdantPrisonAITest, ProjectsAndSubmitsExactSummonsForAnIncompleteRing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(enemy->getCount(), survivingEnemyPeasantCount)
		<< "Keep an enemy stack alive through the normal turn-queue forecast so the AI can exercise casting";
	const auto spell = verdantPrisonSpell();
	const auto * verdantPrison = spell.toSpell();
	ASSERT_NE(verdantPrison, nullptr);
	ASSERT_TRUE(useCommands);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(verdantPrison), 10);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto unitCountBefore = battle()->battleGetAllUnits(false).size();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	std::vector<int64_t> blockerHealthBefore;
	for(const auto * blocker : ringBlockers)
		blockerHealthBefore.push_back(blocker->getAvailableHealth());
	EXPECT_TRUE(natureDendroidSummons(*battle()).empty());

	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, verdantPrison);
	const auto targetMechanics = verdantPrison->battleMechanics(&targetPreview);
	ASSERT_EQ(targetMechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::CREATURE}));
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	ASSERT_EQ(viableTargets.front().size(), 1u);
	ASSERT_NE(viableTargets.front().front().unitValue, nullptr);
	EXPECT_EQ(viableTargets.front().front().unitValue->unitId(), enemy->unitId());

	const auto legalRing = targetMechanics->rangeInHexes(enemy->getPosition());
	ASSERT_EQ(legalRing.size(), 4u)
		<< "Shared mechanics must report the four empty neighboring hexes";
	std::set<int> legalHexes;
	for(const auto hex : legalRing)
		EXPECT_TRUE(legalHexes.insert(hex.toInt()).second);
	EXPECT_TRUE(targetMechanics->canBeCastAt(viableTargets.front()));

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	const auto * projectedEnemy = projected.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedEnemy, nullptr);
	const spells::Target projectedTarget{spells::Destination(projectedEnemy)};
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, verdantPrison);
	const auto projectedMechanics = verdantPrison->battleMechanics(&projectedCast);
	ASSERT_TRUE(projectedMechanics->canBeCastAt(projectedTarget));
	projectedMechanics->castEval(projected.getServerCallback(), projectedTarget);

	const auto projectedDendroids = natureDendroidSummons(projected);
	ASSERT_EQ(projectedDendroids.size(), legalHexes.size());
	EXPECT_EQ(unitPositions(projectedDendroids), legalHexes);
	const int64_t expectedPool = 180 + 3LL * attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	std::vector<int64_t> projectedStackHealth;
	int64_t projectedPool = 0;
	for(const auto * dendroid : projectedDendroids)
	{
		projectedStackHealth.push_back(dendroid->getAvailableHealth());
		projectedPool += dendroid->getAvailableHealth();
		EXPECT_EQ(dendroid->getCount(),
			(dendroid->getAvailableHealth() + dendroid->getMaxHealth() - 1) / dendroid->getMaxHealth());
	}
	std::ranges::sort(projectedStackHealth);
	EXPECT_EQ(projectedStackHealth, (std::vector<int64_t>{120, 121, 121, 121}));
	EXPECT_EQ(projectedPool, expectedPool)
		<< "The shared pool must be divided across the available ring with only its remainder distributed";

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
	ASSERT_NE(selectedTarget.front().unitValue, nullptr);
	EXPECT_EQ(selectedTarget.front().unitValue->unitId(), enemy->unitId());

	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore)
		<< "Projected Dendroids must remain in the detached battle";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "AI preview must not spend Mana before command validation";
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	for(size_t index = 0; index < ringBlockers.size(); ++index)
		EXPECT_EQ(ringBlockers[index]->getAvailableHealth(), blockerHealthBefore[index]);
	EXPECT_TRUE(natureDendroidSummons(*battle()).empty());

	const auto projectedHealthByPosition = unitHealthByPosition(projectedDendroids);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto resolvedDendroids = natureDendroidSummons(*battle());
	ASSERT_EQ(resolvedDendroids.size(), projectedDendroids.size());
	EXPECT_EQ(unitPositions(resolvedDendroids), legalHexes);
	EXPECT_EQ(unitHealthByPosition(resolvedDendroids), projectedHealthByPosition)
		<< "Detached AI forecast and authoritative ring creation must agree on position and HP";
	int64_t resolvedPool = 0;
	for(const auto * dendroid : resolvedDendroids)
		resolvedPool += dendroid->getAvailableHealth();
	EXPECT_EQ(resolvedPool, expectedPool);
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
}

TEST_F(NewHorizonsVerdantPrisonAITest, PrefersTheEnemyWithMoreLegalRingHexes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2, true));
	ASSERT_NE(openRingEnemy, nullptr);
	ASSERT_EQ(enemy->getCount(), survivingEnemyPeasantCount);
	ASSERT_EQ(openRingEnemy->getCount(), enemy->getCount())
		<< "Equal target values isolate the legal ring size preference";
	const auto spell = verdantPrisonSpell();
	const auto * verdantPrison = spell.toSpell();
	ASSERT_NE(verdantPrison, nullptr);

	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, verdantPrison);
	const auto targetMechanics = verdantPrison->battleMechanics(&targetPreview);
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(targetMechanics.get());
	ASSERT_EQ(viableTargets.size(), 2u);
	const auto constrainedRing = targetMechanics->rangeInHexes(enemy->getPosition());
	const auto openRing = targetMechanics->rangeInHexes(openRingEnemy->getPosition());
	EXPECT_EQ(constrainedRing.size(), 2u);
	EXPECT_EQ(openRing.size(), 6u);

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
	ASSERT_NE(selectedTarget.front().unitValue, nullptr);
	EXPECT_EQ(selectedTarget.front().unitValue->unitId(), openRingEnemy->unitId())
		<< "When summon HP value is equal, the AI should prefer the fuller, more restrictive ring";
	EXPECT_TRUE(natureDendroidSummons(*battle()).empty());
}

TEST_F(NewHorizonsVerdantPrisonAITest, DoesNotOfferAnEnemyWhoseRingHasNoLegalHexes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	const auto spell = verdantPrisonSpell();
	const auto * verdantPrison = spell.toSpell();
	ASSERT_NE(verdantPrison, nullptr);

	spells::BattleCast targetPreview(battle(), attackerSideHero, spells::Mode::HERO, verdantPrison);
	const auto targetMechanics = verdantPrison->battleMechanics(&targetPreview);
	EXPECT_TRUE(SpellTargetEvaluator::getViableTargets(targetMechanics.get()).empty());

	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	evaluator.attemptCastingSpell(active);
	EXPECT_TRUE(std::ranges::none_of(callback->submitted, [spell](const BattleAction & action)
	{
		return action.actionType == EActionType::HERO_SPELL && action.spell == spell;
	})) << "An empty ring excludes Verdant Prison, not otherwise legal Orders";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(natureDendroidSummons(*battle()).empty());
}
