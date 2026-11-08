/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../AI/BattleAI/HeroSpellVariants.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/ElementalAttunementTerminalValue.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"
#include <tuple>

using namespace heroQueueTest;

class NativeRulesResourceProjectionTest : public HeroQueueSelectorTest
{
protected:
	NativeRulesResourceProjectionTest() { useRevisedOrders = true; }
	static constexpr auto side = BattleSide::ATTACKER;
	void branches()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(arena());
		ASSERT_TRUE(newHorizonsCommand::usesRules(battle()->getHeroCommandRules()));
		ASSERT_NO_FATAL_FAILURE(branches());
	}
};

TEST_F(NativeRulesResourceProjectionTest, AcceptedOrderSpendsOnlyChildOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = parent->getCommandResources(side);
	ASSERT_GT(before.remaining, 0);
	const auto outside = observation();
	const auto allowance = child->prepareHeroOrderAllowance(side);
	ASSERT_TRUE(allowance);
	const auto effects = child->beginProjectedHeroOrder(side, HeroCommand::CHARGE, {}, *allowance);
	ASSERT_TRUE(effects);
	EXPECT_EQ(child->getCommandResources(side), before);
	ASSERT_TRUE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	EXPECT_EQ(child->getCommandResources(side).remaining, before.remaining - 1);
	EXPECT_EQ(child->getCommandResources(side).maximum, before.maximum);
	EXPECT_EQ(parent->getCommandResources(side), before);
	EXPECT_EQ(sibling->getCommandResources(side), before);
	EXPECT_EQ(battle()->getCommandResources(side), before);
	EXPECT_FALSE(child->projectAcceptedHeroOrder(side, *effects, *allowance));
	EXPECT_EQ(child->getCommandResources(side).remaining, before.remaining - 1);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, ExhaustedPoolBlocksOrderButKeepsSpellAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto spent = child->getCommandResources(side);
	spent.remaining = 0;
	child->setCommandResources(side, spent);
	EXPECT_FALSE(child->prepareHeroOrderAllowance(side));
	PossibleSpellcast order;
	order.command = HeroCommand::CHARGE;
	EXPECT_FALSE(heroActionProjection::admitCandidate(*child, side, order).admitted);
	EXPECT_TRUE(child->prepareHeroSpellAllowance(side, SpellID::CURE, false, false));
	EXPECT_EQ(child->battleActiveUnit()->unitId(), active->unitId());
	EXPECT_GT(parent->getCommandResources(side).remaining, 0);
}

TEST_F(NativeRulesResourceProjectionTest, PoolChangedAfterPreparationRejectsBeforeOrderEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto allowance = child->prepareHeroOrderAllowance(side);
	ASSERT_TRUE(allowance);
	auto changed = child->getCommandResources(side);
	ASSERT_GT(changed.remaining, 1);
	--changed.remaining;
	child->setCommandResources(side, changed);
	EXPECT_FALSE(child->beginProjectedHeroOrder(side, HeroCommand::CHARGE, {}, *allowance));
	EXPECT_FALSE(child->getHeroOrderState(side, HeroCommand::CHARGE));
	EXPECT_EQ(child->getCommandResources(side), changed);
}

TEST_F(NativeRulesResourceProjectionTest, ExhaustionStopsCandidateReadsBeforeValuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto spent = battle()->getCommandResources(side);
	spent.remaining = 0;
	battle()->setCommandResources(side, spent);
	class CountingCallback final : public CBattleCallback
	{
	public:
		int reads = 0;
		CountingCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
		std::shared_ptr<CPlayerBattleCallback> getBattle(const BattleID & id) override
		{ ++reads; return CBattleCallback::getBattle(id); }
	} calls;
	class Valuation final : public heroOrderCandidates::Valuation
	{
	public:
		mutable int scored = 0;
		float score(const heroOrderCandidates::ValuationRequest &) const override
		{ ++scored; return 1.0f; }
	} valuation;
	calls.onBattleStarted(battle());
	std::vector<PossibleSpellcast> candidates;
	heroOrderCandidates::appendOrders(candidates, calls, BattleID(0), side, false, valuation);
	EXPECT_TRUE(candidates.empty());
	EXPECT_EQ(calls.reads, 1);
	EXPECT_EQ(valuation.scored, 0);
}

