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
		// Test Scholar's older-format admission independently of the later
		// Thant/Frailty replacements; retain Haste and all other captured rules.
		std::erase_if(heroRules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & spell) { return spell.String() == "new-horizons:reanimate"
				|| spell.String() == "new-horizons:frailty"; });
		heroRules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroRules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		isolateHistoricalAdventurePolicies(*map);
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

TEST_F(NewHorizonsScholarTest, ActualFieldMeetingTeachesOneHighestSpellInEachDirection)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto high = named("core:implosion");
	const auto low = named("core:magicArrow");
	const auto other = named("core:haste");
	handler.changeSpells(first, true, {high, low});
	handler.changeSpells(second, true, {other});
	ASSERT_TRUE(second->canLearnSpell(high.toSpell()));
	ASSERT_EQ(first->getNewHorizonsScholarSpellFor(*second), high);
	ASSERT_EQ(second->getNewHorizonsScholarSpellFor(*first), other);
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(high));
	EXPECT_FALSE(second->spellbookContainsSpell(low));
	EXPECT_TRUE(first->spellbookContainsSpell(other));
	EXPECT_TRUE(first->hasNewHorizonsScholarMeeting(second->id, 0));
	EXPECT_TRUE(second->hasNewHorizonsScholarMeeting(first->id, 0));
}

TEST_F(NewHorizonsScholarTest, StableScopedTieRepeatedPairAndNextWeek)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	const auto earlier = named("core:haste");
	const auto later = named("core:magicArrow");
	handler.changeSpells(first, true, {earlier, later});
	ASSERT_EQ(first->getSpellLevel(earlier.toSpell()), first->getSpellLevel(later.toSpell()));
	ASSERT_EQ(first->getNewHorizonsScholarSpellFor(*second), earlier);
	handler.heroExchange(first->id, second->id);
	EXPECT_TRUE(second->spellbookContainsSpell(earlier));
	EXPECT_FALSE(second->spellbookContainsSpell(later));
	second->applyPerkSelection({LEARNING, SCHOLAR});
	handler.heroExchange(second->id, first->id);
	EXPECT_FALSE(second->spellbookContainsSpell(later));
	gameState()->day = 8;
	handler.heroExchange(second->id, first->id);
	EXPECT_TRUE(second->spellbookContainsSpell(later));
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), 1);
	EXPECT_EQ(second->getNewHorizonsScholarWeek(), 1);
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

TEST_F(NewHorizonsScholarTest, ActualTownMeetingAndSavedPairReceiptPreventRepeat)
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
	EXPECT_TRUE(second->hasNewHorizonsScholarMeeting(first->id, 0));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedFirst = restored.getHero(first->id);
	const auto * savedSecond = restored.getHero(second->id);
	ASSERT_NE(savedFirst, nullptr);
	ASSERT_NE(savedSecond, nullptr);
	EXPECT_TRUE(savedFirst->hasNewHorizonsScholarMeeting(savedSecond->id, 0));
	EXPECT_TRUE(savedSecond->hasNewHorizonsScholarMeeting(savedFirst->id, 0));
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

TEST_F(NewHorizonsScholarTest, PacketAndHeroOlderWritersRejectBeforePayloadAndCurrentPacketRoundtrips)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	SetNewHorizonsScholarMeeting receipt;
	receipt.first = std::min(first->id, second->id);
	receipt.second = std::max(first->id, second->id);
	receipt.week = 0;
	CMemorySerializer current;
	current.oser & receipt;
	SetNewHorizonsScholarMeeting restored;
	current.iser & restored;
	EXPECT_EQ(restored.first, receipt.first);
	EXPECT_EQ(restored.second, receipt.second);
	EXPECT_EQ(restored.week, receipt.week);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_LEARNING_SCHOLAR) - 1);
	EXPECT_THROW(old.oser & receipt, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	first->markNewHorizonsScholarMeeting(second->id, 0);
	CMemorySerializer oldHero;
	oldHero.oser.version = old.oser.version;
	EXPECT_THROW(oldHero.oser & *first, std::runtime_error);
	EXPECT_TRUE(oldHero.extractBuffer().empty());
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsScholarState(first->id, -1, {second->id}));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsScholarState(first->id, 0, {first->id}));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsScholarState(first->id, 0, {second->id, second->id}));
	receipt.second = receipt.first;
	CMemorySerializer malformed;
	EXPECT_THROW(malformed.oser & receipt, std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}

TEST_F(NewHorizonsScholarTest, StalePacketRejectsAtomicallyBeforeEitherHeroIsChanged)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	configure(handler);
	handler.changeSpells(first, true, {named("core:magicArrow")});
	SetNewHorizonsScholarMeeting receipt;
	receipt.first = std::min(first->id, second->id);
	receipt.second = std::max(first->id, second->id);
	receipt.week = 1;
	EXPECT_THROW(handler.sendAndApply(receipt), std::runtime_error);
	EXPECT_EQ(first->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(second->getNewHorizonsScholarWeek(), -1);
	receipt.week = 0;
	handler.sendAndApply(receipt);
	EXPECT_THROW(handler.sendAndApply(receipt), std::runtime_error);
	EXPECT_EQ(first->getNewHorizonsScholarPartners(), (std::vector<ObjectInstanceID>{second->id}));
	EXPECT_EQ(second->getNewHorizonsScholarPartners(), (std::vector<ObjectInstanceID>{first->id}));
}

TEST_F(NewHorizonsScholarTest, WorldMapLobbyAndActualOffMapPoolRejectUnsupportedOrMalformedReceiptsBeforePrefixes)
{
	prepare();
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_LEARNING_SCHOLAR) - 1);
	const auto checkWriters = [&](ESerializationVersion version, bool rejected)
	{
		const auto check = [&](auto & value)
		{
			CMemorySerializer writer;
			writer.oser.version = version;
			if(rejected)
			{
				EXPECT_THROW(writer.oser & value, std::runtime_error);
				EXPECT_TRUE(writer.extractBuffer().empty());
			}
			else
			{
				EXPECT_NO_THROW(writer.oser & value);
				EXPECT_FALSE(writer.extractBuffer().empty());
			}
		};
		check(*first);
		check(*map());
		check(*gameState());
		check(lobby);
	};
	checkWriters(previous, false);
	first->markNewHorizonsScholarMeeting(second->id, 0);
	checkWriters(previous, true);
	EXPECT_NO_THROW(gameState()->validateNewHorizonsScholarSerialization(true));
	EXPECT_NO_THROW(lobby.validateNewHorizonsScholarSerialization(true));

	// Inspect the actual serialized off-map hero object, not a Tavern ID index.
	const auto pooled = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(first->id));
	ASSERT_NE(pooled, nullptr);
	map()->addToHeroPool(pooled);
	ASSERT_EQ(map()->tryGetFromHeroPool(pooled->getHeroTypeID()), first);
	ASSERT_EQ(map()->objects[first->id.getNum()], nullptr);
	checkWriters(previous, true);

	// A corrupt current receipt also fails before enclosing prefixes. The const
	// getter is deliberately bypassed only here to represent malformed state.
	auto & partners = const_cast<std::vector<ObjectInstanceID> &>(first->getNewHorizonsScholarPartners());
	partners.push_back(second->id);
	checkWriters(ESerializationVersion::CURRENT, true);
	partners.pop_back();
	EXPECT_NO_THROW(gameState()->validateNewHorizonsScholarSerialization(true));
	LobbyStartGame emptyLobby;
	CMemorySerializer empty;
	empty.oser.version = previous;
	EXPECT_NO_THROW(empty.oser & emptyLobby);
}
