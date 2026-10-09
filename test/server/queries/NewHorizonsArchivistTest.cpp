/*
 * NewHorizonsArchivistTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../battles/HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto ARCHIVIST = "new-horizons:learning.archivist";

class NewHorizonsArchivistTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("core:tyris")), PLAYER);
		startWithMap(std::move(builder));
	}
	void configure(CGHeroInstance * hero, CGameHandler & handler, bool selected, bool school = true, bool book = true)
	{
		ASSERT_NE(hero, nullptr);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			hero->applyPerkSelection({LEARNING, "new-horizons:learning.eagleEye"});
			hero->applyPerkSelection({LEARNING, ARCHIVIST});
		}
		EXPECT_EQ(hero->hasActivePerk(LEARNING, ARCHIVIST), selected);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
			school ? MasteryLevel::BASIC : MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		if(book && !hero->hasSpellbook())
			ASSERT_TRUE(handler.giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
		if(!book && hero->hasSpellbook())
			handler.removeArtifact(ArtifactLocation(hero->id, ArtifactPosition::SPELLBOOK));
		handler.changeSpells(hero, false, {spell()});
	}
	static SpellID spell()
	{
		return SpellID(SpellID::decode("core:magicArrow"));
	}
};
}

TEST_F(NewHorizonsArchivistTest, AcceptedNewScrollLearnsImmediatelyAndSurvivesScrollRemoval)
{
	prepare();
	auto * hero = findHeroAt({5, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(hero, handler, true);
	ASSERT_TRUE(hero->canLearnSpellFromAcquiredScroll(spell()));
	ASSERT_TRUE(handler.giveHeroNewScroll(hero, spell(), ArtifactPosition::BACKPACK_START));
	EXPECT_TRUE(hero->spellbookContainsSpell(spell()));
	EXPECT_FALSE(hero->canLearnSpellFromAcquiredScroll(spell()));
	const auto position = hero->getScrollPos(spell(), false);
	ASSERT_NE(position, ArtifactPosition::PRE_FIRST);
	handler.removeArtifact(ArtifactLocation(hero->id, position));
	EXPECT_TRUE(hero->spellbookContainsSpell(spell()));
	EXPECT_FALSE(hero->hasScroll(spell(), false));
}

TEST_F(NewHorizonsArchivistTest, AcceptedPutArtifactReceiptLearnsFromAnExistingScrollInstance)
{
	prepare();
	auto * hero = findHeroAt({5, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(hero, handler, true);
	// Map loading owns scroll construction; the actual pickup receipt uses
	// putArtifact and its accepted outgoing packet, not a direct spell mutation.
	const auto * scroll = gameState()->getMap().createScroll(spell());
	ASSERT_NE(scroll, nullptr);
	ASSERT_TRUE(handler.putArtifact(ArtifactLocation(hero->id, ArtifactPosition::FIRST_AVAILABLE), scroll->getId(), false));
	EXPECT_TRUE(hero->spellbookContainsSpell(spell()));
	EXPECT_TRUE(hero->hasScroll(spell(), false));
}

TEST_F(NewHorizonsArchivistTest, AcceptedHeroTransferLearnsAndKeepsTheOriginalScroll)
{
	prepare();
	auto * recipient = findHeroAt({5, 5, 0});
	auto * donor = findHeroAt({7, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(recipient, handler, true);
	configure(donor, handler, false);
	ASSERT_TRUE(handler.giveHeroNewScroll(donor, spell(), ArtifactPosition::BACKPACK_START));
	const auto * scroll = donor->getArt(donor->getScrollPos(spell(), false));
	ASSERT_NE(scroll, nullptr);
	const auto id = scroll->getId();
	handler.heroExchange(donor->id, recipient->id);
	ASSERT_TRUE(handler.moveArtifact(PLAYER,
		ArtifactLocation(donor->id, donor->getArtPos(scroll)), ArtifactLocation(recipient->id, ArtifactPosition::BACKPACK_START)));
	EXPECT_TRUE(recipient->spellbookContainsSpell(spell()));
	EXPECT_FALSE(donor->spellbookContainsSpell(spell()));
	EXPECT_EQ(recipient->getArt(recipient->getScrollPos(spell(), false))->getId(), id);
}

TEST_F(NewHorizonsArchivistTest, SelectingPerkThenRearrangingAnAlreadyOwnedScrollDoesNotTeach)
{
	prepare();
	auto * hero = findHeroAt({5, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(hero, handler, false);
	ASSERT_TRUE(handler.giveHeroNewScroll(hero, spell(), ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(hero->spellbookContainsSpell(spell()));
	hero->applyPerkSelection({LEARNING, "new-horizons:learning.eagleEye"});
	hero->applyPerkSelection({LEARNING, ARCHIVIST});
	ASSERT_TRUE(hero->canLearnSpellFromAcquiredScroll(spell()));
	const auto source = hero->getScrollPos(spell(), false);
	ASSERT_TRUE(handler.moveArtifact(PLAYER, ArtifactLocation(hero->id, source),
		ArtifactLocation(hero->id, ArtifactPosition::TRANSITION_POS)));
	EXPECT_FALSE(hero->spellbookContainsSpell(spell()));
	ASSERT_TRUE(handler.moveArtifact(PLAYER, ArtifactLocation(hero->id, ArtifactPosition::TRANSITION_POS),
		ArtifactLocation(hero->id, ArtifactPosition::MISC1)));
	EXPECT_FALSE(hero->spellbookContainsSpell(spell()));
}

TEST_F(NewHorizonsArchivistTest, SchoolAndSpellbookEligibilityAreNotBypassedByReceipt)
{
	prepare();
	auto * noSchool = findHeroAt({5, 5, 0});
	auto * noBook = findHeroAt({7, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(noSchool, handler, true, false, true);
	configure(noBook, handler, true, true, false);
	// Magic Arrow is deliberately rank-zero learnable in New Horizons.
	// Use a genuine School-gated spell for both negative receipt controls.
	const SpellID gatedSpell(SpellID::decode("core:implosion"));
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	handler.changeSpells(noSchool, false, {gatedSpell});
	handler.changeSpells(noBook, false, {gatedSpell});
	noSchool->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(noSchool->canLearnSpellFromAcquiredScroll(gatedSpell));
	noSchool->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	noBook->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(noSchool->getSecSkillLevel(sorcery), MasteryLevel::NONE);
	ASSERT_EQ(noBook->getSecSkillLevel(sorcery), MasteryLevel::EXPERT);
	ASSERT_FALSE(noBook->hasSpellbook());
	EXPECT_FALSE(noSchool->canLearnSpellFromAcquiredScroll(gatedSpell));
	EXPECT_FALSE(noBook->canLearnSpellFromAcquiredScroll(gatedSpell));
	ASSERT_TRUE(handler.giveHeroNewScroll(noSchool, gatedSpell, ArtifactPosition::BACKPACK_START));
	ASSERT_TRUE(handler.giveHeroNewScroll(noBook, gatedSpell, ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(noSchool->spellbookContainsSpell(gatedSpell));
	EXPECT_FALSE(noBook->spellbookContainsSpell(gatedSpell));
}

TEST_F(NewHorizonsArchivistTest, FailedPlacementAndGuildOnlyAdventureScrollDoNotTeach)
{
	prepare();
	auto * hero = findHeroAt({5, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(hero, handler, true);
	ASSERT_FALSE(handler.giveHeroNewScroll(hero, spell(), ArtifactPosition::SPELLBOOK));
	EXPECT_FALSE(hero->spellbookContainsSpell(spell()));
	const SpellID adventure(SpellID::decode("core:townPortal"));
	handler.changeSpells(hero, false, {adventure});
	ASSERT_FALSE(hero->canLearnSpell(adventure.toSpell()));
	ASSERT_FALSE(hero->canLearnSpellFromAcquiredScroll(adventure));
	ASSERT_TRUE(handler.giveHeroNewScroll(hero, adventure, ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(hero->spellbookContainsSpell(adventure));
}

TEST_F(NewHorizonsArchivistTest, BannedSpellAndInsufficientLearningRankRemainIneligible)
{
	prepare();
	auto * banned = findHeroAt({5, 5, 0});
	auto * belowRank = findHeroAt({7, 5, 0});
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(banned, handler, true);
	configure(belowRank, handler, true);
	belowRank->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(belowRank->hasActivePerk(LEARNING, ARCHIVIST));
	EXPECT_FALSE(belowRank->canLearnSpellFromAcquiredScroll(spell()));
	ASSERT_TRUE(handler.giveHeroNewScroll(belowRank, spell(), ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(belowRank->spellbookContainsSpell(spell()));
	gameState()->getMap().allowedSpells.erase(spell());
	EXPECT_FALSE(banned->canLearnSpellFromAcquiredScroll(spell()));
	ASSERT_TRUE(handler.giveHeroNewScroll(banned, spell(), ArtifactPosition::BACKPACK_START));
	EXPECT_FALSE(banned->spellbookContainsSpell(spell()));
}

class NewHorizonsArchivistBattleLootTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void verifyResult(bool withScroll)
	{
		startGame();
		const SpellID spell(SpellID::decode("core:magicArrow"));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({LEARNING, "new-horizons:learning.eagleEye"});
		attackerSideHero->applyPerkSelection({LEARNING, ARCHIVIST});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(LEARNING, ARCHIVIST));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
		gameHandler->changeSpells(attackerSideHero, false, {spell});
		ASSERT_TRUE(attackerSideHero->canLearnSpellFromAcquiredScroll(spell));
		ArtifactInstanceID scrollId;
		ASSERT_FALSE(scrollId.hasValue());
		if(withScroll)
		{
			ASSERT_TRUE(gameHandler->giveHeroNewScroll(defenderSideHero, spell, ArtifactPosition::BACKPACK_START));
			scrollId = defenderSideHero->getArt(defenderSideHero->getScrollPos(spell, false))->getId();
		}
		else
			ASSERT_FALSE(defenderSideHero->hasScroll(spell, false));
		ASSERT_EQ(scrollId.hasValue(), withScroll);
		startBattle();
		beginCombat();
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		// Exercise the established production victory/result finalization path,
		// which generates nested loot moves rather than a fabricated result pack.
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
		ASSERT_TRUE(query->result);
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			const auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
		}
		EXPECT_EQ(attackerSideHero->spellbookContainsSpell(spell), withScroll);
		if(withScroll)
		{
			ASSERT_TRUE(attackerSideHero->hasScroll(spell, false));
			const auto position = attackerSideHero->getScrollPos(spell, false);
			EXPECT_EQ(attackerSideHero->getArt(position)->getId(), scrollId);
			gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, position));
			EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell));
		}
	}
};

TEST_F(NewHorizonsArchivistBattleLootTest, ActualVictoryLootReceiptPermanentlyLearnsTheNewScroll)
{
	verifyResult(true);
}

TEST_F(NewHorizonsArchivistBattleLootTest, ActualVictoryWithoutScrollLootDoesNotLearn)
{
	verifyResult(false);
}
