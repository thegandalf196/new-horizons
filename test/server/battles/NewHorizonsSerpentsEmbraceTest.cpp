/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSerpentsEmbraceFixture.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, NagaAttackBindsAndRetainsIntrinsicNoRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto health = source->getAvailableHealth();
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_TRUE(target->alive());
	EXPECT_TRUE(heldBy(source));
	EXPECT_EQ(target->getMovementRange(), 0);
	EXPECT_TRUE(target->canMove());
	EXPECT_EQ(source->getAvailableHealth(), health);
	EXPECT_FALSE(std::ranges::any_of(server.attacks, [](const auto & attack) { return attack.counter(); }));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, NagaQueenHasTheSameGuaranteedPrimaryBinding)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(attack(source, target->getPosition()));
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, OrdinaryNagaRetaliationBindsIncomingStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Leave another ordinary actor pending so automatic queue selection cannot
	// immediately begin the source’s next activation and hide the new hold.
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 8), 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(source)));
	battle()->activeStack = target->unitId();
	ASSERT_TRUE(attack(target, source->getPosition()));
	EXPECT_TRUE(heldBy(source));
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [](const auto & attack) { return attack.counter(); }));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, CapturedReplacementDoesNotStackOldGenericRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsHeroes::hasSerpentsEmbrace(attackerSideHero));
	EXPECT_FALSE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
		{ return bonus && bonus->stacking.find("creature-line-specialty") != std::string::npos; }));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, HistoricalAbsencePreservesGenericRiderWithoutBinding)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasSerpentsEmbrace(attackerSideHero));
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
		{ return bonus && bonus->stacking.find("creature-line-specialty") != std::string::npos; }));
	ASSERT_TRUE(attack(source, target->getPosition()));
	EXPECT_FALSE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, EachSourceExpiresIndependentlyAtItsActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * second = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:nagaQueen")), BattleHex(3, 2), 10);
	install(source, target);
	install(second, target);
	ASSERT_TRUE(heldBy(source));
	ASSERT_TRUE(heldBy(second));
	activate(source, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(heldBy(source));
	EXPECT_TRUE(heldBy(second));
	EXPECT_EQ(target->getMovementRange(), 0);
	activate(second, BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(heldBy(second));
	EXPECT_GT(target->getMovementRange(), 0);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, ContinuationsAndStoppedSyntheticQueueDoNotExpireSourceHold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
	{
		activate(source, reason);
		EXPECT_TRUE(heldBy(source));
	}
	auto stopped = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(BattleSide::ATTACKER));
	source->addNewBonus(stopped);
	activate(source, BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, SourceExpiryRemovesOnlyItsHoldOnStoppedTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	auto stopped = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(BattleSide::DEFENDER));
	target->addNewBonus(stopped);
	activate(source, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(heldBy(source));
	EXPECT_TRUE(target->isTimeStopped());
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, ForcedDisplacementRetainsSourceDurationHold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	const BattleHex destination(12, 4);
	BattleStackMoved move;
	move.battleID = battle()->getBattleID();
	move.stack = target->unitId();
	move.tilesToMove.insert(destination);
	move.displacementCause = BattleDisplacementCause::NON_MAGICAL;
	gameHandler->sendAndApply(move);
	EXPECT_EQ(target->getPosition(), destination);
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, SourceRemovalReleasesItsBindings)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	BattleUnitsChanged remove;
	remove.battleID = battle()->getBattleID();
	remove.changedStacks.emplace_back(source->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	EXPECT_FALSE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, ZeroPrimaryDamageAndExcludedContextsHaveNoApplication)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleStackAttacked hit;
	hit.stackAttacked = target->unitId();
	hit.damageAmount = 0;
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::prepareBinding(*battle(), source, target,
		false, true, false, {hit}).has_value());
	hit.damageAmount = 1;
	for(const auto context : {std::array{true, true, false}, std::array{false, false, false}, std::array{false, true, true}})
		EXPECT_FALSE(newHorizonsSerpentsEmbrace::prepareBinding(*battle(), source, target,
			context[0], context[1], context[2], {hit}).has_value());
	EXPECT_FALSE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, VoluntaryWalkIsRejectedBeforeAcceptingAnAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	battle()->activeStack = target->unitId();
	const auto before = target->save();
	const auto actions = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMove(target, BattleHex(rightHex + 1))));
	EXPECT_EQ(target->save(), before);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_TRUE(heldBy(source));
	EXPECT_TRUE(attack(target, source->getPosition())) << "A stationary adjacent attack remains legal";
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, VoluntaryTeleportIsRejectedWithoutManaOrActionCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(!defenderSideHero->hasSpellbook())
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::TELEPORT);
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->initializeSpellPoints(1000);
	install(source, target);
	battle()->activeStack = target->unitId();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::DEFENDER;
	action.spell = SpellID::TELEPORT;
	action.aimToUnit(target);
	action.aimToHex(BattleHex(12, 4));
	const auto before = target->save();
	const auto mana = defenderSideHero->getManaAvailable();
	const auto actions = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_EQ(target->save(), before);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.startedActions.size(), actions);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, CureHealsTheWoundedTargetWithoutRemovingPhysicalRestraint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(!defenderSideHero->hasSpellbook())
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::CURE);
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->initializeSpellPoints(1000);
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_TRUE(heldBy(source));
	// Whole-creature attack casualties need not leave a wounded survivor.
	StacksInjured wound;
	wound.battleID = battle()->getBattleID();
	auto & injury = wound.stacks.emplace_back();
	injury.stackAttacked = target->unitId();
	injury.damageAmount = 1;
	target->prepareAttacked(injury, gameHandler->getRandomGenerator());
	ASSERT_EQ(injury.killedAmount, 0u);
	gameHandler->sendAndApply(wound);
	ASSERT_GT(target->getSurvivingMissingHealth(), 0);
	battle()->activeStack = target->unitId();
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::CURE, target));
	EXPECT_GT(target->getAvailableHealth(), health);
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, LethalUnitUpdateBreaksHoldEvenWhenItsPacketImmediatelyRevivesSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	const auto health = source->getAvailableHealth();
	StacksInjured packet;
	packet.battleID = battle()->getBattleID();
	auto & hit = packet.stacks.emplace_back();
	hit.stackAttacked = source->unitId();
	hit.attackerID = target->unitId();
	hit.damageAmount = health;
	hit.killedAmount = source->getCount();
	hit.flags = BattleStackAttacked::KILLED | BattleStackAttacked::REBIRTH;
	hit.newState = UnitChanges(source->unitId(), UnitChanges::EOperation::UPDATE);
	// The legitimate injury domain contains a lethal delta with a revived final state.
	// Exercise that visitor boundary directly, without a later activation hiding expiry.
	hit.newState.data = source->save();
	hit.newState.healthDelta = -health;
	gameHandler->sendAndApply(packet);
	EXPECT_TRUE(source->alive());
	EXPECT_EQ(source->getAvailableHealth(), health);
	EXPECT_FALSE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, DoubleAttackAddsOnlyOneTimelessSourceIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attack(source, target->getPosition()));
	ASSERT_TRUE(target->alive());
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*target, source->unitId()).size(), 1u);
	EXPECT_EQ(server.attacks.size(), 2u);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, LegacyUnbindDoesNotEraseIndependentSourceRestraint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BIND_EFFECT, BonusSource::OTHER, 0, BonusSourceID()));
	ASSERT_EQ(target->getBonusesOfType(BonusType::BIND_EFFECT)->size(), 2u);
	BattleSetStackProperty unbind;
	unbind.battleID = battle()->getBattleID();
	unbind.stackID = target->unitId();
	unbind.which = BattleSetStackProperty::UNBIND;
	gameHandler->sendAndApply(unbind);
	EXPECT_TRUE(heldBy(source));
	EXPECT_EQ(target->getBonusesOfType(BonusType::BIND_EFFECT)->size(), 1u);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, DispelRemovesMagicWithoutRemovingThisPhysicalRestraint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(!attackerSideHero->hasSpellbook())
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->initializeSpellPoints(1000);
	install(source, target);
	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 1;
	SetStackEffect magic;
	magic.battleID = battle()->getBattleID();
	magic.toUpdate.emplace_back(target->unitId(), std::vector<Bonus>{haste});
	gameHandler->sendAndApply(magic);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::DISPEL, target));
	EXPECT_FALSE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, MissingSourceRejectsWholeEffectBatchBeforeRemovingExistingHold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	const auto missing = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	ASSERT_EQ(battle()->battleGetUnitByID(missing), nullptr);
	SetStackEffect packet;
	packet.battleID = battle()->getBattleID();
	packet.toRemove.emplace_back(target->unitId(), newHorizonsSerpentsEmbrace::releasePlan(*target, source->unitId()));
	packet.toUpdate.emplace_back(target->unitId(), std::vector<Bonus>{
		newHorizonsSerpentsEmbrace::marker(missing, source->creatureId())});
	const auto before = target->save();
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(target->save(), before);
	EXPECT_TRUE(heldBy(source));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, VictimHeroIsNotRequiredForBinding)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & side = battle()->getSide(BattleSide::DEFENDER);
	const auto original = side.heroID;
	side.heroID = ObjectInstanceID();
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::qualifiesAttack(*battle(), source, target, false, true, false));
	side.heroID = original;
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, HiddenFafnerAndPuppetFriendlyVictimHaveNoEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(hidden.battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::qualifiesAttack(hidden, source, target, false, true, false));
	const SpellID puppet(SpellID::decode("new-horizons:puppetMaster"));
	source->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
	ASSERT_EQ(battle()->battleGetOwner(source), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetActionController(source), PlayerColor(1));
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::qualifiesAttack(*battle(), source, target, false, true, false));
}

