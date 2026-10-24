/*
 * NewHorizonsLearningExchangeTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../../lib/entities/hero/NewHorizonsLearningExchangeSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include <array>
#include <limits>
namespace
{
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto SCHOLAR = "new-horizons:learning.scholar";
constexpr auto EAGLE_EYE = "new-horizons:learning.eagleEye";
const std::array<const char *, 6> SCHOOLS{"new-horizons:lightMagic", "new-horizons:natureMagic",
	"new-horizons:sorceryMagic", "new-horizons:havocMagic", "new-horizons:shadowMagic", "new-horizons:chaosMagic"};
SpellID named(const char * key) { return SpellID(SpellID::decode(key)); }
class ExchangeServer : public GameHandlerTestServer
{
public:
	explicit ExchangeServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(std::move(state), PlayerColor(0)) {}
	int receipts = 0;
	std::vector<size_t> noticeSpellCounts;
	void applyPack(CPackForClient & packet) override
	{
		if(dynamic_cast<SetNewHorizonsScholarMeeting *>(&packet))
			++receipts;
		if(const auto * notice = dynamic_cast<InfoWindow *>(&packet))
			noticeSpellCounts.push_back(notice->components.size());
		GameHandlerTestServer::applyPack(packet);
	}
};
class NewHorizonsLearningExchangeTest : public TinyMapGameTest
{
protected:
	bool captured = true;
	bool legacyMagic = false;
	int learningPercentPerLevel = 5;
	int learningMaximumPercent = 100;
	int spellBonus = 1;
	CGHeroInstance * sanya = nullptr;
	CGHeroInstance * geon = nullptr;
	CGHeroInstance * scholar = nullptr;
	std::unique_ptr<ExchangeServer> server;
	std::unique_ptr<CGameHandler> handler;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
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
				table[key]["learningPercentPerLevel"].Integer() = learningPercentPerLevel;
				table[key]["learningMaximumPercent"].Integer() = learningMaximumPercent;
			}
			table["ecumenicalScholar"]["additionalSpellsTaught"].Integer() = spellBonus;
			table["occultScholar"]["additionalSpellsReceived"].Integer() = spellBonus;
		}
		if(legacyMagic)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:sanya")), PlayerColor(0))
			.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("core:geon")), PlayerColor(0))
			.hero({9, 5, 0}, HeroTypeID(HeroTypeID::decode("core:vidomina")), PlayerColor(0))
			.hero({15, 15, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1));
		startWithMap(std::move(builder));
		sanya = findHeroAt({5, 5, 0});
		geon = findHeroAt({7, 5, 0});
		scholar = findHeroAt({9, 5, 0});
		ASSERT_NE(sanya, nullptr);
		ASSERT_NE(geon, nullptr);
		ASSERT_NE(scholar, nullptr);
		ASSERT_TRUE(sanya->hasActivePerk(LEARNING, EAGLE_EYE));
		ASSERT_TRUE(geon->hasActivePerk(LEARNING, EAGLE_EYE));
		ASSERT_FALSE(sanya->hasActivePerk(LEARNING, SCHOLAR));
		ASSERT_FALSE(geon->hasActivePerk(LEARNING, SCHOLAR));
		ASSERT_TRUE(scholar->hasActivePerk(LEARNING, SCHOLAR));
		server = std::make_unique<ExchangeServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		// Explicit fixture school proficiency isolates exchange selection; it
		// is not evidence of ordinary rank progression or a starting grant.
		for(auto * hero : {sanya, geon, scholar})
		{
			for(const auto school : SCHOOLS)
				hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(school)), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			if(!hero->hasSpellbook())
				ASSERT_TRUE(handler->giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			handler->changeSpells(hero, false, hero->getSpellsInSpellbook());
		}
	}
};
}
TEST_F(NewHorizonsLearningExchangeTest, SanyaActualPartnerScholarMeetingTeachesTwoButLearnsOnlyOne)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), middle = named("core:lightningBolt"), low = named("core:magicArrow");
	const auto firstTie = named("core:dispel"), otherTie = named("core:haste");
	handler->changeSpells(sanya, true, {high, middle, low});
	handler->changeSpells(scholar, true, {firstTie, otherTie});
	ASSERT_TRUE(scholar->canLearnSpell(high.toSpell()));
	const auto plan = newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, 0);
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->firstTeaches, (std::vector<SpellID>{high, middle}));
	EXPECT_EQ(plan->secondTeaches, (std::vector<SpellID>{firstTie}));
	EXPECT_EQ(sanya->getNewHorizonsScholarWeek(), -1);
	EXPECT_FALSE(scholar->spellbookContainsSpell(high));
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_TRUE(scholar->spellbookContainsSpell(high));
	EXPECT_TRUE(scholar->spellbookContainsSpell(middle));
	EXPECT_FALSE(scholar->spellbookContainsSpell(low));
	EXPECT_TRUE(sanya->spellbookContainsSpell(firstTie));
	EXPECT_FALSE(sanya->spellbookContainsSpell(otherTie));
	EXPECT_EQ(server->receipts, 1);
	EXPECT_EQ(server->noticeSpellCounts, (std::vector<size_t>{2, 1}));
}
TEST_F(NewHorizonsLearningExchangeTest, GeonActualPartnerScholarMeetingLearnsTwoButTeachesOnlyOne)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), middle = named("core:lightningBolt"), low = named("core:magicArrow");
	const auto firstTie = named("core:dispel"), otherTie = named("core:haste");
	handler->changeSpells(scholar, true, {high, middle, low});
	handler->changeSpells(geon, true, {firstTie, otherTie});
	handler->heroExchange(scholar->id, geon->id);
	EXPECT_TRUE(geon->spellbookContainsSpell(high));
	EXPECT_TRUE(geon->spellbookContainsSpell(middle));
	EXPECT_FALSE(geon->spellbookContainsSpell(low));
	EXPECT_TRUE(scholar->spellbookContainsSpell(firstTie));
	EXPECT_FALSE(scholar->spellbookContainsSpell(otherTie));
	EXPECT_EQ(server->receipts, 1);
	EXPECT_EQ(server->noticeSpellCounts, (std::vector<size_t>{2, 1}));
}
TEST_F(NewHorizonsLearningExchangeTest, CoincidentDirectionalBonusesUseMaximumButDoNotGrantScholar)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->changeSpells(sanya, true, {named("core:implosion"), named("core:magicArrow")});
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*sanya, *geon), 2);
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*geon, *sanya), 1);
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *geon, 0));
	handler->heroExchange(sanya->id, geon->id);
	EXPECT_FALSE(geon->spellbookContainsSpell(named("core:implosion")));
	EXPECT_EQ(server->receipts, 0);
	EXPECT_TRUE(server->noticeSpellCounts.empty());
}
TEST_F(NewHorizonsLearningExchangeTest, PairReceiptBlocksRepeatsAndAllowsNextWeekWithOnlyRemainingSpells)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto high = named("core:implosion"), middle = named("core:lightningBolt"), low = named("core:magicArrow");
	handler->changeSpells(sanya, true, {high, middle, low});
	handler->heroExchange(sanya->id, scholar->id);
	handler->heroExchange(scholar->id, sanya->id);
	EXPECT_FALSE(scholar->spellbookContainsSpell(low));
	EXPECT_EQ(server->receipts, 1);
	gameState()->day = 8;
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_TRUE(scholar->spellbookContainsSpell(low));
	EXPECT_EQ(server->receipts, 2);
	EXPECT_EQ(sanya->getNewHorizonsScholarWeek(), 1);
}
TEST_F(NewHorizonsLearningExchangeTest, EmptySchoolBookEnemyAndScrollRestrictionsKeepOpportunityUnspent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_EQ(server->receipts, 0);
	ASSERT_TRUE(handler->giveHeroNewScroll(sanya, named("core:magicArrow"), ArtifactPosition::MISC1));
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, 0));
	handler->changeSpells(sanya, true, {named("core:implosion")});
	for(const auto school : SCHOOLS)
		scholar->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(school)), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, 0));
	scholar->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	handler->removeArtifact(ArtifactLocation(scholar->id, ArtifactPosition::SPELLBOOK));
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, 0));
	const auto * enemy = findHeroAt({15, 15, 0});
	ASSERT_NE(enemy, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *enemy, 0));
	EXPECT_FALSE(newHorizonsHeroes::prepareScholarExchange(*sanya, *sanya, 0));
	EXPECT_EQ(sanya->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(server->receipts, 0);
}
TEST_F(NewHorizonsLearningExchangeTest, AbsentCapturedPolicyKeepsOrdinaryMeetingAndExperience)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	sanya->level = geon->level = 20;
	EXPECT_FALSE(newHorizonsHeroes::learningExchangeSpecialtyRules(sanya));
	EXPECT_EQ(sanya->calculateXp(1000), 1100);
	EXPECT_EQ(geon->calculateXp(1000), 1100);
	handler->changeSpells(sanya, true, {named("core:implosion"), named("core:magicArrow")});
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_TRUE(scholar->spellbookContainsSpell(named("core:implosion")));
	EXPECT_FALSE(scholar->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_TRUE(server->noticeSpellCounts.empty());
}
TEST_F(NewHorizonsLearningExchangeTest, CoreExperienceLevelFloorsCapsAndOtherModifiersStaySeparate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * hero : {sanya, geon})
	{
		hero->level = 0;
		EXPECT_EQ(hero->calculateXp(1000), 1100);
		hero->level = 1;
		EXPECT_EQ(hero->calculateXp(1000), 1105);
		hero->level = 10;
		EXPECT_EQ(hero->calculateXp(19), 20); // ordinary20 + floor(core1*50%).
		hero->level = 20;
		EXPECT_EQ(hero->calculateXp(1000), 1200);
		EXPECT_EQ(hero->calculateXp(1000, 50), 1700); // Only Learning's100 receives the extra100.
		EXPECT_EQ(hero->calculateXp(19), 21);
		EXPECT_EQ(hero->calculateXp(0), 0);
		EXPECT_EQ(hero->calculateXp(-1000), -1100);
		hero->level = std::numeric_limits<uint32_t>::max();
		EXPECT_EQ(hero->calculateXp(1000), 1200);
		EXPECT_EQ(hero->calculateXp(std::numeric_limits<TExpType>::max()), std::numeric_limits<TExpType>::max());
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(hero->calculateXp(1000), 1000);
	}
	EXPECT_FALSE(newHorizonsHeroes::learningExchangeSpecialtyRules(scholar));
	EXPECT_EQ(scholar->calculateXp(1000), 1100);
}
TEST_F(NewHorizonsLearningExchangeTest, CapturedPresentationStatesDirectionPartnerRequirementAndNoFreePerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto outgoing = sanya->getSpecialtyPresentation();
	const auto incoming = geon->getSpecialtyPresentation();
	EXPECT_EQ(outgoing.name, "Ecumenical Scholar");
	EXPECT_EQ(incoming.name, "Occult Scholar");
	EXPECT_NE(outgoing.description.find("teaches up to 1"), std::string::npos);
	EXPECT_NE(incoming.description.find("learns up to 1"), std::string::npos);
	EXPECT_NE(outgoing.description.find("Either participant must have active Scholar"), std::string::npos);
	EXPECT_NE(incoming.description.find("No perk is granted"), std::string::npos);
}
TEST_F(NewHorizonsLearningExchangeTest, LegacyMagicKeepsCoreExperienceButDisablesDirectedBonus)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	sanya->level = geon->level = 20;
	EXPECT_EQ(sanya->calculateXp(1000), 1200);
	EXPECT_EQ(geon->calculateXp(1000), 1200);
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*sanya, *geon), 1);
	EXPECT_NE(sanya->getSpecialtyPresentation().description.find("inactive under this game's legacy magic rules"), std::string::npos);
}

TEST_F(NewHorizonsLearningExchangeTest, ExplicitZeroKeepsCaptureButOrdinaryExperienceAndMeetingOutput)
{
	learningPercentPerLevel = learningMaximumPercent = spellBonus = 0;
	ASSERT_NO_FATAL_FAILURE(prepare());
	sanya->level = geon->level = 20;
	ASSERT_TRUE(newHorizonsHeroes::learningExchangeSpecialtyRules(sanya));
	EXPECT_EQ(sanya->calculateXp(1000), 1100);
	EXPECT_EQ(geon->calculateXp(1000), 1100);
	EXPECT_EQ(newHorizonsHeroes::scholarExchangeSpellBudget(*sanya, *geon), 1);
	handler->changeSpells(sanya, true, {named("core:implosion"), named("core:magicArrow")});
	handler->heroExchange(sanya->id, scholar->id);
	EXPECT_TRUE(scholar->spellbookContainsSpell(named("core:implosion")));
	EXPECT_FALSE(scholar->spellbookContainsSpell(named("core:magicArrow")));
	EXPECT_TRUE(server->noticeSpellCounts.empty());
	EXPECT_EQ(sanya->getSpecialtyPresentation().name, "Ecumenical Scholar");
}
TEST_F(NewHorizonsLearningExchangeTest, EachLearningRankUsesOnlyItsOwnCoreContribution)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * hero : {sanya, geon})
	{
		hero->level = 20;
		for(const auto rank : {MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), rank, ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(hero->calculateXp(1000), 1000 + 200 * rank);
			EXPECT_EQ(hero->calculateXp(1000, 25), 1250 + 200 * rank);
		}
	}
}

TEST_F(NewHorizonsLearningExchangeTest, MaximumConfiguredCoreScalingClipsBeforeNarrowingHugeLevel)
{
	learningPercentPerLevel = 1000;
	learningMaximumPercent = 200;
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * hero : {sanya, geon})
	{
		hero->level = std::numeric_limits<uint32_t>::max();
		EXPECT_EQ(hero->calculateXp(1000), 1300);
		EXPECT_EQ(hero->calculateXp(std::numeric_limits<TExpType>::max()), std::numeric_limits<TExpType>::max());
	}
}

TEST_F(NewHorizonsLearningExchangeTest, BoundedQueriesRejectMalformedEitherSideBeforeSpendingReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->changeSpells(sanya, true, {named("core:implosion"), named("core:magicArrow")});
	EXPECT_TRUE(sanya->getNewHorizonsScholarSpellsFor(*scholar, 0).empty());
	EXPECT_THROW(sanya->getNewHorizonsScholarSpellsFor(*scholar, 3), std::invalid_argument);
	const auto sanyaBook = sanya->getSpellsInSpellbook();
	const auto partnerBook = scholar->getSpellsInSpellbook();
	// Represent corrupted captured data on a non-specialist partner: strict
	// present-table validation still precedes the caller's first state mutation.
	auto & corrupt = const_cast<JsonNode &>(scholar->getPrimaryGrowthRules());
	corrupt["learningExchangeSpecialties"]["occultScholar"]["additionalSpellsReceived"].Integer() = 2;
	EXPECT_THROW(newHorizonsHeroes::prepareScholarExchange(*sanya, *scholar, 0), std::runtime_error);
	EXPECT_EQ(sanya->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(scholar->getNewHorizonsScholarWeek(), -1);
	EXPECT_EQ(sanya->getSpellsInSpellbook(), sanyaBook);
	EXPECT_EQ(scholar->getSpellsInSpellbook(), partnerBook);
	EXPECT_EQ(server->receipts, 0);
}
