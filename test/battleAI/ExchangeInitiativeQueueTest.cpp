/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlecraftPreemptiveAIFixture.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ExchangeInitiativeQueue.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/battle/NewHorizonsRapidResponse.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"

class ExchangeInitiativeQueueTest : public battlecraftTest::PreemptiveFixture
{
protected:
	CStack * opening = nullptr;
	CStack * target = nullptr;
	CStack * other = nullptr;
	void prepare()
	{
		startGame();
		startBattle();
		beginCombat();
		opening = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
		other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
		removeOtherStacks({opening, target, other});
		for(const auto & [unit, value] : {std::pair{opening, 30}, {target, 20}, {other, 18}})
		{
			// Wingbeat scales the base; flat points are added after percentages.
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
					BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER,
					value, BonusSourceID(), BonusSubtypeID(), bound));
			ASSERT_EQ(unit->getInitiative(), value);
		}
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = opening->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		initializeAI(PlayerColor(0));
	}
	static Bonus wingbeat(int percent = 15)
	{
		Bonus result(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::STACKS_INITIATIVE,
			BonusSource::HERO_SPECIAL, -percent,
			BonusSourceID(HeroTypeID(HeroTypeID::decode("core:aeris"))));
		result.statusIdentity = "wingbeat";
		result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
		return result;
	}
	static battle::Units queue(const std::shared_ptr<HypotheticBattle> & branch)
	{
		std::vector<battle::Units> result;
		branch->battleGetTurnOrder(result, 0, 1);
		return result.empty() ? battle::Units{} : result.front();
	}
};

TEST_F(ExchangeInitiativeQueueTest, NoRiderPreservesOriginalOrderAndPointers)
{
	prepare();
	auto branch = simulation();
	const auto original = queue(branch);
	ASSERT_EQ(original.size(), 3u);
	ExchangeInitiativeQueue unchanged(branch, false);
	unchanged.beginRound(original, original);
	for(const auto * expected : original)
		EXPECT_EQ(unchanged.next(), expected);
	EXPECT_EQ(unchanged.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, OtherInitiativeEffectsDoNotActivateNewPolicy)
{
	prepare();
	auto branch = simulation();
	const auto original = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE, BonusSource::OTHER, -50, BonusSourceID())});
	EXPECT_EQ(watched.next(), original[1]);
	EXPECT_EQ(watched.next(), original[2]);
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, WingbeatChangesNextActorBeforeItsOwnExpiry)
{
	prepare();
	auto branch = simulation();
	const auto original = queue(branch);
	ASSERT_EQ(original[1]->unitId(), target->unitId());
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 17);
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 20);
	EXPECT_EQ(watched.next(), nullptr); // Neither spent actor can reappear.
	EXPECT_EQ(target->getInitiative(), 20); // Live state is untouched.
}

TEST_F(ExchangeInitiativeQueueTest, FlatInitiativeRemainsOutsideWingbeatPercentage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER, 2, BonusSourceID()));
	ASSERT_EQ(target->getInitiative(), 22);
	auto branch = simulation();
	const auto original = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	ASSERT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	EXPECT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 19);
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
	EXPECT_EQ(target->getInitiative(), 22);
}

TEST_F(ExchangeInitiativeQueueTest, WaitingUsesSlowerFirstAndRetainsFixedMembership)
{
	prepare();
	auto branch = simulation();
	branch->getForUpdate(target->unitId())->afterWait();
	branch->getForUpdate(other->unitId())->afterWait();
	const auto global = queue(branch);
	ASSERT_EQ(global.size(), 3u);
	ASSERT_EQ(global[1]->unitId(), other->unitId());
	ASSERT_EQ(global[2]->unitId(), target->unitId());
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(global, global);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, ExpiryCannotPoisonTheCachedFutureRound)
{
	prepare();
	auto branch = simulation();
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	std::vector<battle::Units> cached;
	branch->battleGetTurnOrder(cached, 0, 2);
	ASSERT_EQ(cached.size(), 2u);
	ASSERT_LT(branch->battleGetUnitByID(target->unitId())->getInitiative(), other->getInitiative());
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(cached[0], cached[0]);
	while(const auto * actor = watched.next())
		branch->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	branch->nextRound();
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 20);
	watched.beginRound(cached[1], cached[1]);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, ReorderingNeverAdmitsANonparticipantOrConsumesRNG)
{
	prepare();
	auto branch = simulation();
	const auto global = queue(branch);
	battle::Units selected{global[0], global[1]}; // 'other' is outside this exchange.
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(global, selected);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	const auto before = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto generatorBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next(), nullptr);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), before);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), generatorBefore);
}

