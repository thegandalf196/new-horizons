/*
 * NewHorizonsScrollCastingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGMarket.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{
class NewHorizonsScrollCastingTest : public HeroCommandFixture
{
protected:
	const SpellID spell{SpellID::IMPLOSION};
	CStack * target = nullptr;
	CStack * friendly = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(bool memory = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("ScrollCastingTest")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}})
			.town({24, 24, 0}, FactionID::CONFLUX, PlayerColor(0)).townGarrison({});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		for(auto * hero : {attackerSideHero, defenderSideHero})
			for(int skill = 0; skill < LIBRARY->skillh->size(); ++skill)
				hero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		gameHandler->changeSpells(attackerSideHero, false, {spell});
		if(memory)
		{
			const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.arcaneMemory"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneMemory"));
		}
		setTestSpellPointTotal(attackerSideHero, 1000);
		for(const auto school : newHorizonsMagic::schoolSkills(attackerSideHero->getMagicRules()))
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(school), MasteryLevel::NONE);
		ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
		ASSERT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell()));
	}

	void begin()
	{
		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1000);
		beginCombat();
	}

	bool cast(const CStack * destination = nullptr)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(destination ? destination : target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
}

TEST_F(NewHorizonsScrollCastingTest, EquippedHighLevelScrollCastsWithoutSchoolOrPermanentLearning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	ASSERT_NO_FATAL_FAILURE(begin());
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	ASSERT_TRUE(cast());
	EXPECT_LT(target->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell()));
	const auto after = target->getAvailableHealth();
	EXPECT_FALSE(cast()) << "Scroll access must not grant another Hero Action";
	EXPECT_EQ(target->getAvailableHealth(), after);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	const auto saved = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(saved);
	const auto * restoredHero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasScroll(spell, true));
	EXPECT_TRUE(restoredHero->canCastThisSpell(spell.toSpell()));
	EXPECT_FALSE(restoredHero->spellbookContainsSpell(spell));
	EXPECT_FALSE(restoredHero->canLearnSpell(spell.toSpell()));
}

TEST_F(NewHorizonsScrollCastingTest, ArcaneMemoryDoesNotBypassSchoolLearningAfterGenuineScrollCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	ASSERT_NO_FATAL_FAILURE(begin());
	ASSERT_TRUE(cast());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell()));
}

TEST_F(NewHorizonsScrollCastingTest, QualifiedArcaneMemoryStillLearnsFromAcceptedScrollCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->canLearnSpell(spell.toSpell()));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	ASSERT_NO_FATAL_FAILURE(begin());
	ASSERT_TRUE(cast());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
}

TEST_F(NewHorizonsScrollCastingTest, OnlyMatchingUnlockedEquippedScrollSourcesBypassSchool)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(nullptr));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_LEVEL,
		BonusSource::OTHER, 0, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::spellLevel(newHorizonsMagic::spellLevel(attackerSideHero->getMagicRules(), spell)))));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL,
		BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(spell)));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, SpellID(SpellID::HASTE), ArtifactPosition::MISC1));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC2));
	EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->lockSlot(ArtifactPosition::MISC2);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	attackerSideHero->artifactsWorn.at(ArtifactPosition::MISC2).locked = false;
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::MISC2));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
}

TEST_F(NewHorizonsScrollCastingTest, ScrollsDoNotGrantUnavailablePerkVariantsOrRetiredSpells)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto * identity : {"new-horizons:massBless", "new-horizons:counterspell"})
	{
		const SpellID unavailable(SpellID::decode(identity));
		ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, unavailable, ArtifactPosition::MISC1));
		EXPECT_FALSE(attackerSideHero->canCastThisSpell(unavailable.toSpell())) << identity;
		gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::MISC1));
	}
}

TEST_F(NewHorizonsScrollCastingTest, ScrollDoesNotReplacePhysicalSpellbookForAuthoritativeCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::SPELLBOOK));
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	ASSERT_NO_FATAL_FAILURE(begin());
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	ASSERT_TRUE(cast());
}

TEST_F(NewHorizonsScrollCastingTest, MapBanAndInsufficientManaStillRejectWithoutSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	gameState()->getMap().allowedSpells.erase(spell);
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	gameState()->getMap().allowedSpells.insert(spell);
	ASSERT_NO_FATAL_FAILURE(begin());
	const auto health = target->getAvailableHealth();
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(cast());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
	setTestSpellPointTotal(attackerSideHero, 1000);
	ASSERT_TRUE(cast()) << "Rejected scroll attempts must not spend the Hero Action";
}

TEST_F(NewHorizonsScrollCastingTest, ScrollAccessDoesNotBypassHostileTargetLegality)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	ASSERT_NO_FATAL_FAILURE(begin());
	const auto health = friendly->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(friendly));
	EXPECT_EQ(friendly->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(cast());
}

TEST_F(NewHorizonsScrollCastingTest, ScrollAccessDoesNotBypassSpellLock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, spell, ArtifactPosition::MISC1));
	ASSERT_NO_FATAL_FAILURE(begin());
	const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	if(!defenderSideHero->hasSpellbook())
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(lock);
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(castOn(defenderSideHero, lock, target));
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*target));
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
}

TEST_F(NewHorizonsScrollCastingTest, DirectHouseOfWisdomPurchaseGrantsCastableScrollWithoutItsSchool)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::SPECIAL_2);
	SetHouseOfWisdomScrolls stock;
	stock.townId = town->id;
	stock.scrolls = {spell};
	gameHandler->sendAndApply(stock);
	const auto price = newHorizonsHouseOfWisdom::price(spell);
	gameHandler->giveResources(PlayerColor(0), price * 2);
	const auto resources = gameState()->getPlayerState(PlayerColor(0))->resources;
	// Direct authoritative purchase API, not a normal market visit/command test.
	ASSERT_TRUE(gameHandler->buyHouseOfWisdomScroll(town, attackerSideHero, spell));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resources - price);
	EXPECT_TRUE(town->getHouseOfWisdomScrolls().empty());
	ASSERT_TRUE(attackerSideHero->hasScroll(spell, true));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	ASSERT_NO_FATAL_FAILURE(begin());
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(spell.toSpell()));
	EXPECT_TRUE(attackerSideHero->hasScroll(spell, true));
}
