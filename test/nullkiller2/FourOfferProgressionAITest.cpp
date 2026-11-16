/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/CGlobalAI.h"
#include "../../lib/callback/IClient.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include <algorithm>
#include <chrono>
#include <future>

namespace
{
using namespace newHorizonsHeroes;
constexpr PlayerColor OWNER(0);

class ChoiceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ChoiceEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ChoiceClient final : public IClient
{
public:
	std::promise<int> completed;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * reply = dynamic_cast<const QueryReply *>(&request);
		if(!reply || player != OWNER || !reply->reply.has_value())
			throw std::runtime_error("Unexpected four-offer AI request");
		completed.set_value(*reply->reply);
		return 1;
	}
};

class FourOfferProgressionAITest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder map(EMapFormat::SOD);
		map.size(36, false).playerActive(OWNER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), OWNER)
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(map));
		hero = findHeroByOwner(OWNER);
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(usesFourOfferProgression(hero->getPerkState().rules));
		auto & state = const_cast<PerkState &>(hero->getPerkState());
		state.selected.clear();
		const auto learned = hero->secSkills;
		for(const auto & [skill, rank] : learned)
			hero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
	}
	int answer(const FourOfferProgressionOffer & offer)
	{
		const auto ai = AIFactory::createAdventureAI("Nullkiller2");
		if(!ai) throw std::runtime_error("Nullkiller2 factory unavailable");
		auto client = std::make_shared<ChoiceClient>();
		auto callback = makeCallback(OWNER, client.get());
		ai->initGameInterface(std::make_shared<ChoiceEnvironment>(gameState()), callback);
		auto result = client->completed.get_future();
		ai->heroGotFourOfferLevel(hero, PrimarySkill::ATTACK, offer, QueryID(77), false, QueryID(-1), 2);
		const auto ready = result.wait_for(std::chrono::seconds(10));
		ai->finish();
		EXPECT_EQ(ready, std::future_status::ready);
		return ready == std::future_status::ready ? result.get() : -1;
	}
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(FourOfferProgressionAITest, OnlyNewSkillUsesFixedSecondSlotReply)
{
	prepare();
	const auto candidates = fourOfferCandidates(*gameState(), *hero);
	const auto eligible = std::find_if(candidates.begin(), candidates.end(), [](const auto & candidate)
	{ return candidate.rank == 0 && candidate.skillEligible; });
	ASSERT_NE(eligible, candidates.end());
	FourOfferProgressionOffer offer;
	offer.skills[1] = eligible->skill;
	offer.targetRanks[1] = 1;
	EXPECT_EQ(answer(offer), 1);
	EXPECT_EQ(hero->getSecSkillLevel(eligible->skill), 0);
}

TEST_F(FourOfferProgressionAITest, AlternativePerkKeepsCatalogueReplyAfterPrimaryPoolExpands)
{
	prepare();
	const auto wisdom = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
	const auto faction = factionSkill(hero->getPrimaryGrowthRules(), hero->getFactionID());
	ASSERT_TRUE(faction);
	const auto factionId = SecondarySkill::encode(faction->getNum());
	hero->setSecSkillLevel(wisdom, 1, ChangeValueMode::ABSOLUTE);
	hero->setSecSkillLevel(*faction, 1, ChangeValueMode::ABSOLUTE);
	const auto initial = PerkSelectionOperation(hero->getPerkState()).eligibleCandidates("new-horizons:wisdom", 1);
	ASSERT_FALSE(initial.empty());
	hero->applyPerkSelection(initial.front().selection);
	FourOfferProgressionOffer offer;
	offer.perkSkills = {"new-horizons:wisdom", factionId};
	const auto before = fourOfferPerkGroups(offer, fourOfferCandidates(*gameState(), *hero));
	ASSERT_FALSE(before[1].empty());
	// Model a legitimate external rank grant while this parent pair is held.
	hero->setSecSkillLevel(wisdom, 2, ChangeValueMode::ABSOLUTE);
	const auto after = fourOfferPerkGroups(offer, fourOfferCandidates(*gameState(), *hero));
	ASSERT_GT(after[0].size(), before[0].size());
	EXPECT_EQ(after[1], before[1]);
	const auto selectedBefore = hero->getPerkState().selected;
	const int reply = answer(offer);
	const auto choice = fourOfferSelection(offer, hero->getPerkState(), reply);
	ASSERT_TRUE(choice);
	ASSERT_TRUE(choice->perk);
	EXPECT_EQ(choice->perk->selection.skillId, factionId);
	EXPECT_EQ(reply, fourOfferPerkReply(offer, hero->getPerkState(), 1, choice->perk->selection.perkId));
	EXPECT_GE(reply, 12);
	EXPECT_EQ(hero->getPerkState().selected, selectedBefore);
}

TEST_F(FourOfferProgressionAITest, AllStaleChoicesCompleteWithoutWaitingForAnotherOffer)
{
	prepare();
	const auto authority = SecondarySkill(SecondarySkill::decode("new-horizons:authority"));
	FourOfferProgressionOffer offer;
	offer.skills[0] = authority;
	offer.targetRanks[0] = 2;
	hero->setSecSkillLevel(authority, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(legalFourOfferReplies(offer, hero->getPerkState(), fourOfferCandidates(*gameState(), *hero)).empty());
	EXPECT_EQ(answer(offer), 0);
	EXPECT_TRUE(hero->getPerkState().selected.empty());
}
