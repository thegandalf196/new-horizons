/*
 * SelectiveDispelPerkTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto selectiveDispelPerk = "new-horizons:sorceryMagic.selectiveDispel";

class SelectiveDispelPerkTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareDispel(bool selectPerk, int rank = 1)
	{
		startGame();
		const auto decoded = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({sorcerySkill, selectiveDispelPerk});
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		beginCombat();
		ASSERT_EQ(attackerSideHero->hasActivePerk(sorcerySkill, selectiveDispelPerk), selectPerk);
	}

	void addOppositeEffects(const CStack * stack)
	{
		Bonus positive(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::BLESS)));
		positive.turnsRemain = 3;
		Bonus negative(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, -2, BonusSourceID(SpellID(SpellID::CURSE)));
		negative.turnsRemain = 3;

		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{positive, negative});
		gameHandler->sendAndApply(effect);
		ASSERT_TRUE(hasEffect(stack, SpellID::BLESS));
		ASSERT_TRUE(hasEffect(stack, SpellID::CURSE));
	}

	bool hasEffect(const CStack * stack, SpellID spell) const
	{
		return stack->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}

	bool castDispel(const CStack * target, bool selective)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.spellSelectiveDispel = selective;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castMassDispel(bool selective)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.spellSelectiveDispel = selective;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
};
}

TEST_F(SelectiveDispelPerkTest, FriendlySelectiveCastRemovesOnlyHostileEffect)
{
	prepareDispel(true);
	addOppositeEffects(friendly);

	ASSERT_TRUE(castDispel(friendly, true));
	EXPECT_TRUE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(friendly, SpellID::CURSE));
}

TEST_F(SelectiveDispelPerkTest, EnemySelectiveCastRemovesOnlyBeneficialEffect)
{
	prepareDispel(true);
	addOppositeEffects(enemy);

	ASSERT_TRUE(castDispel(enemy, true));
	EXPECT_FALSE(hasEffect(enemy, SpellID::BLESS));
	EXPECT_TRUE(hasEffect(enemy, SpellID::CURSE));
}

TEST_F(SelectiveDispelPerkTest, HeroMayStillChooseFullDispel)
{
	prepareDispel(true);
	addOppositeEffects(friendly);

	ASSERT_TRUE(castDispel(friendly, false));
	EXPECT_FALSE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(friendly, SpellID::CURSE));
}

TEST_F(SelectiveDispelPerkTest, ServerRejectsSelectiveModeWithoutActiveSavedPerk)
{
	prepareDispel(false);
	addOppositeEffects(friendly);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(castDispel(friendly, true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_TRUE(hasEffect(friendly, SpellID::CURSE));
}

TEST_F(SelectiveDispelPerkTest, ExpertSelectiveMassDispelFiltersEachSideIndependently)
{
	prepareDispel(true, 3);
	addOppositeEffects(friendly);
	addOppositeEffects(enemy);

	ASSERT_TRUE(castMassDispel(true));
	EXPECT_TRUE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(friendly, SpellID::CURSE));
	EXPECT_FALSE(hasEffect(enemy, SpellID::BLESS));
	EXPECT_TRUE(hasEffect(enemy, SpellID::CURSE));
}
