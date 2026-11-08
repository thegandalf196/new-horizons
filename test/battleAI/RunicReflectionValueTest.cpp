/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../server/battles/RunicReflectionFixture.h"
#include "../../AI/BattleAI/RunicReflectionValue.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../lib/spells/effects/native/Timed.h"
#include "../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../lib/GameSettings.h"
class RunicReflectionValueTest : public NewHorizonsRunicReflectionCombatTest
{
protected:
	std::shared_ptr<heroQueueTest::QueueEnvironment> environment;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;
	void branches()
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(const auto * state = dynamic_cast<const battle::CUnitState *>(unit))
			{ (void)state->counterAttacks.available(); (void)state->shots.available(); (void)state->casts.available(); }
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	runicReflectionAI::Estimate estimate(SpellID id, const spells::Target & aim)
	{
		spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, id.toSpell());
		const auto mechanics = id.toSpell()->battleMechanics(&cast);
		return runicReflectionAI::expectedValue(*mechanics, aim, PlayerColor(0), child, {1, 1, 1, true});
	}
	std::vector<std::byte> outside()
	{
		CMemorySerializer bytes;
		bytes.oser & *gameState(); bytes.oser & *gameHandler->randomizer;
		bytes.oser & *dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
		bytes.oser & CRandomGenerator::getDefault();
		for(const auto * view : {parent.get(), child.get(), sibling.get()})
		{
			bytes.oser & view->getTreeVersion();
			bytes.oser & view->getMoraleSuppressionState(BattleSide::ATTACKER);
			bytes.oser & view->getMoraleSuppressionState(BattleSide::DEFENDER);
			for(const auto * unit : view->battleGetAllUnits(false)) bytes.oser & unit->acquireState()->save();
		}
		return bytes.extractBuffer();
	}
};
TEST_F(RunicReflectionValueTest, SingleArrowPricesNativePortionWithoutChoosingRandomRecipientOrMutatingAnyWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepare(15));
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto original = outside();
	const auto command = child->getCommandResources(BattleSide::ATTACKER);
	const auto mask = child->getElementalAttunementMask(BattleSide::ATTACKER);
	const spells::Target aim{spells::Destination(child->battleGetUnitByID(dwarf->unitId()))};
	const auto value = estimate(SpellID::MAGIC_ARROW, aim);
	EXPECT_NEAR(value.value, -8, 1e-5);
	EXPECT_FALSE(value.partial);
	EXPECT_EQ(child->getCommandResources(BattleSide::ATTACKER), command);
	EXPECT_EQ(child->getElementalAttunementMask(BattleSide::ATTACKER), mask);
	EXPECT_EQ(outside(), original);
}
TEST_F(RunicReflectionValueTest, CompleteResistanceAndUnsupportedPairedEffectDoNotInventAReflectedCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(60));
	ASSERT_NO_FATAL_FAILURE(branches());
	EXPECT_EQ(estimate(SpellID::MAGIC_ARROW, {spells::Destination(dwarf)}).value, 0);
	const SpellID chain(SpellID::decode("new-horizons:soulChain"));
	for(const auto aim : {spells::Target{spells::Destination(dwarf), spells::Destination(casterArmy)},
		spells::Target{spells::Destination(casterArmy), spells::Destination(dwarf)}})
	{
		const auto result = estimate(chain, aim);
		EXPECT_EQ(result.value, 0);
		EXPECT_FALSE(result.partial); // Native attestation uses ordinary resistance.
	}
}
TEST_F(RunicReflectionValueTest, ExistingDefenderRerollWeightsTheAcceptedNativeSamplePartition)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, false, true));
	ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
	ASSERT_NO_FATAL_FAILURE(branches());
	EXPECT_NEAR(estimate(SpellID::MAGIC_ARROW, {spells::Destination(dwarf)}).value, -12.8f, 1e-4);
}
TEST_F(RunicReflectionValueTest, ElementalConduitIsCapturedOnceInReflectedDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	ASSERT_NO_FATAL_FAILURE(branches());
	EXPECT_NEAR(estimate(SpellID::MAGIC_ARROW, {spells::Destination(dwarf)}).value, -9.2f, 1e-4);
}
TEST_F(RunicReflectionValueTest, AreaReturnsOneRecipientShareRatherThanReplayingTheArea)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(69), 1000), nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000), nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	EXPECT_NEAR(estimate(SpellID::FIREBALL, {spells::Destination(dwarf->getPosition())}).value, -10, 1e-4);
}
TEST_F(RunicReflectionValueTest, LifeDrainKeepsItsDamageAndActualReflectorHealingPair)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID drain(SpellID::decode(std::string(newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL)));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 3, ChangeValueMode::ABSOLUTE);
	auto wounded = dwarf->acquireState(); int64_t injury = 7; wounded->damage(injury);
	BattleUnitsChanged update; update.battleID = BattleID(0);
	update.changedStacks.emplace_back(dwarf->unitId(), UnitChanges::EOperation::UPDATE);
	update.changedStacks.back().data = wounded->save(); update.changedStacks.back().healthDelta = -7;
	gameHandler->sendAndApply(update);
	ASSERT_NO_FATAL_FAILURE(branches());
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, drain.toSpell());
	const auto mechanics = drain.toSpell()->battleMechanics(&cast);
	const auto reflected = mechanics->runicReflectionView();
	const auto expectedDamage = reflected->adjustEffectValue(casterArmy);
	ASSERT_GT(expectedDamage * newHorizonsMagic::lifeDrainHealingPercent(attackerSideHero) / 100, 7);
	const auto result = estimate(drain, {spells::Destination(dwarf), spells::Destination(casterArmy)});
	EXPECT_NEAR(result.value, -0.4f * static_cast<float>(expectedDamage + 7), 1e-3);
}
TEST_F(RunicReflectionValueTest, ReflectedCurseUsesPreparedCurrentStatsWithoutExecutingTheReadOnlyView)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = outside();
	const auto result = estimate(SpellID::CURSE, {spells::Destination(dwarf)});
	EXPECT_FALSE(result.partial);
	EXPECT_LT(result.value, 0);
	EXPECT_EQ(outside(), before);
}
TEST_F(RunicReflectionValueTest, SorrowAndSlowReuseBoundedMoraleAndInitiativeKernelsWithoutSpendingSuppression)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	casterArmy->removeBonuses(Selector::type()(BonusType::NO_MORALE));
	casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, 3, BonusSourceID()));
	ASSERT_GT(battle()->battleGetMorale(casterArmy), 0);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = outside();
	for(const auto spell : {SpellID(SpellID::SORROW), SpellID(SpellID::SLOW)})
	{
		const auto result = estimate(spell, {spells::Destination(dwarf)});
		EXPECT_FALSE(result.partial);
		EXPECT_LT(result.value, 0);
	}
	EXPECT_EQ(outside(), before);
}
TEST_F(RunicReflectionValueTest, HandOfFateReusesTheExistingLateSpillBudgetAndUniformPoolWeight)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID fate(SpellID::decode("new-horizons:handOfFate"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(fate);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000);
	ASSERT_NE(primary, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, fate.toSpell());
	const auto mechanics = fate.toSpell()->battleMechanics(&cast);
	const spells::Target aim{spells::Destination(primary)};
	const auto before = outside();
	const auto result = SpellTargetEvaluator::handOfFateExpectedDamageValue(
		mechanics.get(), aim, PlayerColor(0), child, nullptr, {}, child);
	ASSERT_TRUE(result); EXPECT_LT(result->runicSpillValue, 0);
	const auto * effect = mechanics->findEffect<spells::effects::native::Damage>(); ASSERT_NE(effect, nullptr);
	const auto raw = mechanics->adjustEffectValue(primary) / 2;
	const auto reflectedSpill = runicReflectionAI::expectedRawDamageShare(*mechanics, *effect, *dwarf,
		raw, PlayerColor(0), child, {1, 1, 1, false});
	EXPECT_NEAR(result->runicSpillValue, reflectedSpill / 2, 1e-4);
	EXPECT_EQ(outside(), before);
}
TEST_F(RunicReflectionValueTest, BerserkAveragesTheExistingNearestForcedRecipientsWithoutActionOrWorldSearch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(69), 1000), nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = outside();
	const auto result = estimate(SpellID::BERSERK, {spells::Destination(dwarf)});
	EXPECT_FALSE(result.partial);
	EXPECT_LT(result.value, 0);
	EXPECT_EQ(outside(), before);
}
TEST_F(RunicReflectionValueTest, ReflectedFrailtyPricesIncomingDamageWithoutChangingCurrentDefense)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 3, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = outside();
	const SpellID frailty(SpellID::decode("new-horizons:frailty"));
	const auto result = estimate(frailty, {spells::Destination(dwarf)});
	EXPECT_FALSE(result.partial);
	EXPECT_LT(result.value, 0);
	EXPECT_EQ(outside(), before);
}
TEST_F(RunicReflectionValueTest, UnsupportedReflectedHypnotizeIsRejectedWhileNonRunicTargetRemainsSelectable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	auto * ordinary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1);
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = outside();
	std::vector<PossibleSpellcast> candidates(2);
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		auto & candidate = candidates[index];
		candidate.spell = SpellID(SpellID::HYPNOTIZE).toSpell();
		candidate.dest = {spells::Destination(child->battleGetUnitByID(index == 0 ? dwarf->unitId() : ordinary->unitId()))};
		spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
		const auto mechanics = candidate.spell->battleMechanics(&cast);
		ASSERT_TRUE(mechanics->canBeCastAt(candidate.dest));
		const auto reflected = runicReflectionAI::expectedValue(*mechanics, candidate.dest, PlayerColor(0), child, {});
		candidate.spellRunicExpectedValue = reflected.value;
		candidate.spellRunicEstimatePartial = reflected.partial;
		candidate.value = index == 0 ? 100 : 1;
	}
	ASSERT_TRUE(candidates.front().spellRunicEstimatePartial);
	EXPECT_EQ(candidates.front().spellRunicExpectedValue, 0);
	ASSERT_FALSE(candidates.back().spellRunicEstimatePartial);
	const auto selected = heroActionSelection::choose(candidates, 0, false);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->ordinal, 1);
	EXPECT_EQ(selected->disposition, heroActionSelection::Disposition::PERFORM);
	EXPECT_FALSE(heroActionSelection::choose(std::span<const PossibleSpellcast>(candidates.data(), 1), 0, false));
	EXPECT_EQ(outside(), before);
}
