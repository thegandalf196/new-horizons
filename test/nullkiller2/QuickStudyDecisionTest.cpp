/* Quick Study AI policy tests, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/HeroManager.h"
#include "lib/callback/CCallback.h"
#include "lib/CRandomGenerator.h"
#include "lib/entities/hero/NewHorizonsHeroRules.h"
#include "lib/entities/hero/NewHorizonsPerkRules.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/serializer/CMemorySerializer.h"

namespace
{
const PlayerColor OWNER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto OFFENSE = "new-horizons:offense";
constexpr auto LUCK = "new-horizons:luck";
class QuickStudyDecisionTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	newHorizonsHeroes::PerkOfferCandidate weak, strong;
	newHorizonsHeroes::PerkOfferCandidate candidate(const std::string & skill, int tier)
	{
		for(const auto & definition : newHorizonsHeroes::perkOptions(hero->getPerkState().rules, skill))
			if(definition.effect["status"].String() == "active"
				&& newHorizonsHeroes::perkRequiredRank(definition.requiredRank) == tier
				&& hero->getPerkState().canAcquirePerk(skill, definition.id, hero->getPerkSkillRank(skill)))
				return {{skill, definition.id}, definition.name, definition.description, tier};
		throw std::runtime_error("Missing actual active test perk");
	}
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(OWNER).hero({5, 5, 0}, HeroTypeID(0), OWNER)
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		ASSERT_NE(hero, nullptr);
		// An authored, otherwise empty development roster; retain the full captured registry/weights.
		hero->secSkills.clear();
		const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).selected.clear();
		auto setRank = [&](const std::string & skill, int rank)
		{ hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE); };
		setRank(LEARNING, 1);
		hero->applyPerkSelection({LEARNING, "new-horizons:learning.mentor"});
		setRank(LEARNING, 2);
		hero->applyPerkSelection({LEARNING, "new-horizons:learning.quickStudy"});
		setRank(OFFENSE, 1);
		hero->applyPerkSelection(candidate(OFFENSE, 1).selection);
		setRank(OFFENSE, 2);
		setRank(LUCK, 1);
		weak = candidate(LUCK, 1);
		strong = candidate(OFFENSE, 2);
		hero->level = 6;
		gateway = makeGateway(OWNER);
		gateway->nullkiller->heroManager->update();
	}
	bool decide(const std::vector<SecondarySkill> & skills,
		const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, int level = 6)
	{
		return gateway->nullkiller->heroManager->shouldRerollLevelUpOffer(NK2AI::HeroPtr(hero, gateway->cc.get()), skills, perks, level);
	}
};
}

TEST_F(QuickStudyDecisionTest, WeakBasicOfferHasPositiveEligibleAdvancedAlternativeWithoutChangingStateOrRng)
{
	const auto state = hero->getPerkState().toJson();
	const auto skills = hero->secSkills;
	CMemorySerializer before;
	dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(before.oser);
	CRandomGenerator::getDefault().serialize(before.oser);
	EXPECT_TRUE(decide({}, {weak}));
	EXPECT_EQ(hero->getPerkState().toJson(), state);
	EXPECT_EQ(hero->secSkills, skills);
	CMemorySerializer after;
	dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(after.oser);
	CRandomGenerator::getDefault().serialize(after.oser);
	EXPECT_EQ(before.extractBuffer(), after.extractBuffer());
}

TEST_F(QuickStudyDecisionTest, KeepsExistingAdvancedPerk)
{
	EXPECT_FALSE(decide({}, {strong}));
}

TEST_F(QuickStudyDecisionTest, KeepsPositiveFactionSkillUsingExistingOfferEvaluator)
{
	const auto faction = newHorizonsHeroes::factionSkill(hero->getPrimaryGrowthRules(), hero->getFactionID());
	ASSERT_TRUE(faction);
	ASSERT_TRUE(hero->canLearnSkill(*faction));
	EXPECT_FALSE(decide({*faction}, {weak}));
}

TEST_F(QuickStudyDecisionTest, RawZeroWeightUsesPositiveCanonicalSamplerProjection)
{
	auto & captured = const_cast<JsonNode &>(hero->getPrimaryGrowthRules());
	captured["skillOfferWeights"][OFFENSE].Integer() = 0;
	const auto weights = hero->getPerkState().quantizedParentWeights(hero->getPerkOfferWeightContext());
	ASSERT_EQ(weights.at(OFFENSE), "4294967296");
	EXPECT_TRUE(decide({}, {weak}));
}

TEST_F(QuickStudyDecisionTest, PreferredAlternativeStillRequiresCurrentRankEligibility)
{
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(OFFENSE)), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(hero->getPerkState().canAcquirePerk(strong.selection.skillId,
		strong.selection.perkId, hero->getPerkSkillRank(OFFENSE)));
	EXPECT_FALSE(decide({}, {weak}));
}

TEST_F(QuickStudyDecisionTest, LegacyPolicyAndWrongReachedLevelNeverReroll)
{
	EXPECT_FALSE(decide({}, {weak}, 5));
	auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
	rules.Struct().erase("quickStudyPolicy");
	EXPECT_FALSE(decide({}, {weak}));
}

TEST_F(QuickStudyDecisionTest, EmptyOrMalformedOfferIsKept)
{
	EXPECT_FALSE(decide({}, {}));
	auto malformed = weak;
	malformed.requiredRank = 3;
	EXPECT_FALSE(decide({}, {malformed}));
}