TEST_F(NewHorizonsSerpentsEmbraceBattleTest, RecipientNagaFormAndSuppressionRetainExternalPhysicalRestraint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * recipient = addStack(BattleSide::DEFENDER, creatureByName("core:naga"), BattleHex(12, 7), 10);
	install(source, recipient);
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT, 2,
		BonusSourceID(SpellID(SpellID::FORGETFULNESS))));
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*recipient));
	EXPECT_EQ(recipient->getMovementRange(), 0);
	recipient->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*recipient));
	recipient->endBattleForm();
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*recipient));
	activate(source, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*recipient));
	EXPECT_GT(recipient->getMovementRange(), 0);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, ChangedSourceFormAndControlDoNotChangeItsStoredExpiryAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	source->beginBattleForm(creatureByName("core:pikeman"), 2);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(source), PlayerColor(1));
	ASSERT_FALSE(newHorizonsSerpentsEmbrace::qualifiesAttack(*battle(), source, target, false, true, false));
	activate(source, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(heldBy(source));
	EXPECT_GT(target->getMovementRange(), 0);
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, OriginalLethalityReleasesLinkedCloneHoldBeforeGhostFlush)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * clone = addStack(BattleSide::ATTACKER, creatureByName("core:naga"), BattleHex(3, 2), 10);
	source->cloneID = clone->unitId();
	clone->cloned = true;
	install(clone, target);
	ASSERT_TRUE(heldBy(clone));
	// Lethal provenance plus an immediately revived original still kills its clone.
	battle()->updateUnit(source->unitId(), source->save(), -source->getAvailableHealth());
	EXPECT_TRUE(source->alive());
	EXPECT_TRUE(clone->ghostPending);
	EXPECT_FALSE(heldBy(clone));
	EXPECT_GT(target->getMovementRange(), 0);
}

