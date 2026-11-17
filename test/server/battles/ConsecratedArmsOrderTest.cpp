/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/ConsecratedArmsOrder.h"
#include "../../../lib/battle/ConsecratedArmsCombat.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/RedemptionEvent.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../server/battles/BattleConsecratedArms.h"

namespace
{
using namespace consecratedArms;
class ConsecratedArmsOrderTest : public HeroCommandFixture
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		startGame();
		ASSERT_TRUE(enabled(attackerSideHero->getPerkState().getRules()));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SKILL)),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->removeAllSpells();
		attackerSideHero->initializeSpellPoints(0, 0);
		startBattle();
		beginCombat();
	}
	void grant(const battle::Unit & unit, Prayer prayer, Policy policy = Policy{1}, int faith = 200)
	{
		auto effects = orderEffects(*battle(), battle()->playerToSide(battle()->battleGetOwner(&unit)),
			{unit.unitId()}, {prayer}, policy, faith);
		gameHandler->sendAndApply(effects);
	}
};
}

TEST_F(ConsecratedArmsOrderTest, AcceptedOrderGrantsPrayerWithoutSpellOrAdditionalAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * recipient = battle()->battleActiveUnit();
	ASSERT_NE(recipient, nullptr);
	ASSERT_EQ(capturePolicy(attackerSideHero).rank, 1);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(activePrayerCount(*recipient), 1);
	EXPECT_TRUE(server.casts.empty());
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(activePrayerCount(*recipient), 1);
}

TEST_F(ConsecratedArmsOrderTest, CurrentCatalogueOffersAllBasicChoicesAndOnlyOnePerTier)
{
	useRevisedOrders = true;
	startGame();
	const auto skill = SecondarySkill(SecondarySkill::decode(SKILL));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	for(const auto * suffix : {"enduringPrayer", "joyfulDevotion", "fearlessAdvance", "unshakableFaith"})
		EXPECT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(SKILL, std::string(SKILL) + "." + suffix, 1));
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".unshakableFaith"});
	EXPECT_FALSE(attackerSideHero->getPerkState().canAcquirePerk(SKILL, std::string(SKILL) + ".fearlessAdvance", 1));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".ferventInvocation"});
	EXPECT_FALSE(attackerSideHero->getPerkState().canAcquirePerk(SKILL, std::string(SKILL) + ".sacredAccord", 2));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".antiphonalPrayer"});
	EXPECT_FALSE(attackerSideHero->getPerkState().canAcquirePerk(SKILL, std::string(SKILL) + ".livingLiturgy", 3));
	const auto policy = capturePolicy(attackerSideHero);
	ASSERT_EQ(policy.rank, 3);
	EXPECT_TRUE(policy.has(Perk::UNSHAKABLE_FAITH));
	EXPECT_TRUE(policy.has(Perk::FERVENT_INVOCATION));
	EXPECT_TRUE(policy.has(Perk::ANTIPHONAL_PRAYER));
	EXPECT_EQ(potencyFaith(200, policy), 400);
	EXPECT_EQ(duration(200, policy), 3);
}

TEST_F(ConsecratedArmsOrderTest, AllSixCoexistAndSamePrayerRefreshDoesNotStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * unit = battle()->battleActiveUnit();
	for(int index = 0; index < 6; ++index)
		grant(*unit, static_cast<Prayer>(index));
	ASSERT_EQ(activePrayerCount(*unit), 6);
	grant(*unit, Prayer::DIVINE_PURPOSE, Policy{1}, 400);
	ASSERT_EQ(activePrayerCount(*unit), 6);
	const auto purpose = activePrayer(*unit, Prayer::DIVINE_PURPOSE);
	ASSERT_TRUE(purpose);
	EXPECT_EQ(purpose->potencyFaith, 400);
	EXPECT_EQ(purpose->remainingRounds, 4);
	const auto components = unit->getBonuses(CSelector([](const Bonus * bonus)
	{
		return identity(bonus) == Prayer::DIVINE_PURPOSE;
	}));
	EXPECT_EQ(components->size(), 4);
}

TEST_F(ConsecratedArmsOrderTest, LiturgyRefreshesOtherDurationAndSelectedPrayerRecaptures)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * unit = battle()->battleActiveUnit();
	grant(*unit, Prayer::DIVINE_PURPOSE, Policy{1}, 100);
	advanceRound();
	Policy liturgy{3, static_cast<uint16_t>(Perk::LIVING_LITURGY), 100, 100};
	grant(*unit, Prayer::STEADFAST_SOUL, liturgy, 400);
	const auto old = activePrayer(*unit, Prayer::DIVINE_PURPOSE);
	const auto selected = activePrayer(*unit, Prayer::STEADFAST_SOUL);
	ASSERT_TRUE(old);
	ASSERT_TRUE(selected);
	EXPECT_EQ(old->potencyFaith, 100);
	EXPECT_EQ(old->remainingRounds, 4);
	EXPECT_EQ(old->policy.perks, 0);
	EXPECT_EQ(selected->potencyFaith, 500);
	EXPECT_TRUE(selected->policy.has(Perk::LIVING_LITURGY));
}

