/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustPrimaryShotFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/battle/NewHorizonsGraveDustStrike.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../server/battles/BattlePrimaryStrikeAftermath.h"
#include <tuple>
namespace
{
class GraveDustCapturedShotTest : public GraveDustPrimaryShotFixture,
	public ::testing::WithParamInterface<std::tuple<bool, int>> {};
TEST_P(GraveDustCapturedShotTest, ActualMagicalPrimaryAppliesOnlyCurrentEntitlementAndNeverCloudCollateral)
{
	const auto [upgraded, mode] = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare(upgraded, mode));
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_FALSE(BattleAttackInfo(actor, victim, 0, true).physicalDamage);
	const auto before = victim->health.getCreatureHealthAvailable();
	const auto collateralBefore = collateral->getAvailableHealth();
	ASSERT_TRUE(shoot());
	ASSERT_FALSE(observed->attacks.empty());
	const auto & attack = observed->attacks.front();
	ASSERT_TRUE(attack.shot());
	ASSERT_TRUE(attack.spellLike());
	const auto primary = std::ranges::find(attack.bsa, victim->unitId(), &BattleStackAttacked::stackAttacked);
	const auto cloud = std::ranges::find(attack.bsa, collateral->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(primary, attack.bsa.end()); ASSERT_NE(cloud, attack.bsa.end());
	EXPECT_FALSE(primary->isSecondary()); EXPECT_TRUE(cloud->isSecondary());
	BattleAttackInfo cloudContext(actor, collateral, 0, true);
	cloudContext.secondaryAttack = true;
	EXPECT_TRUE(battlePrimaryStrikeAftermath::prepare(*battle(), cloudContext, attack.bsa).graveDust.add.empty());
	EXPECT_LT(victim->health.getCreatureHealthAvailable(), before);
	EXPECT_LT(collateral->getAvailableHealth(), collateralBefore);
	EXPECT_EQ(battle::healingReceivedReduction(*victim), mode == 0 ? 5000 : 0);
	EXPECT_EQ(battle::healingReceivedReduction(*collateral), 0);
	EXPECT_EQ(observed->dustRecipients, mode == 0 ? std::vector<uint32_t>{victim->unitId()} : std::vector<uint32_t>{});
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero), mode != 1);
	EXPECT_EQ(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
	{
		return bonus && bonus->stacking.find("creature-line-specialty") != std::string::npos;
	}), mode == 1);
	auto healing = victim->acquireState();
	const auto beforeHeal = healing->getAvailableHealth();
	const auto rawCapacity = healing->healingCapacity(1'000'000, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_GT(rawCapacity, 0);
	const auto expected = mode == 0 ? rawCapacity - rawCapacity / 2 : rawCapacity;
	int64_t budget = 1'000'000;
	EXPECT_EQ(healing->heal(budget, EHealLevel::RESURRECT, EHealPower::PERMANENT).healedHealthPoints, expected);
	EXPECT_EQ(budget, expected);
	EXPECT_EQ(healing->getAvailableHealth() - beforeHeal, expected);
	EXPECT_EQ(victim->getAvailableHealth(), beforeHeal);
}
INSTANTIATE_TEST_SUITE_P(CurrentMissingZeroLichAndPowerLich, GraveDustCapturedShotTest,
	::testing::Combine(::testing::Bool(), ::testing::Values(0, 1, 2)));
