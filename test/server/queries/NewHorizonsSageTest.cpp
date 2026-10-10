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
	bool isolateFutureCrisisProfile = false;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode heroRules(JsonPath::builtin("config/newHorizonsHeroes"));
		heroRules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
		heroRules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
		heroRules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
		heroRules["startingSkills"].Struct().erase("startingBookReplacements");
		heroRules.Struct().erase("remainingSpellSpecialtyReplacements");
		heroRules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		heroRules.Struct().erase("defaultCreatureLineReplacements");
		std::erase_if(heroRules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
		heroRules.Struct().erase("lighthouseDeparture");
		heroRules["nonDamageSpellSpecialties"].Struct().erase("aenainFrailtyReplacement");
		heroRules["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
		heroRules["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
		std::erase_if(heroRules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
		{
			return spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
				|| spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
		});
		// Isolate Sage's prior-format controls from the later independent opt-in.
		auto & specialties = heroRules["nonDamageSpellSpecialties"]["spells"].Vector();
		std::erase_if(specialties, [](const JsonNode & spell)
		{
			return spell.isString() && spell.String() == "new-horizons:frailty";
		});
		heroRules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroRules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(isolateFutureCrisisProfile)
			for(auto & perk : perkRules["skills"]["new-horizons:command"]["perks"].Vector())
				if(perk["id"].String() == "new-horizons:command.crisisCommand")
					perk["effect"]["status"].String() = "planned";
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		// Sage's old-positive writer control predates these independent clauses;
		// preserve the current spell catalog/levels but isolate absent opt-ins.
		for(const auto & [spell, field] : std::array<std::pair<const char *, const char *>, 5>{{
			{"core:implosion", "implosion"}, {"core:teleport", "ignoreInterveningBarriers"},
			{"core:dispel", "temporaryMagicalEffectsOnly"}, {"core:curse", "schoolRankDurations"},
			{"core:fireWall", "burnGroundedFlyers"}}})
			magicRules["spells"][spell].Struct().erase(field);
		magicRules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		isolateHistoricalAdventurePolicies(*map);
	}
	void prepare(int guildLevel = 1, bool book = true, bool historicalWriterProfile = false)
	{
		isolateFutureCrisisProfile = historicalWriterProfile;
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
			current->setSecSkillLevel(school("new-horizons:wisdom"), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
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
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.sage"});
		}
		if(learning)
		{
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.eagleEye"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.fieldStudy"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.sage"});
		}
		EXPECT_EQ(current->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage"), wisdom);
		EXPECT_EQ(newHorizonsSage::learningSelected(*current), learning);
	}
	void visit(CGameHandler & handler) { handler.heroVisitCastle(town, hero); }
};
}

TEST_F(NewHorizonsSageTest, LearningPersonallyLearnsHighestUndrawnBuiltSchoolSpellWithoutPublishingIt)
{
	prepare(4);
	isolatedCatalog();
	select(false, true);
	ASSERT_EQ(newHorizonsSage::selectSpell(*hero, *town), named("core:implosion"));
	const auto guildRows = town->spells;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:implosion")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:dispel")));
	EXPECT_EQ(town->spells, guildRows);
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	visit(handler);
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:dispel")));
}

TEST_F(NewHorizonsSageTest, WisdomRevealsGloballyAndOtherVisitorsLearnThePersistentExtra)
{
	prepare();
	isolatedCatalog();
	select(true, false);
	const auto ordinaryCounts = town->newHorizonsMageGuildVisibleSpells;
	const auto ordinarySchools = town->newHorizonsMageGuildVisibleSpellSchools;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[0], (std::vector<SpellID>{named("core:dispel")}));
	EXPECT_EQ(town->spellsAtLevel(1, true), 2);
	EXPECT_EQ(town->spells[0].back(), named("core:dispel"));
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:dispel")));
	EXPECT_EQ(town->newHorizonsMageGuildVisibleSpells, ordinaryCounts);
	EXPECT_EQ(town->newHorizonsMageGuildVisibleSpellSchools, ordinarySchools);
	const auto researchCount = town->spellResearchCounterDay;
	visit(handler);
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[0].size(), 1);
	auto * other = findHeroAt({7, 5, 0});
	ASSERT_NE(other, nullptr);
	town->setVisitingHero(nullptr);
	hero->setVisitedTown(nullptr, false);
	handler.heroVisitCastle(town, other);
	EXPECT_TRUE(other->spellbookContainsSpell(named("core:dispel")));
	EXPECT_EQ(town->spellResearchCounterDay, researchCount);
}

TEST_F(NewHorizonsSageTest, BothSagesComposeAfterOrdinaryLearningAndWisdomRevealWithoutDuplicateTeaching)
{
	prepare(4);
	isolatedCatalog();
	select(true, true);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[3], (std::vector<SpellID>{named("core:implosion")}));
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:implosion")));
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:dispel")));
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
	visit(handler);
	EXPECT_FALSE(hero->spellbookContainsSpell(named("core:haste")));
}

TEST_F(NewHorizonsSageTest, DifferentWisdomHoldersPublishDistinctExtrasButEachHeroTownVisitIsOnceOnly)
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
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[3], (std::vector<SpellID>{named("core:implosion")}));
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[0], (std::vector<SpellID>{named("core:dispel")}));
	EXPECT_TRUE(other->spellbookContainsSpell(named("core:implosion")));
	EXPECT_TRUE(other->spellbookContainsSpell(named("core:dispel")));
	handler.heroVisitCastle(town, other);
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[0], (std::vector<SpellID>{named("core:dispel")}));
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

