/*
 * NewHorizonsShieldOfChaosAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto shieldOfChaosKey = "new-horizons:shieldOfChaos";

SpellID shieldOfChaosSpell()
{
	return SpellID(SpellID::decode(shieldOfChaosKey));
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
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
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

class ShieldOfChaosEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ShieldOfChaosEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ShieldOfChaosCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	ShieldOfChaosCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

template<typename T>
std::vector<std::byte> serializedState(T & object)
{
	CMemorySerializer serializer;
	serializer.oser & object;
	return serializer.extractBuffer();
}

Bonus permanentBonus(BonusType type, int value)
{
	return Bonus(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}

enum class ShieldScenario
{
	FRIENDLY_PROTECTION,
	ENEMY_OUTPUT,
	NO_THREAT
};
}

class NewHorizonsShieldOfChaosAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * friendlyTarget = nullptr;
	CStack * enemyTarget = nullptr;
	std::shared_ptr<ShieldOfChaosEnvironment> environment;
	std::shared_ptr<ShieldOfChaosCallback> callback;
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

	void prepare(ShieldScenario scenario, bool savedV2Rules = false)
	{
		useSavedV2Rules = savedV2Rules;
		useCommands = false;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = shieldOfChaosSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(true))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		const auto skeleton = creatureByName("core:skeleton");
		const auto titan = creatureByName("core:titan");
		if(scenario == ShieldScenario::FRIENDLY_PROTECTION)
		{
			active = addStack(BattleSide::ATTACKER, skeleton, BattleHex(3, 5), 1);
			friendlyTarget = addStack(BattleSide::ATTACKER, skeleton, BattleHex(3, 8), 1000);
			enemyTarget = addStack(BattleSide::DEFENDER, titan, BattleHex(12, 5), 100);
			ASSERT_TRUE(enemyTarget->canShoot());
			ASSERT_FALSE(enemyTarget->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
			ASSERT_TRUE(battle()->battleCanShoot(enemyTarget, friendlyTarget->getPosition()));
		}
		else if(scenario == ShieldScenario::ENEMY_OUTPUT)
		{
			active = addStack(BattleSide::ATTACKER, skeleton, BattleHex(3, 5), 1000);
			Bonus cappedPhysicalProtection = permanentBonus(
				BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 8000);
			active->addNewBonus(std::make_shared<Bonus>(cappedPhysicalProtection));
			enemyTarget = addStack(BattleSide::DEFENDER, titan, BattleHex(12, 5), 1);
			ASSERT_NE(enemyTarget, nullptr);
			enemyTarget->addNewBonus(std::make_shared<Bonus>(permanentBonus(BonusType::MORALE, 3)));
			enemyTarget->addNewBonus(std::make_shared<Bonus>(permanentBonus(BonusType::LUCK, 3)));
			ASSERT_TRUE(battle()->battleCanShoot(enemyTarget, active->getPosition()));
		}
		else
		{
			active = addStack(BattleSide::ATTACKER, skeleton, BattleHex(3, 5), 1);
			friendlyTarget = addStack(BattleSide::ATTACKER, skeleton, BattleHex(3, 8), 1);
			enemyTarget = addStack(BattleSide::DEFENDER, skeleton, BattleHex(12, 5), 1);
			EXPECT_FALSE(battle()->battleCanShoot(enemyTarget, active->getPosition()));
		}
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemyTarget, nullptr);

		if(scenario == ShieldScenario::FRIENDLY_PROTECTION)
		{
			ASSERT_NE(friendlyTarget, nullptr);
		}
		else if(scenario == ShieldScenario::NO_THREAT)
		{
			ASSERT_NE(friendlyTarget, nullptr);
		}

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<ShieldOfChaosCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ShieldOfChaosEnvironment>(gameState());
	}

	std::vector<spells::Target> viableTargets() const
	{
		spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO,
			shieldOfChaosSpell().toSpell());
		const auto mechanics = shieldOfChaosSpell().toSpell()->battleMechanics(&preview);
		return SpellTargetEvaluator::getViableTargets(mechanics.get());
	}

	bool targetIsViable(uint32_t unitId) const
	{
		const auto targets = viableTargets();
		return std::any_of(targets.begin(), targets.end(), [&](const auto & target)
		{
			return target.size() == 1 && target.front().unitValue
				&& target.front().unitValue->unitId() == unitId;
		});
	}

	bool attemptShield()
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		if(!evaluator.canCastSpell())
		{
			ADD_FAILURE() << "The test hero must have a legal Hero Action before evaluating Shield of Chaos";
			return false;
		}
		return evaluator.attemptCastingSpell(active);
	}

	void expectLiveStateUnchanged(int manaBefore, int64_t activeHealthBefore,
		int64_t friendlyHealthBefore, int64_t enemyHealthBefore)
	{
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
		if(friendlyTarget)
		{
			EXPECT_EQ(friendlyTarget->getAvailableHealth(), friendlyHealthBefore);
		}
		EXPECT_EQ(enemyTarget->getAvailableHealth(), enemyHealthBefore);
	}

	void expectNoLiveShield(const CStack * target) const
	{
		EXPECT_FALSE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(shieldOfChaosSpell()))));
	}

	void expectShieldReducesProjectedPhysicalThreat()
	{
		ASSERT_NE(enemyTarget, nullptr);
		ASSERT_NE(friendlyTarget, nullptr);
		ASSERT_FALSE(enemyTarget->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));

		const BattleAttackInfo incomingBefore(enemyTarget, friendlyTarget, 0, true);
		ASSERT_TRUE(incomingBefore.physicalDamage);
		const auto beforeRange = battle()->battleEstimateDamage(incomingBefore).damage;
		const auto beforeDamage = beforeRange.min + (beforeRange.max - beforeRange.min) / 2;
		ASSERT_GT(beforeDamage, 0);

		HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
		const auto * projectedFriendly = projected.battleGetUnitByID(friendlyTarget->unitId());
		const auto * projectedShooter = projected.battleGetUnitByID(enemyTarget->unitId());
		ASSERT_NE(projectedFriendly, nullptr);
		ASSERT_NE(projectedShooter, nullptr);
		spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO,
			shieldOfChaosSpell().toSpell());
		const auto mechanics = shieldOfChaosSpell().toSpell()->battleMechanics(&preview);
		ASSERT_NE(mechanics, nullptr);
		spells::Target target{spells::Destination(projectedFriendly)};
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(mechanics->canBeCastAt(target, problem));
		mechanics->castEval(projected.getServerCallback(), target);

		const auto * projectedFriendlyAfterCast = projected.battleGetUnitByID(friendlyTarget->unitId());
		ASSERT_NE(projectedFriendlyAfterCast, nullptr);
		const auto projectedPhysicalReduction = projectedFriendlyAfterCast->getAllBonuses(
			Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(shieldOfChaosSpell()))
				.And(Selector::typeSubtype(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID())));
		ASSERT_NE(projectedPhysicalReduction, nullptr);
		ASSERT_FALSE(projectedPhysicalReduction->empty())
			<< "The detached spell cast must attach its physical reduction to the projected target";

		const BattleAttackInfo incomingAfter(projectedShooter, projectedFriendlyAfterCast, 0, true);
		ASSERT_TRUE(incomingAfter.physicalDamage);
		const auto afterRange = projected.battleEstimateDamage(incomingAfter).damage;
		const auto afterDamage = afterRange.min + (afterRange.max - afterRange.min) / 2;
		const auto availableHealth = friendlyTarget->getAvailableHealth();
		EXPECT_LT(std::min(afterDamage, availableHealth), std::min(beforeDamage, availableHealth))
			<< "Shield must reduce the projected physical hit before the target's HP cap; raw before range ["
			<< beforeRange.min << ", " << beforeRange.max << "], raw after range ["
			<< afterRange.min << ", " << afterRange.max << "], available health " << availableHealth
			<< ", projected physical reduction " << projectedPhysicalReduction->front()->val;
	}
};

TEST_F(NewHorizonsShieldOfChaosAITest,
	NeutralSpellEnumeratesBothSidesAndChoosesFriendlyProtectionWithoutMutatingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ShieldScenario::FRIENDLY_PROTECTION));
	ASSERT_TRUE(targetIsViable(friendlyTarget->unitId()));
	ASSERT_TRUE(targetIsViable(enemyTarget->unitId()));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), shieldOfChaosSpell()));
	expectShieldReducesProjectedPhysicalThreat();

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto friendlyHealthBefore = friendlyTarget->getAvailableHealth();
	const auto enemyHealthBefore = enemyTarget->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());

	ASSERT_TRUE(attemptShield());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, shieldOfChaosSpell());
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, friendlyTarget->unitId());
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	expectLiveStateUnchanged(manaBefore, activeHealthBefore, friendlyHealthBefore, enemyHealthBefore);
	expectNoLiveShield(friendlyTarget);
	expectNoLiveShield(enemyTarget);

	const auto submittedTarget = action.getTarget(battle());
	ASSERT_EQ(submittedTarget.size(), 1u);
	EXPECT_EQ(submittedTarget.front().unitValue, friendlyTarget);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(friendlyTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(shieldOfChaosSpell()))));
	EXPECT_FALSE(enemyTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(shieldOfChaosSpell()))));
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(friendlyTarget->getAvailableHealth(), friendlyHealthBefore);
	EXPECT_EQ(enemyTarget->getAvailableHealth(), enemyHealthBefore);
}

TEST_F(NewHorizonsShieldOfChaosAITest,
	ChoosesEnemyWhenItsMoraleAndLuckLossOutweighItsProtection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ShieldScenario::ENEMY_OUTPUT));
	ASSERT_TRUE(targetIsViable(active->unitId()));
	ASSERT_TRUE(targetIsViable(enemyTarget->unitId()));
	ASSERT_EQ(enemyTarget->moraleVal(), 3);
	ASSERT_EQ(enemyTarget->valOfBonuses(BonusType::LUCK), 3);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto enemyHealthBefore = enemyTarget->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());

	ASSERT_TRUE(attemptShield());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, shieldOfChaosSpell());
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, enemyTarget->unitId());
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(enemyTarget->getAvailableHealth(), enemyHealthBefore);
	expectNoLiveShield(enemyTarget);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(enemyTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(shieldOfChaosSpell()))));
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(enemyTarget->getAvailableHealth(), enemyHealthBefore);
}

TEST_F(NewHorizonsShieldOfChaosAITest, DeclinesWhenNeitherSideHasProjectedThreatOrOutputValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ShieldScenario::NO_THREAT));
	ASSERT_TRUE(targetIsViable(friendlyTarget->unitId()));
	ASSERT_TRUE(targetIsViable(enemyTarget->unitId()));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto activeHealthBefore = active->getAvailableHealth();
	const auto friendlyHealthBefore = friendlyTarget->getAvailableHealth();
	const auto enemyHealthBefore = enemyTarget->getAvailableHealth();
	const auto serverRandomBefore = serializedState(*gameHandler->randomizer);
	const auto defaultRandomBefore = serializedState(CRandomGenerator::getDefault());

	EXPECT_FALSE(attemptShield());
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_EQ(serializedState(*gameHandler->randomizer), serverRandomBefore);
	EXPECT_EQ(serializedState(CRandomGenerator::getDefault()), defaultRandomBefore);
	expectLiveStateUnchanged(manaBefore, activeHealthBefore, friendlyHealthBefore, enemyHealthBefore);
	expectNoLiveShield(friendlyTarget);
	expectNoLiveShield(enemyTarget);
}

TEST_F(NewHorizonsShieldOfChaosAITest, DoesNotOfferTheSpellToSavedVersionTwoBattles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ShieldScenario::FRIENDLY_PROTECTION, true));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), shieldOfChaosSpell()))
		<< "The fixture retains the current roster entry so the saved-rules version gate is exercised";
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	expectNoLiveShield(friendlyTarget);
	EXPECT_FALSE(enemyTarget->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(shieldOfChaosSpell()))));
}
