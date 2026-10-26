/*
 * HeroCommandRevisionEffectsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/HeroOrderEffects.h"
#include "../../../lib/battle/HeroCommandRoyalStandard.h"
#include "../../../lib/battle/HeroCommandRevision.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"

class HeroCommandRevisionEffectsTest : public HeroCommandFixture
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		prepareCommands();
	}
	void acceptCommandPerk(const char * id, const char * skillId = "new-horizons:command")
	{
		const auto rank = [this](const std::string & skill) { return attackerSideHero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rank, seed,
				attackerSideHero->getPerkOfferWeightContext());
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == id)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skillId, id));
					return;
				}
		}
		FAIL() << "Missing legal Command perk offer";
	}
	HeroOrderState snapshot(std::vector<uint32_t> recipients, int value, int duration)
	{
		HeroOrderState order;
		order.command = HeroCommand::VANGUARD;
		order.issuedRound = battle()->getRound();
		order.revision.emplace();
		order.revision->recipientUnitIds = std::move(recipients);
		std::sort(order.revision->recipientUnitIds.begin(), order.revision->recipientUnitIds.end());
		order.revision->vanguardStatBonus = value;
		order.revision->vanguardInitiativePercent = 5 * value;
		order.revision->expiresAfterRound = order.issuedRound + duration - 1;
		order.validateShape();
		return order;
	}
};

TEST_F(HeroCommandRevisionEffectsTest, RefreshRemovesOldBonusesBeforeLowerGrant)
{
	prepare();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	const auto first = snapshot({friendly->unitId()}, 3, 3);
	const auto initial = heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, first);
	heroCommands::applyIssuedOrderGrants(*battle(), initial);
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(friendly));
	EXPECT_EQ(heroCommands::activeVanguardBonuses(friendly)->statBonus, 3);
	const auto refresh = snapshot({friendly->unitId()}, 1, 1);
	const auto grants = heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, refresh);
	ASSERT_EQ(grants.removals.size(), 1);
	ASSERT_EQ(grants.removals.front().front().second.size(), 4);
	heroCommands::applyIssuedOrderGrants(*battle(), grants);
	const auto active = heroCommands::activeVanguardBonuses(friendly);
	ASSERT_TRUE(active);
	EXPECT_EQ(active->statBonus, 1);
	EXPECT_EQ(active->initiativePercent, 5);
	EXPECT_EQ(active->roundsRemaining, 1);
	EXPECT_EQ(friendly->getBonuses(CSelector(heroCommands::isVanguardBonus))->size(), 4);
}

TEST_F(HeroCommandRevisionEffectsTest, StasisPreservesOldNativeGrantAndRejectsNewGrant)
{
	prepare();
	auto * stopped = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(73), 10);
	const auto first = snapshot({stopped->unitId(), ordinary->unitId()}, 3, 3);
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, first));
	Bonus stasis(BonusDuration::PERMANENT, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	stopped->addNewBonus(std::make_shared<Bonus>(stasis));
	ASSERT_TRUE(stopped->isTimeStopped());
	EXPECT_FALSE(heroCommands::isVanguardCreature(stopped));
	const auto refresh = snapshot({ordinary->unitId()}, 1, 1);
	const auto grants = heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, refresh);
	for(const auto & batch : grants.removals)
		for(const auto & [id, bonuses] : batch)
			EXPECT_NE(id, stopped->unitId());
	heroCommands::applyIssuedOrderGrants(*battle(), grants);
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(stopped));
	EXPECT_EQ(heroCommands::activeVanguardBonuses(stopped)->statBonus, 3);
	EXPECT_EQ(heroCommands::activeVanguardBonuses(stopped)->roundsRemaining, 3);
	EXPECT_EQ(heroCommands::activeVanguardBonuses(ordinary)->statBonus, 1);
	EXPECT_EQ(stopped->getBonuses(CSelector(heroCommands::isVanguardBonus))->size(), 4);
}

TEST_F(HeroCommandRevisionEffectsTest, NativeMoraleImmunityAndLivingEligibilityRemainIntact)
{
	prepare();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	const auto * undead = addStack(BattleSide::ATTACKER, creatureByName("skeleton"), BattleHex(73), 10);
	EXPECT_FALSE(heroCommands::isVanguardCreature(undead));
	Bonus immunity(BonusDuration::PERMANENT, BonusType::NO_MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	friendly->addNewBonus(std::make_shared<Bonus>(immunity));
	const auto order = snapshot({friendly->unitId()}, 3, 3);
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, order));
	EXPECT_EQ(friendly->moraleVal(), 0);
	EXPECT_TRUE(friendly->hasBonusOfType(BonusType::NO_MORALE));
}

TEST_F(HeroCommandRevisionEffectsTest, FeintReducesPhysicalRetaliationWithoutOpposingHero)
{
	prepare();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 10);
	battle()->getSide(BattleSide::DEFENDER).heroID = ObjectInstanceID();
	ASSERT_EQ(battle()->getSideHero(BattleSide::DEFENDER), nullptr);
	BattleAttackInfo retaliation(enemy, friendly, 0, false);
	retaliation.retaliation = true;
	const auto before = battle()->calculateDmgRange(retaliation).damage;
	ASSERT_TRUE(issue(HeroCommand::FEINT));
	const auto after = battle()->calculateDmgRange(retaliation).damage;
	EXPECT_LT(after.min, before.min);
	EXPECT_LT(after.max, before.max);
	retaliation.retaliation = false;
	EXPECT_EQ(battle()->calculateDmgRange(retaliation).damage.min, before.min);
	retaliation.retaliation = true;
	retaliation.physicalDamage = false;
	const auto magical = battle()->calculateDmgRange(retaliation).damage;
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {});
	EXPECT_EQ(battle()->calculateDmgRange(retaliation).damage.min, magical.min);
}

TEST_F(HeroCommandRevisionEffectsTest, RetainedNativeVanguardStillEnablesCommandingPresence)
{
	useRevisedOrders = true;
	startGame();
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.aggressiveCommander"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.commandingPresence"));
	startBattle();
	beginCombat();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	const auto order = snapshot({friendly->unitId()}, 3, 3);
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, order));
	// A native timer can outlast the latest side snapshot after stasis/refresh.
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {});
	EXPECT_TRUE(battle()->battleHasCommandingPresence(friendly));
	friendly->removeBonusesRecursive(CSelector(heroCommands::isVanguardBonus));
	EXPECT_FALSE(battle()->battleHasCommandingPresence(friendly));
}

TEST_F(HeroCommandRevisionEffectsTest, LeadershipEfficiencyRetainsCommandWarcastingDivineAndVeteran)
{
	useRevisedOrders = true;
	startGame();
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.aggressiveCommander"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.veteranCommander"));
	EXPECT_EQ(heroCommands::leadershipEfficiencyPercent(*attackerSideHero, 40, 10), 195);
	EXPECT_EQ(heroCommands::leadershipEfficiencyPercent(*attackerSideHero, 0, 0), 145);
}

TEST_F(HeroCommandRevisionEffectsTest, RevisedRidersUseCapturedValuesAndLeaveHistoricalProfileAlone)
{
	useRevisedOrders = true;
	startGame();
	const auto command = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
	attackerSideHero->setSecSkillLevel(command, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.aggressiveCommander"));
	attackerSideHero->setSecSkillLevel(command, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:command.combinedArms"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:offense")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:offense.encirclement", "new-horizons:offense"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:armorer")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptCommandPerk("new-horizons:armorer.shieldMaster", "new-horizons:armorer"));
	const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
	auto current = file["combat"]["heroCommands"];
	const JsonNode oldFile(JsonPath::builtin("config/newHorizonsCombatV3"));
	const auto historical = oldFile["combat"]["heroCommands"];
	for(const auto order : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::FEINT})
	{
		EXPECT_EQ(heroCommands::revisedPerkBonus(current, attackerSideHero, order), 5);
		EXPECT_EQ(heroCommands::revisedPerkBonus(historical, attackerSideHero, order), 0);
	}
	current["perkBonuses"]["spearheadDamagePercent"].Integer() = 7;
	EXPECT_EQ(heroCommands::revisedPerkBonus(current, attackerSideHero, HeroCommand::CHARGE), 7);
}


class HeroCommandRevisionNativeAcceptanceTest : public HeroCommandRevisionEffectsTest
{
protected:
	CStack * actor = nullptr;
	CStack * pending = nullptr;
	CStack * opponent = nullptr;

	void activate(const CStack * unit)
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0);
		packet.stack = unit->unitId();
		packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}

	void clearDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	CStack * fixedMelee(BattleSide side, int hex)
	{
		EXPECT_TRUE(BattleHex(hex).isAvailable()) << "Native melee fixture requires an interior battlefield hex";
		auto * unit = addStack(side, creatureByName("core:pikeman"), BattleHex(hex), 1000);
		forceMaximumDamage(unit);
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		return unit;
	}

	std::pair<int64_t, int64_t> strike(CStack * source, CStack * target)
	{
		activate(source);
		const auto sourceHealth = source->getAvailableHealth();
		const auto targetHealth = target->getAvailableHealth();
		const bool accepted = gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(source),
			BattleAction::makeMeleeAttack(source, target->getPosition(), source->getPosition()));
		EXPECT_TRUE(accepted);
		return {targetHealth - target->getAvailableHealth(), sourceHealth - source->getAvailableHealth()};
	}

	void prepareNativeVanguard(int rounds)
	{
		useRevisedOrders = true;
		startGame();
		const auto capacity = attackerSideHero->getLeadershipCapacity();
		ASSERT_TRUE(capacity);
		const auto authoredLeadership = std::max<int64_t>(capacity->capacity, rounds * 750);
		ASSERT_EQ(std::clamp(authoredLeadership / 750, int64_t{1}, int64_t{3}), rounds);
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LEADERSHIP, BonusSource::OTHER,
			static_cast<int>(authoredLeadership - capacity->capacity), BonusSourceID()));
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 260, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 260, ChangeValueMode::ABSOLUTE);
		startBattle();
		clearDeployedUnits();
		actor = fixedMelee(BattleSide::ATTACKER, 70);
		pending = fixedMelee(BattleSide::ATTACKER, 73);
		opponent = fixedMelee(BattleSide::DEFENDER, 77);
		for(const auto & [unit, initiative] : std::vector<std::pair<CStack *, int>>{
			{actor, 200}, {pending, 100}, {opponent, 110}})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, initiative,
				BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		ASSERT_EQ(pending->acquireState()->getInitiative(), 100);
	}

	void assertNativeVanguardExpiry(int rounds)
	{
		ASSERT_NO_FATAL_FAILURE(prepareNativeVanguard(rounds));
		const auto issuedRound = battle()->getRound();
		ASSERT_TRUE(issue(HeroCommand::VANGUARD));
		const auto state = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD);
		ASSERT_TRUE(state && state->revision);
		EXPECT_EQ(state->revision->expiresAfterRound, issuedRound + rounds - 1);
		auto nativeLifetime = *state;
		EXPECT_FALSE(nativeLifetime.completeDisciplineActivation(pending->unitId(), issuedRound + 1));
		EXPECT_TRUE(nativeLifetime.divineDisciplineCompletedUnitIds.empty());
		EXPECT_NO_THROW(nativeLifetime.validateShape());
		auto malformed = *state;
		malformed.divineDisciplineCompletedUnitIds = {pending->unitId()};
		EXPECT_THROW(malformed.validateShape(), std::runtime_error);

		for(int elapsed = 0; elapsed < rounds; ++elapsed)
		{
			const auto current = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD);
			ASSERT_TRUE(current);
			EXPECT_TRUE(current->divineDisciplineRecipientUnitIds.empty());
			EXPECT_TRUE(current->divineDisciplineCompletedUnitIds.empty());
			const auto bonus = heroCommands::activeVanguardBonuses(pending);
			ASSERT_TRUE(bonus);
			EXPECT_EQ(bonus->roundsRemaining, rounds - elapsed);
			EXPECT_EQ(pending->acquireState()->getInitiative(), 118);
			ASSERT_NO_FATAL_FAILURE(endRound());
		}
		EXPECT_EQ(battle()->getRound(), issuedRound + rounds);
		EXPECT_FALSE(heroCommands::activeVanguardBonuses(pending));
		EXPECT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD));
		EXPECT_EQ(pending->acquireState()->getInitiative(), 100);
	}
};

TEST_F(HeroCommandRevisionNativeAcceptanceTest, ActualFeintMeleeReturnIsReducedWithoutChangingOrdinaryHitsAndExpires)
{
	useRevisedOrders = true;
	startGame();
	startBattle();
	clearDeployedUnits();
	battle()->getSide(BattleSide::DEFENDER).heroID = ObjectInstanceID();
	ASSERT_EQ(battle()->getSideHero(BattleSide::DEFENDER), nullptr);
	auto * first = fixedMelee(BattleSide::ATTACKER, 70);
	auto * firstTarget = fixedMelee(BattleSide::DEFENDER, 71);
	auto * second = fixedMelee(BattleSide::ATTACKER, 73);
	auto * secondTarget = fixedMelee(BattleSide::DEFENDER, 74);
	auto * third = fixedMelee(BattleSide::ATTACKER, 76);
	auto * thirdEnemy = fixedMelee(BattleSide::DEFENDER, 77);
	auto * fourth = fixedMelee(BattleSide::ATTACKER, 79);
	auto * fourthEnemy = fixedMelee(BattleSide::DEFENDER, 80);
	beginCombat();
	ASSERT_EQ(battle()->battleGetAttackLuck(first, firstTarget, false), 0);
	ASSERT_EQ(battle()->battleGetAttackLuck(firstTarget, first, false), 0);
	const auto untreated = strike(first, firstTarget);
	const auto ordinaryEnemyHit = strike(thirdEnemy, third);
	ASSERT_GT(untreated.first, 0);
	ASSERT_GT(untreated.second, 0);
	activate(second);
	ASSERT_TRUE(issue(HeroCommand::FEINT));
	const auto treated = strike(second, secondTarget);
	EXPECT_EQ(treated.first, untreated.first);
	EXPECT_GT(treated.second, 0);
	EXPECT_LT(treated.second, untreated.second);
	EXPECT_EQ(strike(fourthEnemy, fourth).first, ordinaryEnemyHit.first);
	ASSERT_NO_FATAL_FAILURE(endRound());
	EXPECT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FEINT));
	auto * afterExpiry = fixedMelee(BattleSide::ATTACKER, leftHex);
	auto * afterExpiryTarget = fixedMelee(BattleSide::DEFENDER, rightHex);
	const auto expired = strike(afterExpiry, afterExpiryTarget);
	EXPECT_EQ(expired.first, untreated.first);
	EXPECT_EQ(expired.second, untreated.second);
}

TEST_F(HeroCommandRevisionNativeAcceptanceTest, ActualVanguardExpiresAfterIssuingRound)
{
	assertNativeVanguardExpiry(1);
}

TEST_F(HeroCommandRevisionNativeAcceptanceTest, ActualVanguardExpiresAfterTwoRounds)
{
	assertNativeVanguardExpiry(2);
}

TEST_F(HeroCommandRevisionNativeAcceptanceTest, ActualVanguardExpiresAfterThreeRounds)
{
	assertNativeVanguardExpiry(3);
}

TEST_F(HeroCommandRevisionNativeAcceptanceTest, ActualVanguardReordersPendingQueueWithoutNewCreatureActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeVanguard(3));
	std::vector<battle::Units> before;
	battle()->battleGetTurnOrder(before, 0, 1);
	ASSERT_FALSE(before.empty());
	const auto beforePending = std::find(before.front().begin(), before.front().end(), pending);
	const auto beforeOpponent = std::find(before.front().begin(), before.front().end(), opponent);
	ASSERT_NE(beforePending, before.front().end());
	ASSERT_NE(beforeOpponent, before.front().end());
	EXPECT_LT(beforeOpponent - before.front().begin(), beforePending - before.front().begin());
	const auto activationsBefore = server.stackActivations.size();
	const auto actorBefore = battle()->getActiveStackID();
	ASSERT_FALSE(actor->moved());
	ASSERT_FALSE(pending->moved());
	ASSERT_TRUE(issue(HeroCommand::VANGUARD));
	EXPECT_EQ(pending->acquireState()->getInitiative(), 118);
	EXPECT_EQ(opponent->acquireState()->getInitiative(), 110);
	EXPECT_EQ(battle()->getActiveStackID(), actorBefore);
	EXPECT_FALSE(actor->moved());
	EXPECT_FALSE(pending->moved());
	for(size_t index = activationsBefore; index < server.stackActivations.size(); ++index)
		EXPECT_EQ(server.stackActivations[index].reason, BattleUnitTurnReason::HERO_COMMAND);
	std::vector<battle::Units> after;
	battle()->battleGetTurnOrder(after, 0, 1);
	ASSERT_FALSE(after.empty());
	EXPECT_EQ(after.front().size(), before.front().size());
	EXPECT_EQ(std::count(after.front().begin(), after.front().end(), pending), 1);
	const auto afterPending = std::find(after.front().begin(), after.front().end(), pending);
	const auto afterOpponent = std::find(after.front().begin(), after.front().end(), opponent);
	ASSERT_NE(afterPending, after.front().end());
	ASSERT_NE(afterOpponent, after.front().end());
	EXPECT_LT(afterPending - after.front().begin(), afterOpponent - after.front().begin());
}


class HeroCommandVanguardRoyalStandardTest : public HeroCommandRevisionEffectsTest
{
protected:
	CStack * recipient = nullptr;
	CStack * other = nullptr;

	void prepareEntitlement()
	{
		prepare();
		recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 10);
		other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(73), 10);
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MORALE, BonusSource::OTHER, -10, BonusSourceID()));
		auto order = snapshot({recipient->unitId()}, 3, 3);
		order.royalStandardRecipientUnitIds = {recipient->unitId()};
		order.revision->potencyFinalized = true;
		heroCommands::applyIssuedOrderGrants(*battle(),
			heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, order));
		battle()->setHeroOrderStates(BattleSide::ATTACKER, {order});
		ASSERT_LT(recipient->moraleVal(), 0);
		ASSERT_EQ(battle()->battleGetMorale(recipient), 0);
		ASSERT_TRUE(heroCommands::hasVanguardRoyalStandardEntitlement(recipient, BattleSide::ATTACKER));
	}
};

TEST_F(HeroCommandVanguardRoyalStandardTest, PausedRoyalEntitlementSurvivesRefreshElsewhereAndLatestSnapshotExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepareEntitlement());
	auto stasis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	recipient->addNewBonus(stasis);
	ASSERT_TRUE(recipient->isTimeStopped());
	auto refresh = snapshot({other->unitId()}, 1, 1);
	refresh.revision->potencyFinalized = true;
	heroCommands::applyIssuedOrderGrants(*battle(),
		heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, refresh));
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {refresh});
	advanceRound();
	EXPECT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD));
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(recipient));
	EXPECT_EQ(heroCommands::activeVanguardBonuses(recipient)->roundsRemaining, 3);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	EXPECT_FALSE(heroCommands::activeVanguardBonuses(other));
	recipient->removeBonus(stasis);
	ASSERT_FALSE(recipient->isTimeStopped());
	for(int round = 0; round < 2; ++round)
	{
		advanceRound();
		EXPECT_TRUE(heroCommands::hasVanguardRoyalStandardEntitlement(recipient, BattleSide::ATTACKER));
		EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
	}
	advanceRound();
	EXPECT_FALSE(heroCommands::hasVanguardRoyalStandardEntitlement(recipient, BattleSide::ATTACKER));
	EXPECT_FALSE(heroCommands::activeVanguardBonuses(recipient));
	EXPECT_LT(battle()->battleGetMorale(recipient), 0);
}

TEST_F(HeroCommandVanguardRoyalStandardTest, EntitlementChecksCurrentControllerAndReturnsWithOriginalControl)
{
	ASSERT_NO_FATAL_FAILURE(prepareEntitlement());
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {});
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	recipient->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwnerHero(recipient), defenderSideHero);
	ASSERT_EQ(recipient->unitSide(), BattleSide::ATTACKER);
	const auto currentSide = battle()->playerToSide(battle()->battleGetOwner(recipient));
	ASSERT_EQ(currentSide, BattleSide::DEFENDER);
	EXPECT_FALSE(heroCommands::hasVanguardRoyalStandardEntitlement(recipient, currentSide));
	EXPECT_LT(battle()->battleGetMorale(recipient), 0);
	recipient->removeBonus(control);
	EXPECT_EQ(battle()->battleGetOwnerHero(recipient), attackerSideHero);
	EXPECT_EQ(battle()->battleGetMorale(recipient), 0);
}

TEST_F(HeroCommandVanguardRoyalStandardTest, NonRoyalRefreshRemovesOldEntitlementWithSameBonusGroup)
{
	ASSERT_NO_FATAL_FAILURE(prepareEntitlement());
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {});
	const auto refresh = snapshot({recipient->unitId()}, 1, 1);
	const auto grants = heroCommands::prepareIssuedOrderGrants(*battle(), BattleSide::ATTACKER, refresh);
	ASSERT_EQ(grants.removals.size(), 1);
	ASSERT_EQ(grants.removals.front().front().second.size(), 5);
	heroCommands::applyIssuedOrderGrants(*battle(), grants);
	EXPECT_FALSE(heroCommands::hasVanguardRoyalStandardEntitlement(recipient, BattleSide::ATTACKER));
	EXPECT_LT(battle()->battleGetMorale(recipient), 0);
	EXPECT_EQ(recipient->getBonuses(CSelector(heroCommands::isVanguardBonus))->size(), 4);
}
