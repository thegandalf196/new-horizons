/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/rewardable/Info.h"
#include "../../lib/rewardable/Reward.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CSaveFile.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include <algorithm>
#include <string_view>
#include <boost/filesystem/operations.hpp>

namespace
{
using namespace newHorizonsHeroes;
constexpr PlayerColor HUMAN(0);

SecondarySkill skill(const char * identity) { return SecondarySkill(SecondarySkill::decode(identity)); }

class SkillTeachingObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using Rewardable::Interface::grantRewardBeforeLevelup;
};

class FourOfferHeroProgressionTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		perks.Struct().erase("unifiedSkillProgressionVersion");
		perks["fourOfferProgressionVersion"].Integer() = 1;
		perks.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder map(EMapFormat::SOD);
		map.size(36, false).playerActive(HUMAN)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), HUMAN)
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(map));
		hero = findHeroByOwner(HUMAN);
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(usesFourOfferProgression(hero->getPerkState().rules));
		server = std::make_unique<GameHandlerTestServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->randomizer->setSeed(1337);
		handler->onAdvInterfaceReady(HUMAN);
		ASSERT_FALSE(current());
	}
	void TearDown() override
	{
		handler.reset();
		server.reset();
		TinyMapGameTest::TearDown();
	}
	std::shared_ptr<CHeroLevelUpDialogQuery> current() const
	{
		return std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler->queries->topQuery(HUMAN));
	}
	void gainLevel()
	{
		handler->giveExperienceWithoutLevelUp(hero, LIBRARY->heroh->reqExp(hero->level + 1) - hero->exp);
		handler->expGiven(hero);
		ASSERT_TRUE(current());
		ASSERT_TRUE(current()->prompted);
	}
	void onlyAuthorityWithBasicPerk()
	{
		const auto initial = hero->secSkills;
		for(const auto & [known, rank] : initial) handler->changeSecSkill(hero, known, 0, ChangeValueMode::ABSOLUTE);
		const auto authority = skill("new-horizons:authority");
		handler->changeSecSkill(hero, authority, 1, ChangeValueMode::ABSOLUTE);
		const auto pool = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates("new-horizons:authority", 1);
		ASSERT_FALSE(pool.empty());
		hero->applyPerkSelection(pool.front().selection);
	}
	FourOfferPerkGroups groups(const FourOfferProgressionOffer & offer) const
	{
		return fourOfferPerkGroups(offer, fourOfferCandidates(*gameState(), *hero));
	}
	std::array<int, GameConstants::PRIMARY_SKILLS> primary() const
	{
		std::array<int, GameConstants::PRIMARY_SKILLS> result{};
		for(size_t index = 0; index < result.size(); ++index)
			result[index] = hero->getBasePrimarySkillValue(PrimarySkill(index));
		return result;
	}
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(FourOfferHeroProgressionTest, SkillChoiceConsumesTheLevelWithoutGrantingAPerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto acquired = hero->getPerkState().selected;
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	const auto offered = query->hlu.fourOffers.skills[0];
	ASSERT_NE(offered, SecondarySkill::NONE);
	const int oldRank = hero->getSecSkillLevel(offered);
	const auto afterGrowth = primary();
	EXPECT_EQ(hero->level, 2);
	EXPECT_FALSE(query->isValidReply(-1));
	ASSERT_TRUE(handler->queryReply(query->queryID, 0, HUMAN));
	EXPECT_EQ(hero->getSecSkillLevel(offered), oldRank + 1);
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	EXPECT_EQ(primary(), afterGrowth);
	EXPECT_FALSE(current());
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	EXPECT_FALSE(handler->queryReply(query->queryID, 0, HUMAN));
}