TEST_F(ExchangeInitiativeQueueTest, EqualInitiativeUsesCanonicalSideAndSlotPreference)
{
	prepare();
	auto branch = simulation();
	branch->getForUpdate(other->unitId())->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER, -1, BonusSourceID())});
	const auto original = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 17);
	ASSERT_EQ(branch->battleGetUnitByID(other->unitId())->getInitiative(), 17);
	// Last actor is attacker-side: the enemy wins an otherwise equal NORMAL tie.
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, GenuineActivationDurationSurvivesSpellContinuation)
{
	prepare();
	auto branch = simulation();
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 17);
	// Typed lifecycle check only; this does not grant or certify a legal extra action.
	branch->nextTurn(target->unitId(), BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
	EXPECT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 20);
}

TEST_F(ExchangeInitiativeQueueTest, WingbeatOnSpentActorCannotGrantAnotherActivation)
{
	prepare();
	auto branch = simulation();
	const auto original = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next()->unitId(), opening->unitId());
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	branch->getForUpdate(opening->unitId())->addUnitBonus({wingbeat(60)});
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, HeroQueueCounterspellBoundaryMovesAfterWingbeat)
{
	prepare();
	auto branch = simulation();
	auto pending = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(pending, pending);
	ASSERT_EQ(watched.nextIn(pending, 0)->unitId(), opening->unitId());
	// The ordinary selected-hit receipt applies this effect before loop advance.
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	ASSERT_EQ(watched.nextIn(pending, 1)->unitId(), other->unitId());
	ASSERT_EQ(watched.nextIn(pending, 2)->unitId(), target->unitId());
	EXPECT_EQ(watched.nextIn(pending, 3), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, HeroQueueKeepsExistingPriorityPermutationWithoutWingbeat)
{
	prepare();
	auto branch = simulation();
	auto pending = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(pending, pending);
	ASSERT_EQ(watched.nextIn(pending, 0)->unitId(), opening->unitId());
	std::swap(pending[1], pending[2]); // Existing completion owner permutes the suffix.
	EXPECT_EQ(watched.nextIn(pending, 1)->unitId(), other->unitId());
	EXPECT_EQ(watched.nextIn(pending, 2)->unitId(), target->unitId());
	EXPECT_EQ(watched.nextIn(pending, 3), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, ExistingSeizeRecipientSurvivesCanonicalWingbeatRefresh)
{
	prepare();
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 7), 10);
	reserve->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER,
		10 - reserve->getInitiative(), BonusSourceID()));
	auto branch = simulation();
	SeizeInitiativeState seized;
	seized.round = branch->battleGetRound();
	seized.sides[0].enabled = true;
	seized.sides[0].triggered = true;
	seized.sides[0].recipient = reserve->unitId();
	branch->setSeizeInitiativeState(seized);
	const auto global = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(global, global);
	ASSERT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	EXPECT_EQ(watched.next()->unitId(), reserve->unitId());
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, CapturedRapidWaiterSurvivesWingbeatRefreshWithoutReappearing)
{
	prepare();
	const auto skill = SecondarySkill(SecondarySkill::decode(battlecraftTest::battlecraftSkill));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, battlecraftTest::basicPerk));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, newHorizonsRapidResponse::PERK_KEY));
	auto branch = simulation();
	branch->getForUpdate(other->unitId())->afterWait();
	RapidResponseState rapid;
	rapid.enabled = true;
	rapid.pendingRound = branch->battleGetRound();
	rapid.pendingUnitId = other->unitId();
	branch->setRapidResponseState(BattleSide::ATTACKER, rapid);
	const auto global = queue(branch);
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(global, global);
	ASSERT_EQ(watched.next()->unitId(), opening->unitId());
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	EXPECT_EQ(watched.next()->unitId(), other->unitId());
	EXPECT_EQ(watched.next()->unitId(), target->unitId());
	EXPECT_EQ(watched.next(), nullptr);
}

TEST_F(ExchangeInitiativeQueueTest, SelectedReceiptRestoresBothRiderOverlaysAfterScalarReplay)
{
	prepare();
	auto destination = simulation();
	auto preview = std::make_shared<HypotheticBattle>(environment.get(), destination);
	preview->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	auto volley = wingbeat();
	volley.type = BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION;
	volley.sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:theodorus")));
	volley.statusIdentity = "measuredVolley";
	volley.val = 20;
	preview->getForUpdate(opening->unitId())->addUnitBonus({volley});
	AttackPossibility captured(BattleHex(), BattleHex(), BattleAttackInfo(opening, target, 0, false));
	captured.effectPreview = preview;
	captured.attackerState = preview->getForUpdate(opening->unitId());
	captured.affectedUnits.push_back(preview->getForUpdate(target->unitId()));
	*destination->getForUpdate(target->unitId()) = *captured.affectedUnits.front();
	ASSERT_EQ(destination->battleGetUnitByID(target->unitId())->getInitiative(), 20);
	AttackPreviewHitContext::replayOrdinaryStrikeEffects(*destination, captured);
	EXPECT_EQ(destination->battleGetUnitByID(target->unitId())->getInitiative(), 17);
	const CSelector rider(newHorizonsOrdinaryStrike::isStrikeEffect);
	EXPECT_TRUE(destination->battleGetUnitByID(opening->unitId())->hasBonus(rider));
	EXPECT_FALSE(opening->hasBonus(rider));
	EXPECT_EQ(target->getInitiative(), 20);
}