TEST_F(NativeRulesResourceProjectionTest, ArrowVariantsUseRawPowerAndRejectNewUnaffordableSurcharge)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 12);
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto variants = heroSpellVariants::prepare(*battle(), attackerSideHero, arrow, side);
	EXPECT_EQ(variants.maxOvercharge, 5);
	std::vector<int> affordable;
	for(int q = 0; q <= variants.maxOvercharge; ++q)
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, arrow);
		cast.setOvercharge(q);
		spells::detail::ProblemImpl problem;
		if(arrow->battleMechanics(&cast)->canBeCast(problem)) affordable.push_back(q);
	}
	EXPECT_EQ(affordable, std::vector<int>({0, 1}));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 12);
}

TEST_F(NativeRulesResourceProjectionTest, EqualArrowCombatUtilityKeepsCheaperLevel)
{
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(arrow, nullptr);
	std::vector<PossibleSpellcast> candidates;
	for(int q = 0; q <= 6; ++q)
	{
		PossibleSpellcast option;
		option.spell = arrow;
		option.spellOvercharge = q;
		option.value = 100.0f; // Same resolved continuation, e.g. an already lethal hit.
		candidates.push_back(option);
	}
	const auto choice = heroActionSelection::choose(candidates, 0.0f, false);
	ASSERT_TRUE(choice);
	EXPECT_EQ(candidates[choice->ordinal].spellOvercharge, 0);
}

TEST_F(NativeRulesResourceProjectionTest, AttunementFlagsAreBranchLocalAndRepeatedFlagDoesNotInvalidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto version = child->getTreeVersion();
	child->setElementalAttunementMask(side, 4);
	EXPECT_EQ(child->getElementalAttunementMask(side), 4);
	EXPECT_EQ(parent->getElementalAttunementMask(side), 0);
	EXPECT_EQ(sibling->getElementalAttunementMask(side), 0);
	EXPECT_EQ(battle()->getElementalAttunementMask(side), 0);
	EXPECT_GT(child->getTreeVersion(), version);
	const auto attunedVersion = child->getTreeVersion();
	child->setElementalAttunementMask(side, 4);
	EXPECT_EQ(child->getTreeVersion(), attunedVersion);
	EXPECT_THROW(child->setElementalAttunementMask(side, 16), std::invalid_argument);
	EXPECT_EQ(child->getElementalAttunementMask(side), 4);
}