TEST_F(FourOfferHeroProgressionTest, PerkChoiceConsumesTheLevelWithoutGrantingASkillRank)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto ranks = hero->secSkills;
	const auto acquired = hero->getPerkState().selected.size();
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	const auto pool = groups(query->hlu.fourOffers);
	ASSERT_FALSE(pool[0].empty());
	const auto selected = pool[0].front();
	const auto reply = fourOfferPerkReply(query->hlu.fourOffers, hero->getPerkState(), 0, selected.selection.perkId);
	ASSERT_TRUE(handler->queryReply(query->queryID, reply, HUMAN));
	EXPECT_EQ(hero->secSkills, ranks);
	EXPECT_EQ(hero->getPerkState().selected.size(), acquired + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(selected.selection.skillId, selected.selection.perkId));
	EXPECT_FALSE(current());
}

TEST_F(FourOfferHeroProgressionTest, ExplicitExternalRanksBypassPrerequisitesAndNeverBackfillPerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto authority = skill("new-horizons:authority");
	ASSERT_EQ(hero->getSecSkillLevel(authority), 0);
	const auto selected = hero->getPerkState().selected;
	handler->changeSecSkill(hero, authority, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_THROW(handler->levelUpHero(hero, authority, false), std::runtime_error);
	handler->changeSecSkill(hero, authority, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSecSkillLevel(authority), 3);
	EXPECT_EQ(hero->getPerkState().selected, selected);
	EXPECT_FALSE(current());
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	const auto offered = handler->randomizer->rollFourOfferProgression(hero);
	const auto pools = groups(offered);
	ASSERT_EQ(pools[0].size(), 4);
	for(const auto & candidate : pools[0])
	{
		EXPECT_EQ(candidate.selection.skillId, "new-horizons:authority");
		EXPECT_EQ(candidate.requiredRank, 1);
	}
}

TEST_F(FourOfferHeroProgressionTest, TeacherRewardGrantsBasicOrExplicitUpgradeWithoutAPerkPrompt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto authority = skill("new-horizons:authority");
	const auto acquired = hero->getPerkState().selected;
	SkillTeachingObject teacher(gameState().get());
	teacher.ID = MapObjectID::WITCH_HUT;
	Rewardable::VisitInfo reward;
	reward.reward.secondary[authority] = 1;
	teacher.grantRewardBeforeLevelup(*handler, reward, hero);
	EXPECT_EQ(hero->getSecSkillLevel(authority), 1);
	EXPECT_FALSE(current());
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	// The generic teacher's explicit +1 reward may improve a known Skill.
	// No new "visit a known Skill" upgrade behavior is introduced here.
	teacher.grantRewardBeforeLevelup(*handler, reward, hero);
	EXPECT_EQ(hero->getSecSkillLevel(authority), 2);
	EXPECT_FALSE(current());
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	EXPECT_EQ(hero->level, 1);
}

TEST_F(FourOfferHeroProgressionTest, UniversityPurchaseChargesItsExistingPriceAndGrantsOnlyBasic)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto authority = skill("new-horizons:authority");
	CGUniversity university(gameState().get());
	university.skills.emplace_back(authority);
	const auto acquired = hero->getPerkState().selected;
	const auto price = newHorizonsUniversity::tuition(&university, gameState()->getMagicRules(), gameState()->getSettings());
	handler->giveResources(HUMAN, price * 2);
	const auto before = gameState()->getPlayerState(HUMAN)->resources;
	ASSERT_TRUE(handler->buySecSkill(&university, hero, authority));
	EXPECT_EQ(hero->getSecSkillLevel(authority), 1);
	EXPECT_EQ(gameState()->getPlayerState(HUMAN)->resources, before - price);
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	EXPECT_FALSE(current());
	EXPECT_EQ(hero->level, 1);
	EXPECT_FALSE(handler->buySecSkill(&university, hero, authority));
	EXPECT_EQ(hero->getSecSkillLevel(authority), 1);
	EXPECT_EQ(gameState()->getPlayerState(HUMAN)->resources, before - price);
}