TEST_F(ExchangeInitiativeQueueTest, ReceiptExpiryRemovesOnlyExactRiders)
{
	prepare();
	auto destination = simulation();
	destination->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	auto unrelated = wingbeat();
	unrelated.sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:theodorus")));
	unrelated.val = -10;
	destination->getForUpdate(target->unitId())->addUnitBonus({unrelated});
	auto preview = std::make_shared<HypotheticBattle>(environment.get(), destination);
	preview->getForUpdate(target->unitId())->removeUnitBonus(CSelector(newHorizonsOrdinaryStrike::isStrikeEffect));
	AttackPossibility captured(BattleHex(), BattleHex(), BattleAttackInfo(opening, target, 0, false));
	captured.effectPreview = preview;
	captured.affectedUnits.push_back(preview->getForUpdate(target->unitId()));
	AttackPreviewHitContext::replayOrdinaryStrikeEffects(*destination, captured);
	const auto * final = destination->battleGetUnitByID(target->unitId());
	EXPECT_FALSE(final->hasBonus(CSelector(newHorizonsOrdinaryStrike::isStrikeEffect)));
	EXPECT_TRUE(final->hasBonus(CSelector([&](const Bonus * bonus) { return bonus->sid == unrelated.sid
		&& bonus->type == unrelated.type && bonus->statusIdentity == unrelated.statusIdentity; })));
	EXPECT_EQ(final->getInitiative(), 18);
}

TEST_F(ExchangeInitiativeQueueTest, ParticipantPartitionPreservesRoundOrderSeedAndPointerIdentity)
{
	prepare();
	auto branch = simulation();
	const auto alias = branch->getForUpdate(opening->unitId());
	ASSERT_NE(static_cast<const battle::Unit *>(alias.get()), opening);
	battle::Units ours;
	battle::Units enemies{target};
	const std::map<int, battle::Units> rounds{{0, {other, target, opening, other}},
		{1, {opening, alias.get(), target}}};
	ExchangeInitiativeQueue::appendParticipantArmies(*branch, opening, rounds, ours, enemies);
	EXPECT_EQ(ours, (battle::Units{other, opening, alias.get()}));
	EXPECT_EQ(enemies, (battle::Units{target}));
	// Duplicate membership is pointer-based, exactly as in the original partition.
	ExchangeInitiativeQueue::appendParticipantArmies(*branch, opening, rounds, ours, enemies);
	EXPECT_EQ(ours, (battle::Units{other, opening, alias.get()}));
	EXPECT_EQ(enemies, (battle::Units{target}));
}

TEST_F(ExchangeInitiativeQueueTest, ParticipantPartitionSkipsProjectedDeadAndTurret)
{
	prepare();
	auto * turret = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::ARROW_TOWERS),
		BattleHex(10, 5), 1);
	ASSERT_TRUE(turret->isTurret());
	auto branch = simulation();
	auto dead = branch->getForUpdate(other->unitId());
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage);
	ASSERT_FALSE(dead->alive());
	ASSERT_TRUE(other->alive());
	battle::Units ours;
	battle::Units enemies;
	ExchangeInitiativeQueue::appendParticipantArmies(*branch, opening,
		{{0, {other, turret, target, opening}}, {1, {other, turret}}}, ours, enemies);
	EXPECT_EQ(ours, (battle::Units{opening}));
	EXPECT_EQ(enemies, (battle::Units{target}));
}

TEST_F(ExchangeInitiativeQueueTest, ParticipantPartitionRetainsArmyRelationWithoutActionControllerTransfer)
{
	prepare();
	auto branch = simulation();
	auto controlled = branch->getForUpdate(target->unitId());
	controlled->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID())});
	auto puppet = branch->getForUpdate(other->unitId());
	puppet->addUnitBonus({newHorizonsPuppetMaster::controlMarker(
		SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID))), PlayerColor(1))});
	ASSERT_EQ(branch->battleGetOwner(controlled.get()), PlayerColor(0));
	ASSERT_EQ(branch->battleGetActionController(puppet.get()), PlayerColor(1));
	battle::Units ours;
	battle::Units enemies;
	ExchangeInitiativeQueue::appendParticipantArmies(*branch, opening,
		{{0, {controlled.get(), puppet.get(), opening}}}, ours, enemies);
	EXPECT_EQ(ours, (battle::Units{puppet.get(), opening}));
	EXPECT_EQ(enemies, (battle::Units{controlled.get()}));
}