TEST_F(NewHorizonsSerpentsEmbraceBattleTest, RemovingAlreadyGhostedOriginalStillReleasesImpliedCloneSourceHold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * clone = addStack(BattleSide::ATTACKER, creatureByName("core:naga"), BattleHex(3, 2), 10);
	source->cloneID = clone->unitId();
	clone->cloned = true;
	install(clone, target);
	source->makeGhost();
	source->ghost = true;
	battle()->removeUnit(source->unitId());
	EXPECT_FALSE(heldBy(clone));
	EXPECT_GT(target->getMovementRange(), 0);
}

TEST_F(NewHorizonsSerpentsEmbraceBattleTest, LegalEnemyPhoenixNativeRebirthRetainsHostilityAndBindsSurvivor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * phoenix = preparePhoenix(false);
	ASSERT_NO_FATAL_FAILURE(assertPhoenixMeleeGeometry(phoenix));
	ASSERT_EQ(battle()->battleGetActionController(phoenix), PlayerColor(1));
	ASSERT_TRUE(battle()->battleCanAttackUnitAction(source, phoenix));
	ASSERT_TRUE(phoenix->canCast());
	ASSERT_GT(phoenix->valOfBonuses(BonusType::REBIRTH), 0);
	ASSERT_EQ(battle()->battleGetOwner(phoenix), PlayerColor(1));
	ASSERT_TRUE(newHorizonsSerpentsEmbrace::qualifiesAttack(*battle(), source, phoenix, false, true, false));
	ASSERT_NO_THROW(ASSERT_TRUE(attack(source, phoenix->getPosition())));
	ASSERT_TRUE(std::ranges::any_of(server.attacks, [phoenix](const auto & attack)
	{
		return std::ranges::any_of(attack.bsa, [phoenix](const auto & hit)
			{ return hit.stackAttacked == phoenix->unitId() && hit.willRebirth(); });
	}));
	ASSERT_TRUE(phoenix->alive());
	EXPECT_EQ(battle()->battleGetOwner(phoenix), PlayerColor(1));
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*phoenix));
}
TEST_F(NewHorizonsSerpentsEmbraceBattleTest, LoadedApplicationRejectsNoncanonicalHeroSidBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto raw = newHorizonsSerpentsEmbrace::marker(source->unitId(), source->creatureId());
	raw.sid = BonusSourceID(attackerSideHero->getHeroTypeID() == HeroTypeID(0) ? HeroTypeID(1) : HeroTypeID(0));
	ASSERT_NO_THROW(newHorizonsSerpentsEmbrace::sourceId(raw));
	EXPECT_THROW(battle()->addUnitBonus(target->unitId(), {raw}), std::runtime_error);
	EXPECT_FALSE(heldBy(source));
}
