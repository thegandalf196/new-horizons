/* Quick Study AI policy tests, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/Analyzers/HeroManager.h"
#include "lib/callback/CCallback.h"
#include "lib/CRandomGenerator.h"
#include "lib/entities/hero/NewHorizonsHeroRules.h"
#include "lib/entities/hero/NewHorizonsPerkRules.h"
#include "lib/entities/hero/SkillChoiceEligibility.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/serializer/CMemorySerializer.h"

namespace
{
const PlayerColor OWNER(0);
constexpr auto LEARNING = "new-horizons:learning";
class QuickStudyDecisionTest : public NullkillerTest
{
protected:
 CGHeroInstance * hero = nullptr;
 std::unique_ptr<NK2AI::AIGateway> gateway;
 SecondarySkill weak, best;
 void SetUp() override
 {
  NullkillerTest::SetUp();
  ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
  TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
  builder.size(36, false).playerActive(OWNER).hero({5, 5, 0}, HeroTypeID(0), OWNER).heroGarrison({{CreatureID(0), 10}});
  startWithMap(std::move(builder)); hero = findHeroAt({5, 5, 0}); ASSERT_NE(hero, nullptr);
  hero->secSkills.clear();
  const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).selected.clear();
  const auto learning = SecondarySkill(SecondarySkill::decode(LEARNING));
  hero->setSecSkillLevel(learning, 1, ChangeValueMode::ABSOLUTE);
  hero->applyPerkSelection({LEARNING, "new-horizons:learning.scholar"});
  hero->setSecSkillLevel(learning, 2, ChangeValueMode::ABSOLUTE);
  hero->applyPerkSelection({LEARNING, "new-horizons:learning.connoisseur"});
  hero->level = 6;
  gateway = makeGateway(OWNER); gateway->nullkiller->heroManager->update();
  const auto pool = newHorizonsHeroes::connoisseurSkillCandidates(*gateway->cc, *hero, {});
  ASSERT_GT(pool.size(), 1u);
  const std::vector<SecondarySkill> cards(pool.begin(), pool.end());
  best = cards[gateway->nullkiller->heroManager->selectBestSkillIndex(NK2AI::HeroPtr(hero, gateway->cc.get()), cards)];
  const auto inferior = std::ranges::find_if(cards, [&](auto card)
  {
   return gateway->nullkiller->heroManager->selectBestSkillIndex(NK2AI::HeroPtr(hero, gateway->cc.get()), {card, best}) == 1;
  });
  ASSERT_NE(inferior, cards.end()); weak = *inferior;
 }
 std::optional<size_t> decide(std::vector<SecondarySkill> cards, int level = 6)
 { return gateway->nullkiller->heroManager->connoisseurSkillCard(NK2AI::HeroPtr(hero, gateway->cc.get()), cards, level); }
 void restrictAlternatives()
 {
  auto & rules = const_cast<JsonNode &>(hero->getPrimaryGrowthRules());
  for(auto & [key, weight] : rules["skillOfferWeights"].Struct()) weight.Integer() = 0;
  rules["skillOfferWeights"][SecondarySkill::encode(best.getNum())].Integer() = 100;
  rules["skillOfferWeights"][SecondarySkill::encode(weak.getNum())].Integer() = 100;
 }
};
}

TEST_F(QuickStudyDecisionTest, WeakSkillCardHasVisibleBetterAlternativeWithoutChangingStateOrRng)
{
 const auto state = gameState()->saveToMemory();
 CMemorySerializer before; dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(before.oser); CRandomGenerator::getDefault().serialize(before.oser);
 EXPECT_EQ(decide({weak}), 0u);
 EXPECT_EQ(gameState()->saveToMemory(), state);
 CMemorySerializer after; dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(after.oser); CRandomGenerator::getDefault().serialize(after.oser);
 EXPECT_EQ(before.extractBuffer(), after.extractBuffer());
}
TEST_F(QuickStudyDecisionTest, KeepsExistingBestVisibleSkill)
{ EXPECT_FALSE(decide({best})); }
TEST_F(QuickStudyDecisionTest, ChoosesOnlyWeakerCardAndPreservesBetterCard)
{
 const auto * manager = gateway->nullkiller->heroManager.get();
 const NK2AI::HeroPtr heroPtr(hero, gateway->cc.get());
 const auto low = SecondarySkill(SecondarySkill::decode("new-horizons:offense"));
 const auto middle = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
 const auto top = SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"));
 const auto eligible = newHorizonsHeroes::connoisseurSkillCandidates(*gateway->cc, *hero, {});
 for(const auto card : {low, middle, top})
 {
  const auto key = SecondarySkill::encode(card.getNum());
  ASSERT_TRUE(eligible.contains(card));
  ASSERT_TRUE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gateway->cc, hero, card));
  ASSERT_FALSE(newHorizonsHeroes::isExcludedSkill(hero->getPrimaryGrowthRules(), card));
  ASSERT_GT(newHorizonsHeroes::skillOfferWeight(hero->getPrimaryGrowthRules(), card).value_or(0), 0);
  ASSERT_EQ(hero->getPerkState().rules["skills"][key]["ranks"]["basic"]["effect"]["status"].String(), "active");
 }
 ASSERT_EQ(manager->getHeroRoleOrDefault(heroPtr), NK2AI::MAIN);
 ASSERT_TRUE(hero->getSpellsInSpellbook().empty());
 ASSERT_EQ(hero->getSecSkillLevel(middle), MasteryLevel::NONE);
 // Current main-hero scores: ordinary Offense0, first School1, own faction1000.
 ASSERT_FLOAT_EQ(manager->evaluateSecSkill(low, hero), 0.0f);
 ASSERT_FLOAT_EQ(manager->evaluateSecSkill(middle, hero), 1.0f);
 ASSERT_EQ(best, top);
 ASSERT_EQ(manager->selectBestSkillIndex(heroPtr, {low, middle}), 1);
 ASSERT_EQ(manager->selectBestSkillIndex(heroPtr, {middle, top}), 1);

 // A legal unoffered third card beats the weakest visible card.
 const auto positivePool = newHorizonsHeroes::connoisseurSkillCandidates(
  *gateway->cc, *hero, {middle, low});
 ASSERT_TRUE(positivePool.contains(top));
 ASSERT_FALSE(positivePool.contains(middle));
 ASSERT_FALSE(positivePool.contains(low));
 EXPECT_EQ(decide({middle, low}), 1u);

 // These are the top two scores: ties cannot justify discarding the School.
 const auto negativePool = newHorizonsHeroes::connoisseurSkillCandidates(
  *gateway->cc, *hero, {top, middle});
 ASSERT_FALSE(negativePool.contains(top));
 ASSERT_FALSE(negativePool.contains(middle));
 for(const auto card : negativePool)
  ASSERT_EQ(manager->selectBestSkillIndex(heroPtr, {middle, card}), 0);
 EXPECT_FALSE(decide({top, middle}));
}
TEST_F(QuickStudyDecisionTest, ZeroWeightAlternativeIsNotAConnoisseurCandidate)
{
 restrictAlternatives();
 auto & rules = const_cast<JsonNode &>(hero->getPrimaryGrowthRules());
 rules["skillOfferWeights"][SecondarySkill::encode(best.getNum())].Integer() = 0;
 EXPECT_FALSE(decide({weak}));
}
TEST_F(QuickStudyDecisionTest, ExpertAlternativeStillRequiresCurrentRankEligibility)
{
 restrictAlternatives();
 hero->setSecSkillLevel(best, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
 EXPECT_FALSE(decide({weak}));
}
TEST_F(QuickStudyDecisionTest, WrongReachedLevelOrUnselectedPerkNeverRerolls)
{
 EXPECT_FALSE(decide({weak}, 5));
 const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).selected.clear();
 EXPECT_FALSE(decide({weak}));
}
TEST_F(QuickStudyDecisionTest, EmptyOrMalformedSkillOfferIsKept)
{ EXPECT_FALSE(decide({})); EXPECT_FALSE(decide({SecondarySkill::NONE})); }
