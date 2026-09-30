/*
 * NewHorizonsEntangleAITest.cpp, part of VCMI engine
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
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto entangleKey = "new-horizons:entangle";

SpellID entangleSpell()
{
	return SpellID(SpellID::decode(entangleKey));
}

JsonNode savedV2MagicRulesWithCurrentSpellRoster()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
	}
	if(newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
		< newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		auto & sorrow = rules["spells"]["core:sorrow"];
		sorrow.Struct().erase("level");
		sorrow.Struct().erase("costs");
		sorrow["schools"].Vector().clear();
		sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class EntangleEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit EntangleEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class EntangleCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	EntangleCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::shared_ptr<Bonus> entangleBinding(const battle::Unit * unit, SpellID spell)
{
	if(!unit)
		return {};
	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(spell)).And(Selector::type()(BonusType::BIND_EFFECT)));
	if(!effects)
		return {};
	for(const auto & effect : *effects)
		if(effect && Bonus::NTurns(effect.get()) && effect->turnsRemain > 0)
			return effect;
	return {};
}

bool canMoveToMeleeAttack(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	if(!attacker || !defender || !battle.battleCanAttackUnit(attacker, defender))
		return false;
	const auto available = battle.battleGetAvailableHexes(attacker, false);
	for(const auto & defenderHex : defender->getHexes())
		for(int direction = 0; direction < 8; ++direction)
		{
			const auto attackDirection = static_cast<BattleHex::EDir>(direction);
			if(!battle.battleCanAttackHex(available, attacker, defenderHex, attackDirection))
				continue;
			const auto attackFrom = battle.fromWhichHexAttack(attacker, defenderHex, attackDirection);
			if(attackFrom.isValid() && attackFrom != attacker->getPosition())
				return true;
		}
	return false;
}
}

class NewHorizonsEntangleAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * threatenedAlly = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<EntangleEnvironment> environment;
	std::shared_ptr<EntangleCallback> callback;
	bool useSavedV2Rules = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, useSavedV2Rules
			? savedV2MagicRulesWithCurrentSpellRoster()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare(bool savedV2Rules = false)
	{
		useSavedV2Rules = savedV2Rules;
		useCommands = false;
		startGame();

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = entangleSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		threatenedAlly = addStack(BattleSide::ATTACKER,
			creatureByName("core:marksman"), BattleHex(3, 8), 40);
		// Hellhounds are ground melee attackers. This fixture places them far
		// enough away that Entangle must deny a legal move to create an attack.
		enemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:hellHound"), BattleHex(8, 5), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(threatenedAlly, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_TRUE(enemy->isMeleeAttacker());
		ASSERT_FALSE(enemy->isShooter());
		ASSERT_TRUE(enemy->willMove());
		ASSERT_TRUE(battle()->battleCanAttackUnit(enemy, threatenedAlly));
		ASSERT_TRUE(battle()->meleeAttackHexes(enemy, threatenedAlly,
			enemy->getPosition()).empty());
		ASSERT_TRUE(canMoveToMeleeAttack(*battle(), enemy, threatenedAlly));

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

		callback = std::make_shared<EntangleCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<EntangleEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsEntangleAITest, ChoosesProjectsAndSubmitsTheRootAgainstMovementThreat)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = entangleSpell();
	const auto * entangle = spell.toSpell();
	ASSERT_NE(entangle, nullptr);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto movementBefore = enemy->getMovementRange();
	const auto initiativeBefore = enemy->getInitiative();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto threatenedHealthBefore = threatenedAlly->getAvailableHealth();
	const auto enemyHealthBefore = enemy->getAvailableHealth();

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO, entangle);
	const auto mechanics = entangle->battleMechanics(&preview);
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	ASSERT_EQ(viableTargets.front().size(), 1u);
	ASSERT_NE(viableTargets.front().front().unitValue, nullptr);
	EXPECT_EQ(viableTargets.front().front().unitValue->unitId(), enemy->unitId());
	mechanics->castEval(projected.getServerCallback(), viableTargets.front());

	const auto * projectedEnemy = projected.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedEnemy, nullptr);
	EXPECT_EQ(projectedEnemy->getMovementRange(), 0u);
	EXPECT_EQ(projectedEnemy->getInitiative(), initiativeBefore);
	const auto projectedBinding = entangleBinding(projectedEnemy, spell);
	ASSERT_NE(projectedBinding, nullptr);
	EXPECT_EQ(projectedBinding->val, 0);
	EXPECT_EQ(projectedBinding->turnsRemain, 1);
	EXPECT_EQ(projectedBinding->parameters, nullptr)
		<< "Entangle's BIND_EFFECT marker carries no parameter payload";
	EXPECT_GT(enemy->getMovementRange(), 0u);
	EXPECT_EQ(enemy->getMovementRange(), movementBefore);
	EXPECT_EQ(enemy->getInitiative(), initiativeBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(threatenedAlly->getAvailableHealth(), threatenedHealthBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(entangleBinding(enemy, spell), nullptr)
		<< "AI projection must not mutate the live unit";

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, spell);
	const auto submittedTarget = action.getTarget(battle());
	ASSERT_EQ(submittedTarget.size(), 1u);
	ASSERT_NE(submittedTarget.front().unitValue, nullptr);
	EXPECT_EQ(submittedTarget.front().unitValue->unitId(), enemy->unitId());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getMovementRange(), movementBefore);
	EXPECT_EQ(enemy->getInitiative(), initiativeBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(threatenedAlly->getAvailableHealth(), threatenedHealthBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getMovementRange(), 0u);
	EXPECT_EQ(enemy->getInitiative(), initiativeBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(threatenedAlly->getAvailableHealth(), threatenedHealthBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	ASSERT_NE(entangleBinding(enemy, spell), nullptr);
}

TEST_F(NewHorizonsEntangleAITest, DoesNotOfferEntangleToSavedVersionTwoBattles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto spell = entangleSpell();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), spell))
		<< "The v2 fixture deliberately keeps the current roster entry so the version gate is tested";
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_EQ(entangleBinding(enemy, spell), nullptr);
}
