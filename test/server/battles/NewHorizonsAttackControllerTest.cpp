/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcFixture.h"
#include "../../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/spells/CurePhysicalAffliction.h"
#include "../../../server/battles/BattleSpecialtyAttackControllers.h"
namespace
{
class NewHorizonsAttackControllerTest : public NewHorizonsSpecialtyProcFixture
{
protected:
	void shooting(const char * hero, const char * creature, int count = 20)
	{
		prepare(hero, creature, count);
		enemy->setPosition(BattleHex(10, 5));
		noMorale(*actor); noMorale(*enemy);
		forceMaximumDamage(actor);
	}
	void shoot()
	{
		beginCombat();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, enemy)));
	}
	int shots() const
	{
		return std::ranges::count_if(server.attacks, [this](const BattleAttack & attack)
		{ return attack.stackAttacking == actor->unitId() && attack.shot(); });
	}
};
TEST_F(NewHorizonsAttackControllerTest, SharedBloodlustPlanMatchesLoadedAdvancedMeleeEffectWithoutMutation)
{
	prepare("core:krellion", "core:ogre");
	const auto before = actor->getAttack(false);
	const auto plan = newHorizonsAttackControllers::bloodlustBonusPlan();
	ASSERT_EQ(plan.size(), 1);
	EXPECT_EQ(plan.front().type, BonusType::PRIMARY_SKILL);
	EXPECT_EQ(plan.front().val, 6);
	EXPECT_EQ(plan.front().duration, BonusDuration::N_TURNS);
	EXPECT_EQ(plan.front().turnsRemain, 3);
	EXPECT_EQ(plan.front().effectRange, BonusLimitEffect::ONLY_MELEE_FIGHT);
	EXPECT_EQ(plan.front().source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(plan.front().sid, BonusSourceID(SpellID(SpellID::BLOODLUST)));
	EXPECT_EQ(actor->getAttack(false), before);
	EXPECT_FALSE(actor->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLOODLUST)))));
}
TEST_F(NewHorizonsAttackControllerTest, BarrageKillFactsExcludeFriendlyMachinesAndNonCasualties)
{
	shooting("core:valeska", "core:archer");
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	auto * machine = addStack(BattleSide::DEFENDER, creatureByName("core:ballista"), BattleHex(12, 5), 1);
	EXPECT_TRUE(newHorizonsAttackControllers::barrageKillQualifies(*battle(), *actor, *enemy, 1));
	EXPECT_FALSE(newHorizonsAttackControllers::barrageKillQualifies(*battle(), *actor, *enemy, 0));
	EXPECT_FALSE(newHorizonsAttackControllers::barrageKillQualifies(*battle(), *actor, *enemy, -1));
	EXPECT_FALSE(newHorizonsAttackControllers::barrageKillQualifies(*battle(), *actor, *friendly, 1));
	EXPECT_FALSE(newHorizonsAttackControllers::barrageKillQualifies(*battle(), *actor, *machine, 1));
}
TEST_F(NewHorizonsAttackControllerTest, BarrageAdmissionKeepsAcceptedIdentityImpactAmmoAndActivationLatch)
{
	shooting("core:valeska", "core:archer");
	const auto impact = enemy->getPosition();
	const auto id = enemy->unitId();
	const auto allowed = [&] { return newHorizonsAttackControllers::barrageRepeatAllowed(*battle(), actor, enemy, id, impact); };
	ASSERT_TRUE(allowed());
	EXPECT_FALSE(newHorizonsAttackControllers::barrageRepeatAllowed(*battle(), actor, enemy, id + 1, impact));
	EXPECT_FALSE(newHorizonsAttackControllers::barrageRepeatAllowed(*battle(), actor, enemy, id, BattleHex()));
	enemy->setPosition(BattleHex(11, 5));
	EXPECT_FALSE(allowed());
	enemy->setPosition(impact);
	auto state = actor->acquireState();
	state->armorerLastStandEndedActivation = true;
	publish(*state);
	EXPECT_FALSE(allowed());
	state = actor->acquireState();
	state->armorerLastStandEndedActivation = false;
	state->shots.use(state->shots.available());
	publish(*state);
	EXPECT_FALSE(allowed());
}
TEST_F(NewHorizonsAttackControllerTest, ArcherKillRepeatsOneSequenceAtHalfDamageWithoutSelfRetrigger)
{
	shooting("core:valeska", "core:archer");
	const auto ammo = actor->shots.available();
	shoot();
	ASSERT_EQ(shots(), 2);
	ASSERT_GE(server.attacks.size(), 2);
	EXPECT_EQ(actor->shots.available(), ammo - 2);
	EXPECT_GT(server.attacks.front().bsa.front().killedAmount, 0);
	const auto first = server.attacks.front().bsa.front().damageAmount;
	const auto repeat = server.attacks.back().bsa.front().damageAmount;
	EXPECT_LE(repeat, first / 2 + 1);
	EXPECT_GT(repeat, 0);
}
TEST_F(NewHorizonsAttackControllerTest, MarksmanKillRepeatsBothNativeShots)
{
	shooting("core:valeska", "core:marksman", 10);
	shoot();
	EXPECT_EQ(shots(), 4);
}
TEST_F(NewHorizonsAttackControllerTest, InsufficientAmmunitionStopsBarrageWithoutRestoringShots)
{
	shooting("core:valeska", "core:archer");
	auto state = actor->acquireState();
	state->shots.use(state->shots.total() - 1);
	publish(*state);
	shoot();
	EXPECT_EQ(shots(), 1);
	EXPECT_EQ(actor->shots.available(), 0);
}
TEST_F(NewHorizonsAttackControllerTest, SequenceWithoutCasualtiesDoesNotRepeat)
{
	shooting("core:valeska", "core:archer", 1);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 10000, BonusSourceID(), BonusSubtypeID(), BonusValueType::INDEPENDENT_MAX));
	shoot();
	EXPECT_EQ(shots(), 1);
}
TEST_F(NewHorizonsAttackControllerTest, DestroyedPrimaryNeverRetargetsAnotherEnemy)
{
	shooting("core:valeska", "core:archer", 10000);
	const auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
	const auto health = other->getAvailableHealth();
	shoot();
	EXPECT_EQ(shots(), 1);
	EXPECT_EQ(other->getAvailableHealth(), health);
}
TEST_F(NewHorizonsAttackControllerTest, ChristianDoublesPhysicalBallistaAndRetainsNormalDistancePenalty)
{
	shooting("core:christian", "core:ballista", 1);
	EXPECT_EQ(newHorizonsAttackControllers::ballistaPhysicalDamagePercent(*battle(), *actor), 200);
	EXPECT_FALSE(actor->hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE));
	enemy->setPosition(BattleHex(13, 5));
	EXPECT_FALSE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), enemy->getPosition()));
	enemy->setPosition(BattleHex(14, 5));
	EXPECT_TRUE(battle()->battleHasDistancePenalty(actor, actor->getPosition(), enemy->getPosition()));
}
TEST_F(NewHorizonsAttackControllerTest, GerwulfRequiresActualDamageAndRefreshesExactlyOneCripplePair)
{
	shooting("core:gerwulf", "core:ballista", 1);
	BattleAttackInfo hit(actor, enemy, 0, true);
	EXPECT_TRUE(newHorizonsAttackControllers::captureStrike(*battle(), hit, 0).cripple.add.empty());
	const auto speed = enemy->getMovementRange(0);
	for(int application = 0; application < 2; ++application)
	{
		const auto plan = newHorizonsAttackControllers::captureStrike(*battle(), hit, 1);
		ASSERT_EQ(plan.cripple.add.size(), 2);
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan.cripple);
	}
	EXPECT_EQ(newHorizonsAttackControllers::crippleRemoval(*enemy).size(), 2);
	EXPECT_EQ(enemy->getMovementRange(0), std::max<int64_t>(0, static_cast<int64_t>(speed) - 3));
	for(const auto & bonus : newHorizonsAttackControllers::crippleRemoval(*enemy)) EXPECT_EQ(bonus.turnsRemain, 3);
}
TEST_F(NewHorizonsAttackControllerTest, CripplePhysicalRemovalPreservesAnUnrelatedPermanentSameHeroBonus)
{
	shooting("core:gerwulf", "core:ballista", 1);
	const auto plan = newHorizonsAttackControllers::captureStrike(*battle(), BattleAttackInfo(actor, enemy, 0, true), 1);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan.cripple);
	const int luckBefore = enemy->valOfBonuses(BonusType::LUCK);
	Bonus permanent(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::HERO_SPECIAL, 2,
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:gerwulf"))));
	enemy->addNewBonus(std::make_shared<Bonus>(permanent));
	EXPECT_TRUE(spells::curePhysical::present(battle()->getMagicRules(), *enemy, "cripple"));
	const auto groups = physicalAfflictions::enumerate(*enemy);
	const auto group = std::ranges::find(groups, std::string("cripple"), &physicalAfflictions::Affliction::kind);
	ASSERT_NE(group, groups.end());
	EXPECT_EQ(physicalAfflictions::removalPlan(*enemy, *group).size(), 2);
	SetStackEffect cleanse; cleanse.battleID = BattleID(0);
	cleanse.toRemove.emplace_back(enemy->unitId(), newHorizonsAttackControllers::crippleRemoval(*enemy));
	gameHandler->sendAndApply(cleanse);
	EXPECT_EQ(enemy->valOfBonuses(BonusType::LUCK), luckBefore + 2);
	EXPECT_FALSE(spells::curePhysical::present(battle()->getMagicRules(), *enemy, "cripple"));
}
TEST_F(NewHorizonsAttackControllerTest, FireballUsesActualHeroPowerAndMasteryWithoutManualHeroCast)
{
	shooting("core:torosar", "core:ballista", 1);
	newHorizonsAttackControllers::BallistaCaster caster(*actor, *attackerSideHero);
	const auto * spell = SpellID(SpellID::FIREBALL).toSpell();
	EXPECT_EQ(caster.getEffectPower(spell), attackerSideHero->getEffectPower(spell));
	EXPECT_EQ(caster.getEffectLevel(spell), attackerSideHero->getEffectLevel(spell));
	EXPECT_EQ(caster.getCasterUnitId(), actor->unitId());
	const auto mana = std::pair{attackerSideHero->getNormalSpellPoints(), attackerSideHero->getBufferSpellPoints()};
	BattleAttackInfo accepted(actor, enemy, 0, true); accepted.defenderPos = enemy->getPosition();
	auto plan = newHorizonsAttackControllers::captureStrike(*battle(), accepted, 0);
	ASSERT_TRUE(plan.fireball);
	battleSpecialtyAttackControllers::apply(*gameHandler, *battle(), plan);
	const auto casts = server.castsOf(SpellID(SpellID::FIREBALL));
	ASSERT_EQ(casts.size(), 1);
	EXPECT_FALSE(casts.front().announcement.castByHero);
	EXPECT_FALSE(casts.front().announcement.activeCast);
	EXPECT_EQ((std::pair{attackerSideHero->getNormalSpellPoints(), attackerSideHero->getBufferSpellPoints()}), mana);
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
}
TEST_F(NewHorizonsAttackControllerTest, FireballRemembersImpactHexAfterThePrimaryDies)
{
	shooting("core:torosar", "core:ballista", 1);
	BattleAttackInfo accepted(actor, enemy, 0, true); accepted.defenderPos = enemy->getPosition();
	const auto plan = newHorizonsAttackControllers::captureStrike(*battle(), accepted, 1);
	const auto * neighbor = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 5), 100);
	const auto health = neighbor->getAvailableHealth();
	StacksInjured death; death.battleID = BattleID(0);
	BattleStackAttacked injury; injury.stackAttacked = enemy->unitId(); injury.damageAmount = enemy->getAvailableHealth();
	CStack::prepareAttacked(injury, gameHandler->getRandomGenerator(), enemy->acquireState());
	death.stacks.push_back(injury); gameHandler->sendAndApply(death);
	ASSERT_FALSE(enemy->alive());
	battleSpecialtyAttackControllers::apply(*gameHandler, *battle(), plan);
	EXPECT_LT(neighbor->getAvailableHealth(), health);
}
TEST_F(NewHorizonsAttackControllerTest, OgreBloodlustIncludesSelfAndOnlyCurrentLegallyReceptiveFriends)
{
	prepare("core:krellion", "core:ogre");
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	const auto pool = newHorizonsAttackControllers::bloodlustRecipients(*battle(), *actor);
	EXPECT_TRUE(vstd::contains(pool, actor->unitId()));
	EXPECT_TRUE(vstd::contains(pool, friendly->unitId()));
	EXPECT_FALSE(vstd::contains(pool, enemy->unitId()));
	BattleAttackInfo hit(actor, enemy, 0, false); hit.retaliation = true;
	const auto plan = newHorizonsAttackControllers::captureStrike(*battle(), hit, 0);
	ASSERT_TRUE(plan.bloodlust);
	battleSpecialtyAttackControllers::apply(*gameHandler, *battle(), plan);
	const auto casts = server.castsOf(SpellID(SpellID::BLOODLUST)); ASSERT_EQ(casts.size(), 1);
	EXPECT_FALSE(casts.front().announcement.activeCast);
	int affected = 0;
	for(auto * unit : {actor, friendly})
		for(const auto & bonus : *unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLOODLUST)))))
		{
			++affected; EXPECT_EQ(bonus->val, 6); EXPECT_EQ(bonus->turnsRemain, 3);
			EXPECT_EQ(bonus->effectRange, BonusLimitEffect::ONLY_MELEE_FIGHT);
		}
	EXPECT_EQ(affected, 1);
	hit.secondaryAttack = true;
	EXPECT_FALSE(newHorizonsAttackControllers::captureStrike(*battle(), hit, 1).bloodlust);
}
TEST_F(NewHorizonsAttackControllerTest, CrippleCopiesAndCurrentPacketRoundtripsButOldWriterRefusesBeforePrefix)
{
	shooting("core:gerwulf", "core:ballista", 1);
	const auto plan = newHorizonsAttackControllers::captureStrike(*battle(), BattleAttackInfo(actor, enemy, 0, true), 1);
	SetStackEffect packet; packet.battleID = BattleID(0); packet.toAdd.emplace_back(enemy->unitId(), plan.cripple.add);
	CMemorySerializer current; packet.serialize(current.oser);
	SetStackEffect restored; restored.serialize(current.iser);
	ASSERT_EQ(restored.toAdd.size(), 1); ASSERT_EQ(restored.toAdd.front().second.size(), 2);
	for(size_t i = 0; i < 2; ++i)
		EXPECT_EQ(restored.toAdd.front().second[i].toJsonNode(), packet.toAdd.front().second[i].toJsonNode());
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan.cripple);
	auto copy = enemy->acquireState();
	EXPECT_EQ(newHorizonsAttackControllers::crippleRemoval(*copy).size(), 2);
	CMemorySerializer old; old.oser.version = ESerializationVersion::NEW_HORIZONS_FACTION_SKILL_REVISIONS;
	EXPECT_THROW(packet.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST_F(NewHorizonsAttackControllerTest, ForgedCurrentCrippleRejectsBeforeDirectAndPacketPrefixes)
{
	shooting("core:gerwulf", "core:ballista", 1);
	auto plan = newHorizonsAttackControllers::captureStrike(*battle(), BattleAttackInfo(actor, enemy, 0, true), 1);
	ASSERT_EQ(plan.cripple.add.size(), 2);
	auto forged = plan.cripple.add.front();
	forged.val = -4;
	CMemorySerializer direct;
	EXPECT_THROW(direct.oser & forged, std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	SetStackEffect packet; packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{forged});
	CMemorySerializer enclosing;
	EXPECT_THROW(packet.serialize(enclosing.oser), std::runtime_error);
	EXPECT_TRUE(enclosing.extractBuffer().empty());
}
TEST_F(NewHorizonsAttackControllerTest, PreemptiveCollateralAndSpellLikeShotsDoNotCreateMachineSpellReceipts)
{
	shooting("core:torosar", "core:ballista", 1);
	BattleAttackInfo hit(actor, enemy, 0, true);
	hit.secondaryAttack = true;
	EXPECT_FALSE(newHorizonsAttackControllers::captureStrike(*battle(), hit, 1).fireball);
	hit.secondaryAttack = false; hit.preemptiveDamagePercent = 50;
	EXPECT_FALSE(newHorizonsAttackControllers::captureStrike(*battle(), hit, 1).fireball);
	hit.preemptiveDamagePercent = 0; hit.physicalDamage = false;
	EXPECT_FALSE(newHorizonsAttackControllers::captureStrike(*battle(), hit, 1).fireball);
}
TEST_F(NewHorizonsAttackControllerTest, AcceptedOgreBloodlustPrecedesLethalFireShieldReaction)
{
	prepare("core:krellion", "core:ogre", 1);
	noMorale(*actor); noMorale(*enemy); blockRetaliation(actor); forceMaximumDamage(actor);
	passiveCast(SpellID(SpellID::FIRE_SHIELD), *enemy, *enemy);
	auto state = actor->acquireState();
	int64_t wound = state->getAvailableHealth() - 1;
	state->health.damage(wound);
	publish(*state);
	const auto actorId = actor->unitId();
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy, actor->getPosition())));
	EXPECT_EQ(server.castsOf(SpellID(SpellID::BLOODLUST)).size(), 1);
	if(const auto * current = gameState()->getBattle(BattleID(0)))
	{
		const auto * body = current->battleGetStackByID(actorId, false);
		EXPECT_TRUE(!body || !body->alive());
	}
}
}

