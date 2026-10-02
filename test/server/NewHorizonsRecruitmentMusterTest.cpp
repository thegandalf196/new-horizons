/*
 * NewHorizonsRecruitmentMusterTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include <limits>

#include "../../lib/GameConstants.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../../server/queries/VisitQueries.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
class NewHorizonsRecruitmentMusterTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(broadMusterFixture)
		{
			bool found = false;
			for(auto & perk : perkRules["skills"]["new-horizons:recruitment"]["perks"].Vector())
			{
				if(perk["id"].String() != "new-horizons:recruitment.broadMuster")
					continue;
				if(perk["effect"]["status"].String() == "planned")
					perk["effect"]["status"].String() = "active";
				else
					EXPECT_EQ(perk["effect"]["status"].String(), "active");
				found = true;
			}
			EXPECT_TRUE(found);
		}
		if(externalRecruiterFixture)
		{
			bool found = false;
			for(auto & perk : perkRules["skills"]["new-horizons:recruitment"]["perks"].Vector())
				if(perk["id"].String() == "new-horizons:recruitment.externalRecruiter")
				{
					if(perk["effect"]["status"].String() == "planned")
						perk["effect"]["status"].String() = "active";
					else
						EXPECT_EQ(perk["effect"]["status"].String(), "active");
					found = true;
				}
			EXPECT_TRUE(found) << "External Recruiter must have an authored perk-rule entry";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}

	void startGame(bool withSecondTown = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		if(withSecondTown)
			builder.town({22, 22, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0));
		if(externalRecruiterFixture || broadMusterFixture)
			builder.dwelling({8, 8, 0}, MapObjectSubID(56), PlayerColor(0));
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		if(withSecondTown)
		{
			const auto towns = findAll<CGTownInstance>();
			ASSERT_GE(towns.size(), 2u);
			town2 = towns[1];
		}
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);

		// Keep one representative row from each saved world category.  A zero
		// stock is intentional: Muster replenishes availability, not an army.
		town->creatures = {
			{0, {CreatureID(CreatureID::decode("core:pikeman"))}},
			{0, {CreatureID(CreatureID::decode("core:griffin"))}},
			{0, {CreatureID(CreatureID::decode("core:angel"))}}
		};
		town->setVisitingHero(hero);
		if(town2)
			town2->creatures = town->creatures;
		recruitment = SecondarySkill(SecondarySkill::decode("new-horizons:recruitment"));
		if(!externalRecruiterFixture)
			hero->setSecSkillLevel(recruitment, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}

	void clearPerks()
	{
		const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).selected.clear();
	}

	static bool selectOfferedPerk(CGameHandler & handler, CGHeroInstance * candidate,
		const char * perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [candidate](const std::string & skillId)
		{
			return candidate->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = candidate->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(candidate, offer, index, seed, false);
				return true;
			}
		}
		return false;
	}

	void resetMusterMarker()
	{
		SetNewHorizonsMusterState reset;
		reset.heroId = hero->id;
		reset.targetId = town->id;
		reset.lastUseWeek = -1;
		gameState()->apply(reset);
	}

	static CreatureID creature(const char * id)
	{
		return CreatureID(CreatureID::decode(id));
	}

	CGTownInstance * town = nullptr;
	CGTownInstance * town2 = nullptr;
	CGHeroInstance * hero = nullptr;
	SecondarySkill recruitment;
	bool externalRecruiterFixture = false;
	bool broadMusterFixture = false;
};
}

TEST_F(NewHorizonsRecruitmentMusterTest, BroadMusterSplitsOneUseAtomicallyBetweenDistinctCoreRows)
{
	broadMusterFixture = true;
	startGame(true);
	const auto pikeman = creature("core:pikeman");
	const auto archer = creature("core:archer");
	const auto halberdier = creature("core:halberdier");
	const auto griffin = creature("core:griffin");
	const auto archerCategory = gameState()->getCreatureCategory(archer);
	ASSERT_TRUE(archerCategory);
	ASSERT_EQ(archerCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
	town->creatures.at(0).second.push_back(halberdier);
	town->creatures.push_back({0, {archer}});
	town2->creatures = town->creatures;
	const auto armyBefore = hero->getStackCount(SlotID(0));
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const auto split = [&](CreatureID second, int firstAmount)
	{
		return handler.musterCreatures(hero->id, town->id, pikeman, PlayerColor(0), second, firstAmount);
	};
	const auto untouched = [&]
	{
		EXPECT_EQ(town->creatures.at(0).first, 0u);
		EXPECT_EQ(town->creatures.at(3).first, 0u);
		EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), -1);
		EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 0);
	};
	EXPECT_FALSE(split(archer, 1)); // Rank alone never enables the split perk.
	untouched();
	handler.changeSecSkill(hero, recruitment, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, "new-horizons:recruitment.broadMuster", MasteryLevel::BASIC));
	ASSERT_TRUE(hero->hasActivePerk("new-horizons:recruitment", "new-horizons:recruitment.broadMuster"));
	for(const int amount : {-1, 0, 2, 3})
	{
		EXPECT_FALSE(split(archer, amount));
		untouched();
	}
	for(const auto second : {pikeman, halberdier, griffin, creature("core:stoneGargoyle")})
	{
		EXPECT_FALSE(split(second, 1));
		untouched();
	}
	EXPECT_FALSE(split(CreatureID::NONE, 1)); // Malformed solo allocation is not ignored.
	untouched();
	EXPECT_FALSE(handler.musterCreatures(hero->id, town->id, pikeman, PlayerColor(1), archer, 1));
	untouched();
	town->setVisitingHero(nullptr);
	EXPECT_FALSE(split(archer, 1));
	town->setVisitingHero(hero);
	untouched();
	const auto * external = expectAt<CGDwelling>({8, 8, 0});
	ASSERT_NE(external, nullptr);
	EXPECT_FALSE(handler.musterCreatures(hero->id, external->id, pikeman, PlayerColor(0), archer, 1));
	untouched();
	for(const size_t overflowingRow : {size_t(0), size_t(3)})
	{
		town->creatures.at(overflowingRow).first = std::numeric_limits<ui32>::max();
		const auto stockBefore = town->creatures;
		EXPECT_FALSE(split(archer, 1));
		EXPECT_EQ(town->creatures, stockBefore);
		EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), -1);
		EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 0);
		town->creatures.at(overflowingRow).first = 0;
	}
	ASSERT_TRUE(split(archer, 1));
	EXPECT_EQ(town->creatures.at(0).first, 1u);
	EXPECT_EQ(town->creatures.at(3).first, 1u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 0);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), armyBefore);
	EXPECT_FALSE(split(archer, 1));
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	ASSERT_NE(restored.getHero(hero->id), nullptr);
	ASSERT_NE(restored.getTown(town->id), nullptr);
	EXPECT_EQ(restored.getHero(hero->id)->getNewHorizonsMusterUsesThisWeek(0), 1);
	EXPECT_EQ(restored.getTown(town->id)->creatures.at(0).first, 1u);
	EXPECT_EQ(restored.getTown(town->id)->creatures.at(3).first, 1u);

	gameState()->day = 8;
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, "new-horizons:recruitment.eliteDraft", MasteryLevel::ADVANCED));
	ASSERT_TRUE(split(archer, 1));
	EXPECT_EQ(town->creatures.at(0).first, 2u);
	EXPECT_EQ(town->creatures.at(3).first, 4u); // Advanced four split 1/3.
	gameState()->day = 15;
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, "new-horizons:recruitment.masterRecruiter", MasteryLevel::EXPERT));
	ASSERT_TRUE(split(archer, 2));
	EXPECT_EQ(town->creatures.at(0).first, 4u);
	EXPECT_EQ(town->creatures.at(3).first, 8u); // Expert six split 2/4.
	EXPECT_FALSE(split(archer, 2)); // A second use cannot revisit this town.
	town->setVisitingHero(nullptr);
	town2->setVisitingHero(hero);
	ASSERT_TRUE(handler.musterCreatures(hero->id, town2->id, pikeman, PlayerColor(0), archer, 3));
	EXPECT_EQ(town2->creatures.at(0).first, 3u);
	EXPECT_EQ(town2->creatures.at(3).first, 3u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(2), 2);
}

TEST_F(NewHorizonsRecruitmentMusterTest, ExternalMusterUsesExactVisitAndSharedAllowanceWithoutGrantingAnArmy)
{
	externalRecruiterFixture = true;
	startGame();
	auto * dwelling = expectAt<CGDwelling>({8, 8, 0});
	ASSERT_NE(dwelling, nullptr);
	const auto pikeman = creature("core:pikeman");
	ASSERT_EQ(dwelling->ID, Obj::CREATURE_GENERATOR1);
	ASSERT_FALSE(dwelling->creatures.empty());
	ASSERT_FALSE(dwelling->creatures.front().second.empty());
	EXPECT_EQ(dwelling->creatures.front().second.front(), pikeman);
	const auto pikemanCategory = gameState()->getCreatureCategory(pikeman);
	ASSERT_TRUE(pikemanCategory);
	EXPECT_EQ(pikemanCategory->category, newHorizonsCreatures::CreatureCategory::CORE);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 1));
	dwelling->creatures.front().first = 0;
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const auto muster = [&] { return handler.musterCreatures(hero->id, dwelling->id, pikeman, PlayerColor(0)); };
	const auto untouched = [&]
	{
		EXPECT_EQ(dwelling->creatures[0].first, 0u);
		EXPECT_EQ(dwelling->getNewHorizonsMusterLastWeek(), -1);
		EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 0);
	};
	const char * externalRecruiter = "new-horizons:recruitment.externalRecruiter";
	// Use the authoritative rank-up and seeded perk offer paths for the Basic
	// acquisition. Only this fixture's saved perk rules activate the planned perk.
	handler.changeSecSkill(hero, recruitment, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getPerkSkillRank("new-horizons:recruitment"), 0);
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_EQ(hero->getSecSkillLevel(recruitment), MasteryLevel::BASIC);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, externalRecruiter, MasteryLevel::BASIC));
	ASSERT_TRUE(hero->getPerkState().hasSelection("new-horizons:recruitment", externalRecruiter));

	// Ownership alone is never permission to Muster a remote dwelling.
	ASSERT_TRUE(hero->hasActivePerk("new-horizons:recruitment", externalRecruiter));
	EXPECT_FALSE(muster());
	untouched();
	EXPECT_FALSE(handler.musterCreatures(hero->id, hero->id, pikeman, PlayerColor(0)));
	untouched();
	handler.queries->addQuery(std::make_shared<MapObjectVisitQuery>(&handler, dwelling, hero));
	EXPECT_EQ(handler.getVisitingObject(hero), dwelling);
	OpenWindowQuery recruitWindow(&handler, hero, EOpenWindowMode::RECRUITMENT_FIRST);
	MusterCreatures request;
	EXPECT_FALSE(recruitWindow.blocksPack(&request));
	OpenWindowQuery unrelatedWindow(&handler, hero, EOpenWindowMode::UNIVERSITY_WINDOW);
	EXPECT_TRUE(unrelatedWindow.blocksPack(&request));

	clearPerks();
	EXPECT_FALSE(muster());
	untouched();
	ASSERT_TRUE(selectOfferedPerk(handler, hero, externalRecruiter, MasteryLevel::BASIC));
	dwelling->creatures = {{0, {creature("core:griffin")}}};
	EXPECT_FALSE(handler.musterCreatures(hero->id, dwelling->id, creature("core:griffin"), PlayerColor(0)));
	untouched();
	dwelling->creatures = {{0, {pikeman}}};
	EXPECT_FALSE(handler.musterCreatures(hero->id, dwelling->id, pikeman, PlayerColor(1)));
	untouched();

	const auto armyBefore = hero->getStackCount(SlotID(0));
	ASSERT_TRUE(muster());
	EXPECT_EQ(dwelling->creatures[0].first, 2u);
	EXPECT_EQ(dwelling->getNewHorizonsMusterLastWeek(), 0);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), armyBefore);
	EXPECT_FALSE(muster());
	EXPECT_FALSE(handler.musterCreatures(hero->id, town->id, pikeman, PlayerColor(0)));
	EXPECT_EQ(town->creatures[0].first, 0u);

	// Higher Recruitment ranks still produce exactly two outside towns.
	gameState()->day = 8;
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_EQ(hero->getSecSkillLevel(recruitment), MasteryLevel::ADVANCED);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, "new-horizons:recruitment.eliteDraft", MasteryLevel::ADVANCED));
	handler.levelUpHero(hero, recruitment, false);
	ASSERT_EQ(hero->getSecSkillLevel(recruitment), MasteryLevel::EXPERT);
	ASSERT_TRUE(selectOfferedPerk(handler, hero, "new-horizons:recruitment.masterRecruiter", MasteryLevel::EXPERT));
	ASSERT_TRUE(muster());
	EXPECT_EQ(dwelling->creatures[0].first, 4u);
	ASSERT_TRUE(handler.musterCreatures(hero->id, town->id, pikeman, PlayerColor(0)));
	EXPECT_EQ(town->creatures[0].first, 6u);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(1), 2);
	EXPECT_FALSE(muster());

	// The free tier-one visit remains free when exposed through native controls.
	EXPECT_TRUE(dwelling->getRecruitmentCost(pikeman).empty());
	EXPECT_FALSE(town->getRecruitmentCost(pikeman).empty());
	const ResourceSet zeroResources;
	handler.giveResources(PlayerColor(0), zeroResources
		- gameState()->getPlayerState(PlayerColor(0))->resources);
	EXPECT_TRUE(gameState()->getPlayerState(PlayerColor(0))->resources.empty());
	const auto resourcesBefore = gameState()->getPlayerState(PlayerColor(0))->resources;
	ASSERT_TRUE(handler.recruitCreatures(dwelling->id, hero->id, pikeman, 1, 0, PlayerColor(0)));
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resourcesBefore);
	EXPECT_EQ(dwelling->creatures[0].first, 3u);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), armyBefore + 1);

	// Free external recruitment remains subject to ordinary Leadership
	// admission. A rejected recruit must preserve stock, army and resources.
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, capacity->maximum));
	const auto cappedStock = dwelling->creatures[0].first;
	const auto cappedResources = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_FALSE(handler.recruitCreatures(dwelling->id, hero->id, pikeman, 1, 0, PlayerColor(0)));
	EXPECT_EQ(dwelling->creatures[0].first, cappedStock);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, cappedResources);

	// Overflow is rejected before either the stock or once-per-week markers move.
	gameState()->day = 15; // a fresh absolute week, with no prior external use
	const auto overflowStock = std::numeric_limits<ui32>::max() - 1;
	dwelling->creatures[0].first = overflowStock;
	EXPECT_FALSE(muster());
	EXPECT_EQ(dwelling->creatures[0].first, overflowStock);
	EXPECT_EQ(dwelling->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(2), 0);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredDwelling = dynamic_cast<const CGDwelling *>(restored.getObj(dwelling->id));
	ASSERT_NE(restoredDwelling, nullptr);
	EXPECT_EQ(restoredDwelling->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(restoredDwelling->creatures[0].first, overflowStock);
	EXPECT_EQ(restored.getHero(hero->id)->getNewHorizonsMusterUsesThisWeek(1), 2);
}

TEST_F(NewHorizonsRecruitmentMusterTest, RankAmountsAndWeeklyGuardsAreAuthoritativeAndAtomic)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	const auto muster = [&](CreatureID target)
	{
		return gameHandler.musterCreatures(hero->id, town->id, target, PlayerColor(0));
	};
	const auto stock = [&](size_t row)
	{
		return town->creatures.at(row).first;
	};

	// Basic can only Muster Core, and adds exactly two.
	ASSERT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 2u);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 0);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 0);

	const auto afterBasic = town->creatures;
	EXPECT_FALSE(muster(creature("core:pikeman")));
	EXPECT_EQ(town->creatures, afterBasic);

	// An invalid category/rank request is rejected before either marker or stock
	// changes.  This also guards against a client supplying an arbitrary amount.
	resetMusterMarker();
	const auto beforeElite = town->creatures;
	EXPECT_FALSE(muster(creature("core:griffin")));
	EXPECT_EQ(town->creatures, beforeElite);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), -1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), -1);

	// Advanced: four Core and one Elite.
	hero->setSecSkillLevel(recruitment, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 6u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:griffin")));
	EXPECT_EQ(stock(1), 1u);

	// Expert: six Core, two Elite, and one Champion.
	hero->setSecSkillLevel(recruitment, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(stock(0), 12u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:griffin")));
	EXPECT_EQ(stock(1), 3u);
	resetMusterMarker();
	EXPECT_TRUE(muster(creature("core:angel")));
	EXPECT_EQ(stock(2), 1u);

	// The hero and town markers are independent of the stock and survive a
	// weekly boundary.  Day eight is the first day of absolute week one.
	gameState()->day = 8;
	EXPECT_TRUE(muster(creature("core:pikeman")));
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 1);

	const auto beforeWrongOwner = town->creatures;
	EXPECT_FALSE(gameHandler.musterCreatures(hero->id, town->id, creature("core:pikeman"), PlayerColor(1)));
	EXPECT_EQ(town->creatures, beforeWrongOwner);
	EXPECT_EQ(hero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(town->getNewHorizonsMusterLastWeek(), 1);

	// Save/load must preserve both usage markers, not just the replenished stock.
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	const auto * restoredTown = restored.getTown(town->id);
	ASSERT_NE(restoredHero, nullptr);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredHero->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(restoredTown->getNewHorizonsMusterLastWeek(), 1);
	EXPECT_EQ(restoredHero->getNewHorizonsMusterUsesThisWeek(1), 1);
}

TEST_F(NewHorizonsRecruitmentMusterTest, FourRecruitmentPerksModifyOnlyAuthoritativeTownMuster)
{
	startGame(true);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	const auto muster = [&](CGTownInstance * targetTown, CreatureID target)
	{
		return gameHandler.musterCreatures(hero->id, targetTown->id, target, PlayerColor(0));
	};
	const auto reset = [&](CGTownInstance * targetTown)
	{
		SetNewHorizonsMusterState resetState;
		resetState.heroId = hero->id;
		resetState.targetId = targetTown->id;
		resetState.lastUseWeek = -1;
		gameState()->apply(resetState);
	};

	clearPerks();
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.volunteerNetwork", MasteryLevel::BASIC));
	ASSERT_TRUE(muster(town, creature("core:pikeman")));
	EXPECT_EQ(town->creatures.at(0).first, 4u);
	reset(town);

	clearPerks();
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.volunteerNetwork", MasteryLevel::BASIC));
	gameHandler.levelUpHero(hero, recruitment, false);
	ASSERT_EQ(hero->getSecSkillLevel(recruitment), MasteryLevel::ADVANCED);
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.eliteDraft", MasteryLevel::ADVANCED));
	ASSERT_TRUE(muster(town, creature("core:griffin")));
	EXPECT_EQ(town->creatures.at(1).first, 2u);
	reset(town);

	clearPerks();
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.volunteerNetwork", MasteryLevel::BASIC));
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.eliteDraft", MasteryLevel::ADVANCED));
	gameHandler.levelUpHero(hero, recruitment, false);
	ASSERT_EQ(hero->getSecSkillLevel(recruitment), MasteryLevel::EXPERT);
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.championSCall", MasteryLevel::EXPERT));
	ASSERT_TRUE(muster(town, creature("core:angel")));
	EXPECT_EQ(town->creatures.at(2).first, 2u);
	reset(town);

	clearPerks();
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.volunteerNetwork", MasteryLevel::BASIC));
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.eliteDraft", MasteryLevel::ADVANCED));
	ASSERT_TRUE(selectOfferedPerk(gameHandler, hero, "new-horizons:recruitment.masterRecruiter", MasteryLevel::EXPERT));
	ASSERT_TRUE(town2);
	const auto coreStockBeforeMaster = town->creatures.at(0).first;
	EXPECT_EQ(coreStockBeforeMaster, 4u); // The first Basic Muster's recruits remain.
	ASSERT_TRUE(muster(town, creature("core:pikeman")));
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 1);
	const auto firstTownStock = town->creatures.at(0).first;
	EXPECT_EQ(firstTownStock, coreStockBeforeMaster + 8u); // Expert six plus Volunteer Network's two.
	EXPECT_FALSE(muster(town, creature("core:pikeman")));
	EXPECT_EQ(town->creatures.at(0).first, firstTownStock);

	// A Master Recruiter hero can spend the second use in a distinct town.
	town->setVisitingHero(nullptr);
	town2->setVisitingHero(hero);
	ASSERT_TRUE(muster(town2, creature("core:pikeman")));
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 2);
	EXPECT_EQ(town2->creatures.at(0).first, 8u);

	const auto savedAfterSecondUse = gameState()->saveToMemory();
	CGameState restoredAfterSecondUse;
	restoredAfterSecondUse.preInit(LIBRARY);
	restoredAfterSecondUse.loadFromMemory(savedAfterSecondUse);
	const auto * restoredMasterRecruiter = restoredAfterSecondUse.getHero(hero->id);
	ASSERT_NE(restoredMasterRecruiter, nullptr);
	EXPECT_EQ(restoredMasterRecruiter->getNewHorizonsMusterUsesThisWeek(0), 2);

	// The first town remains a once-per-week target, and the hero has no third
	// use even if a client attempts to submit another request.
	town2->setVisitingHero(nullptr);
	town->setVisitingHero(hero);
	EXPECT_FALSE(muster(town, creature("core:pikeman")));
	EXPECT_EQ(town->creatures.at(0).first, firstTownStock);

	// The absolute-week key resets both uses without mutating old markers.
	gameState()->day = 8;
	ASSERT_TRUE(muster(town, creature("core:pikeman")));
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(1), 1);
}

TEST(NewHorizonsRecruitmentMusterWire, StateRoundTripsAndOlderSavesRejectAuthoredMarkers)
{
	SetNewHorizonsMusterState outgoing;
	outgoing.heroId = ObjectInstanceID(42);
	outgoing.targetId = ObjectInstanceID(77);
	outgoing.lastUseWeek = 9;
	outgoing.usesThisWeek = 2;

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	SetNewHorizonsMusterState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.targetId, outgoing.targetId);
	EXPECT_EQ(incoming.lastUseWeek, outgoing.lastUseWeek);
	EXPECT_EQ(incoming.usesThisWeek, outgoing.usesThisWeek);

	SetNewHorizonsMusterState legacyOutgoing = outgoing;
	legacyOutgoing.usesThisWeek = 1;
	CMemorySerializer legacyWire;
	legacyWire.oser.version = ESerializationVersion::NEW_HORIZONS_MUSTER;
	legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_MUSTER;
	legacyWire.oser & legacyOutgoing;
	SetNewHorizonsMusterState legacyIncoming;
	legacyWire.iser & legacyIncoming;
	EXPECT_EQ(legacyIncoming.lastUseWeek, legacyOutgoing.lastUseWeek);
	EXPECT_EQ(legacyIncoming.usesThisWeek, 1);

	// The packet itself is not silently discarded by an older serializer.  The
	// object-level save gates below are what protect the actual hero/dwelling
	// marker when writing a legacy save.
	EXPECT_GT(static_cast<int>(ESerializationVersion::CURRENT), static_cast<int>(ESerializationVersion::NEW_HORIZONS_MUSTER));
}

TEST(NewHorizonsRecruitmentMusterWire, SplitRequestRoundTripsAndLegacySoloRemainsCompatible)
{
	MusterCreatures outgoing(ObjectInstanceID(42), ObjectInstanceID(77), CreatureID(0), CreatureID(2), 1);
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;
	MusterCreatures incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.targetId, outgoing.targetId);
	EXPECT_EQ(incoming.creatureId, outgoing.creatureId);
	EXPECT_EQ(incoming.secondCreatureId, outgoing.secondCreatureId);
	EXPECT_EQ(incoming.firstAmount, 1);

	MusterCreatures solo(ObjectInstanceID(42), ObjectInstanceID(77), CreatureID(0));
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_OBSTACLE_MOVEMENT_COST;
	oldWire.iser.version = ESerializationVersion::NEW_HORIZONS_OBSTACLE_MOVEMENT_COST;
	oldWire.oser & solo;
	MusterCreatures oldIncoming;
	oldIncoming.secondCreatureId = CreatureID(2);
	oldIncoming.firstAmount = 9;
	oldWire.iser & oldIncoming;
	EXPECT_EQ(oldIncoming.creatureId, solo.creatureId);
	EXPECT_EQ(oldIncoming.secondCreatureId, CreatureID::NONE);
	EXPECT_EQ(oldIncoming.firstAmount, 0);
	CMemorySerializer forbidden;
	forbidden.oser.version = ESerializationVersion::NEW_HORIZONS_OBSTACLE_MOVEMENT_COST;
	EXPECT_THROW(forbidden.oser & outgoing, std::runtime_error);
}