TEST_F(ExchangeInitiativeQueueTest, WatchedQueueWithoutRiderKeepsCanonicalPointersAndRandomGenerators)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto branch = simulation();
	const auto original = queue(branch);
	ASSERT_EQ(original.size(), 3u);
	const auto authorityRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto openingState = branch->battleGetUnitByID(opening->unitId())->acquireState()->save();
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	for(const auto * expected : original)
		EXPECT_EQ(watched.next(), expected);
	EXPECT_EQ(watched.next(), nullptr);
	EXPECT_EQ(branch->battleGetUnitByID(opening->unitId())->acquireState()->save(), openingState);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), authorityRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(ExchangeInitiativeQueueTest, CanonicalRefreshFiltersSpentPrefixBeforeEqualInitiativeSideAlternation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(2, 7), 10);
	ASSERT_NE(reserve, nullptr);
	reserve->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER,
		18 - reserve->getInitiative(), BonusSourceID()));
	auto branch = simulation();
	const auto original = queue(branch);
	ASSERT_EQ(original.size(), 4u);
	ASSERT_EQ(original[0]->unitId(), opening->unitId());
	ASSERT_EQ(original[1]->unitId(), target->unitId());
	ASSERT_EQ(original[2]->unitId(), other->unitId());
	ASSERT_EQ(original[3]->unitId(), reserve->unitId());
	const auto active = branch->battleActiveUnit();
	const auto authorityRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto fixtureRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next(), original[0]);
	EXPECT_EQ(watched.next(), original[1]);
	// A spent defender's rider triggers refresh. If spent actors were merely
	// removed AFTER scheduling, opening's attacker side would flip this tie.
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	ASSERT_EQ(branch->battleGetUnitByID(target->unitId())->getInitiative(), 17);
	EXPECT_EQ(watched.next(), original[2]);
	EXPECT_EQ(watched.next(), original[3]);
	EXPECT_EQ(watched.next(), nullptr);
	EXPECT_EQ(branch->battleActiveUnit(), active); // View never clears authoritative/projected active state.
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), authorityRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(ExchangeInitiativeQueueTest, CanonicalRefreshCannotReinsertSpentRapidAndSeizeRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(battlecraftTest::battlecraftSkill));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, battlecraftTest::basicPerk));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, newHorizonsRapidResponse::PERK_KEY));
	auto branch = simulation();
	branch->getForUpdate(other->unitId())->afterWait();
	RapidResponseState rapid;
	rapid.enabled = true;
	rapid.pendingRound = branch->battleGetRound();
	rapid.pendingUnitId = other->unitId();
	branch->setRapidResponseState(BattleSide::ATTACKER, rapid);
	SeizeInitiativeState seized;
	seized.round = branch->battleGetRound();
	seized.sides[0].enabled = true;
	seized.sides[0].triggered = true;
	seized.sides[0].recipient = other->unitId();
	branch->setSeizeInitiativeState(seized);
	const auto original = queue(branch);
	ASSERT_EQ(original.size(), 3u);
	ASSERT_EQ(original[0]->unitId(), opening->unitId());
	ASSERT_EQ(original[1]->unitId(), other->unitId());
	ASSERT_EQ(original[2]->unitId(), target->unitId());
	const auto * pending = newHorizonsRapidResponse::pendingWaiter(*branch, BattleSide::ATTACKER);
	ASSERT_NE(pending, nullptr);
	ASSERT_EQ(pending->unitId(), other->unitId());
	const auto rapidBefore = projectedHitTest::serialized(branch->getRapidResponseState(BattleSide::ATTACKER));
	const auto seizeBefore = projectedHitTest::serialized(branch->getSeizeInitiativeState());
	ExchangeInitiativeQueue watched(branch, true);
	watched.beginRound(original, original);
	EXPECT_EQ(watched.next(), original[0]);
	EXPECT_EQ(watched.next(), original[1]);
	branch->getForUpdate(target->unitId())->addUnitBonus({wingbeat()});
	// The source ledger deliberately still has the pending recipient. The
	// callback's virtual ID lookup must exclude it before Rapid inserts it.
	EXPECT_EQ(watched.next(), original[2]);
	EXPECT_EQ(watched.next(), nullptr);
	EXPECT_EQ(projectedHitTest::serialized(branch->getRapidResponseState(BattleSide::ATTACKER)), rapidBefore);
	EXPECT_EQ(projectedHitTest::serialized(branch->getSeizeInitiativeState()), seizeBefore);
	EXPECT_EQ(newHorizonsRapidResponse::pendingWaiter(*branch, BattleSide::ATTACKER), pending);
}