TEST_F(FourOfferHeroProgressionTest, ZeroWeightExternalSkillHasPerksButNeverOrdinarySkillOffers)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto wisdom = skill("new-horizons:wisdom");
	ASSERT_EQ(skillOfferWeight(hero->getPrimaryGrowthRules(), wisdom).value_or(-1), 0);
	handler->changeSecSkill(hero, wisdom, 1, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getSecSkillLevel(wisdom), 1);
	const auto pool = fourOfferCandidates(*gameState(), *hero);
	const auto entry = std::find_if(pool.begin(), pool.end(), [wisdom](const auto & candidate) { return candidate.skill == wisdom; });
	ASSERT_NE(entry, pool.end());
	EXPECT_EQ(entry->weight, 0);
	EXPECT_FALSE(entry->skillEligible);
	EXPECT_EQ(entry->perks.size(), 4);
	hero->applyPerkSelection(entry->perks.front().selection);
	EXPECT_TRUE(hero->getPerkState().canAdvanceSkillNormally("new-horizons:wisdom", 1));
	const auto eligibleAfterPerk = fourOfferCandidates(*gameState(), *hero);
	const auto retained = std::find_if(eligibleAfterPerk.begin(), eligibleAfterPerk.end(), [wisdom](const auto & candidate)
		{ return candidate.skill == wisdom; });
	ASSERT_NE(retained, eligibleAfterPerk.end());
	EXPECT_FALSE(retained->skillEligible);
	EXPECT_EQ(skillOfferWeight(hero->getPrimaryGrowthRules(), wisdom).value_or(-1), 0);
	EXPECT_FALSE(current());
}