TEST_F(NativeRulesResourceProjectionTest, AcceptedElementalSpellChangesProjectedAttackPassivesOnly)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const auto brissa = HeroTypeID::decode("core:brissa");
	ASSERT_GE(brissa, 0);
	attackerSideHero->setHeroType(HeroTypeID(brissa));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::ICE_BOLT, "new-horizons:havocMagic"));
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth"));
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * water = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 5), 10);
	ASSERT_NE(water, nullptr);
	const auto * spellTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 3), 10000);
	ASSERT_NE(spellTarget, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	EXPECT_FALSE(child->unitElementalAttunement(water).attuned);
	DamageCache cache;
	cache.cacheDamage(water, enemy, child);
	const auto ordinaryDamage = cache.getDamage(water, enemy, child);
	PossibleSpellcast candidate;
	candidate.spell = SpellID(SpellID::ICE_BOLT).toSpell();
	candidate.dest.emplace_back(spellTarget);
	const auto admission = heroActionProjection::admitCandidate(*child, side, candidate);
	ASSERT_TRUE(admission.admitted);
	uint32_t target = spellTarget->unitId();
	const auto result = battleEvaluatorProjection::projectSpell(*child, *live,
		attackerSideHero, side, candidate, admission, target, {});
	ASSERT_TRUE(result.accepted);
	EXPECT_EQ(child->getElementalAttunementMask(side), 4);
	const auto childWater = child->getForUpdate(water->unitId());
	EXPECT_TRUE(childWater->elementalAttunementPassives().attuned);
	EXPECT_EQ(childWater->elementalAttunementPassives().meleeDamagePercent, 40);
	const auto cachedDamage = cache.getDamage(childWater.get(), child->battleGetUnitByID(enemy->unitId()), child);
	DamageCache fresh;
	EXPECT_EQ(cachedDamage, fresh.getDamage(childWater.get(), child->battleGetUnitByID(enemy->unitId()), child));
	EXPECT_GT(cachedDamage, ordinaryDamage);
	EXPECT_FALSE(parent->unitElementalAttunement(water).attuned);
	EXPECT_FALSE(sibling->unitElementalAttunement(water).attuned);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, TerminalElementalCastCreditsFirstFamilyOnly)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const auto brissa = HeroTypeID::decode("core:brissa");
	ASSERT_GE(brissa, 0);
	attackerSideHero->setHeroType(HeroTypeID(brissa));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FIRE_WALL, "new-horizons:havocMagic"));
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NE(addStack(side, creatureByName("core:fireElemental"), BattleHex(6, 5), 10), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 3), 10000), nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache original;
	original.buildDamageCache(parent, side);
	const auto allowance = child->prepareHeroSpellAllowance(side, SpellID::FIRE_WALL, false, false);
	ASSERT_TRUE(allowance);
	ASSERT_TRUE(child->beginProjectedHeroAction(side, *allowance));
	ASSERT_TRUE(child->projectAcceptedHeroSpell(side, SpellID::FIRE_WALL,
		std::numeric_limits<uint32_t>::max(), false, false, false, false, *allowance));
	ASSERT_EQ(child->getElementalAttunementMask(side), 2);
	EXPECT_GT(elementalAttunementAI::terminalMarginal(child, side, 0, original), 0.0f);
	EXPECT_EQ(elementalAttunementAI::terminalMarginal(child, side, 2, original), 0.0f);
	EXPECT_EQ(parent->getElementalAttunementMask(side), 0);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, CounteredElementalCastGrantsNoTerminalAttunementValue)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const auto brissa = HeroTypeID::decode("core:brissa");
	ASSERT_GE(brissa, 0);
	attackerSideHero->setHeroType(HeroTypeID(brissa));
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FIRE_WALL, "new-horizons:havocMagic"));
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NE(addStack(side, creatureByName("core:fireElemental"), BattleHex(6, 5), 10), nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto allowance = child->prepareHeroSpellAllowance(side, SpellID::FIRE_WALL, false, false);
	ASSERT_TRUE(allowance);
	ASSERT_TRUE(child->beginProjectedHeroAction(side, *allowance));
	ASSERT_TRUE(child->projectAcceptedHeroSpell(side, SpellID::FIRE_WALL,
		std::numeric_limits<uint32_t>::max(), false, false, true, true, *allowance));
	EXPECT_EQ(child->getElementalAttunementMask(side), 0);
	DamageCache original;
	EXPECT_EQ(elementalAttunementAI::terminalMarginal(child, side, 0, original), 0.0f);
}

