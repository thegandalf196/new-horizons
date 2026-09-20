/*
 * NewHorizonsTimeStopTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{
constexpr auto timeStopKey = newHorizonsSorcery::TIME_STOP_SPELL;

SpellID timeStopSpell()
{
	return SpellID(SpellID::decode(timeStopKey));
}

std::shared_ptr<Bonus> timeStopMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(
		BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP,
		// This fixture deliberately exercises the dedicated runtime marker without
		// resolving the optional content spell.  The production Lua marker carries
		// SPELL_EFFECT/new-horizons:timeStop; the authoritative TIME_STOP type is
		// sufficient for the content-independent blocker/lifecycle assertions.
		BonusSource::OTHER,
		1,
		BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}

std::shared_ptr<Bonus> timedEffect(int turns)
{
	auto effect = std::make_shared<Bonus>(
		BonusDuration::N_TURNS,
		BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT,
		1,
		BonusSourceID());
	effect->turnsRemain = turns;
	return effect;
}

void removeAllBattleUnits(BattleTestFixture & fixture, std::initializer_list<const CStack *> keep)
{
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : fixture.battle()->battleGetAllUnits(false))
		if(std::find(keep.begin(), keep.end(), unit) == keep.end())
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	fixture.gameHandler->sendAndApply(remove);
}

void beginTimeStopTestCombat(BattleTestFixture & fixture, const CStack * active)
{
	ASSERT_NE(active, nullptr);
	// These tests replace the fixture's complete deployment after battle creation.
	// Enter the first ordinary round explicitly so tactics visibility and opening
	// spell side effects do not obscure the Time Stop lifecycle under test.
	fixture.battle()->tacticDistance = 0;
	BattleNextRound nextRound;
	nextRound.battleID = BattleID(0);
	fixture.gameHandler->sendAndApply(nextRound);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	fixture.gameHandler->sendAndApply(activate);
}
}

TEST_F(BattleTestFixture, TimeStopMarkerBlocksUnitInteractionsAndPausesRoundEffects)
{
	// This test uses a real CUnitState and the real BattleInfo lifecycle, so it
	// remains useful in the legacy test profile where the optional content mod is
	// not installed.
	startGame();
	startBattle();
	auto * stopped = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(8, 5), 10);
	auto * control = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(12, 5), 10);
	ASSERT_NE(stopped, nullptr);
	ASSERT_NE(control, nullptr);
	removeAllBattleUnits(*this, {stopped, control});
	// This state-level test does not need battle-start triggers. The fixture's
	// synthetic tactics transition depends on callback-visible tactic state,
	// which is intentionally absent after replacing every layout stack.
	battle()->tacticDistance = 0;

	battle()->addOrUpdateUnitBonus(stopped, *timeStopMarker(BattleSide::ATTACKER), true);
	ASSERT_TRUE(stopped->isTimeStopped());

	const auto originalPosition = stopped->getPosition();
	battle()->moveUnit(stopped->unitId(), originalPosition.copyToEast());
	EXPECT_EQ(stopped->getPosition(), originalPosition);
	EXPECT_FALSE(stopped->canMove());
	EXPECT_EQ(stopped->getMovementRange(), 0);
	EXPECT_FALSE(stopped->ableToRetaliate());
	EXPECT_FALSE(stopped->isValidTarget(false));

	// Existing clients expose DEFEND as their close-turn control. The server
	// canonicalizes that owner-authenticated request to a no-op for stasis, so
	// it must not grant the defending bonus or mutate the stopped stack.
	battle()->activeStack = stopped->unitId();
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(stopped)));
	EXPECT_FALSE(stopped->defending);
	EXPECT_FALSE(stopped->isTimeStopped());

	// Only the server may attach the replicated pass marker. A client-authored
	// marker is rejected and cannot release stasis.
	battle()->addOrUpdateUnitBonus(stopped, *timeStopMarker(BattleSide::ATTACKER), true);
	BattleAction forgedPass = BattleAction::makeNoAction(stopped);
	forgedPass.timeStopHeroActionPass = true;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), forgedPass));
	EXPECT_TRUE(stopped->isTimeStopped());

	// WAIT remains an actual creature action and is still rejected while the
	// stack is stopped; only the visible DEFEND close-turn request is special.
	battle()->activeStack = stopped->unitId();
	BattleAction wrongSide = BattleAction::makeNoAction(stopped);
	wrongSide.side = BattleSide::DEFENDER;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), wrongSide));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(stopped)));
	EXPECT_TRUE(stopped->isTimeStopped());

	// Teleporting is represented by the same server movement pack as ordinary
	// movement.  The state visitor must still preserve a stopped unit's hex.
	BattleStackMoved teleport;
	teleport.battleID = BattleID(0);
	teleport.stack = stopped->unitId();
	teleport.tilesToMove.insert(originalPosition.copyToEast());
	teleport.teleporting = true;
	gameHandler->sendAndApply(teleport);
	EXPECT_EQ(stopped->getPosition(), originalPosition);
	EXPECT_TRUE(stopped->isTimeStopped());

	const auto originalHealth = stopped->getAvailableHealth();
	auto state = stopped->acquireState();
	int64_t damage = 1000;
	state->damage(damage);
	EXPECT_EQ(damage, 0);
	EXPECT_EQ(stopped->getAvailableHealth(), originalHealth);
	int64_t healing = 1000;
	state->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	EXPECT_EQ(healing, 0);
	EXPECT_EQ(stopped->getAvailableHealth(), originalHealth);

	// A normal network effect update is rejected while the dedicated marker is
	// present, while an effect that predates stasis is retained and does not age.
	const auto blocked = std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 99, BonusSourceID());
	battle()->addOrUpdateUnitBonus(stopped, *blocked, true);
	const auto blockedBonuses = stopped->getBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::STACKS_SPEED && bonus->val == 99;
	}));
	EXPECT_TRUE(blockedBonuses->empty());
	stopped->addNewBonus(timedEffect(3));
	battle()->nextRound();
	battle()->nextRound();
	const auto retained = stopped->getBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::STACKS_SPEED
			&& bonus->source == BonusSource::SPELL_EFFECT && Bonus::NTurns(bonus);
	}));
	ASSERT_EQ(retained->size(), 1);
	EXPECT_EQ(retained->front()->turnsRemain, 3);

	// Expiry is side-scoped and occurs at the Hero Action boundary. A follow-up
	// StartAction from the same sequence does not release the marker.
	battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount = 1;
	BattleAction followup = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
	followup.actionType = EActionType::HERO_SPELL;
	followup.spell = SpellID::MAGIC_ARROW;
	followup.metamagicFollowup = true;
	StartAction followupStart(followup);
	followupStart.battleID = BattleID(0);
	gameHandler->sendAndApply(followupStart);
	EXPECT_TRUE(stopped->isTimeStopped());

	battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount = 0;
	BattleAction nextHeroAction = followup;
	nextHeroAction.metamagicFollowup = false;
	StartAction nextHeroStart(nextHeroAction);
	nextHeroStart.battleID = BattleID(0);
	gameHandler->sendAndApply(nextHeroStart);
	EXPECT_FALSE(stopped->isTimeStopped());
}

TEST(NewHorizonsTimeStopTest, MarkerRoundTripsAndLegacyBonusesRemainReadable)
{
	Bonus marker = *timeStopMarker(BattleSide::DEFENDER);
	marker.val = 2;
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & marker);
	Bonus restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.type, BonusType::TIME_STOP);
	EXPECT_EQ(restored.val, 2);
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toNumber(), static_cast<int32_t>(BattleSide::DEFENDER));

	// A save written before the marker existed remains loadable when it contains
	// only ordinary effects. New state is rejected rather than silently dropped.
	Bonus ordinary(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
	ordinary.turnsRemain = 2;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_METAMAGIC;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_METAMAGIC;
	ASSERT_NO_THROW(old.oser & ordinary);
	Bonus oldRestored;
	ASSERT_NO_THROW(old.iser & oldRestored);
	EXPECT_EQ(oldRestored.type, BonusType::STACKS_SPEED);

	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_METAMAGIC;
	EXPECT_THROW(rejected.oser & marker, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
}

class NewHorizonsTimeStopContentTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
};

TEST_F(NewHorizonsTimeStopContentTest, RealCastDrainsRoundAndNextHeroActionExpiresOnlyCasterMarkers)
{
	const auto spell = timeStopSpell();
	ASSERT_TRUE(spell.hasValue());
	ASSERT_NE(spell.toSpell(), nullptr);
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	attackerSideHero->mana = 1000;
	startBattle();

	const BattleHex center(8, 5);
	auto * attacker = addStack(BattleSide::ATTACKER, CreatureID(0), center, 10);
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(0), center.copyToEast(), 10);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	removeAllBattleUnits(*this, {attacker, defender});
	beginTimeStopTestCombat(*this, attacker);

	// The focused combat bootstrap selects the attacker first for equal
	// initiative, matching the ordinary first-round queue order.
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), attacker->unitId());
	const auto startingRound = battle()->getRound();

	BattleAction cast;
	cast.actionType = EActionType::HERO_SPELL;
	cast.side = BattleSide::ATTACKER;
	cast.spell = spell;
	// Leave a real Metamagic follow-up pending while the stopped queue drains.
	// The synthetic automatic NO_ACTION must be rejected, then the ordinary
	// TURN_QUEUE activation must remain visible so the side can decline it.
	// The Time Stop cast itself consumes one continuation. Keep a second one
	// pending so the post-stasis control window genuinely has a decline choice.
	battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount = 2;
	cast.metamagicFollowup = true;
	cast.aimToHex(center);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(BattleSide::ATTACKER), cast));

	// The real cast consumed one continuation, but the remaining Metamagic
	// choice belongs to the same Hero Action. It must stay in this round until
	// the player casts again or declines, without releasing Time Stop.
	EXPECT_EQ(battle()->getRound(), startingRound);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::ATTACKER);
	EXPECT_TRUE(attacker->isTimeStopped());
	EXPECT_TRUE(defender->isTimeStopped());
	ASSERT_FALSE(server.stackActivations.empty());
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::TURN_QUEUE);

	// A separate defender-origin marker is deliberately kept on a third unit.
	// It must survive the attacker's next Hero Action even though all three
	// stacks remain physical and blocked while the pending window is declined.
	battle()->addOrUpdateUnitBonus(defender, *timeStopMarker(BattleSide::DEFENDER), true);
	auto * defenderOrigin = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(15, 5), 10);
	ASSERT_NE(defenderOrigin, nullptr);
	battle()->addOrUpdateUnitBonus(defenderOrigin, *timeStopMarker(BattleSide::DEFENDER), true);
	battle()->notePendingTimeStopHeroAction(BattleSide::DEFENDER);
	EXPECT_TRUE(defenderOrigin->isTimeStopped());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(BattleSide::ATTACKER),
		BattleAction::makeMetamagicDecline(BattleSide::ATTACKER)));
	EXPECT_GT(battle()->getRound(), startingRound);
	EXPECT_TRUE(attacker->isTimeStopped());
	EXPECT_TRUE(defender->isTimeStopped());
	EXPECT_TRUE(defenderOrigin->isTimeStopped());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(battle()->hasPendingTimeStopHeroAction(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->hasPendingTimeStopHeroAction(BattleSide::DEFENDER));
	const auto restoredBattle = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restoredBattle, nullptr);
	EXPECT_TRUE(restoredBattle->hasPendingTimeStopHeroAction(BattleSide::ATTACKER));
	EXPECT_TRUE(restoredBattle->hasPendingTimeStopHeroAction(BattleSide::DEFENDER));

	// A side may have no useful Hero Action (or no hero at all). Existing clients
	// submit DEFEND to close the control-visible activation; the authoritative
	// server canonicalizes it to a no-op and reaches the next boundary without
	// granting a creature action or throwing.
	const auto * stoppedAnchor = battle()->battleActiveUnit();
	ASSERT_NE(stoppedAnchor, nullptr);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(stoppedAnchor->unitSide()),
		BattleAction::makeDefend(stoppedAnchor)));
	EXPECT_FALSE(stoppedAnchor->defended());
	EXPECT_FALSE(attacker->isTimeStopped());
	EXPECT_TRUE(defender->isTimeStopped());
	EXPECT_TRUE(defenderOrigin->isTimeStopped());
	EXPECT_FALSE(battle()->hasPendingTimeStopHeroAction(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->hasPendingTimeStopHeroAction(BattleSide::DEFENDER));
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::HERO_COMMAND);

	// The attacker is now unstopped, but every defender-controlled stack is
	// still stopped by the independent defender origin. Ending the attacker's
	// ordinary activation must schedule the defender anchor at the next round
	// boundary instead of letting the attacker side loop forever.
	for(int guard = 0; guard < 8 && battle()->battleActiveUnit() != defender; ++guard)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_FALSE(active->isTimeStopped());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
			BattleID(0), battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
	}
	EXPECT_EQ(battle()->battleActiveUnit(), defender);
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::TURN_QUEUE);

	// A stopped anchor with no fighting hero still has a safe, authenticated
	// close-turn path. Its pass is the terminal boundary for that origin rather
	// than a permanent re-scheduling loop.
	battle()->getSide(BattleSide::DEFENDER).heroID = ObjectInstanceID();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(BattleSide::DEFENDER),
		BattleAction::makeDefend(defender)));
	EXPECT_FALSE(defender->isTimeStopped());
	EXPECT_FALSE(defenderOrigin->isTimeStopped());
	EXPECT_FALSE(battle()->hasPendingTimeStopHeroAction(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsTimeStopContentTest, InvalidHeroTargetDoesNotExpireOrAdvanceStasis)
{
	const auto spell = SpellID(SpellID::decode("core:magicArrow"));
	ASSERT_TRUE(spell.hasValue());
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	startBattle();

	auto * attacker = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(8, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(12, 5), 10);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	removeAllBattleUnits(*this, {attacker, defender});
	beginTimeStopTestCombat(*this, attacker);
	battle()->addOrUpdateUnitBonus(attacker, *timeStopMarker(BattleSide::ATTACKER), true);
	ASSERT_TRUE(attacker->isTimeStopped());
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), attacker->unitId());
	const auto initialRound = battle()->getRound();
	const auto initialActivationCount = server.stackActivations.size();

	// Magic Arrow requires a creature target. This reaches the real server
	// action path but must fail before StartAction/Time Stop expiry.
	BattleAction invalid;
	invalid.actionType = EActionType::HERO_SPELL;
	invalid.side = BattleSide::ATTACKER;
	invalid.spell = spell;
	const auto activationsBeforeRejection = server.stackActivations.size();
	const auto activeBeforeRejection = battle()->getActiveStackID();
	const auto activationSerialBeforeRejection = battle()->getActivationSerial();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), battle()->sideToPlayer(BattleSide::ATTACKER), invalid));
	EXPECT_TRUE(attacker->isTimeStopped());
	ASSERT_EQ(server.stackActivations.size(), activationsBeforeRejection + 1);
	EXPECT_EQ(server.stackActivations.back().stack, static_cast<uint32_t>(activeBeforeRejection));
	EXPECT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_EQ(battle()->getActiveStackID(), activeBeforeRejection);
	EXPECT_EQ(battle()->getActivationSerial(), activationSerialBeforeRejection);
	EXPECT_EQ(battle()->getRound(), initialRound);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), attacker->unitId());
	EXPECT_EQ(server.stackActivations.size(), initialActivationCount + 1);
}

TEST_F(NewHorizonsTimeStopContentTest, SelectedHexUsesOccupiedIntersectionAndAllowsEmptyFootprint)
{
	const auto spell = timeStopSpell();
	ASSERT_TRUE(spell.hasValue());
	ASSERT_NE(spell.toSpell(), nullptr);
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	attackerSideHero->mana = 1000;
	startBattle();

	const BattleHex center(8, 5);
	const BattleHex adjacent = center.copyToEast();
	const BattleHex outside = adjacent.copyToEast();
	const auto * inCenter = addStack(BattleSide::DEFENDER, CreatureID(0), center, 10);
	const auto * inAdjacent = addStack(BattleSide::ATTACKER, CreatureID(0), adjacent, 10);
	const auto * outOfRadius = addStack(BattleSide::DEFENDER, CreatureID(0), outside, 10);
	ASSERT_NE(inCenter, nullptr);
	ASSERT_NE(inAdjacent, nullptr);
	ASSERT_NE(outOfRadius, nullptr);
	removeAllBattleUnits(*this, {inCenter, inAdjacent, outOfRadius});
	beginTimeStopTestCombat(*this, inAdjacent);

	// Location selection is legal even when the chosen hex currently has no
	// unit footprint; the effect itself discovers occupied intersections.
	spells::BattleCast emptyProbe(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	spells::Target emptyTarget;
	emptyTarget.emplace_back(BattleHex(2, 2));
	spells::detail::ProblemImpl emptyProblem;
	const auto emptyMechanics = spell.toSpell()->battleMechanics(&emptyProbe);
	spells::detail::ProblemImpl generalProblem;
	std::vector<std::string> generalProblems;
	const bool generallyCastable = emptyMechanics->canBeCast(generalProblem);
	generalProblem.getAll(generalProblems);
	EXPECT_TRUE(generallyCastable) << boost::algorithm::join(generalProblems, "; ");
	std::vector<std::string> emptyProblems;
	const bool emptyCastable = emptyMechanics->canBeCastAt(emptyTarget, emptyProblem);
	emptyProblem.getAll(emptyProblems);
	EXPECT_TRUE(emptyCastable) << boost::algorithm::join(emptyProblems, "; ");

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = inAdjacent->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell;
	action.aimToHex(center);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(inCenter->isTimeStopped());
	EXPECT_TRUE(inAdjacent->isTimeStopped());
	EXPECT_FALSE(outOfRadius->isTimeStopped());
}

TEST_F(NewHorizonsTimeStopContentTest, ChronomancerRadiusThreeIncludesDoubleWideFootprint)
{
	const auto spell = timeStopSpell();
	ASSERT_TRUE(spell.hasValue());
	ASSERT_NE(spell.toSpell(), nullptr);
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setSecSkillLevel(SecondarySkill::WISDOM, 3, ChangeValueMode::ABSOLUTE);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.chronomancer"});
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	attackerSideHero->mana = 1000;
	startBattle();

	// Find a legal location where only the non-head hex of a double-wide unit
	// lies within radius three.  The head is deliberately four hexes away, so
	// checking only getPosition() would miss this target.
	auto * wide = addStack(BattleSide::DEFENDER,
		CreatureID(CreatureID::decode("core:basilisk")), BattleHex(10, 5), 1);
	ASSERT_NE(wide, nullptr);
	ASSERT_TRUE(wide->doubleWide());
	const auto occupied = wide->getHexes();
	ASSERT_EQ(occupied.size(), 2u);
	BattleHex center = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(!candidate.isAvailable() || wide->coversPos(candidate)
			|| BattleHex::getDistance(candidate, wide->getPosition()) <= 3)
			continue;
		if(vstd::contains_if(occupied, [&candidate](const BattleHex & hex)
			{
				return BattleHex::getDistance(candidate, hex) == 3;
			}))
		{
			center = candidate;
			break;
		}
	}
	ASSERT_TRUE(center.isValid());

	BattleHex outside = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && candidate != center && !wide->coversPos(candidate)
			&& BattleHex::getDistance(candidate, center) == 4)
		{
			outside = candidate;
			break;
		}
	}
	ASSERT_TRUE(outside.isValid());

	auto * casterStack = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(8, 5), 10);
	auto * outOfRadius = addStack(BattleSide::DEFENDER, CreatureID(0), outside, 10);
	ASSERT_NE(casterStack, nullptr);
	ASSERT_NE(outOfRadius, nullptr);
	removeAllBattleUnits(*this, {wide, casterStack, outOfRadius});
	beginTimeStopTestCombat(*this, casterStack);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = casterStack->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell;
	action.aimToHex(center);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(wide->isTimeStopped());
	EXPECT_FALSE(outOfRadius->isTimeStopped());
}
