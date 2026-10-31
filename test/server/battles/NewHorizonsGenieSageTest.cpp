/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGenieSageFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
using NewHorizonsGenieSageTest = NewHorizonsGenieSageFixture;
TEST_F(NewHorizonsGenieSageTest, ThaneStartsHistorianAndGainsTwoRawRatingsWithoutExtraRng)
{
	startAs("core:thane");
	ASSERT_EQ(attackerSideHero->level, 1);
	EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:learning"), 2);
	EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:metamagic"), 1);
	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:learning", "new-horizons:learning.historian"));
	EXPECT_FALSE(attackerSideHero->hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"));
	const auto view = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_EQ(view->fixedSpecialtyGrowth, (std::array<int, 4>{0, 0, 1, 1}));
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(attackerSideHero->getBasePrimarySkillValue(PrimarySkill(i)), view->profile.starting[i]);
	ASSERT_TRUE(view->extraGrowth.empty());
	const auto before = wire(*gameHandler->randomizer);
	const auto gains = gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero);
	EXPECT_EQ(wire(*gameHandler->randomizer), before);
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(gains[i], view->profile.growth[i] + (i >= 2 ? 1 : 0));
	gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(2), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	EXPECT_EQ(attackerSideHero->level, 2);
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(attackerSideHero->getBasePrimarySkillValue(PrimarySkill(i)), view->base[i] + gains[i]);
}
TEST_F(NewHorizonsGenieSageTest, HistoricalThaneRetainsScholarAndOrdinaryGrowth)
{
	historical = true;
	startAs("core:thane");
	EXPECT_FALSE(newHorizonsHeroes::hasSageOfTheAges(attackerSideHero));
	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"));
	const auto view = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_EQ(view->fixedSpecialtyGrowth, (std::array<int, 4>{}));
	EXPECT_EQ(gameHandler->randomizer->rollPrimarySkillsForLevelup(attackerSideHero), view->profile.growth);
}
TEST_F(NewHorizonsGenieSageTest, BestowIsExplicitFixedSingleTargetAndConsumesOneCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie());
	ASSERT_TRUE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, bestow()));
	EXPECT_FALSE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, turn()));
	const auto before = ally->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_EQ(genie->casts.available(), 2);
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), before + 3);
	EXPECT_EQ(server.casts.back().announcement.spellID, bestow());
	EXPECT_EQ(newHorizonsHeroes::genieLuckStatusSpell(*ally, SpellID::FORTUNE), bestow());
	ASSERT_FALSE(luckFamily(*ally, SpellID::FORTUNE)->empty());
	EXPECT_EQ(luckFamily(*ally, SpellID::FORTUNE)->front()->turnsRemain, 5);
	EXPECT_NE(battle()->battleActiveUnit(), genie);
}
TEST_F(NewHorizonsGenieSageTest, MasterSharesThreeUsesAcrossBothNamedOptionsAndWish)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie(true));
	ASSERT_EQ(genie->casts.available(), 3);
	ASSERT_TRUE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, turn()));
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_EQ(genie->casts.available(), 2);
	activateGenie();
	const auto before = enemy->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(castGenie(enemy, turn()));
	EXPECT_EQ(enemy->valOfBonuses(BonusType::LUCK), before - 3);
	EXPECT_EQ(newHorizonsHeroes::genieLuckStatusSpell(*enemy, SpellID::MISFORTUNE), turn());
	EXPECT_EQ(genie->casts.available(), 1);
	activateGenie();
	ASSERT_TRUE(castGenie(ally, SpellID::NONE));
	EXPECT_EQ(genie->casts.available(), 0);
	activateGenie();
	EXPECT_FALSE(castGenie(enemy, turn()));
	EXPECT_FALSE(castGenie(ally, bestow()));
	EXPECT_EQ(genie->casts.available(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}
TEST_F(NewHorizonsGenieSageTest, WrongSideAndUnauthorizedNamedAbilityRejectWithoutCost)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie());
	const auto rng = wire(*gameHandler->randomizer);
	EXPECT_FALSE(castGenie(enemy, bestow()));
	EXPECT_FALSE(castGenie(enemy, turn()));
	EXPECT_FALSE(castGenie(ally, turn()));
	EXPECT_EQ(genie->casts.available(), 3);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	EXPECT_EQ(battle()->battleActiveUnit(), genie);
}
TEST_F(NewHorizonsGenieSageTest, RepeatedBestowRefreshesWithoutStackingAndExpiresNormally)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie());
	const auto before = ally->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(castGenie(ally, bestow()));
	activateGenie();
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), before + 3);
	EXPECT_EQ(luckFamily(*ally, SpellID::FORTUNE)->size(), 1);
	for(int i = 0; i < 5; ++i)
		advanceRound();
	EXPECT_TRUE(luckFamily(*ally, SpellID::FORTUNE)->empty());
}
TEST_F(NewHorizonsGenieSageTest, CurrentSettingsRoundTripAndOldWriterAdmission)
{
	GameSettings settings;
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer memory;
	settings.serialize(memory.oser);
	GameSettings restored;
	restored.serialize(memory.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_DREAD_CAVALRY;
	EXPECT_THROW(settings.serialize(older.oser), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
	rules.Struct().erase("genieSageSpecialties");
	EXPECT_NO_THROW(newHorizonsHeroes::validateGenieSageSerialization(rules, false));
	for(int bad : {0, 2})
	{
		rules["genieSageSpecialties"]["version"].Integer() = bad;
		EXPECT_THROW(newHorizonsHeroes::validateGenieSageSerialization(rules, true), std::runtime_error);
	}
}
TEST_F(NewHorizonsGenieSageTest, HistoricalIonaDoesNotAcquireNamedAbilities)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepareGenie(true));
	EXPECT_FALSE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, bestow()));
	EXPECT_FALSE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, turn()));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().artwork, "newHorizons/heroSpecialties/IonaGenieLine");
	EXPECT_EQ(genie->casts.available(), 3);
}
TEST_F(NewHorizonsGenieSageTest, NewThaneDoesNotRetainGenieAbilities)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie(true, "core:thane"));
	EXPECT_TRUE(newHorizonsHeroes::hasSageOfTheAges(attackerSideHero));
	EXPECT_FALSE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, bestow()));
	EXPECT_FALSE(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, turn()));
	EXPECT_FALSE(genie->hasBonus(Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->getHeroTypeID()))));
}

