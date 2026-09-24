/*
 * WarcastingSpellComponentTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../SpellPointTestUtils.h"

#include "../../lib/spells/ISpellMechanics.h"
#include "../server/battles/HeroCommandFixture.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/AlternatingHeroActionState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"

TEST(WarcastingSpellComponent, ZeroBonusMatchesExistingIntegerDivision)
{
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(19, 10, 0), 1);
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(99, 10, 0), 9);
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(100, 10, 0), 10);
}

TEST(WarcastingSpellComponent, AppliesPercentageBeforeDivisorRounding)
{
	// A 10% increase to this low Spell Power component crosses the integer
	// boundary only when the percentage is applied before division.
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(19, 10, 0), 1);
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(19, 10, 10), 2);

	// The caller adds fixed spell power afterward; Warcasting only scales the
	// component that it explicitly passes to this helper.
	constexpr int64_t fixedBase = 25;
	EXPECT_EQ(fixedBase + spells::scaleWarcastingSpellPowerComponent(19, 10, 10), 27);
}

TEST(WarcastingSpellComponent, PreservesFractionalComponentUntilAfterStackMultiplication)
{
	// Sacrifice's existing expression multiplies the Spell Power fraction by
	// the victim count before floor. Its caller therefore passes the complete
	// stack numerator instead of rounding one creature's component first.
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(19 * 2, 10, 0), 3);
	EXPECT_EQ(spells::scaleWarcastingSpellPowerComponent(19 * 2, 10, 10), 4);
}

TEST(WarcastingSpellComponent, RejectsInvalidInputs)
{
	EXPECT_THROW(spells::scaleWarcastingSpellPowerComponent(-1, 1, 10), std::invalid_argument);
	EXPECT_THROW(spells::scaleWarcastingSpellPowerComponent(1, 0, 10), std::invalid_argument);
	EXPECT_THROW(spells::scaleWarcastingSpellPowerComponent(1, 1, -10), std::invalid_argument);
}

TEST(WarcastingSpellComponent, HandlesLargeIntermediatesAndReportsFinalOverflow)
{
	const auto largeValid = spells::scaleWarcastingSpellPowerComponent(
		std::numeric_limits<int64_t>::max(), std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
	EXPECT_GT(largeValid, std::numeric_limits<int64_t>::max() / 101);
	EXPECT_LT(largeValid, std::numeric_limits<int64_t>::max() / 99);

	EXPECT_THROW(spells::scaleWarcastingSpellPowerComponent(
		std::numeric_limits<int64_t>::max(), 1, std::numeric_limits<int32_t>::max()), std::overflow_error);
}

class WarcastingSpellComponentMechanicsTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		startGame();
		const auto decoded = SecondarySkill::decode("new-horizons:warcasting");
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		beginCombat();
	}

	spells::BattleCast heroCast(SpellID spellId, bool followup = false)
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(spellId).toSpell());
		cast.setMetamagicFollowup(followup);
		return cast;
	}
};

TEST_F(WarcastingSpellComponentMechanicsTest, OrdinarySpellSnapshotsBoostedArrowAndCureComponentsOnly)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	ASSERT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).bonusFor(
		AlternatingHeroActionState::Action::SPELL, battle()->getRound()), 10);

	auto arrow = heroCast(SpellID::MAGIC_ARROW);
	const auto arrowMechanics = SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&arrow);
	EXPECT_EQ(arrowMechanics->getWarcastingBonusPercent(), 10);
	EXPECT_EQ(arrowMechanics->getEffectPower(), 24);
	EXPECT_EQ(arrowMechanics->getEffectValue(), 72); // fixed 20 + floor(20 * 24 * 110 / (10 * 100))
	EXPECT_EQ(arrowMechanics->getEffectDuration(), attackerSideHero->getEnchantPower(SpellID(SpellID::MAGIC_ARROW).toSpell()));

	auto cure = heroCast(SpellID::CURE);
	const auto cureMechanics = SpellID(SpellID::CURE).toSpell()->battleMechanics(&cure);
	EXPECT_EQ(cureMechanics->getWarcastingBonusPercent(), 10);
	EXPECT_EQ(cureMechanics->getEffectPower(), 24);
	EXPECT_EQ(cureMechanics->getEffectValue(), 64); // fixed 25 + floor(3 * 24 * 110 / 200)
	EXPECT_EQ(cureMechanics->getEffectDuration(), attackerSideHero->getEnchantPower(SpellID(SpellID::CURE).toSpell()));

	// A Metamagic follow-up cannot reuse the pending ordinary-spell readiness.
	auto followupArrow = heroCast(SpellID::MAGIC_ARROW, true);
	const auto followupMechanics = SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&followupArrow);
	EXPECT_EQ(followupMechanics->getWarcastingBonusPercent(), 0);
	EXPECT_EQ(followupMechanics->getEffectValue(), 68);

	// The server's real cast path constructs its mechanics before BattleSpellCast
	// records the action and consumes readiness. Verify it actually casts, then
	// verify a previously captured calculation remains intact after consumption.
	const auto healthBefore = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::MAGIC_ARROW;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(target->getAvailableHealth(), healthBefore);
	EXPECT_EQ(server.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("consumes Warcasting (+10%) while casting") != std::string::npos;
	}));
	EXPECT_EQ(battle()->getWarcastingState(BattleSide::ATTACKER).bonusFor(
		AlternatingHeroActionState::Action::SPELL, battle()->getRound()), 0);
	EXPECT_EQ(arrowMechanics->getEffectValue(), 72);
}
