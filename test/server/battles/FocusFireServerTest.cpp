/*
 * FocusFireServerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FocusFireFixture.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"

class FocusFireServerTest : public FocusFireFixture {};

TEST_F(FocusFireServerTest, InvalidTargetsPreserveBudgetAndPermitLegalRetry)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	const auto started = server.startedActions.size();
	const auto mana = attackerSideHero->mana;
	std::vector<BattleAction> invalid;
	invalid.push_back(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	invalid.push_back(focusAction(shooter->unitId()));
	invalid.push_back(focusAction(std::numeric_limits<int32_t>::max()));
	auto extra = focusAction(target->unitId());
	extra.target.push_back(extra.target.front());
	invalid.push_back(extra);
	auto wrongHex = focusAction(target->unitId());
	wrongHex.target.front().hexValue = target->getPosition();
	invalid.push_back(wrongHex);
	auto spellPayload = focusAction(target->unitId());
	spellPayload.spell = SpellID::HASTE;
	invalid.push_back(spellPayload);
	for(const auto & action : invalid)
	{
		EXPECT_FALSE(submit(action));
		EXPECT_EQ(server.startedActions.size(), started);
		EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
		EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
		EXPECT_EQ(attackerSideHero->mana, mana);
	}
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	ASSERT_EQ(server.startedActions.size(), started + 1);
	ASSERT_TRUE(server.startedActions.back().focusFire);
	EXPECT_EQ(server.startedActions.back().focusFire->targetUnitId, target->unitId());
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getActiveStackID(), shooter->unitId());
	EXPECT_EQ(attackerSideHero->mana, mana);
}

TEST_F(FocusFireServerTest, SpellLikePhysicalAreaPreviewOnlyRaisesTheMarkedPrimary)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
		BattleHex(rightHex + 5), 100);
	const auto * area = SpellID(SpellID::FIREBALL).toSpell();
	// Two adjacent equal-defense targets: establish the unmarked production
	// area path before testing primary-versus-collateral context in that path.
	ASSERT_EQ(battle()->estimateSpellLikeAttackDamage(shooter, area, target->getPosition()).damage.min, 300);
	ASSERT_EQ(battle()->estimateSpellLikeAttackDamage(shooter, area, other->getPosition()).damage.min, 300);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto primary = battle()->estimateSpellLikeAttackDamage(shooter, area, target->getPosition());
	const auto collateral = battle()->estimateSpellLikeAttackDamage(shooter, area, other->getPosition());
	EXPECT_EQ(primary.damage.min, 330);
	EXPECT_EQ(primary.damage.max, 330);
	EXPECT_EQ(collateral.damage.min, 300);
	EXPECT_EQ(collateral.damage.max, 300);
}

TEST_F(FocusFireServerTest, MarkedPrimaryShotLogsFocusFireWithResolvedDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
		BattleHex(rightHex + 5), 100);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true)).attackerOrderCause,
		HeroCommand::FOCUS_FIRE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(shooter, other, 0, true)).attackerOrderCause,
		HeroCommand::NONE);
	BattleAttackInfo secondary(shooter, target, 0, true);
	secondary.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(secondary).attackerOrderCause, HeroCommand::NONE);

	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(shooter, target)));
	const auto attack = std::ranges::find_if(server.attacks, [this](const BattleAttack & value)
	{
		return value.stackAttacking == shooter->unitId() && value.shot() && !value.counter();
	});
	ASSERT_NE(attack, server.attacks.end());
	const auto hit = std::ranges::find(attack->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attack->bsa.end());
	const auto causalLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Focus Fire:") != std::string::npos;
	});
	ASSERT_NE(causalLine, server.battleLogLines.end()) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(*causalLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
}

TEST_F(FocusFireServerTest, ShootingWarMachineCannotSupplyLegalityOrJoinFrozenCohort)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(battle()->battleCanShoot(machine, target->getPosition()));
	shooter->shots.use(1);
	ASSERT_FALSE(shooter->canShoot());
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_FALSE(submit(focusAction(target->unitId())));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	shooter->shots.reset(); // Fixture-only restoration, not an effect of issuing an Order.
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	EXPECT_FALSE(std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), machine->unitId()));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(machine, target, true), 0);
}

TEST_F(FocusFireServerTest, CommanderSlotAndTowerTypeAreExcludedAtIssue)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	// Fixture-only commander-slot sentinel. This does not activate commanders,
	// castellans, a second hero actor, or a new command-budget model.
	CStackBasicDescriptor descriptor(creatureByName("core:angel"), 2);
	auto sentinel = std::make_unique<CStack>(&descriptor, PlayerColor(0), battle()->nextUnitId(),
		BattleSide::ATTACKER, SlotID::COMMANDER_SLOT_PLACEHOLDER);
	auto * commander = sentinel.get();
	commander->initialPosition = BattleHex(leftHex - 2 * GameConstants::BFIELD_WIDTH);
	battle()->stacks.push_back(std::move(sentinel));
	commander->localInit(battle());
	commander->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
	commander->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOTS, BonusSource::OTHER, 1, BonusSourceID()));
	auto * tower = addStack(BattleSide::ATTACKER, CreatureID::ARROW_TOWERS,
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	ASSERT_EQ(commander->unitSlot(), SlotID::COMMANDER_SLOT_PLACEHOLDER);
	ASSERT_TRUE(battle()->battleCanShoot(commander, target->getPosition()));
	ASSERT_TRUE(tower->isTurret());
	shooter->shots.use(1);
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_FALSE(submit(focusAction(target->unitId())));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	shooter->shots.reset();
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	for(const auto * excluded : {commander, tower})
	{
		EXPECT_FALSE(std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), excluded->unitId()));
		EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(excluded, target, true), 0);
	}
}

TEST_F(FocusFireServerTest, TargetedOrderPreservesExistingUntilGetsTurnBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	Bonus temporary;
	temporary.type = BonusType::STACKS_SPEED;
	temporary.val = 5;
	temporary.duration = BonusDuration::STACK_GETS_TURN;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(shooter->unitId(), std::vector<Bonus>{temporary});
	gameHandler->sendAndApply(effect);
	const auto speed = shooter->getMovementRange();
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_EQ(battle()->getActiveStackID(), shooter->unitId());
	EXPECT_EQ(shooter->getMovementRange(), speed);
	EXPECT_FALSE(shooter->getAllBonuses(Bonus::UntilGetsTurn)->empty());
}

TEST_F(FocusFireServerTest, InternalStartActionRejectsForgedSnapshotsBeforeBudgetMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	StartAction valid;
	valid.battleID = BattleID(0);
	valid.ba = focusAction(target->unitId());
	valid.focusFire = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId());
	ASSERT_TRUE(valid.focusFire);
	std::vector<StartAction> malformed(6, valid);
	malformed[0].focusFire.reset();
	malformed[1].focusFire->rangedDamagePercent++;
	malformed[2].focusFire->issuedRound++;
	malformed[3].ba.command = HeroCommand::CHARGE;
	malformed[4].battleID = BattleID();
	malformed[5].battleID = BattleID(1);
	ASSERT_EQ(gameState()->getBattle(malformed[4].battleID), nullptr);
	ASSERT_EQ(gameState()->getBattle(malformed[5].battleID), nullptr);
	const auto mana = attackerSideHero->mana;
	const auto defenderMana = defenderSideHero->mana;
	for(auto & packet : malformed)
	{
		// Deliberately injected internal packets: test canonical publication,
		// not the recorder's count of explicitly supplied malformed packets.
		EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
		EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
		EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
		EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
		EXPECT_EQ(attackerSideHero->mana, mana);
		EXPECT_EQ(defenderSideHero->mana, defenderMana);
		EXPECT_EQ(attackerSideHero->battle, battle());
		EXPECT_EQ(defenderSideHero->battle, battle());
		EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::DEFENDER));
		EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::DEFENDER));
		EXPECT_EQ(battle()->getActiveStackID(), shooter->unitId());
	}
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	// An unknown/retired ID must not disturb a different live battle's active mark.
	// This injects an absent ID; it is not a battle-retirement or replay rollback test.
	try
	{
		gameHandler->sendAndApply(malformed[5]);
		ADD_FAILURE() << "Missing targeted battle context was accepted";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Missing targeted StartAction battle context");
	}
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::FOCUS_FIRE);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), mark);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::DEFENDER));
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_EQ(defenderSideHero->mana, defenderMana);
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
	EXPECT_EQ(battle()->getActiveStackID(), shooter->unitId());
}

TEST_F(FocusFireServerTest, RealLastShotAndPredictionUseAdditiveArcheryNotFinalMultiply)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	BattleAttackInfo info(shooter, target, 0, true);
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.min, 150);
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.max, 150);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.min, 180);
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.max, 180);
	info.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.min, 150);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, false), 0);

	const auto before = server.attacks.size();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(shooter, target)));
	unsigned found = 0;
	for(size_t i = before; i < server.attacks.size(); ++i)
	{
		const auto & attack = server.attacks[i];
		if(attack.stackAttacking != shooter->unitId() || !attack.shot() || attack.counter())
			continue;
		for(const auto & victim : attack.bsa)
		{
			if(victim.stackAttacked == target->unitId() && !victim.isSecondary())
			{
				++found;
				EXPECT_EQ(victim.damageAmount, 180); //195 would incorrectly multiply Archery's result.
			}
		}
	}
	EXPECT_EQ(found, 1u);
	EXPECT_EQ(shooter->shots.available(), 0);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
}

TEST_F(FocusFireServerTest, CohortIncludesEmptyAmmoButNeverLateIdsAndPremiumIsSnapshotted)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	auto * empty = addShooter(BattleSide::ATTACKER, BattleHex(leftHex + GameConstants::BFIELD_WIDTH));
	empty->shots.use(1);
	ASSERT_EQ(empty->shots.available(), 0);
	ASSERT_TRUE(empty->isShooter());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	EXPECT_TRUE(std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), empty->unitId()));
	EXPECT_EQ(empty->shots.available(), 0);
	auto * late = addShooter(BattleSide::ATTACKER, BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(late, target, true), 0);
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 20, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 30);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), mark);
}

TEST_F(FocusFireServerTest, StaticShotLegalityAllowsAlreadyActedButDoesNotGrantAnActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	shooter->movedThisRound = true;
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	activate(melee);
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_TRUE(shooter->movedThisRound);
	EXPECT_EQ(battle()->getActiveStackID(), melee->unitId());
	EXPECT_EQ(shooter->shots.available(), 1);
}

TEST_F(FocusFireServerTest, NoLegalAmmunitionRefusesBeforeStartAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	shooter->shots.use(1);
	const auto before = server.startedActions.size();
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_FALSE(submit(focusAction(target->unitId())));
	EXPECT_EQ(server.startedActions.size(), before);
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(shooter->shots.available(), 0);
}

TEST_F(FocusFireServerTest, SharedBudgetAndRoundExpiryKeepLegacyDoctrinesInactive)
{
	ASSERT_NO_FATAL_FAILURE(prepareFocus());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(submit(focusAction(target->unitId())));
	advanceRound();
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_FALSE(issue(HeroCommand::AGGRESSIVE));
	EXPECT_FALSE(issue(HeroCommand::DEFENSIVE));
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_TRUE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	advanceRound();
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, target, true), 0);
}
