/*
 * NewHorizonsHolyWrathAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto holyWrathKey = "new-horizons:holyWrath";

SpellID holyWrathSpell()
{
	return SpellID(SpellID::decode(holyWrathKey));
}

class HolyWrathEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HolyWrathEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class HolyWrathCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	HolyWrathCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsHolyWrathAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		for(auto & command : commandRules["commands"].Struct())
			for(auto & effect : command.second["effects"].Struct())
			{
				effect.second["base"].Float() = 0;
				effect.second["attack"].Float() = 0;
				effect.second["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare(CStack *& active, CStack *& ordinary, CStack *& undead)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands(true));

		const auto initialSpells = attackerSideHero->getSpellsInSpellbook();
		for(const auto known : initialSpells)
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = holyWrathSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 110, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		ordinary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 3), 20);
		undead = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 7), 20);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(ordinary, nullptr);
		ASSERT_NE(undead, nullptr);
		undead->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::UNDEAD, BonusSource::OTHER, 1, BonusSourceID()));

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != active && unit != ordinary && unit != undead)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		for(auto * stack : {active, ordinary, undead})
		{
			Bonus immobilized;
			immobilized.type = BonusType::STACKS_SPEED;
			immobilized.duration = BonusDuration::ONE_BATTLE;
			immobilized.val = -static_cast<int32_t>(stack->getMovementRange());
			stack->addNewBonus(std::make_shared<Bonus>(immobilized));
		}
	}
};

TEST_F(NewHorizonsHolyWrathAITest, PrefersUndeadTargetAndHypotheticalDamageMatchesAuthoritativeCast)
{
	CStack * active = nullptr;
	CStack * ordinary = nullptr;
	CStack * undead = nullptr;
	ASSERT_NO_FATAL_FAILURE(prepare(active, ordinary, undead));
	const auto * spell = holyWrathSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
	ASSERT_EQ(newHorizonsMagic::spellCost(battle()->getMagicRules(), holyWrathSpell(), MasteryLevel::NONE), 11);

	const auto ordinaryHealth = ordinary->getAvailableHealth();
	const auto undeadHealth = undead->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	auto environment = std::make_shared<HolyWrathEnvironment>(gameState());
	auto callback = std::make_shared<HolyWrathCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	EXPECT_EQ(action.spell, holyWrathSpell());
	const auto actionTarget = action.getTarget(battle());
	ASSERT_EQ(actionTarget.size(), 1u);
	ASSERT_NE(actionTarget.front().unitValue, nullptr);
	EXPECT_EQ(actionTarget.front().unitValue->unitId(), undead->unitId());

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	const auto * projectedUndead = projected.battleGetUnitByID(undead->unitId());
	ASSERT_NE(projectedUndead, nullptr);
	spells::Target projectedTarget;
	projectedTarget.emplace_back(projectedUndead);
	spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO, spell);
	spell->battleMechanics(&preview)->castEval(projected.getServerCallback(), projectedTarget);
	const auto projectedDamage = undeadHealth
		- projected.battleGetUnitByID(undead->unitId())->getAvailableHealth();
	EXPECT_EQ(projectedDamage, 93);
	EXPECT_EQ(ordinary->getAvailableHealth(), ordinaryHealth);
	EXPECT_EQ(undead->getAvailableHealth(), undeadHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(undeadHealth - undead->getAvailableHealth(), projectedDamage);
	EXPECT_EQ(ordinary->getAvailableHealth(), ordinaryHealth);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 11);
	ASSERT_EQ(server.castsOf(holyWrathSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(holyWrathSpell()).front().damage, projectedDamage);
}
