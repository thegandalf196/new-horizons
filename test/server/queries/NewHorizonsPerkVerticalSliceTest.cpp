/*
 * NewHorizonsPerkVerticalSliceTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/MapQueries.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
constexpr auto rampartHeroId = HeroTypeID(16); // Mephala, a Rampart Ranger.
constexpr auto christianHeroId = HeroTypeID(6); // Christian, a Castle Knight.
constexpr auto sylvanLuckId = "new-horizons:sylvanLuck";
constexpr auto sorceryMagicId = "new-horizons:sorceryMagic";
constexpr auto overchargerId = "new-horizons:sorceryMagic.overcharger";
constexpr auto disciplineId = "new-horizons:discipline";
constexpr auto inspirationalLeaderId = "new-horizons:discipline.inspirationalLeader";

class NewHorizonsPerkVerticalSliceTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS,
			combat["combat"]["heroCommands"]);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGame(HeroTypeID heroType = rampartHeroId)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, heroType, PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
	}

	static SecondarySkill skill(const char * id)
	{
		const int decoded = SecondarySkill::decode(id);
		EXPECT_GE(decoded, 0) << "unknown secondary skill " << id;
		return SecondarySkill(decoded);
	}

	static bool offerContains(const std::vector<newHorizonsHeroes::PerkOfferCandidate> & offer,
		const char * perkId)
	{
		return std::any_of(offer.begin(), offer.end(), [perkId](const auto & candidate)
		{
			return candidate.selection.perkId == perkId;
		});
	}
};
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ScoutingImmediatelyRevealsExpandedSightAndSurvivesSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	const auto logistics = skill("new-horizons:logistics");
	gameHandler.changeSecSkill(hero, logistics, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const int originalRadius = hero->getSightRadius();
	const auto expandedTile = hero->getSightCenter() + int3(originalRadius + 2, 0, 0);
	ASSERT_FALSE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
	const auto ranks = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
	bool selected = false;
	for(uint64_t seed = 0; seed < 1000 && !selected; ++seed)
	{
		const auto offer = hero->getPerkState().prepareOffer(ranks, seed);
		for(size_t index = 0; index < offer.size(); ++index)
		{
			if(offer[index].selection.perkId != "new-horizons:logistics.scouting")
				continue;
			gameHandler.levelUpHero(hero, offer, index, seed, false);
			selected = true;
			break;
		}
	}
	ASSERT_TRUE(selected);
	EXPECT_EQ(hero->getSightRadius(), originalRadius + 5);
	EXPECT_TRUE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	EXPECT_EQ(restored.getHero(hero->id)->getSightRadius(), originalRadius + 5);
	EXPECT_TRUE(restored.isVisibleFor(expandedTile, hero->getOwner()));
	gameHandler.changeSecSkill(hero, logistics, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSightRadius(), originalRadius);
	// Losing sight range does not erase terrain already explored.
	EXPECT_TRUE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ExperienceOfferChoiceActivatesEffectAndSurvivesSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getFactionID(), FactionID::RAMPART);

	const auto sylvanLuck = skill(sylvanLuckId);
	const auto sorceryMagic = skill(sorceryMagicId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::NONE);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameHandler.changeSecSkill(hero, sorceryMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	// CGameHandler consumes one global value for the hero-specific skill RNG and
	// the next one for the server-authored offer seed. Find a reproducible seed
	// whose real offer includes the active Overcharger candidate.
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt();
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed), overchargerId))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	const auto previousLevel = hero->level;
	const auto nextLevelExperience = LIBRARY->heroh->reqExp(previousLevel + 1);
	gameHandler.giveExperience(hero, nextLevelExperience - hero->exp);

	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	const auto perk = std::find_if(query->hlu.perks.begin(), query->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == overchargerId;
	});
	ASSERT_NE(perk, query->hlu.perks.end());
	const auto perkChoice = static_cast<int>(query->hlu.skills.size()
		+ std::distance(query->hlu.perks.begin(), perk));
	ASSERT_TRUE(query->isValidReply(perkChoice));
	ASSERT_TRUE(gameHandler.queryReply(query->queryID, perkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, previousLevel + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(sorceryMagicId, overchargerId));
	EXPECT_TRUE(hero->hasActivePerk(sorceryMagicId, overchargerId));
	const auto definition = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules,
		sorceryMagicId, overchargerId);
	ASSERT_TRUE(definition.has_value());
	EXPECT_EQ(definition->effect["status"].String(), "active");

	const auto modifiers = newHorizonsMagic::magicArrowOverchargeModifiers(hero);
	EXPECT_EQ(modifiers.maximumBonus, 1);
	EXPECT_EQ(modifiers.damagePercentTenths, 175);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(gameState()->getMagicRules(),
		SpellID(SpellID::MAGIC_ARROW), 150, modifiers), 6);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getFactionID(), FactionID::RAMPART);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sorceryMagic), MasteryLevel::BASIC);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(sorceryMagicId, overchargerId));
	EXPECT_TRUE(restoredHero->hasActivePerk(sorceryMagicId, overchargerId));

	const auto restoredModifiers = newHorizonsMagic::magicArrowOverchargeModifiers(restoredHero);
	EXPECT_EQ(restoredModifiers.maximumBonus, 1);
	EXPECT_EQ(restoredModifiers.damagePercentTenths, 175);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(restored.getMagicRules(),
		SpellID(SpellID::MAGIC_ARROW), 150, restoredModifiers), 6);
}

TEST_F(NewHorizonsPerkVerticalSliceTest, RampartFactionSkillProgressionAndPerkChoiceSurviveSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getFactionID(), FactionID::RAMPART);

	const auto sylvanLuck = skill(sylvanLuckId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());

	// Keep the level-up offer focused on the already-owned faction skill and on
	// the one active Sylvan Luck perk. This still goes through the normal
	// randomizer/query path; the other skills are only closed out so a test
	// cannot accidentally depend on a random unrelated skill or perk.
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
	{
		const SecondarySkill candidate(index);
		// Leave the two skills that can open their separate mastery dialog out of
		// this focused perk journey.
		if(candidate == SecondarySkill::ARTILLERY || candidate == SecondarySkill::LOGISTICS)
			continue;
		hero->setSecSkillLevel(candidate, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	}
	hero->setSecSkillLevel(sylvanLuck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt(); // hero-specific secondary/primary skill stream seed
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed),
			"new-horizons:sylvanLuck.elvenPrecision"))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	// The first real level-up presents the active faction perk. Select it via
	// CHeroLevelUpDialogQuery so validation, HeroPerkChosen replication, and the
	// query-resolution ordering are all exercised.
	const auto firstLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(firstLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto firstQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(firstQuery, nullptr);
	const auto firstPerk = std::find_if(firstQuery->hlu.perks.begin(), firstQuery->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == "new-horizons:sylvanLuck.elvenPrecision";
	});
	ASSERT_NE(firstPerk, firstQuery->hlu.perks.end());
	const auto firstPerkChoice = static_cast<int>(firstQuery->hlu.skills.size()
		+ std::distance(firstQuery->hlu.perks.begin(), firstPerk));
	ASSERT_TRUE(firstQuery->isValidReply(firstPerkChoice));
	ASSERT_TRUE(gameHandler.queryReply(firstQuery->queryID, firstPerkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, firstLevel + 1);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_TRUE(hero->hasActivePerk(sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));

	// On the following level-up the faction skill is offered as an upgrade even
	// though its legacy gain chance is zero. Selecting it proves progression is
	// tied to the owning faction rather than to a legacy class table entry.
	const auto secondLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(secondLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	const auto topAfterSecondLevel = gameHandler.queries->topQuery(hero->getOwner());
	ASSERT_NE(topAfterSecondLevel, nullptr) << "hero level=" << hero->level << " exp=" << hero->exp;
	auto secondQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(topAfterSecondLevel);
	ASSERT_NE(secondQuery, nullptr);
	const auto factionSkillChoice = std::find(secondQuery->hlu.skills.begin(), secondQuery->hlu.skills.end(), sylvanLuck);
	ASSERT_NE(factionSkillChoice, secondQuery->hlu.skills.end());
	const auto factionSkillIndex = static_cast<int>(std::distance(secondQuery->hlu.skills.begin(), factionSkillChoice));
	ASSERT_TRUE(secondQuery->isValidReply(factionSkillIndex));
	ASSERT_TRUE(gameHandler.queryReply(secondQuery->queryID, factionSkillIndex, hero->getOwner()));

	EXPECT_EQ(hero->level, secondLevel + 1);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::ADVANCED);
	EXPECT_TRUE(hero->hasActivePerk(sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getFactionID(), FactionID::RAMPART);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sylvanLuck), MasteryLevel::ADVANCED);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(
		sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));
	EXPECT_TRUE(restoredHero->hasActivePerk(
		sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ChristianCanChooseInspirationalLeaderAndKeepDisciplineMorale)
{
	startGame(christianHeroId);
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getFactionID(), FactionID::CASTLE);

	const auto discipline = skill(disciplineId);
	ASSERT_EQ(hero->getSecSkillLevel(discipline), MasteryLevel::BASIC);
	const auto moraleWithDiscipline = hero->moraleVal();
	EXPECT_GE(moraleWithDiscipline, 1);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};

	// Find a deterministic ordinary level-up seed whose server-authored
	// candidate list contains the one active Discipline perk.
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt();
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed),
			inspirationalLeaderId))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	const auto previousLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(previousLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	const auto perk = std::find_if(query->hlu.perks.begin(), query->hlu.perks.end(),
		[](const auto & candidate)
	{
		return candidate.selection.perkId == inspirationalLeaderId;
	});
	ASSERT_NE(perk, query->hlu.perks.end());
	const auto perkChoice = static_cast<int>(query->hlu.skills.size()
		+ std::distance(query->hlu.perks.begin(), perk));
	ASSERT_TRUE(query->isValidReply(perkChoice));
	ASSERT_TRUE(gameHandler.queryReply(query->queryID, perkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, previousLevel + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(disciplineId, inspirationalLeaderId));
	EXPECT_TRUE(hero->hasActivePerk(disciplineId, inspirationalLeaderId));
	EXPECT_EQ(hero->moraleVal(), moraleWithDiscipline);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getSecSkillLevel(discipline), MasteryLevel::BASIC);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(disciplineId, inspirationalLeaderId));
	EXPECT_TRUE(restoredHero->hasActivePerk(disciplineId, inspirationalLeaderId));
	EXPECT_EQ(restoredHero->moraleVal(), moraleWithDiscipline);
}