TEST_F(NewHorizonsGenieSageTest, OrdinaryFortuneAndMisfortuneReplaceWheelInBothDirections)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie());
	const auto initial = ally->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(castGenie(ally, bestow()));
	ASSERT_NO_FATAL_FAILURE(passive(ally, SpellID::FORTUNE, attackerSideHero));
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), initial + 1);
	EXPECT_EQ(newHorizonsHeroes::genieLuckStatusSpell(*ally, SpellID::FORTUNE), SpellID::NONE);
	activateGenie();
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), initial + 3);
	EXPECT_EQ(luckFamily(*ally, SpellID::FORTUNE)->size(), 1);
	ASSERT_NO_FATAL_FAILURE(passive(ally, SpellID::MISFORTUNE, defenderSideHero));
	EXPECT_TRUE(luckFamily(*ally, SpellID::FORTUNE)->empty());
	EXPECT_FALSE(luckFamily(*ally, SpellID::MISFORTUNE)->empty());
	activateGenie();
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_TRUE(luckFamily(*ally, SpellID::MISFORTUNE)->empty());
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), initial + 3);
	ASSERT_NO_FATAL_FAILURE(passive(ally, SpellID::DISPEL, attackerSideHero));
	EXPECT_TRUE(luckFamily(*ally, SpellID::FORTUNE)->empty());
}
TEST_F(NewHorizonsGenieSageTest, OrdinaryMisfortuneReplacesTurnOfFateWithoutAddingLuckComponents)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie(true));
	const auto initial = enemy->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(castGenie(enemy, turn()));
	EXPECT_EQ(enemy->valOfBonuses(BonusType::LUCK), initial - 3);
	ASSERT_NO_FATAL_FAILURE(passive(enemy, SpellID::MISFORTUNE, attackerSideHero));
	EXPECT_EQ(enemy->valOfBonuses(BonusType::LUCK), initial);
	EXPECT_FALSE(luckFamily(*enemy, SpellID::MISFORTUNE)->empty());
}

TEST_F(NewHorizonsGenieSageTest, GainedRatingsSurviveHeroReloadWithoutRetroactiveOrDuplicatedGrowth)
{
	startAs("core:thane");
	const auto before = attackerSideHero->getPrimaryGrowthView();
	ASSERT_TRUE(before);
	gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(2), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	// Rebuild exported primary bonuses through the real game-state load path.
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restoredState;
	memory.iser.cb = &restoredState;
	memory.iser.loadingGamestate = true;
	memory.iser & restoredState;
	const auto * restored = restoredState.getHero(attackerSideHero->id);
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->level, 2);
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(restored->getBasePrimarySkillValue(PrimarySkill(i)),
			before->base[i] + before->profile.growth[i] + (i >= 2 ? 1 : 0));
	const auto next = gameHandler->randomizer->rollPrimarySkillsForLevelup(restored);
	for(int i = 0; i < 4; ++i)
		EXPECT_EQ(next[i], before->profile.growth[i] + (i >= 2 ? 1 : 0));
	EXPECT_EQ(restored->getSpecialtyPresentation().name, "Sage of the Ages");
}

TEST_F(NewHorizonsGenieSageTest, NamedFriendlyCastDoesNotDrawAnOutcomeFromTheRandomWishPool)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie());
	// Isolate selection RNG from possible next-unit Morale rolls.
	for(auto * target : {genie, ally, enemy})
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
			BonusSource::OTHER, 1, BonusSourceID()));
	const auto rng = wire(*gameHandler->randomizer);
	ASSERT_TRUE(castGenie(ally, bestow()));
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	EXPECT_EQ(server.casts.back().announcement.spellID, bestow());
}
TEST_F(NewHorizonsGenieSageTest, ImmunityAndSuppressedCasterRejectNamedOptionsBeforeSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepareGenie(true));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(turn())));
	const auto rng = wire(*gameHandler->randomizer);
	EXPECT_FALSE(castGenie(enemy, turn()));
	EXPECT_EQ(genie->casts.available(), 3);
	const auto usage = genie->acquireState()->save()["state"]["casts"];
	auto suppression = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT, 2,
		BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
	genie->addNewBonus(suppression);
	ASSERT_FALSE(genie->canCast());
	EXPECT_FALSE(castGenie(ally, bestow()));
	// Suppression hides native CASTS; it must not consume their stored usage.
	EXPECT_EQ(genie->acquireState()->save()["state"]["casts"], usage);
	EXPECT_EQ(genie->casts.available(), 0);
	genie->removeBonus(suppression);
	EXPECT_EQ(genie->casts.available(), 3);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
}