class GraveDustCurrentShotTest : public GraveDustPrimaryShotFixture, public ::testing::WithParamInterface<bool> {};
TEST_P(GraveDustCurrentShotTest, TemporaryOnlyAbsorptionDoesNotDepositDust)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	victim->health.addTemporaryHitPoints(10'000);
	const auto bodyBefore = victim->health.getCreatureHealthAvailable();
	beginCombat();
	ASSERT_TRUE(shoot());
	EXPECT_EQ(victim->health.getCreatureHealthAvailable(), bodyBefore);
	EXPECT_LT(victim->health.getTemporaryHitPoints(), 10'000);
	EXPECT_TRUE(observed->dustRecipients.empty());
	EXPECT_EQ(battle::healingReceivedReduction(*victim), 0);
}
TEST_P(GraveDustCurrentShotTest, LethalNativeRebirthDispersesOldDustAndGetsNoNewDust)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam(), 0, false, 1));
	for(const auto [type, value] : {std::pair{BonusType::REBIRTH, 100}, std::pair{BonusType::CASTS, 1}})
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID()));
	battle()->addUnitBonus(victim->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
	beginCombat();
	ASSERT_TRUE(shoot());
	ASSERT_FALSE(observed->attacks.empty());
	const auto & attack = observed->attacks.front();
	const auto primary = std::ranges::find(attack.bsa, victim->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(primary, attack.bsa.end());
	ASSERT_TRUE(primary->killed()); ASSERT_TRUE(primary->willRebirth());
	EXPECT_TRUE(victim->alive());
	EXPECT_EQ(battle::healingReceivedReduction(*victim), 0);
	EXPECT_TRUE(observed->dustRecipients.empty());
}
TEST_P(GraveDustCurrentShotTest, GenuineOverwatchPublishesDustBeforeMoverActionCompletes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam(), 0, true));
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0), BattleAction::makeWait(actor)));
	ASSERT_TRUE(battle()->battleOverwatchReady(actor));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	const auto before = victim->getAvailableHealth();
	ASSERT_TRUE(battle()->battleCanOverwatch(actor, victim, BattleHex(3, 5), BattleHex(4, 5)));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(1), BattleAction::makeMove(victim, BattleHex(4, 5))));
	ASSERT_FALSE(observed->attacks.empty());
	const auto & reaction = observed->attacks.front();
	EXPECT_EQ(reaction.stackAttacking, actor->unitId());
	EXPECT_TRUE(reaction.shot()); EXPECT_TRUE(reaction.counter());
	EXPECT_LT(victim->getAvailableHealth(), before);
	EXPECT_EQ(observed->dustRecipients, std::vector<uint32_t>{victim->unitId()});
	EXPECT_EQ(observed->installedReductions, std::vector<int32_t>{5000});
	EXPECT_EQ(battle::healingReceivedReduction(*victim), 0); // Same accepted MOVE completes its existing clock.
}
TEST_P(GraveDustCurrentShotTest, MeleeCannotApplyDust)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam(), 0, false, 200, true));
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(0), BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	ASSERT_FALSE(observed->attacks.empty());
	EXPECT_FALSE(observed->attacks.front().shot());
	EXPECT_TRUE(observed->dustRecipients.empty());
	EXPECT_EQ(battle::healingReceivedReduction(*victim), 0);
}
TEST_P(GraveDustCurrentShotTest, SeparateHeroSpellDoesNotBecomeAnOrdinaryPrimaryShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	if(!attackerSideHero->getArt(ArtifactPosition::SPELLBOOK)) giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	setTestSpellPointTotal(attackerSideHero, 100);
	beginCombat();
	const auto before = victim->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_LT(victim->getAvailableHealth(), before);
	EXPECT_TRUE(observed->dustRecipients.empty());
	EXPECT_EQ(battle::healingReceivedReduction(*victim), 0);
}
TEST_P(GraveDustCurrentShotTest, ActualRainOfArrowsInjuryDoesNotDepositDustOnItsSecondary)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsArchery::SKILL))), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	// Keep Moandor's authored Basic choice; both prerequisites are inert in this shot.
	ASSERT_TRUE(newHorizonsArchery::hasTargetCaller(attackerSideHero));
	for(const auto perk : {newHorizonsArchery::CROSSFIRE, newHorizonsArchery::RAIN_OF_ARROWS})
	{
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(std::string(newHorizonsArchery::SKILL),
			std::string(perk), MasteryLevel::EXPERT));
		attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL), std::string(perk)});
	}
	ASSERT_TRUE(newHorizonsArchery::hasRainOfArrows(attackerSideHero));
	const auto * original = actor->unitType(); ASSERT_NE(original, nullptr);
	const auto originalId = actor->creatureId();
	const auto originalRecords = original->getExportedBonusList();
	const auto originalRecordValues = originalRecords.toJsonNode();
	const auto originalSourceParents = original->getParentNodes();
	const auto originalActorParents = actor->getParentNodes();
	ASSERT_NE(std::ranges::find(originalActorParents, original), originalActorParents.end());
	ASSERT_TRUE(actor->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	{
		// Narrow physical Lich fixture: retain the real descriptor/family and raw ancestry.
		CBonusSystemNode mirror(BonusNodeType::CREATURE);
		for(const auto & record : originalRecords)
		{
			ASSERT_NE(record, nullptr);
			if(record->type == BonusType::SPELL_LIKE_ATTACK) continue;
			ASSERT_FALSE(record->updater); ASSERT_FALSE(record->propagationUpdater);
			ASSERT_FALSE(record->propagator); ASSERT_FALSE(record->limiter);
			mirror.addNewBonus(std::make_shared<Bonus>(*record));
		}
		for(const auto * parent : originalSourceParents) mirror.attachToSource(*parent);
		bool detached = false, attached = false;
		auto restore = vstd::makeScopeGuard([&]
		{
			if(attached) actor->detachFromSource(mirror);
			if(detached) actor->attachToSource(*original);
		});
		actor->detachFromSource(*original); detached = true;
		actor->attachToSource(mirror); attached = true;
		ASSERT_EQ(actor->unitType(), original); ASSERT_EQ(actor->creatureId(), originalId);
		ASSERT_FALSE(actor->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		ASSERT_TRUE(BattleAttackInfo(actor, victim, 0, true).physicalDamage);
		ASSERT_TRUE(actor->canShoot());
		beginCombat();
		ASSERT_FALSE(actor->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		ASSERT_TRUE(actor->canShoot());
		ASSERT_TRUE(battle()->battleCanShoot(actor, victim->getPosition()));
		const auto secondaryBefore = collateral->getAvailableHealth();
		ASSERT_TRUE(shoot());
		EXPECT_LT(collateral->getAvailableHealth(), secondaryBefore);
		EXPECT_TRUE(std::ranges::any_of(observed->injuries, [this](const StacksInjured & injury)
		{
			return std::ranges::any_of(injury.stacks, [this](const BattleStackAttacked & hit)
			{
				return hit.attackerID == actor->unitId() && hit.stackAttacked == collateral->unitId() && hit.damageAmount > 0;
			});
		}));
		EXPECT_EQ(observed->dustRecipients, std::vector<uint32_t>{victim->unitId()});
		EXPECT_EQ(battle::healingReceivedReduction(*victim), 5000);
		EXPECT_EQ(battle::healingReceivedReduction(*collateral), 0);
	}
	EXPECT_EQ(actor->unitType(), original); EXPECT_EQ(actor->creatureId(), originalId);
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	EXPECT_TRUE(std::ranges::is_permutation(actor->getParentNodes(), originalActorParents));
	EXPECT_EQ(original->getParentNodes(), originalSourceParents);
	EXPECT_EQ(original->getExportedBonusList().toJsonNode(), originalRecordValues);
	ASSERT_EQ(original->getExportedBonusList().size(), originalRecords.size());
	for(size_t index = 0; index < originalRecords.size(); ++index)
		EXPECT_EQ(original->getExportedBonusList()[index], originalRecords[index]);
}
TEST_P(GraveDustCurrentShotTest, CapturedEntitlementUsesVisibleEffectiveOwnerAndAllKnowingAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	beginCombat();
	const BattleAttackInfo attack(actor, victim, 0, true);
	CPlayerBattleCallback known(battle(), PlayerColor(0));
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	EXPECT_TRUE(newHorizonsGraveDustStrike::eligible(*battle(), attack));
	EXPECT_TRUE(newHorizonsGraveDustStrike::eligible(known, attack));
	EXPECT_FALSE(newHorizonsGraveDustStrike::eligible(hidden, attack));
}
INSTANTIATE_TEST_SUITE_P(LichAndPowerLich, GraveDustCurrentShotTest, ::testing::Bool());
}
