/*
 * NewHorizonsCrusadeAITest.cpp, part of VCMI engine
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
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto crusadeKey = "new-horizons:crusade";
constexpr auto lightMagicSkill = "new-horizons:lightMagic";

SpellID crusadeSpell()
{
	return SpellID(SpellID::decode(crusadeKey));
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
		spell.Struct().erase("earthquake");
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

class CrusadeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CrusadeEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class CrusadeCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	CrusadeCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::shared_ptr<Bonus> crusadeBonus(const battle::Unit * unit, SpellID spell,
	BonusType type, BonusSubtypeID subtype = BonusSubtypeID())
{
	if(!unit)
		return {};
	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(spell)).And(Selector::type()(type)));
	if(!effects)
		return {};
	for(const auto & effect : *effects)
		if(effect && effect->subtype == subtype && Bonus::NTurns(effect.get()) && effect->turnsRemain > 0)
			return effect;
	return {};
}

const std::array<std::pair<BonusType, BonusSubtypeID>, 5> & crusadeBonusTypes()
{
	static const std::array<std::pair<BonusType, BonusSubtypeID>, 5> result{{
		{BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)},
		{BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)},
		{BonusType::STACKS_INITIATIVE_FLAT, BonusSubtypeID()},
		{BonusType::MINIMUM_MORALE, BonusSubtypeID()},
		{BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID(SpellSchool::ANY)}}};
	return result;
}
}

class NewHorizonsCrusadeAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * marksmen = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<CrusadeEnvironment> environment;
	std::shared_ptr<CrusadeCallback> callback;
	bool useSavedV2Rules = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, useSavedV2Rules
			? savedV2MagicRulesWithCurrentSpellRoster()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
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

		const auto lightMagic = SecondarySkill::decode(lightMagicSkill);
		ASSERT_GE(lightMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.healer"});
		attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.purifier"});
		attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.crusader"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill,
			"new-horizons:lightMagic.crusader"));

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = crusadeSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		marksmen = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 8), 100);
		// Keep the enemy alive beyond tactical lookahead so BattleAI has reason to
		// value the next several rounds of Crusade rather than suppressing hero use.
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(marksmen, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_TRUE(battle()->battleCanShoot(marksmen, enemy->getPosition()));

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

		callback = std::make_shared<CrusadeCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CrusadeEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsCrusadeAITest, ChoosesProjectsAndSubmitsTheCrusaderMassSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = crusadeSpell();
	const auto * crusade = spell.toSpell();
	ASSERT_NE(crusade, nullptr);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO, crusade);
	const auto mechanics = crusade->battleMechanics(&preview);
	const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(viableTargets.size(), 1u);
	EXPECT_TRUE(viableTargets.front().empty())
		<< "Crusade is a whole-army no-location cast";
	mechanics->castEval(projected.getServerCallback(), viableTargets.front());

	const auto * projectedActive = projected.battleGetUnitByID(active->unitId());
	const auto * projectedMarksmen = projected.battleGetUnitByID(marksmen->unitId());
	ASSERT_NE(projectedActive, nullptr);
	ASSERT_NE(projectedMarksmen, nullptr);
	const battle::Unit * friendlyUnits[] = {projectedActive, projectedMarksmen};
	for(const auto * unit : friendlyUnits)
	{
		for(const auto & [type, subtype] : crusadeBonusTypes())
		{
			const auto bonus = crusadeBonus(unit, spell, type, subtype);
			ASSERT_NE(bonus, nullptr);
			EXPECT_EQ(bonus->turnsRemain, 4);
			if(type == BonusType::MINIMUM_MORALE)
				EXPECT_EQ(bonus->val, 0);
			else
				EXPECT_GT(bonus->val, 0);
		}
	}
	const auto * projectedEnemy = projected.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedEnemy, nullptr);
	EXPECT_FALSE(projectedEnemy->hasBonus(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(spell))))
		<< "The projection must not apply the friendly mass effect to the enemy";

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, spell);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_FALSE(action.target.front().hexValue.isValid())
		<< "The submitted spell uses the shared NO_LOCATION sentinel";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(marksmen->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(spell)))) << "AI projection and choice must leave live stacks untouched";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	const battle::Unit * liveUnits[] = {active, marksmen};
	for(size_t i = 0; i < std::size(liveUnits); ++i)
	{
		const auto * projectedUnit = i == 0 ? projectedActive : projectedMarksmen;
		for(const auto & [type, subtype] : crusadeBonusTypes())
		{
			const auto projectedBonus = crusadeBonus(projectedUnit, spell, type, subtype);
			const auto bonus = crusadeBonus(liveUnits[i], spell, type, subtype);
			ASSERT_NE(projectedBonus, nullptr);
			ASSERT_NE(bonus, nullptr);
			EXPECT_EQ(bonus->val, projectedBonus->val);
			EXPECT_EQ(bonus->turnsRemain, projectedBonus->turnsRemain);
		}
	}
}

TEST_F(NewHorizonsCrusadeAITest, DoesNotOfferCrusadeToSavedVersionTwoBattles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), crusadeSpell()))
		<< "The fixture retains the current roster entry so the ruleset-version gate is exercised";
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active));
	EXPECT_TRUE(callback->submitted.empty());
	EXPECT_FALSE(marksmen->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(crusadeSpell()))));
}
