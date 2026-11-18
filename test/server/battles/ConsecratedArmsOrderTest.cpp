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
#include "../../../lib/battle/StackEffectRemoval.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/battles/BattleConsecratedArms.h"
#include <set>

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
		// Shield capacity is captured for original army slots during localInit.
		// Later synthetic ADD units deliberately have no combat-start HP basis.
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SKILL)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			for(int slot = 0; slot < 3; ++slot)
				ASSERT_TRUE(hero->setCreature(SlotID(slot), creatureByName("core:pikeman"), 100));
		}
		startBattle();
		beginCombat();
	}
	CStack * original(BattleSide side, SlotID slot, BattleHex position)
	{
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == side && stack->unitSlot() == slot)
			{
				EXPECT_GT(stack->getBattleStartMaximumAggregateHP(), 0);
				stack->setPosition(position);
				return stack.get();
			}
		throw std::logic_error("Missing original prayer fixture stack");
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

TEST_F(ConsecratedArmsOrderTest, GrantNamesRecipientsAndPlaysOnePrayerVisualForBothAntiphonalPrayers)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(SKILL));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".enduringPrayer"});
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".sacredAccord"});
	attackerSideHero->applyPerkSelection({SKILL, std::string(SKILL) + ".antiphonalPrayer"});
	const auto * pikemen = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 7);
	const auto * swordsmen = addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(leftHex + 2), 11);
	ASSERT_NE(pikemen, nullptr);
	ASSERT_NE(swordsmen, nullptr);
	server.battleAnimations.clear();
	server.battleLogLines.clear();

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.battleAnimations.size(), 1);
	const auto & animation = server.battleAnimations.front();
	const auto * prayer = SpellID(SpellID::PRAYER).toSpell();
	ASSERT_NE(prayer, nullptr);
	ASSERT_EQ(prayer->animationInfo.affect.size(), 1);
	EXPECT_EQ(animation.animation, prayer->animationInfo.affect.front().resourceName);
	EXPECT_EQ(animation.transparency, prayer->animationInfo.affect.front().transparency);
	EXPECT_EQ(animation.sound, prayer->getCastSound());
	EXPECT_FALSE(animation.deferred);
	std::set<uint32_t> expected;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit && battle()->battleGetOwner(unit) == PlayerColor(0) && activePrayerCount(*unit))
			expected.insert(unit->unitId());
	std::set<uint32_t> actual;
	for(const auto & target : animation.targets)
	{
		ASSERT_GE(target.unitID, 0);
		EXPECT_TRUE(actual.insert(static_cast<uint32_t>(target.unitID)).second);
		EXPECT_EQ(target.tile, battle()->battleGetUnitByID(target.unitID)->getPosition());
	}
	EXPECT_EQ(actual, expected);
	EXPECT_TRUE(actual.contains(pikemen->unitId()));
	EXPECT_TRUE(actual.contains(swordsmen->unitId()));
	std::vector<std::string> grants;
	for(const auto & line : server.battleLogLines)
		if(line.starts_with("Consecrated Arms grants "))
			grants.push_back(line);
	ASSERT_EQ(grants.size(), 2);
	for(const auto & line : grants)
		for(const auto * unit : {pikemen, swordsmen})
		{
			MetaString name;
			name.appendRawString("%d %s");
			name.replaceNumber(unit->getCount());
			name.replaceName(unit->creatureId(), unit->getCount());
			EXPECT_NE(line.find(name.toString(LIBRARY->staticTexts())), std::string::npos);
		}
	EXPECT_TRUE(server.casts.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	const auto logCount = server.battleLogLines.size();
	EXPECT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(server.battleAnimations.size(), 1);
	EXPECT_EQ(server.battleLogLines.size(), logCount);
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
	{
		grant(*unit, static_cast<Prayer>(index));
		ASSERT_EQ(activePrayerCount(*unit), index + 1);
		for(int existing = 0; existing <= index; ++existing)
			ASSERT_TRUE(activePrayer(*unit, static_cast<Prayer>(existing)));
	}
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
	auto * recipient = original(BattleSide::ATTACKER, SlotID(1), BattleHex(leftHex));
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
	auto * recipient = original(BattleSide::DEFENDER, SlotID(2), BattleHex(rightHex));
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
	auto * duplicate = original(BattleSide::ATTACKER, SlotID(1), BattleHex(leftHex));
	const auto bonuses = prayerBonuses(Prayer::UNBROKEN_VOW, 200, Policy{1}, 3, 1);
	duplicate->addNewBonus(std::make_shared<Bonus>(bonuses.front()));
	duplicate->addNewBonus(std::make_shared<Bonus>(bonuses.front()));
	EXPECT_THROW(activePrayers(*duplicate), std::runtime_error);
	auto * inactive = original(BattleSide::DEFENDER, SlotID(2), BattleHex(rightHex));
	auto malformed = bonuses.front();
	malformed.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1, 0, 0});
	inactive->addNewBonus(std::make_shared<Bonus>(malformed));
	EXPECT_THROW(activePrayers(*inactive), std::runtime_error);
}

TEST_F(ConsecratedArmsOrderTest, EliminatedPrimaryStillPaysSurvivingCommunionAllyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * primary = original(BattleSide::ATTACKER, SlotID(1), BattleHex(leftHex));
	auto * ally = original(BattleSide::ATTACKER, SlotID(2), BattleHex(rightHex));
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
	auto * recipient = original(BattleSide::DEFENDER, SlotID(2), BattleHex(rightHex));
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

TEST_F(ConsecratedArmsOrderTest, EqualPayloadPrayerRemovalKeepsDistinctIdentities)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(int selected = 0; selected < 6; ++selected)
		for(int candidate = 0; candidate < 6; ++candidate)
		{
			const auto left = prayerBonuses(static_cast<Prayer>(selected), 200, Policy{1}, 3, 6);
			const auto right = prayerBonuses(static_cast<Prayer>(candidate), 200, Policy{1}, 3, 6);
			EXPECT_EQ(battle::matchesStackEffectRemoval(left.front(), right.front()), selected == candidate);
		}
}

TEST_F(ConsecratedArmsOrderTest, UnrelatedLegacyRemovalRetainsItsExistingContract)
{
	Bonus selected(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::OTHER, 200, BonusSourceID());
	selected.turnsRemain = 3;
	selected.statusIdentity = "custom.first";
	selected.stacking = "custom.first";
	auto candidate = selected;
	candidate.statusIdentity = "custom.second";
	candidate.stacking = "custom.second";
	EXPECT_TRUE(battle::matchesStackEffectRemoval(selected, candidate));
	candidate.val = 201;
	EXPECT_FALSE(battle::matchesStackEffectRemoval(selected, candidate));
}