TEST_F(ConsecratedArmsOrderTest, PrayerRecordsRoundTripThroughNativeBonusSerialization)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const Policy policy{3, static_cast<uint16_t>(Perk::JOYFUL_DEVOTION)
		| static_cast<uint16_t>(Perk::SACRED_ACCORD)};
	auto before = prayerBonuses(Prayer::DIVINE_PURPOSE, 300, policy, 4, 4);
	CMemorySerializer memory;
	memory.oser & before;
	std::vector<Bonus> after;
	memory.iser & after;
	ASSERT_EQ(after.size(), before.size());
	for(size_t index = 0; index < before.size(); ++index)
	{
		EXPECT_EQ(after[index].val, before[index].val);
		EXPECT_EQ(after[index].turnsRemain, before[index].turnsRemain);
		EXPECT_EQ(after[index].statusIdentity, before[index].statusIdentity);
		EXPECT_EQ(after[index].statusTags, before[index].statusTags);
		EXPECT_EQ(identity(&after[index]), Prayer::DIVINE_PURPOSE);
	}
}

TEST_F(ConsecratedArmsOrderTest, DauntlessReducesOnlyProvokedMeleeRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * unit = battle()->battleActiveUnit();
	grant(*unit, Prayer::DAUNTLESS_ASSAULT, Policy{1}, 200);
	BattleAttackInfo hit(unit, unit, 0, false);
	DamageEstimation normal;
	normal.damage = {1000, 1000};
	adjustRetaliationDamage(hit, normal);
	EXPECT_EQ(normal.damage.min, 1000);
	hit.retaliation = true;
	adjustRetaliationDamage(hit, normal);
	EXPECT_EQ(normal.damage.min, 600);
	EXPECT_EQ(normal.damage.max, 600);
	hit.shooting = true;
	adjustRetaliationDamage(hit, normal);
	EXPECT_EQ(normal.damage.min, 600);
}

TEST_F(ConsecratedArmsOrderTest, NativeCasualtiesEarnShieldAfterEventAndRefreshDoesNotRefill)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	grant(*recipient, Prayer::VICARIOUS_REDEMPTION);
	const auto maximumHP = recipient->getMaxHealth();
	ASSERT_GT(maximumHP, 0);
	battle::RedemptionEvent event;
	BattleStackAttacked hit;
	hit.stackAttacked = recipient->unitId();
	hit.damageAmount = maximumHP;
	auto state = recipient->acquireState();
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE};
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state, false, false,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr, &context);
	ASSERT_EQ(hit.killedAmount, 1);
	StacksInjured damage;
	damage.battleID = battle()->getBattleID();
	damage.stacks.push_back(hit);
	event.record(*battle(), damage.stacks);
	gameHandler->sendAndApply(damage);
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), 0);
	event.complete(*battle(), *gameHandler->spellcastEnvironment());
	const int64_t earned = int64_t{maximumHP} * 3000 / 10000;
	ASSERT_EQ(recipient->getRedemptionShieldHitPoints(), earned);
	grant(*recipient, Prayer::VICARIOUS_REDEMPTION);
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), earned);
	advanceRound();
	advanceRound();
	advanceRound();
	EXPECT_FALSE(activePrayer(*recipient, Prayer::VICARIOUS_REDEMPTION));
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), 0);
}

TEST_F(ConsecratedArmsOrderTest, PhysicalStrikeAndJudgmentShareOneVowCasualtyQuota)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * source = battle()->battleActiveUnit();
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	grant(*source, Prayer::HEAVENS_JUDGMENT);
	grant(*recipient, Prayer::UNBROKEN_VOW);
	battleConsecratedArms::AttackContext attack;
	const auto context = battleConsecratedArms::damageContext(attack, *recipient, true);
	ASSERT_NE(context.vow, nullptr);
	BattleStackAttacked hit;
	hit.attackerID = source->unitId();
	hit.stackAttacked = recipient->unitId();
	hit.damageAmount = 1000000;
	auto state = recipient->acquireState();
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state, false, false,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr, &context);
	ASSERT_EQ(hit.killedAmount, 60);
	StacksInjured damage;
	damage.battleID = battle()->getBattleID();
	damage.stacks.push_back(hit);
	const auto judgment = battleConsecratedArms::prepareJudgment(*battle(), *source, true, damage.stacks, attack);
	ASSERT_FALSE(judgment.empty());
	gameHandler->sendAndApply(damage);
	ASSERT_EQ(recipient->getCount(), 40);
	EXPECT_EQ(context.vow->remainingCasualties, 0);
	battleConsecratedArms::applyJudgment(*gameHandler, *battle(), judgment, attack);
	EXPECT_EQ(recipient->getCount(), 40);
}