TEST_F(NativeRulesResourceProjectionTest, UnknownEnemyKeepsPublicHealthRetaliationsAndDamageAcrossBranches)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".greaterEssence", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * water = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 5), 10);
	ASSERT_NE(water, nullptr);
	battle()->setElementalAttunementMask(side, 4);
	water->counterAttacks.use();
	const auto publicFacts = water->elementalAttunementPassives();
	ASSERT_EQ(publicFacts.maximumHealthPercent, 25);
	ASSERT_EQ(publicFacts.additionalRetaliations, 0);
	ASSERT_EQ(water->getAvailableHealth(), 10 * water->getMaxHealth());
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto rootWater = parent->getForUpdate(water->unitId());
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto outside = observation();
	for(const auto & branch : {parent, child, sibling})
	{
		EXPECT_EQ(branch->getElementalAttunementProfile(side), newHorizonsElementalAttunement::Profile{});
		EXPECT_FALSE(branch->hasCommandResourceObservation(side));
		EXPECT_FALSE(newHorizonsCommand::available(*branch, side));
		EXPECT_TRUE(newHorizonsCommand::available(*branch, BattleSide::DEFENDER));
		EXPECT_THROW(branch->getCommandResources(side), std::logic_error);
		EXPECT_FALSE(branch->prepareHeroOrderAllowance(side));
		const auto copy = branch->getForUpdate(water->unitId());
		EXPECT_EQ(copy->elementalAttunementPassives(), publicFacts);
		EXPECT_EQ(copy->getCount(), water->getCount());
		EXPECT_EQ(copy->getMaxHealth(), water->getMaxHealth());
		EXPECT_EQ(copy->getAvailableHealth(), water->getAvailableHealth());
		EXPECT_EQ(copy->counterAttacks.available(), water->counterAttacks.available());
		DamageCache expected, projected;
		EXPECT_EQ(projected.getDamage(copy.get(), branch->battleGetUnitByID(enemy->unitId()), branch),
			expected.getDamage(water, enemy, live));
	}
	const auto detached = child->getForUpdate(water->unitId())->acquireState();
	EXPECT_EQ(detached->elementalAttunementPassives(), publicFacts);
	EXPECT_EQ(detached->getAvailableHealth(), water->getAvailableHealth());
	const auto foreign = water->acquireState();
	ASSERT_EQ(foreign->unitId(), water->unitId());
	EXPECT_THROW(child->unitElementalAttunement(foreign.get()), std::invalid_argument);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, FullInitialExportPreservesResourcesAndAttunementFacts)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".greaterEssence", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * water = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(water, nullptr);
	battle()->setElementalAttunementMask(side, 4);
	water->counterAttacks.use();
	const auto expectedFacts = water->elementalAttunementPassives();
	const auto expectedProfile = battle()->getElementalAttunementProfile(side);
	const auto expectedPool = battle()->getCommandResources(side);
	environment = std::make_shared<QueueEnvironment>(gameState());
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	const auto nativeBefore = state(*authority);
	const auto authorityRngBefore = wire(*gameHandler->randomizer);
	const auto fixtureRngBefore = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto points = [&]
	{
		return std::vector<int64_t>{attackerSideHero->getManaAvailable(), attackerSideHero->getNormalSpellPoints(),
			attackerSideHero->getBufferSpellPoints(), defenderSideHero->getManaAvailable(),
			defenderSideHero->getNormalSpellPoints(), defenderSideHero->getBufferSpellPoints()};
	};
	const auto pointsBefore = points();
	const auto seed = InitialExportBattleSubject::capture(authority, *battle());
	parent = std::make_shared<HypotheticBattle>(environment.get(), seed);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto outside = observation();
	for(const auto & branch : {parent, child, sibling})
	{
		EXPECT_EQ(branch->getCommandResources(side), expectedPool);
		EXPECT_EQ(branch->getElementalAttunementProfile(side), expectedProfile);
		EXPECT_EQ(branch->getElementalAttunementMask(side), 4);
		const auto copy = branch->getForUpdate(water->unitId());
		EXPECT_EQ(copy->elementalAttunementPassives(), expectedFacts);
		EXPECT_EQ(copy->getMaxHealth(), water->getMaxHealth());
		EXPECT_EQ(copy->getAvailableHealth(), water->getAvailableHealth());
		EXPECT_EQ(copy->getCount(), water->getCount());
		EXPECT_EQ(copy->counterAttacks.available(), water->counterAttacks.available());
		DamageCache expected, projected;
		EXPECT_EQ(projected.getDamage(copy.get(), branch->battleGetUnitByID(enemy->unitId()), branch),
			expected.getDamage(water, enemy, authority));
	}
	EXPECT_EQ(observation(), outside);
	EXPECT_EQ(state(*authority), nativeBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), authorityRngBefore);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRngBefore);
	EXPECT_EQ(points(), pointsBefore);
}

