/*
 * NewHorizonsScholarTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../server/CGameHandler.h"
#include "../battles/FullGameSnapshotTypes.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto SCHOLAR = "new-horizons:learning.scholar";
SpellID named(const char * id) { return SpellID(SpellID::decode(id)); }

class NewHorizonsScholarTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * first = nullptr;
	CGHeroInstance * second = nullptr;
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
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.hero({7, 5, 0}, HeroTypeID(1), PLAYER)
			.hero({9, 9, 0}, HeroTypeID(2), PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER);
		startWithMap(std::move(builder));
		first = findHeroAt({5, 5, 0});
		second = findHeroAt({7, 5, 0});
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
	}
	void configure(CGameHandler & handler, bool selected = true)
	{
		for(auto * hero : {first, second})
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			if(!hero->hasSpellbook())
				ASSERT_TRUE(handler.giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			handler.changeSpells(hero, false, hero->getSpellsInSpellbook());
		}
		if(selected)
			first->applyPerkSelection({LEARNING, SCHOLAR});
		EXPECT_EQ(first->hasActivePerk(LEARNING, SCHOLAR), selected);
	}
};
}

TEST_F(NewHorizonsScholarTest, ActualFieldMeetingTeachesAllEligibleSpellsInBothDirections)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto high = named("core:implosion");
	const auto low = named("core:magicArrow");
	const auto other = named("new-horizons:focusMagic");
	handler.changeSpells(first, true, {high, low});
	handler.changeSpells(second, true, {other});
	ASSERT_TRUE(second->canLearnSpell(high.toSpell()));
	ASSERT_TRUE(first->canLearnSpell(other.toSpell()));
	ASSERT_EQ(first->getNewHorizonsScholarSpellFor(*second), high);
	ASSERT_EQ(second->getNewHorizonsScholarSpellFor(*first), other);
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(high));
	EXPECT_TRUE(second->spellbookContainsSpell(low));
	EXPECT_TRUE(first->spellbookContainsSpell(other));
	EXPECT_FALSE(first->hasNewHorizonsScholarMeeting(second->id, 0));
	EXPECT_FALSE(second->hasNewHorizonsScholarMeeting(first->id, 0));
}

TEST_F(NewHorizonsScholarTest, SamePairCanTeachNewUnknownSpellsAgainDuringTheSameWeek)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto earlier = named("new-horizons:focusMagic");
	const auto later = named("core:magicArrow");
	ASSERT_TRUE(second->canLearnSpell(earlier.toSpell()));
	handler.changeSpells(first, true, {earlier});
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(earlier));
	handler.changeSpells(first, true, {later});
	EXPECT_TRUE(first->canExchangeNewHorizonsScholarWith(*second, 0));
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(later));
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(second->getNewHorizonsScholarWeek(), -1);
	gameState()->day = 8;
	EXPECT_FALSE(first->canExchangeNewHorizonsScholarWith(*second, 1));
}

TEST_F(NewHorizonsScholarTest, EmptyMeetingDoesNotSpendAndSchoolBookAndPerkRemainRequired)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler, false);
	const auto spell = named("core:implosion");
	handler.changeSpells(first, true, {spell});
	handler.heroExchange(first->id, second->id);
	EXPECT_FALSE(second->spellbookContainsSpell(spell));
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	first->applyPerkSelection({LEARNING, SCHOLAR});
	second->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	handler.heroExchange(first->id, second->id);
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	second->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	handler.removeArtifact(ArtifactLocation(second->id, ArtifactPosition::SPELLBOOK));
	handler.heroExchange(first->id, second->id);
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	ASSERT_TRUE(handler.giveHeroNewArtifact(second, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(spell));
}

TEST_F(NewHorizonsScholarTest, ActualTownMeetingAndCurrentSaveKeepPermanentBooksWithoutPairReceipt)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto spell = named("core:magicArrow");
	handler.changeSpells(first, true, {spell});
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	// This case isolates garrison teaching, not randomly generated Guild spells.
	for(auto & row : town->spells)
		row.clear();
	town->setGarrisonedHero(first);
	handler.heroVisitCastle(town, second);
	EXPECT_TRUE(second->spellbookContainsSpell(spell));
	EXPECT_FALSE(second->hasNewHorizonsScholarMeeting(first->id, 0));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedFirst = restored.getHero(first->id);
	const auto * savedSecond = restored.getHero(second->id);
	ASSERT_NE(savedFirst, nullptr);
	ASSERT_NE(savedSecond, nullptr);
	EXPECT_EQ(savedFirst->getSpellsInSpellbook(), first->getSpellsInSpellbook());
	EXPECT_EQ(savedSecond->getSpellsInSpellbook(), second->getSpellsInSpellbook());
	EXPECT_FALSE(savedFirst->hasNewHorizonsScholarMeeting(savedSecond->id, 0));
	EXPECT_FALSE(savedSecond->hasNewHorizonsScholarMeeting(savedFirst->id, 0));
	EXPECT_FALSE(savedFirst->canExchangeNewHorizonsScholarWith(*savedSecond, 0));
}

TEST_F(NewHorizonsScholarTest, EnemySelfAndScrollOnlySourcesNeverCreatePairUse)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto * enemy = findHeroAt({9, 9, 0});
	ASSERT_NE(enemy, nullptr);
	EXPECT_FALSE(first->canExchangeNewHorizonsScholarWith(*first, 0));
	EXPECT_FALSE(first->canExchangeNewHorizonsScholarWith(*enemy, 0));
	ASSERT_TRUE(handler.giveHeroNewScroll(second, named("core:magicArrow"), ArtifactPosition::MISC1));
	EXPECT_FALSE(second->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_FALSE(second->getNewHorizonsScholarSpellFor(*first));
	handler.heroExchange(first->id, second->id);
	EXPECT_FALSE(first->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(second->getNewHorizonsScholarWeek(), -1);
}
