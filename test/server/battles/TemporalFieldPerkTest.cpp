/*
 * TemporalFieldPerkTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"

namespace
{
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto temporalFieldPerk = "new-horizons:sorceryMagic.temporalField";

class TemporalFieldPerkTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool selectPerk, int mana = 100, int rank = 2)
	{
		startGame();
		const auto decoded = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({sorcerySkill, temporalFieldPerk});
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		setTestSpellPointTotal(attackerSideHero, mana);

		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		enemyA = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 10);
		enemyB = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 5), 10);
		beginCombat();
		ASSERT_EQ(attackerSideHero->hasActivePerk(sorcerySkill, temporalFieldPerk), selectPerk);
	}

	bool castSlow(bool mass, const CStack * target = nullptr)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::SLOW;
		action.spellMassSlow = mass;
		if(target)
			action.aimToUnit(target);
		else
			action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::shared_ptr<const Bonus> slow(const CStack * unit) const
	{
		return unit->getBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
	}

	CStack * friendly = nullptr;
	CStack * enemyA = nullptr;
	CStack * enemyB = nullptr;
};
}

TEST_F(TemporalFieldPerkTest, MassSlowAffectsEveryEligibleEnemyAtSixtyPercentAndConsumesBudget)
{
	prepare(true);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto movementBefore = enemyA->getMovementRange();
	const auto initiativeBefore = enemyA->getInitiative();

	ASSERT_TRUE(castSlow(true));
	ASSERT_NE(slow(enemyA), nullptr);
	ASSERT_NE(slow(enemyB), nullptr);
	EXPECT_EQ(slow(enemyA)->val, -30);
	EXPECT_EQ(slow(enemyB)->val, -30);
	EXPECT_EQ(slow(friendly), nullptr);
	EXPECT_EQ(enemyA->getMovementRange(), movementBefore);
	EXPECT_LT(enemyA->getInitiative(), initiativeBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 9);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, SecondMassSlowIsRejectedWithoutManaOrStateMutation)
{
	prepare(true);
	ASSERT_TRUE(castSlow(true));
	auto * freshEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(14, 5), 10);
	advanceRound();
	const auto manaAfterFirst = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(castSlow(true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaAfterFirst);
	EXPECT_TRUE(castSlow(false, freshEnemy));
	EXPECT_NE(slow(freshEnemy), nullptr);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, OrdinarySlowKeepsFullMagnitudeAndDoesNotConsumeTemporalField)
{
	prepare(true);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(castSlow(false, enemyA));
	ASSERT_NE(slow(enemyA), nullptr);
	EXPECT_EQ(slow(enemyA)->val, -50);
	EXPECT_EQ(slow(enemyB), nullptr);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 3);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, ExpertOrdinarySlowCannotBypassTemporalFieldTradeoff)
{
	prepare(true, 100, 3);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(castSlow(false, enemyA));
	ASSERT_NE(slow(enemyA), nullptr);
	EXPECT_EQ(slow(enemyA)->val, -50);
	EXPECT_EQ(slow(enemyB), nullptr);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 3);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, ExpertSlowWithoutPerkIsStillSingleTargetInNewHorizons)
{
	prepare(false, 100, 3);

	ASSERT_TRUE(castSlow(false, enemyA));
	EXPECT_NE(slow(enemyA), nullptr);
	EXPECT_EQ(slow(enemyB), nullptr);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, MassMagnitudeScalesTheFinalSpecialistAdjustedSlow)
{
	prepare(true);
	auto specialty = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_ADD_VALUE_ENCHANT, BonusSource::OTHER, 0, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::SLOW)));
	specialty->parameters = std::make_shared<BonusParameters>(-10);
	attackerSideHero->addNewBonus(specialty);

	ASSERT_TRUE(castSlow(true));
	ASSERT_NE(slow(enemyA), nullptr);
	EXPECT_EQ(slow(enemyA)->val, -36); // 60% of the ordinary specialist value -60.
}

TEST_F(TemporalFieldPerkTest, MissingPerkAndInsufficientManaRejectAtomically)
{
	prepare(false);
	const auto manaWithoutPerk = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castSlow(true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaWithoutPerk);
	EXPECT_EQ(slow(enemyA), nullptr);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, TripleListedCostIsCheckedBeforeApplyingEffects)
{
	prepare(true, 8);
	EXPECT_FALSE(castSlow(true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 8);
	EXPECT_EQ(slow(enemyA), nullptr);
	EXPECT_EQ(slow(enemyB), nullptr);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).temporalFieldUsed);
}

TEST_F(TemporalFieldPerkTest, TripleListedCostPrecedesBattlefieldCostReduction)
{
	prepare(true);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CHANGES_SPELL_COST_FOR_ALLY, BonusSource::OTHER, 2, BonusSourceID()));

	ASSERT_TRUE(castSlow(true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 7); // 3 * listed 3, then the 2-point reduction.
}
