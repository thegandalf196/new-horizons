/*
 * NewHorizonsHeroGrowthAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class GrowthEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GrowthEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class GrowthCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	GrowthCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsHeroGrowthAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		// Isolated native mechanics configuration, not canonical activation proof.
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}
};

TEST_F(NewHorizonsHeroGrowthAITest, RealEvaluatorExecutesScaledSpellAgainstLegalCommandAlternative)
{
	prepareCommands(true);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 990, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = attackerSideHero->manaLimit();
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_SCHOOL_SKILL,
		BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	ASSERT_TRUE(attackerSideHero->getPrimaryGrowthView());
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 990);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);
	ASSERT_EQ(attackerSideHero->manaLimit(), 1000);
	ASSERT_EQ(spell->calculateDamage(attackerSideHero), 7725);
	ASSERT_LT(spell->calculateDamage(attackerSideHero), enemy->getAvailableHealth());
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));

	auto callback = std::make_shared<GrowthCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<GrowthEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	ASSERT_EQ(callback->submitted.front().actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(callback->submitted.front().spell, SpellID::IMPLOSION);
	const auto mana = attackerSideHero->mana;
	const auto health = enemy->getAvailableHealth();
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_EQ(health - enemy->getAvailableHealth(), 7725);
	EXPECT_EQ(attackerSideHero->mana, mana - cost);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}