TEST_F(ConsecratedArmsOrderTest, UnstackedDecoderRejectsDuplicateAndInactivePrayerRecords)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * duplicate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	const auto bonuses = prayerBonuses(Prayer::UNBROKEN_VOW, 200, Policy{1}, 3, 1);
	duplicate->addNewBonus(std::make_shared<Bonus>(bonuses.front()));
	duplicate->addNewBonus(std::make_shared<Bonus>(bonuses.front()));
	EXPECT_THROW(activePrayers(*duplicate), std::runtime_error);
	auto * inactive = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	auto malformed = bonuses.front();
	malformed.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1, 0, 0});
	inactive->addNewBonus(std::make_shared<Bonus>(malformed));
	EXPECT_THROW(activePrayers(*inactive), std::runtime_error);
}

TEST_F(ConsecratedArmsOrderTest, EliminatedPrimaryStillPaysSurvivingCommunionAllyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * primary = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	grant(*primary, Prayer::VICARIOUS_REDEMPTION);
	grant(*ally, Prayer::VICARIOUS_REDEMPTION,
		Policy{2, static_cast<uint16_t>(Perk::COMMUNION_OF_SAINTS), 50, 100}, 150);
	battle::RedemptionEvent event;
	BattleStackAttacked hit;
	hit.stackAttacked = primary->unitId();
	hit.damageAmount = 1000000;
	const int64_t lostMaximumHP = int64_t{primary->getCount()} * primary->getMaxHealth();
	auto state = primary->acquireState();
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE};
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state, false, false,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr, &context);
	StacksInjured injuries;
	injuries.battleID = battle()->getBattleID();
	injuries.stacks.push_back(hit);
	event.record(*battle(), injuries.stacks);
	gameHandler->sendAndApply(injuries);
	ASSERT_FALSE(primary->alive());
	EXPECT_EQ(ally->getRedemptionShieldHitPoints(), 0);
	event.complete(*battle(), *gameHandler->spellcastEnvironment());
	EXPECT_EQ(primary->getRedemptionShieldHitPoints(), 0);
	EXPECT_EQ(ally->getRedemptionShieldHitPoints(), lostMaximumHP * 1500 / 10000);
	EXPECT_THROW(event.complete(*battle(), *gameHandler->spellcastEnvironment()), std::logic_error);
	EXPECT_EQ(ally->getRedemptionShieldHitPoints(), lostMaximumHP * 1500 / 10000);
}

TEST_F(ConsecratedArmsOrderTest, ShieldEarnedByPhysicalStrikeCannotAbsorbItsJudgmentRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * source = battle()->battleActiveUnit();
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	grant(*source, Prayer::HEAVENS_JUDGMENT);
	grant(*recipient, Prayer::VICARIOUS_REDEMPTION);
	const int64_t before = recipient->getAvailableHealth();
	const int64_t maximumHP = recipient->getMaxHealth();
	battleConsecratedArms::AttackContext event;
	const auto context = battleConsecratedArms::damageContext(event, *recipient, true);
	BattleStackAttacked hit;
	hit.attackerID = source->unitId();
	hit.stackAttacked = recipient->unitId();
	hit.damageAmount = maximumHP * 2;
	auto state = recipient->acquireState();
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state, false, false,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr, &context);
	StacksInjured physical;
	physical.battleID = battle()->getBattleID();
	physical.stacks.push_back(hit);
	const auto judgment = battleConsecratedArms::prepareJudgment(*battle(), *source, true, physical.stacks, event);
	ASSERT_EQ(judgment.size(), 1);
	const int64_t rider = newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *recipient, judgment.front().rawMagicalDamage);
	ASSERT_GT(rider, 0);
	event.redemption.record(*battle(), physical.stacks);
	gameHandler->sendAndApply(physical);
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), 0);
	battleConsecratedArms::applyJudgment(*gameHandler, *battle(), judgment, event);
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), 0);
	EXPECT_EQ(recipient->getAvailableHealth(), before - hit.damageAmount - rider);
	event.redemption.complete(*battle(), *gameHandler->spellcastEnvironment());
	EXPECT_EQ(recipient->getRedemptionShieldHitPoints(), maximumHP * 2 * 3000 / 10000);
	EXPECT_THROW(event.redemption.complete(*battle(), *gameHandler->spellcastEnvironment()), std::logic_error);
}
