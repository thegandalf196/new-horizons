/*
 * NewHorizonsSageTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/NewHorizonsSage.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../battles/FullGameSnapshotTypes.h"

namespace
{
constexpr PlayerColor PLAYER(0);
SpellID named(const char * name) { return SpellID(SpellID::decode(name)); }
SpellSchool sorcery() { return SpellSchool(SpellSchool::decode("new-horizons:sorcery")); }
SecondarySkill school(const char * name) { return SecondarySkill(SecondarySkill::decode(name)); }

class NewHorizonsSageTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(int guildLevel = 1, bool book = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.hero({7, 5, 0}, HeroTypeID(1), PLAYER)
			.hero({9, 9, 0}, HeroTypeID(2), PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER);
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		for(auto * current : {hero, findHeroAt({7, 5, 0})})
		{
			ASSERT_NE(current, nullptr);
			current->setSecSkillLevel(school("new-horizons:learning"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			current->setSecSkillLevel(school("new-horizons:wisdom"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			current->setSecSkillLevel(school("new-horizons:sorceryMagic"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			// Permanent spell removal is sent through the real handler below.
		}
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		for(auto * current : {hero, findHeroAt({7, 5, 0})})
		{
			if(book && !current->hasSpellbook())
				ASSERT_TRUE(handler.giveHeroNewArtifact(current, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			if(!book && current->hasSpellbook())
				handler.removeArtifact(ArtifactLocation(current->id, ArtifactPosition::SPELLBOOK));
			handler.changeSpells(current, false, current->getSpellsInSpellbook());
		}
		setGuild(guildLevel);
	}
	void setGuild(int level)
	{
		for(int tier = 1; tier <= GameConstants::SPELL_LEVELS; ++tier)
		{
			const BuildingID building(BuildingID::MAGES_GUILD_1 + tier - 1);
			if(tier <= level)
				town->addBuilding(building);
			else
				town->removeBuilding(building);
		}
	}
	void isolatedCatalog()
	{
		map()->allowedSpells = {named("core:magicArrow"), named("core:haste"), named("core:dispel"), named("core:implosion")};
		town->spells.assign(GameConstants::SPELL_LEVELS, {});
		town->newHorizonsMageGuildVisibleSpells.assign(GameConstants::SPELL_LEVELS, 0);
		town->newHorizonsMageGuildVisibleSpellSchools.assign(GameConstants::SPELL_LEVELS, {sorcery()});
		town->spells[0] = {named("core:magicArrow")};
		town->newHorizonsMageGuildVisibleSpells[0] = 1;
	}
	void select(bool wisdom, bool learning, CGHeroInstance * current = nullptr)
	{
		if(!current)
			current = hero;
		if(wisdom)
		{
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.intelligence"});
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.spellScholar"});
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.sage"});
		}
		if(learning)
		{
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.eagleEye"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.fieldStudy"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.philosopher"});
		}
		EXPECT_EQ(current->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage"), wisdom);
		EXPECT_FALSE(newHorizonsSage::learningSelected(*current));
	}
	void visit(CGameHandler & handler) { handler.heroVisitCastle(town, hero); }
};
}

TEST_F(NewHorizonsSageTest, CurrentExpertLearningLearnsOnlyOrdinaryVisibleGuildSpells)
{
	prepare(4);
	isolatedCatalog();
	select(false, true);
	EXPECT_FALSE(newHorizonsSage::selectSpell(*hero, *town));
	const auto guildRows = town->spells;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:implosion")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:dispel")));
	EXPECT_EQ(town->spells, guildRows);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	visit(handler);
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:dispel")));
}

TEST_F(NewHorizonsSageTest, CurrentWisdomSageChangesKnowledgeWithoutRevealingGuildSpells)
{
	prepare();
	isolatedCatalog();
	const auto knowledge = hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	select(true, false);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), knowledge + 50);
	EXPECT_FALSE(newHorizonsSage::wisdomReveal(*hero, *town));
	const auto ordinary = town->spells;
	const auto counts = town->newHorizonsMageGuildVisibleSpells;
	const auto schools = town->newHorizonsMageGuildVisibleSpellSchools;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
	EXPECT_EQ(town->spells, ordinary);
	EXPECT_EQ(town->newHorizonsMageGuildVisibleSpells, counts);
	EXPECT_EQ(town->newHorizonsMageGuildVisibleSpellSchools, schools);
	auto * other = findHeroAt({7, 5, 0});
	ASSERT_NE(other, nullptr);
	town->setVisitingHero(nullptr);
	hero->setVisitedTown(nullptr, false);
	handler.heroVisitCastle(town, other);
	EXPECT_TRUE(other->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_FALSE(other->spellbookContainsSpell(named("core:dispel")));
}

TEST_F(NewHorizonsSageTest, CurrentLearningAndWisdomPerksDoNotRevealOrLearnHiddenSpells)
{
	prepare(4);
	isolatedCatalog();
	select(true, true);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[3].empty());
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:implosion")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:dispel")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
	visit(handler);
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
}

TEST_F(NewHorizonsSageTest, EachCurrentWisdomHolderRecordsItsVisitWithoutPublishingAnyExtra)
{
	prepare(4);
	isolatedCatalog();
	select(true, false);
	auto * other = findHeroAt({7, 5, 0});
	ASSERT_NE(other, nullptr);
	select(true, false, other);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	town->setVisitingHero(nullptr);
	handler.heroVisitCastle(town, other);
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[3].empty());
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	EXPECT_TRUE(other->getNewHorizonsSageGuildVisits().contains(town->id));
	handler.heroVisitCastle(town, other);
	EXPECT_FALSE(other->spellbookContainsSpell(named("core:haste")));
}

TEST_F(NewHorizonsSageTest, PreAcquisitionFirstGuildVisitConsumesButGuildlessArrivalDoesNot)
{
	prepare(0);
	isolatedCatalog();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().empty());
	setGuild(1);
	visit(handler);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	select(true, true);
	visit(handler);
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
}

TEST_F(NewHorizonsSageTest, FirstGuildVisitWithoutBookCannotBeReclaimedAfterBookPurchase)
{
	prepare(1, false);
	isolatedCatalog();
	select(true, true);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	ASSERT_TRUE(handler.giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
	visit(handler);
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
}

TEST_F(NewHorizonsSageTest, NoHiddenCandidateExistsRegardlessOfSchoolLabelsMapBansOrBuiltLevel)
{
	prepare(4);
	isolatedCatalog();
	select(true, true);
	map()->allowedSpells.erase(named("core:implosion"));
	EXPECT_FALSE(newHorizonsSage::selectSpell(*hero, *town));
	town->newHorizonsMageGuildVisibleSpellSchools[0].clear();
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
	town->newHorizonsMageGuildVisibleSpellSchools[0] = {sorcery()};
	map()->allowedSpells.erase(named("core:dispel"));
	EXPECT_FALSE(newHorizonsSage::selectSpell(*hero, *town));
	setGuild(3);
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
}

TEST_F(NewHorizonsSageTest, CurrentWorldRoundtripPreservesFiveOrdinarySlotsAndVisitWithoutExtraReveal)
{
	prepare();
	for(const auto * skill : {"new-horizons:lightMagic", "new-horizons:shadowMagic", "new-horizons:natureMagic", "new-horizons:chaosMagic", "new-horizons:havocMagic"})
		hero->setSecSkillLevel(school(skill), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	select(true, false);
	ASSERT_EQ(town->newHorizonsMageGuildVisibleSpells[0], 5);
	EXPECT_FALSE(newHorizonsSage::wisdomReveal(*hero, *town));
	const auto ordinary = town->spells[0];
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(town->spellsAtLevel(1, true), 5);
	EXPECT_EQ(town->spells[0], ordinary);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedTown = restored.getTown(town->id);
	const auto * savedHero = restored.getHero(hero->id);
	ASSERT_NE(savedTown, nullptr);
	ASSERT_NE(savedHero, nullptr);
	EXPECT_EQ(savedTown->spellsAtLevel(1, true), 5);
	EXPECT_EQ(savedTown->spells, town->spells);
	EXPECT_EQ(savedHero->getSpellsInSpellbook(), hero->getSpellsInSpellbook());
	EXPECT_EQ(savedTown->newHorizonsSageRevealedSpells, town->newHorizonsSageRevealedSpells);
	EXPECT_TRUE(savedHero->getNewHorizonsSageGuildVisits().contains(town->id));
}

TEST_F(NewHorizonsSageTest, TypedVisitRejectsStaleOrWrongRevealAtomicallyAndCurrentPacketRoundtrips)
{
	prepare();
	isolatedCatalog();
	select(true, false);
	town->setVisitingHero(hero);
	hero->setVisitedTown(town, false);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	SetNewHorizonsSageGuildVisit receipt;
	receipt.hero = hero->id;
	receipt.town = town->id;
	receipt.revealedSpell = named("core:haste");
	EXPECT_THROW(handler.sendAndApply(receipt), std::runtime_error);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().empty());
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
	receipt.revealedSpell = SpellID::NONE;
	CMemorySerializer current;
	current.oser & receipt;
	SetNewHorizonsSageGuildVisit restored;
	current.iser & restored;
	EXPECT_EQ(restored.hero, receipt.hero);
	EXPECT_EQ(restored.town, receipt.town);
	EXPECT_EQ(restored.revealedSpell, receipt.revealedSpell);
	handler.sendAndApply(receipt);
	EXPECT_THROW(handler.sendAndApply(receipt), std::runtime_error);
	EXPECT_TRUE(town->newHorizonsSageRevealedSpells[0].empty());
}

TEST_F(NewHorizonsSageTest, CurrentMalformedVisitAndOffMapStateRejectBeforeOwningSavePrefixes)
{
	prepare();
	isolatedCatalog();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	const auto pooled = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(hero->id));
	ASSERT_NE(pooled, nullptr);
	map()->addToHeroPool(pooled);
	ASSERT_EQ(map()->tryGetFromHeroPool(pooled->getHeroTypeID()), hero);
	EXPECT_NO_THROW(gameState()->validateNewHorizonsSageSerialization(true));
	auto & visits = const_cast<std::set<ObjectInstanceID> &>(hero->getNewHorizonsSageGuildVisits());
	visits.insert(ObjectInstanceID::NONE);
	CMemorySerializer malformed;
	EXPECT_THROW(malformed.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
	visits.erase(ObjectInstanceID::NONE);
	// Revealed suffixes without matching Guild rows remain corrupt current data.
	town->newHorizonsSageRevealedSpells[0] = {named("core:haste")};
	CMemorySerializer malformedTown;
	EXPECT_THROW(malformedTown.oser & *town, std::runtime_error);
	EXPECT_TRUE(malformedTown.extractBuffer().empty());
	CMemorySerializer malformedMap;
	EXPECT_THROW(malformedMap.oser & *map(), std::runtime_error);
	EXPECT_TRUE(malformedMap.extractBuffer().empty());
	town->newHorizonsSageRevealedSpells = {};
}