TEST_F(NewHorizonsSageTest, CapturedSchoolLabelsMapBansBuiltLevelsAndHeroEligibilityRestrictCatalog)
{
	prepare(4);
	isolatedCatalog();
	select(true, true);
	map()->allowedSpells.erase(named("core:implosion"));
	EXPECT_EQ(newHorizonsSage::selectSpell(*hero, *town), named("core:dispel"));
	// Dispel and Haste now share the authored level-1 Sorcery bucket.
	town->newHorizonsMageGuildVisibleSpellSchools[0].clear();
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
	town->newHorizonsMageGuildVisibleSpellSchools[0] = {sorcery()};
	map()->allowedSpells.erase(named("core:dispel"));
	EXPECT_EQ(newHorizonsSage::selectSpell(*hero, *town), named("core:haste"));
	map()->allowedSpells.erase(named("core:haste"));
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
	map()->allowedSpells.insert(named("core:implosion"));
	hero->setSecSkillLevel(school("new-horizons:sorceryMagic"), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
	hero->setSecSkillLevel(school("new-horizons:sorceryMagic"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	setGuild(3);
	EXPECT_TRUE(newHorizonsSage::candidates(*hero, *town).empty());
}

TEST_F(NewHorizonsSageTest, SixthVisibleSpellDoesNotReplaceOrdinarySlotsAndWorldRoundtripPreservesRevealAndReceipt)
{
	prepare();
	for(const auto * skill : {"new-horizons:lightMagic", "new-horizons:shadowMagic", "new-horizons:natureMagic", "new-horizons:chaosMagic", "new-horizons:havocMagic"})
		hero->setSecSkillLevel(school(skill), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	select(true, false);
	ASSERT_EQ(town->newHorizonsMageGuildVisibleSpells[0], 5);
	const auto chosen = newHorizonsSage::wisdomReveal(*hero, *town);
	ASSERT_TRUE(chosen);
	const auto ordinary = town->spells[0];
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	ASSERT_EQ(town->spellsAtLevel(1, true), 6);
	EXPECT_EQ(std::vector<SpellID>(town->spells[0].begin(), town->spells[0].begin() + 5), ordinary);
	EXPECT_EQ(town->spells[0][5], *chosen);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedTown = restored.getTown(town->id);
	const auto * savedHero = restored.getHero(hero->id);
	ASSERT_NE(savedTown, nullptr);
	ASSERT_NE(savedHero, nullptr);
	EXPECT_EQ(savedTown->spellsAtLevel(1, true), 6);
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
	receipt.revealedSpell = named("core:dispel");
	CMemorySerializer current;
	current.oser & receipt;
	SetNewHorizonsSageGuildVisit restored;
	current.iser & restored;
	EXPECT_EQ(restored.hero, receipt.hero);
	EXPECT_EQ(restored.town, receipt.town);
	EXPECT_EQ(restored.revealedSpell, receipt.revealedSpell);
	handler.sendAndApply(receipt);
	EXPECT_THROW(handler.sendAndApply(receipt), std::runtime_error);
	EXPECT_EQ(town->newHorizonsSageRevealedSpells[0].size(), 1);
}

TEST_F(NewHorizonsSageTest, SaveGuardsRejectMeaningfulHeroTownAndOffMapReceiptsBeforeEveryPrefix)
{
	// An active Crisis profile itself is later-format metadata, even unselected.
	// Only this synthetic Sage predecessor-format control captures it as planned.
	prepare(1, true, true);
	isolatedCatalog();
	select(true, false);
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	const auto old = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_SAGE_GUILD_VISITS) - 1);
	const auto check = [&](auto & value, bool rejected)
	{
		CMemorySerializer writer;
		writer.oser.version = old;
		if(rejected)
		{
			EXPECT_THROW(writer.oser & value, std::runtime_error);
			EXPECT_TRUE(writer.extractBuffer().empty());
		}
		else
			EXPECT_NO_THROW(writer.oser & value);
	};
	check(*hero, false);
	check(*town, false);
	check(*map(), false);
	check(*gameState(), false);
	check(lobby, false);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	check(*hero, true);
	check(*town, true);
	check(*map(), true);
	check(*gameState(), true);
	check(lobby, true);
	SetNewHorizonsSageGuildVisit receipt;
	receipt.hero = hero->id;
	receipt.town = town->id;
	check(receipt, true);
	// Isolate the actual off-map hero receipt from the town's reveal guard.
	town->newHorizonsSageRevealedSpells = {};
	town->spells[0].pop_back();
	const auto pooled = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(hero->id));
	ASSERT_NE(pooled, nullptr);
	map()->addToHeroPool(pooled);
	ASSERT_EQ(map()->tryGetFromHeroPool(pooled->getHeroTypeID()), hero);
	check(*map(), true);
	check(*gameState(), true);
	check(lobby, true);
	EXPECT_NO_THROW(gameState()->validateNewHorizonsSageSerialization(true));
	// Represent a malformed current receipt without exposing a gameplay setter.
	auto & visits = const_cast<std::set<ObjectInstanceID> &>(hero->getNewHorizonsSageGuildVisits());
	visits.insert(ObjectInstanceID::NONE);
	CMemorySerializer malformed;
	EXPECT_THROW(malformed.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
	visits.erase(ObjectInstanceID::NONE);
	// A malformed revealed suffix rejects at the town and world prefixes too.
	town->newHorizonsSageRevealedSpells[0] = {named("core:haste")};
	CMemorySerializer malformedTown;
	EXPECT_THROW(malformedTown.oser & *town, std::runtime_error);
	EXPECT_TRUE(malformedTown.extractBuffer().empty());
	CMemorySerializer malformedMap;
	EXPECT_THROW(malformedMap.oser & *map(), std::runtime_error);
	EXPECT_TRUE(malformedMap.extractBuffer().empty());
	town->newHorizonsSageRevealedSpells = {};
}