namespace
{
TEST_F(NewHorizonsAttackControllerTest, ExhaustedBarrageDoesNotProvokeAnotherFirstStrikeRangedCounter)
{
	shooting("core:valeska", "core:archer", 20);
	battle()->removeUnit(enemy->unitId());
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(10, 5), 1000);
	noMorale(*enemy);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 10000, BonusSourceID(), BonusSubtypeID(), BonusValueType::INDEPENDENT_MAX));
	for(const auto type : {BonusType::RANGED_RETALIATION, BonusType::UNLIMITED_RETALIATIONS})
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::FIRST_STRIKE,
		BonusSource::OTHER, 1, BonusSourceID(), BonusCustomSubtype::damageTypeRanged));
	auto state = actor->acquireState();
	state->shots.use(state->shots.available() - 1);
	publish(*state);
	const auto enemyAmmo = enemy->shots.available();
	const auto actorHealth = actor->getAvailableHealth();
	const auto enemyHealth = enemy->getAvailableHealth();
	shoot();
	ASSERT_EQ(server.attacks.size(), 2);
	const auto & reaction = server.attacks.front();
	const auto & ordinary = server.attacks.back();
	EXPECT_EQ(reaction.stackAttacking, enemy->unitId());
	EXPECT_TRUE(reaction.counter());
	EXPECT_TRUE(reaction.shot());
	EXPECT_EQ(ordinary.stackAttacking, actor->unitId());
	EXPECT_FALSE(ordinary.counter());
	ASSERT_EQ(ordinary.bsa.size(), 1);
	EXPECT_GT(ordinary.bsa.front().killedAmount, 0) << "The ordinary shot must qualify for Barrage";
	ASSERT_EQ(reaction.bsa.size(), 1);
	EXPECT_EQ(actor->getAvailableHealth(), actorHealth - reaction.bsa.front().damageAmount);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth - ordinary.bsa.front().damageAmount);
	EXPECT_EQ(actor->shots.available(), 0);
	EXPECT_EQ(enemy->shots.available(), enemyAmmo - 1);
	EXPECT_EQ(std::ranges::count_if(server.startedActions, [](const auto & action)
		{ return action.ba.actionType == EActionType::SHOOT; }), 1);
}
}
