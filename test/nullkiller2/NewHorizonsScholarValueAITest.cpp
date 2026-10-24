/*
 * NewHorizonsScholarValueAITest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/CRandomGenerator.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/callback/Calendar.h"
#include "lib/callback/GameRandomizer.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/entities/hero/NewHorizonsLearningExchangeSpecialties.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapping/CMap.h"
#include "lib/filesystem/ResourcePath.h"
#include "lib/modding/CModHandler.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/CSpell.h"
#include "mock/GameHandlerTestServer.h"
#include "server/CGameHandler.h"
#include <array>

namespace
{
const PlayerColor PLAYER(0), OTHER(1);
const std::array<const char *, 6> SCHOOLS{"new-horizons:lightMagic", "new-horizons:natureMagic",
	"new-horizons:sorceryMagic", "new-horizons:havocMagic", "new-horizons:shadowMagic", "new-horizons:chaosMagic"};
SpellID named(const char * key) { return SpellID(SpellID::decode(key)); }
SecondarySkill skill(const char * key) { return SecondarySkill(SecondarySkill::decode(key)); }

template<typename T> std::vector<std::byte> bytes(T & value)
{
	CMemorySerializer serializer;
	serializer.oser & value;
	return serializer.extractBuffer();
}

class NewHorizonsScholarValueAITest : public NullkillerTest
{
protected:
	bool captured = true;
	bool allied = false;
	int bonus = 1;
	CGHeroInstance * sanya = nullptr;
	CGHeroInstance * geon = nullptr;
	CGHeroInstance * scholar = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("learningExchangeSpecialties");
		if(captured)
		{
			auto & table = rules["learningExchangeSpecialties"];
			table["version"].Integer() = 1;
			for(const auto key : {"ecumenicalScholar", "occultScholar"})
			{
				table[key]["learningPercentPerLevel"].Integer() = 5;
				table[key]["learningMaximumPercent"].Integer() = 100;
			}
			table["ecumenicalScholar"]["additionalSpellsTaught"].Integer() = bonus;
			table["occultScholar"]["additionalSpellsReceived"].Integer() = bonus;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		if(allied)
		{
			map->howManyTeams = 1;
			map->players[0].team = map->players[1].team = TeamID(0);
		}
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OTHER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:sanya")), PLAYER)
			.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("core:geon")), PLAYER)
			.hero({9, 5, 0}, HeroTypeID(HeroTypeID::decode("core:vidomina")), allied ? OTHER : PLAYER)
			.hero({20, 20, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), OTHER);
		startWithMap(std::move(builder));
		sanya = findHeroAt({5, 5, 0}); geon = findHeroAt({7, 5, 0}); scholar = findHeroAt({9, 5, 0});
		ASSERT_NE(sanya, nullptr); ASSERT_NE(geon, nullptr); ASSERT_NE(scholar, nullptr);
		ASSERT_TRUE(scholar->hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"));
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		for(auto * hero : {sanya, geon, scholar})
		{
			// Fixture proficiency isolates legal exchange valuation; no starting grant is implied.
			for(const auto school : SCHOOLS)
				hero->setSecSkillLevel(skill(school), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			if(!hero->hasSpellbook())
			{
				ASSERT_TRUE(handler->giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			}
			handler->changeSpells(hero, false, hero->getSpellsInSpellbook());
			hero->level = 1; // Equal levels isolate Scholar from the preserved Mentor reward.
		}
		revealMap(PLAYER);
		gateway = makeGateway(PLAYER);
	}
	int week() const
	{
		const auto calendar = gateway->cc->getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}
	void expectPureReward(const CGHeroInstance * first, const CGHeroInstance * second, float expected)
	{
		const auto world = gameState()->saveToMemory();
		const auto nativeRng = bytes(*handler->randomizer);
		const auto defaultRng = bytes(CRandomGenerator::getDefault());
		EXPECT_FLOAT_EQ(NK2AI::teachingMeetingReward(gateway->nullkiller.get(), first, second), expected);
		EXPECT_FLOAT_EQ(NK2AI::teachingMeetingReward(gateway->nullkiller.get(), first, second), expected);
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(bytes(*handler->randomizer), nativeRng);
		EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
	}
};
}

TEST_F(NewHorizonsScholarValueAITest, SanyaValuesTwoOutgoingAndOneIncomingBeforeActualMeeting)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// TinyMap initialization is day zero; this meeting occurs on ordinary day one.
	gameState()->day = 1;
	ASSERT_EQ(gateway->cc->getCalendar().getCurrentDay(), 1);
	ASSERT_EQ(week(), 0);
	const auto high = named("core:implosion"), middle = named("core:lightningBolt"), low = named("core:magicArrow");
	const auto incoming = named("core:dispel"), leftover = named("core:haste");
	handler->changeSpells(sanya, true, {high, middle, low});
	handler->changeSpells(scholar, true, {incoming, leftover});
	const auto plan = newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, week());
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->firstTeaches, (std::vector<SpellID>{high, middle}));
	EXPECT_EQ(plan->secondTeaches, (std::vector<SpellID>{incoming}));
	const auto expected = sanya->getSpellLevel(incoming.toSpell()) * 0.5f
		+ (scholar->getSpellLevel(high.toSpell()) + scholar->getSpellLevel(middle.toSpell())) * 0.25f;
	expectPureReward(sanya, scholar, expected);
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_TRUE(scholar->spellbookContainsSpell(high));
	EXPECT_TRUE(scholar->spellbookContainsSpell(middle));
	EXPECT_FALSE(scholar->spellbookContainsSpell(low));
	EXPECT_TRUE(sanya->spellbookContainsSpell(incoming));
	EXPECT_FALSE(sanya->spellbookContainsSpell(leftover));
	EXPECT_TRUE(sanya->hasNewHorizonsScholarMeeting(scholar->id, week()));
	EXPECT_TRUE(scholar->hasNewHorizonsScholarMeeting(sanya->id, week()));
	expectPureReward(sanya, scholar, 0);
	gameState()->day += gateway->cc->getCalendar().getDaysInWeek();
	ASSERT_EQ(gateway->cc->getCalendar().getCurrentDay(), 8);
	ASSERT_EQ(week(), 1);
	expectPureReward(sanya, scholar, sanya->getSpellLevel(leftover.toSpell()) * 0.5f
		+ scholar->getSpellLevel(low.toSpell()) * 0.25f);
}

TEST_F(NewHorizonsScholarValueAITest, GeonValuesTwoIncomingAndOneOutgoingAtUnchangedWeights)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), middle = named("core:lightningBolt"), low = named("core:magicArrow");
	const auto outgoing = named("core:dispel"), leftover = named("core:haste");
	handler->changeSpells(scholar, true, {high, middle, low});
	handler->changeSpells(geon, true, {outgoing, leftover});
	const auto plan = newHorizonsHeroes::prepareScholarExchange(*geon, *scholar, week());
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->firstTeaches, (std::vector<SpellID>{outgoing}));
	EXPECT_EQ(plan->secondTeaches, (std::vector<SpellID>{high, middle}));
	const auto expected = (geon->getSpellLevel(high.toSpell()) + geon->getSpellLevel(middle.toSpell())) * 0.5f
		+ scholar->getSpellLevel(outgoing.toSpell()) * 0.25f;
	expectPureReward(geon, scholar, expected);
	handler->heroExchange(geon->id, scholar->id);
	EXPECT_TRUE(geon->spellbookContainsSpell(high));
	EXPECT_TRUE(geon->spellbookContainsSpell(middle));
	EXPECT_FALSE(geon->spellbookContainsSpell(low));
	EXPECT_TRUE(scholar->spellbookContainsSpell(outgoing));
	EXPECT_FALSE(scholar->spellbookContainsSpell(leftover));
}

TEST_F(NewHorizonsScholarValueAITest, AbsentCapturedRidersKeepOneSpellEachDirectionAndOldWrapper)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), low = named("core:magicArrow"), incoming = named("core:dispel");
	handler->changeSpells(sanya, true, {high, low});
	handler->changeSpells(scholar, true, {incoming, named("core:haste")});
	const auto plan = newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, week());
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->firstTeaches, (std::vector<SpellID>{high}));
	EXPECT_EQ(plan->secondTeaches, (std::vector<SpellID>{incoming}));
	EXPECT_EQ(sanya->getNewHorizonsScholarSpellFor(*scholar), high);
	EXPECT_EQ(scholar->getNewHorizonsScholarSpellFor(*sanya), incoming);
	expectPureReward(sanya, scholar, sanya->getSpellLevel(incoming.toSpell()) * 0.5f
		+ scholar->getSpellLevel(high.toSpell()) * 0.25f);
}

TEST_F(NewHorizonsScholarValueAITest, ExplicitZeroBonusKeepsOrdinaryBudget)
{
	bonus = 0;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), low = named("core:magicArrow");
	handler->changeSpells(sanya, true, {high, low});
	const auto plan = newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, week());
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->firstTeaches, (std::vector<SpellID>{high}));
	EXPECT_TRUE(plan->secondTeaches.empty());
	expectPureReward(sanya, scholar, scholar->getSpellLevel(high.toSpell()) * 0.25f);
}

TEST_F(NewHorizonsScholarValueAITest, CoincidentBonusesCapAtTwoAndDoNotInventScholarPermission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->changeSpells(sanya, true, {named("core:implosion"), named("core:lightningBolt"), named("core:magicArrow")});
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*sanya, *geon), 2);
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*geon, *sanya), 1);
	EXPECT_FALSE(sanya->hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"));
	EXPECT_FALSE(geon->hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"));
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *geon, week()));
	expectPureReward(sanya, geon, 0);
}

TEST_F(NewHorizonsScholarValueAITest, SchoolScrollAndBookFailuresContributeNoIllegalSpellValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(handler->giveHeroNewScroll(sanya, named("core:magicArrow"), ArtifactPosition::MISC1));
	expectPureReward(sanya, scholar, 0); // A temporary scroll is not permanent teaching knowledge.
	handler->changeSpells(sanya, true, {named("core:implosion")});
	for(const auto school : SCHOOLS)
		scholar->setSecSkillLevel(skill(school), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	expectPureReward(sanya, scholar, 0);
	scholar->setSecSkillLevel(skill("new-horizons:sorceryMagic"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	handler->removeArtifact(ArtifactLocation(scholar->id, ArtifactPosition::SPELLBOOK));
	expectPureReward(sanya, scholar, 0);
}

TEST_F(NewHorizonsScholarValueAITest, PairReceiptUsesAbsoluteWeekRatherThanCurrentDay)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = named("core:magicArrow");
	handler->changeSpells(sanya, true, {spell});
	gameState()->day = 8;
	ASSERT_EQ(week(), 1);
	sanya->markNewHorizonsScholarMeeting(scholar->id, 1);
	scholar->markNewHorizonsScholarMeeting(sanya->id, 1);
	expectPureReward(sanya, scholar, 0);
	gameState()->day = 9;
	expectPureReward(sanya, scholar, 0);
	gameState()->day = 15;
	ASSERT_EQ(week(), 2);
	expectPureReward(sanya, scholar, scholar->getSpellLevel(spell.toSpell()) * 0.25f);
}

TEST_F(NewHorizonsScholarValueAITest, ZeroManaDoesNotDiscountPermanentLegalTeaching)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = named("core:magicArrow");
	handler->changeSpells(sanya, true, {spell});
	sanya->restoreSpellPointSnapshot(0, 0);
	scholar->restoreSpellPointSnapshot(0, 0);
	ASSERT_EQ(sanya->getManaAvailable(), 0);
	ASSERT_EQ(scholar->getManaAvailable(), 0);
	expectPureReward(sanya, scholar, scholar->getSpellLevel(spell.toSpell()) * 0.25f);
}

TEST_F(NewHorizonsScholarValueAITest, AlliedPartnerCanBenefitButOnlyOwnTravelerIsValued)
{
	allied = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = named("core:magicArrow");
	handler->changeSpells(scholar, true, {spell});
	expectPureReward(sanya, scholar, sanya->getSpellLevel(spell.toSpell()) * 0.5f);
	expectPureReward(scholar, sanya, 0);
	expectPureReward(sanya, sanya, 0);
	expectPureReward(nullptr, scholar, 0);
}

TEST_F(NewHorizonsScholarValueAITest, EnemyPartnerAndEmptyPairHaveNoTeachingValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectPureReward(sanya, scholar, 0);
	const auto * enemy = findHeroAt({20, 20, 0});
	ASSERT_NE(enemy, nullptr);
	handler->changeSpells(scholar, true, {named("core:magicArrow")});
	expectPureReward(scholar, enemy, 0);
}