TEST_F(NativeRulesResourceProjectionTest, UnknownEnemyPrimalVigilancePreservesConsumedRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".rebirthChain", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * water = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(water, nullptr);
	water->counterAttacks.use();
	battle()->setElementalAttunementMask(side, 4);
	ASSERT_EQ(water->counterAttacks.available(), 1);
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto root = parent->getForUpdate(water->unitId());
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto outside = observation();
	for(const auto & branch : {parent, child, sibling})
	{
		EXPECT_EQ(branch->getElementalAttunementProfile(side), newHorizonsElementalAttunement::Profile{});
		EXPECT_FALSE(branch->hasCommandResourceObservation(side));
		EXPECT_FALSE(newHorizonsCommand::available(*branch, side));
		EXPECT_TRUE(newHorizonsCommand::available(*branch, BattleSide::DEFENDER));
		EXPECT_THROW(branch->getCommandResources(side), std::logic_error);
		EXPECT_FALSE(branch->prepareHeroOrderAllowance(side));
		const auto copy = branch->getForUpdate(water->unitId());
		EXPECT_EQ(copy->elementalAttunementPassives().additionalRetaliations, 1);
		EXPECT_EQ(copy->counterAttacks.available(), 1);
	}
	child->getForUpdate(water->unitId())->counterAttacks.use();
	EXPECT_EQ(child->getForUpdate(water->unitId())->counterAttacks.available(), 0);
	EXPECT_EQ(root->counterAttacks.available(), 1);
	EXPECT_EQ(sibling->getForUpdate(water->unitId())->counterAttacks.available(), 1);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, UnknownNativeFormExpiresNormallyWithPublicHealthAndDamageParity)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".greaterEssence", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * water = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(water, nullptr);
	battle()->setElementalAttunementMask(side, 4);
	water->beginBattleForm(creatureByName("core:pikeman"), 1);
	ASSERT_TRUE(water->hasBattleForm());
	const auto oracle = water->acquireState();
	oracle->endBattleForm();
	ASSERT_EQ(oracle->elementalAttunementPassives().maximumHealthPercent, 25);
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto outside = observation();
	ASSERT_NO_THROW(child->nextRound());
	const auto restored = child->getForUpdate(water->unitId());
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_TRUE(parent->getForUpdate(water->unitId())->hasBattleForm());
	EXPECT_TRUE(sibling->getForUpdate(water->unitId())->hasBattleForm());
	EXPECT_EQ(child->getElementalAttunementProfile(side), newHorizonsElementalAttunement::Profile{});
	EXPECT_EQ(restored->elementalAttunementPassives(), oracle->elementalAttunementPassives());
	EXPECT_EQ(restored->getMaxHealth(), oracle->getMaxHealth());
	EXPECT_EQ(restored->getAvailableHealth(), oracle->getAvailableHealth());
	EXPECT_EQ(restored->getCount(), oracle->getCount());
	DamageCache projected, expected;
	EXPECT_EQ(projected.getDamage(restored.get(), child->battleGetUnitByID(enemy->unitId()), child),
		expected.getDamage(oracle.get(), enemy, live));
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeRulesResourceProjectionTest, HypnotizePreservesOriginalArmyEffectsForBothObserversAndHealthStates)
{
	ASSERT_NO_FATAL_FAILURE(world());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".greaterEssence", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * full = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	auto * wounded = addStack(side, creatureByName("core:waterElemental"), BattleHex(6, 2), 10);
	ASSERT_NE(full, nullptr);
	ASSERT_NE(wounded, nullptr);
	int64_t wound = 7;
	wounded->damage(wound);
	wounded->counterAttacks.use();
	battle()->setElementalAttunementMask(side, 4);
	const auto values = [](const battle::CUnitState & unit)
	{
		return std::tuple{unit.getCount(), unit.getMaxHealth(), unit.getFirstHPleft(),
			unit.getAvailableHealth(), unit.counterAttacks.available(), unit.elementalAttunementPassives()};
	};
	// Native available() latches its ordinary retaliation allowance. Establish
	// that observable basis before testing any projected mutation isolation.
	const auto fullBefore = values(*full);
	const auto woundedBefore = values(*wounded);
	const Bonus control(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	environment = std::make_shared<QueueEnvironment>(gameState());
	for(const auto observer : {PlayerColor(0), PlayerColor(1)})
	{
		live = std::make_shared<CPlayerBattleCallback>(battle(), observer);
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto outside = observation();
		if(observer == PlayerColor(1))
			EXPECT_EQ(child->getElementalAttunementProfile(side), newHorizonsElementalAttunement::Profile{});
		for(const auto * original : {full, wounded})
		{
			const auto changed = child->getForUpdate(original->unitId());
			const auto initial = original == full ? fullBefore : woundedBefore;
			ASSERT_EQ(changed->elementalAttunementPassives().maximumHealthPercent, 25);
			ASSERT_NO_THROW(child->addUnitBonus(original->unitId(), {control}));
			EXPECT_EQ(child->battleGetOwner(changed.get()), PlayerColor(1));
			EXPECT_EQ(changed->unitSide(), side);
			EXPECT_EQ(values(*changed), initial);
			child->removeUnitBonus(original->unitId(), {control});
			EXPECT_EQ(child->battleGetOwner(changed.get()), PlayerColor(0));
			EXPECT_EQ(values(*changed), initial);
			EXPECT_EQ(values(*parent->getForUpdate(original->unitId())), initial);
			EXPECT_EQ(values(*sibling->getForUpdate(original->unitId())), initial);
		}
		EXPECT_EQ(observation(), outside);
	}
}