TEST_F(FourOfferHeroProgressionTest, UniversityCanTeachDistinctSchoolsUntilCapacityWithoutPerksOrExtraPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGUniversity university(gameState().get());
	const auto light = skill("new-horizons:lightMagic");
	const auto nature = skill("new-horizons:natureMagic");
	const auto havoc = skill("new-horizons:havocMagic");
	university.skills = {TradeItemBuy(light), TradeItemBuy(nature), TradeItemBuy(havoc)};
	const auto price = newHorizonsUniversity::tuition(&university, gameState()->getMagicRules(), gameState()->getSettings());
	handler->giveResources(HUMAN, price * 6);
	const auto before = gameState()->getPlayerState(HUMAN)->resources;
	const auto perks = hero->getPerkState().selected;
	ASSERT_TRUE(handler->buySecSkill(&university, hero, light));
	ASSERT_TRUE(handler->buySecSkill(&university, hero, nature));
	EXPECT_EQ(gameState()->getPlayerState(HUMAN)->resources, before - price * 2);
	EXPECT_EQ(hero->getPerkState().selected, perks);
	EXPECT_FALSE(current());
	for(const auto * identity : {"new-horizons:authority", "new-horizons:armorer", "new-horizons:offense", "new-horizons:luck"})
		handler->changeSecSkill(hero, skill(identity), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(hero->canLearnSkill());
	EXPECT_FALSE(handler->buySecSkill(&university, hero, havoc));
	EXPECT_EQ(hero->getSecSkillLevel(havoc), 0);
	EXPECT_EQ(gameState()->getPlayerState(HUMAN)->resources, before - price * 2);
	EXPECT_EQ(hero->getPerkState().selected, perks);
}

TEST_F(FourOfferHeroProgressionTest, ForeignFactionSkillCannotLeakIntoAnyOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto necromancy = skill("new-horizons:necromancy");
	handler->changeSecSkill(hero, necromancy, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSecSkillLevel(necromancy), 0);
	// Even an exceptional authored state must not leak a foreign perk pool.
	hero->setSecSkillLevel(necromancy, 2, ChangeValueMode::ABSOLUTE);
	const auto pool = fourOfferCandidates(*gameState(), *hero);
	EXPECT_TRUE(std::none_of(pool.begin(), pool.end(), [necromancy](const auto & candidate) { return candidate.skill == necromancy; }));
	handler->changeSecSkill(hero, necromancy, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSecSkillLevel(necromancy), 1);
	handler->changeSecSkill(hero, necromancy, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSecSkillLevel(necromancy), 0);
}

TEST_F(FourOfferHeroProgressionTest, InspectingAndReopeningHeldOfferConsumesNoRandomDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	const auto expected = query->hlu.fourOffers;
	CMemorySerializer before;
	before.oser & *handler->randomizer;
	for(int repeat = 0; repeat < 5; ++repeat)
	{
		query->onAdded(HUMAN);
		query->onExposure(nullptr);
		handler->resumeMasteryQueries(HUMAN);
		EXPECT_TRUE(query->isValidReply(0));
		EXPECT_EQ(query->hlu.fourOffers, expected);
	}
	CMemorySerializer after;
	after.oser & *handler->randomizer;
	EXPECT_EQ(before.extractBuffer(), after.extractBuffer());
	EXPECT_EQ(hero->level, 2);
}

TEST_F(FourOfferHeroProgressionTest, ExhaustedDevelopmentStillAdvancesPrimaryAttributesOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto starting = hero->secSkills;
	for(const auto & [known, rank] : starting) handler->changeSecSkill(hero, known, 0, ChangeValueMode::ABSOLUTE);
	for(const auto * identity : {"new-horizons:authority", "new-horizons:offense", "new-horizons:armorer",
		"new-horizons:archery", "new-horizons:command", "new-horizons:discipline", "new-horizons:recruitment", "new-horizons:luck"})
	{
		const auto parent = skill(identity);
		handler->changeSecSkill(hero, parent, 3, ChangeValueMode::ABSOLUTE);
		for(int tier = 1; tier <= 3; ++tier)
		{
			if(hero->getPerkState().hasPerkForTier(identity, tier)) continue;
			const auto pool = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates(identity, 3);
			const auto candidate = std::find_if(pool.begin(), pool.end(), [tier](const auto & row) { return row.requiredRank == tier; });
			ASSERT_NE(candidate, pool.end());
			hero->applyPerkSelection(candidate->selection);
		}
	}
	ASSERT_FALSE(hero->canLearnSkill());
	const auto ranks = hero->secSkills;
	const auto perks = hero->getPerkState().selected;
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	EXPECT_TRUE(query->hlu.fourOffers.empty());
	EXPECT_TRUE(query->isValidReply(0));
	EXPECT_FALSE(query->isValidReply(1));
	const auto growth = primary();
	ASSERT_TRUE(handler->queryReply(query->queryID, 0, HUMAN));
	EXPECT_FALSE(current());
	EXPECT_EQ(hero->level, 2);
	EXPECT_EQ(hero->secSkills, ranks);
	EXPECT_EQ(hero->getPerkState().selected, perks);
	EXPECT_EQ(primary(), growth);
}

TEST_F(FourOfferHeroProgressionTest, ExternalRankAfterExperiencePreservesGroupsAndUnlocksAllLivePerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(onlyAuthorityWithBasicPerk());
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	const auto issued = query->hlu.fourOffers;
	ASSERT_EQ(issued.skills[0], skill("new-horizons:authority"));
	ASSERT_EQ(issued.targetRanks[0], 2);
	ASSERT_EQ(groups(issued)[0].size(), 3);
	CMemorySerializer before;
	before.oser & *handler->randomizer;
	// Native scripted/reward continuations can grant a Skill after an XP
	// action exposed the query; received-input blocking does not stop them.
	handler->changeSecSkill(hero, issued.skills[0], 2, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(query->isValidReply(0)); // It must not silently become Expert.
	EXPECT_EQ(query->hlu.fourOffers, issued);
	const auto live = groups(issued);
	ASSERT_EQ(live[0].size(), 7);
	const auto advanced = std::find_if(live[0].begin(), live[0].end(), [](const auto & perk) { return perk.requiredRank == 2; });
	ASSERT_NE(advanced, live[0].end());
	const auto reply = fourOfferPerkReply(issued, hero->getPerkState(), 0, advanced->selection.perkId);
	EXPECT_TRUE(query->isValidReply(reply));
	CMemorySerializer after;
	after.oser & *handler->randomizer;
	EXPECT_EQ(before.extractBuffer(), after.extractBuffer());
	const auto acquired = hero->getPerkState().selected.size();
	ASSERT_TRUE(handler->queryReply(query->queryID, reply, HUMAN));
	EXPECT_EQ(hero->getSecSkillLevel(issued.skills[0]), 2);
	EXPECT_EQ(hero->getPerkState().selected.size(), acquired + 1);
}

TEST_F(FourOfferHeroProgressionTest, AllHeldChoicesBecomingUnavailableHaveOneSafeCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(onlyAuthorityWithBasicPerk());
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	const auto held = query->hlu.fourOffers;
	ASSERT_NE(held.skills[1], SecondarySkill::NONE);
	handler->changeSecSkill(hero, held.skills[0], 3, ChangeValueMode::ABSOLUTE);
	handler->changeSecSkill(hero, held.skills[1], 1, ChangeValueMode::ABSOLUTE);
	for(int tier = 2; tier <= 3; ++tier)
	{
		const auto pool = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates("new-horizons:authority", 3);
		const auto perk = std::find_if(pool.begin(), pool.end(), [tier](const auto & candidate) { return candidate.requiredRank == tier; });
		ASSERT_NE(perk, pool.end());
		hero->applyPerkSelection(perk->selection);
	}
	EXPECT_TRUE(legalFourOfferReplies(held, hero->getPerkState(), fourOfferCandidates(*gameState(), *hero)).empty());
	EXPECT_TRUE(query->isValidReply(0));
	EXPECT_FALSE(query->isValidReply(1));
	const auto ranks = hero->secSkills;
	const auto acquired = hero->getPerkState().selected;
	ASSERT_TRUE(handler->queryReply(query->queryID, 0, HUMAN));
	EXPECT_EQ(hero->secSkills, ranks);
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	EXPECT_EQ(hero->level, 2);
	EXPECT_FALSE(current());
}

TEST_F(FourOfferHeroProgressionTest, PendingGroupsAndRandomStreamSurviveNativeSaveLoad)
{
	NativeBonusGraphAccess graphAccess;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(onlyAuthorityWithBasicPerk());
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	handler->changeSecSkill(hero, skill("new-horizons:authority"), 2, ChangeValueMode::ABSOLUTE);
	const auto original = current()->hlu;
	const auto originalPools = groups(original.fourOffers);
	ASSERT_FALSE(originalPools[0].empty());
	const auto advanced = std::find_if(originalPools[0].begin(), originalPools[0].end(), [](const auto & perk) { return perk.requiredRank == 2; });
	ASSERT_NE(advanced, originalPools[0].end());
	const auto reply = fourOfferPerkReply(original.fourOffers, hero->getPerkState(), 0, advanced->selection.perkId);
	const auto ranks = hero->secSkills;
	const auto selected = hero->getPerkState().selected;
	const auto attributes = primary();
	map()->registerMapStrings();
	const auto directory = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-four-offer-%%%%%%%%");
	boost::filesystem::create_directory(directory);
	const auto cleanup = vstd::makeScopeGuard([&]() { boost::filesystem::remove_all(directory); });
	const auto path = directory / "pending.vsgm1";
	{
		CSaveFile file;
		gameState()->saveGame(file);
		file.save(*handler);
		ASSERT_LT(file.currentContent().size(), 64u * 1024u * 1024u);
		file.write(path);
	}
	auto restoredWorld = std::make_shared<CGameState>();
	restoredWorld->preInit(LIBRARY);
	GameHandlerTestServer restoredServer(restoredWorld, HUMAN);
	CGameHandler restoredHandler(restoredServer, restoredWorld);
	{
		CLoadFile file(path, restoredWorld.get());
		restoredWorld->loadGame(file);
		file.load(restoredHandler);
		ASSERT_TRUE(file.atEnd());
	}
	const StartInfo resumedOptions = *restoredWorld->getStartInfo();
	restoredWorld->updateOnLoad(resumedOptions);
	auto * restoredHero = restoredWorld->getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getPerkState().selected, selected);
	EXPECT_EQ(restoredHero->secSkills, ranks);
	CMemorySerializer sourceRng, restoredRng;
	sourceRng.oser & *handler->randomizer;
	restoredRng.oser & *restoredHandler.randomizer;
	EXPECT_EQ(sourceRng.extractBuffer(), restoredRng.extractBuffer());
	restoredHandler.onAdvInterfaceReady(HUMAN);
	const auto restored = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(restoredHandler.queries->topQuery(HUMAN));
	ASSERT_TRUE(restored);
	EXPECT_EQ(restored->hlu.fourOffers, original.fourOffers);
	EXPECT_FALSE(restored->isValidReply(0));
	EXPECT_EQ(restoredHero->level, 2);
	EXPECT_FALSE(restoredHandler.queryReply(original.queryID, reply, HUMAN));
	ASSERT_TRUE(restoredHandler.queryReply(restored->queryID, reply, HUMAN));
	EXPECT_EQ(restoredHero->getPerkState().selected.size(), selected.size() + 1);
	EXPECT_EQ(restoredHero->secSkills, ranks);
	EXPECT_EQ(restoredHero->level, 2);
	EXPECT_TRUE(restoredHandler.pendingHeroLevelUps.empty());
	EXPECT_FALSE(restoredHandler.queryReply(restored->queryID, reply, HUMAN));
	for(size_t index = 0; index < attributes.size(); ++index)
		EXPECT_EQ(restoredHero->getBasePrimarySkillValue(PrimarySkill(index)), attributes[index]);
}

TEST_F(FourOfferHeroProgressionTest, ConnoisseurRerollPreservesOtherThreeFixedOffers)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(onlyAuthorityWithBasicPerk());
	for(const auto * identity : {"new-horizons:authority", "new-horizons:learning"})
	{
		handler->changeSecSkill(hero, skill(identity), 2, ChangeValueMode::ABSOLUTE);
		if(std::string_view(identity) == "new-horizons:learning")
		{
			const auto basic = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates(identity, 1);
			ASSERT_FALSE(basic.empty());
			hero->applyPerkSelection(basic.front().selection);
		}
		const auto pool = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates(identity, 2);
		const auto advanced = std::find_if(pool.begin(), pool.end(), [identity](const auto & candidate)
		{
			return candidate.requiredRank == 2 && (std::string_view(identity) != "new-horizons:learning"
				|| candidate.selection.perkId == "new-horizons:learning.connoisseur");
		});
		ASSERT_NE(advanced, pool.end());
		hero->applyPerkSelection(advanced->selection);
	}
	ASSERT_NO_FATAL_FAILURE(gainLevel());
	const auto query = current();
	ASSERT_TRUE(query->hlu.optionalRerollAvailable);
	const auto held = query->hlu.fourOffers;
	const auto attributes = primary();
	const auto acquired = hero->getPerkState().selected;
	ASSERT_TRUE(query->requestReroll(HUMAN, 0));
	const auto replacement = query->hlu.fourOffers;
	EXPECT_NE(replacement.skills[0], held.skills[0]);
	EXPECT_EQ(replacement.targetRanks[0], 3);
	EXPECT_EQ(replacement.skills[1], held.skills[1]);
	EXPECT_EQ(replacement.targetRanks[1], held.targetRanks[1]);
	EXPECT_EQ(replacement.perkSkills, held.perkSkills);
	EXPECT_EQ(hero->getPerkState().selected, acquired);
	EXPECT_EQ(primary(), attributes);
	CMemorySerializer before, after;
	before.oser & *handler->randomizer;
	EXPECT_FALSE(query->requestReroll(HUMAN, 0));
	after.oser & *handler->randomizer;
	EXPECT_EQ(before.extractBuffer(), after.extractBuffer());
	EXPECT_EQ(hero->level, 2);
}
