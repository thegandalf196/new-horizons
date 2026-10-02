/*
 * NewHorizonsDivineRetributionAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto divineRetributionKey = "new-horizons:divineRetribution";
constexpr int divineRetributionRawCap = 150;

SpellID divineRetributionSpell()
{
	return SpellID(SpellID::decode(divineRetributionKey));
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

class DivineRetributionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit DivineRetributionEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class DivineRetributionCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	DivineRetributionCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

enum class ThreatKind
{
	MELEE,
	PHYSICAL_SHOOTER,
	SPELL_LIKE_SHOOTER
};
}

class NewHorizonsDivineRetributionAITest : public HeroCommandFixture
{
protected:
	CStack * protectedStack = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<DivineRetributionEnvironment> environment;
	std::shared_ptr<DivineRetributionCallback> callback;
	bool useSavedV2Rules = false;
	bool learnRetributionist = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, useSavedV2Rules
			? savedV2MagicRulesWithCurrentSpellRoster()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		if(learnRetributionist)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare(ThreatKind threat, bool existingFullEffect = false, bool savedV2Rules = false,
		bool withRetributionist = false)
	{
		useSavedV2Rules = savedV2Rules;
		learnRetributionist = withRetributionist;
		useCommands = false;
		startGame();
		if(learnRetributionist)
		{
			const auto lightMagic = SecondarySkill::decode("new-horizons:lightMagic");
			ASSERT_GE(lightMagic, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:lightMagic",
				"new-horizons:lightMagic.healer"});
			attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:lightMagic",
				"new-horizons:lightMagic.retributionist"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic",
				"new-horizons:lightMagic.retributionist"));
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = divineRetributionSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 200);
		const char * enemyCreature = "core:pikeman";
		BattleHex enemyPosition(4, 5);
		int enemyCount = 100;
		switch(threat)
		{
		case ThreatKind::MELEE:
			enemyCreature = "core:pikeman";
			break;
		case ThreatKind::PHYSICAL_SHOOTER:
			enemyCreature = "core:titan";
			enemyPosition = BattleHex(12, 5);
			break;
		case ThreatKind::SPELL_LIKE_SHOOTER:
			enemyCreature = "core:magog";
			enemyPosition = BattleHex(12, 5);
			break;
		}
		enemy = addStack(BattleSide::DEFENDER, creatureByName(enemyCreature), enemyPosition, enemyCount);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(enemy, nullptr);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(true))
			if(unit != protectedStack && unit != enemy)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		if(threat == ThreatKind::MELEE)
		{
			EXPECT_FALSE(battle()->battleCanShoot(enemy, protectedStack->getPosition()));
			EXPECT_FALSE(battle()->meleeAttackHexes(enemy, protectedStack,
				enemy->getPosition()).empty());
		}
		else
		{
			ASSERT_TRUE(battle()->battleCanShoot(enemy, protectedStack->getPosition()));
			if(threat == ThreatKind::SPELL_LIKE_SHOOTER)
				ASSERT_TRUE(enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		}

		if(existingFullEffect)
		{
			JsonNode parameters;
			parameters["retributionistPercent"].Integer() = 100;
			auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS,
				BonusType::DIVINE_RETRIBUTION, BonusSource::SPELL_EFFECT,
				divineRetributionRawCap, BonusSourceID(spell));
			marker->turnsRemain = 2;
			marker->parameters = std::make_shared<BonusParameters>(parameters);
			protectedStack->addNewBonus(marker);
		}

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = protectedStack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<DivineRetributionCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<DivineRetributionEnvironment>(gameState());
	}

	bool attemptDivineRetribution()
	{
		BattleEvaluator evaluator(environment, callback, protectedStack, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(protectedStack);
		if(!evaluator.canCastSpell())
		{
			ADD_FAILURE() << "The test hero must have a legal hero action before evaluating Divine Retribution";
			return false;
		}
		return evaluator.attemptCastingSpell(protectedStack);
	}
};

TEST_F(NewHorizonsDivineRetributionAITest, ChoosesRetributionAgainstMeleeThreat)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::MELEE));
	EXPECT_TRUE(attemptDivineRetribution());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, divineRetributionSpell());
}

TEST_F(NewHorizonsDivineRetributionAITest,
	ChoosesRetributionAgainstPhysicalShotsWithOneFriendlyTargetWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER));
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_TRUE(attemptDivineRetribution());
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, divineRetributionSpell());
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, protectedStack->unitId());
	const auto submittedTarget = action.getTarget(battle());
	ASSERT_EQ(submittedTarget.size(), 1u);
	EXPECT_EQ(submittedTarget.front().unitValue, protectedStack);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(protectedStack->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(divineRetributionSpell())).And(Selector::type()(BonusType::DIVINE_RETRIBUTION))))
		<< "BattleAI must score the detached marker projection without applying it to live state";
}

TEST_F(NewHorizonsDivineRetributionAITest, DoesNotCastAgainstSpellLikeProjectileThreat)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::SPELL_LIKE_SHOOTER));
	EXPECT_FALSE(attemptDivineRetribution());
	EXPECT_TRUE(callback->submitted.empty());
}

TEST_F(NewHorizonsDivineRetributionAITest, DoesNotRefreshAnEqualFullProtectionEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER, true));
	EXPECT_FALSE(attemptDivineRetribution());
	EXPECT_TRUE(callback->submitted.empty());
	const auto marker = protectedStack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(divineRetributionSpell())).And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	ASSERT_EQ(marker->size(), 1u);
	EXPECT_EQ(marker->front()->val, divineRetributionRawCap);
	EXPECT_EQ(marker->front()->turnsRemain, 2);
}

TEST_F(NewHorizonsDivineRetributionAITest, ValuesTheProjectedRetributionistParameterAgainstAnExistingEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER, true, false, true));
	EXPECT_TRUE(attemptDivineRetribution());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().spell, divineRetributionSpell());
	const auto marker = protectedStack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(divineRetributionSpell())).And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	ASSERT_EQ(marker->size(), 1u);
	EXPECT_EQ(marker->front()->val, divineRetributionRawCap);
	EXPECT_EQ(marker->front()->turnsRemain, 2);
	ASSERT_NE(marker->front()->parameters, nullptr);
	EXPECT_EQ(marker->front()->parameters->toCustom<JsonNode>()["retributionistPercent"].Integer(), 100)
		<< "The AI's projected 120% parameter must not mutate the live 100% effect";

	const auto spell = divineRetributionSpell();
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	auto projectedTarget = projected->getForUpdate(protectedStack->unitId());
	spells::BattleCast projectedCast(projected.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&projectedCast);
	const spells::Target target{spells::Destination(projectedTarget.get())};
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(projected->getServerCallback(), target);

	const auto projectedMarker = projectedTarget->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(spell)).And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	ASSERT_EQ(projectedMarker->size(), 1u);
	EXPECT_GT(projectedMarker->front()->val, divineRetributionRawCap)
		<< "Advanced Light strengthens the projected Spell Power cap";
	EXPECT_EQ(projectedMarker->front()->turnsRemain, 2);
	ASSERT_NE(projectedMarker->front()->parameters, nullptr);
	EXPECT_EQ(projectedMarker->front()->parameters->toCustom<JsonNode>()
		["retributionistPercent"].Integer(), 120);
}

TEST_F(NewHorizonsDivineRetributionAITest, DoesNotLeakIntoSavedV2EvenWhenItsRosterContainsTheNewSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare(ThreatKind::PHYSICAL_SHOOTER, false, true));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(
		battle()->getMagicRules(), divineRetributionSpell()))
		<< "This fixture keeps the canonical spell in the saved roster to isolate the version gate";

	EXPECT_FALSE(attemptDivineRetribution());
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_FALSE(protectedStack->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(divineRetributionSpell())).And(Selector::type()(BonusType::DIVINE_RETRIBUTION))));
}
