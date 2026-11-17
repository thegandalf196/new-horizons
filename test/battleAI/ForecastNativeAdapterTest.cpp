/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include <array>
#include "ForecastNativeAdapterFixture.h"
#include "../../lib/bonuses/BonusList.h"

TEST_F(ForecastNativeAdapterTest, BothNativePlayersCanCertifyPublicAbsenceInAnArmyOnlyBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto view = std::make_shared<CPlayerBattleCallback>(visible->getBattle(), player);
		auto scoped = std::make_shared<HypotheticBattle>(environment.get(), view);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto & captured = scoped->forecastSide(side);
			ASSERT_TRUE(captured);
			EXPECT_FALSE(view->battleHasHero(side));
			EXPECT_EQ(captured->knowledge, VisibleHeroPolicy::Knowledge::KNOWN_ABSENT);
			EXPECT_TRUE(captured->attunementKnown);
			EXPECT_EQ(captured->attunement, newHorizonsElementalAttunement::Profile{});
			EXPECT_EQ(captured->magicOwner, scoped->getCapturedMagicRules());
			EXPECT_FALSE(captured->hero); EXPECT_FALSE(captured->mana); EXPECT_FALSE(captured->ledger);
		}
		EXPECT_TRUE(forecast::NativeAdapter::prepareExchange(*scoped));
	}
}
TEST_F(ForecastNativeAdapterTest, CustomObserverProxyWorldAndUnrelatedPlayerCannotManufactureKnownAbsence)
{
	class CustomObserver final : public CPlayerBattleCallback
	{
	public:
		using CPlayerBattleCallback::CPlayerBattleCallback;
	};
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto outside = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	auto custom = std::make_shared<CustomObserver>(visible->getBattle(), PlayerColor(0));
	CPlayerBattleCallback proxyWorld(parent.get(), PlayerColor(0));
	CPlayerBattleCallback unrelated(visible->getBattle(), PlayerColor(2));
	const std::array<const CPlayerBattleCallback *, 3> observers{custom.get(), &proxyWorld, &unrelated};
	for(const auto * observer : observers)
	{
		const auto hidden = observer->battleGetVisibleHeroPolicy(BattleSide::DEFENDER);
		EXPECT_EQ(hidden.kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		EXPECT_EQ(hidden.legacyHero(), nullptr);
		const auto copied = hidden.informationOnly();
		EXPECT_EQ(copied.kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		EXPECT_FALSE(copied.matchesSuppliedHero(nullptr));
	}
	auto proxy = std::make_shared<HypotheticBattle>(environment.get(), custom);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), proxy);
	EXPECT_EQ(proxy->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(sibling->forecastSide(BattleSide::DEFENDER)->knowledge, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*proxy));
	EXPECT_FALSE(proxy->forecastState()); EXPECT_FALSE(sibling->forecastState());
	EXPECT_EQ(gameState()->saveToMemory(), outside);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, ActualPlayerHeroVersusCreepsUsesPublicAbsenceAndAdmitsOrdinaryDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:orrin", false, true));
	ASSERT_EQ(parent->getSidePlayer(BattleSide::DEFENDER), PlayerColor::NEUTRAL);
	ASSERT_EQ(visible->getPlayerID(), std::optional<PlayerColor>(PlayerColor(0)));
	EXPECT_EQ(visible->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
	EXPECT_FALSE(visible->battleHasHero(BattleSide::DEFENDER));
	const auto & absent = parent->forecastSide(BattleSide::DEFENDER);
	ASSERT_TRUE(absent);
	EXPECT_EQ(absent->knowledge, VisibleHeroPolicy::Knowledge::KNOWN_ABSENT);
	EXPECT_TRUE(absent->attunementKnown);
	EXPECT_EQ(absent->attunement, newHorizonsElementalAttunement::Profile{});
	EXPECT_FALSE(absent->hero); EXPECT_FALSE(absent->mana); EXPECT_FALSE(absent->ledger); EXPECT_FALSE(absent->perkOwner);
	EXPECT_EQ(absent->magicOwner, parent->getCapturedMagicRules());
	const auto outside = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto units = branch->battleGetAllUnits(false);
	const auto target = std::ranges::find_if(units, [](const battle::Unit * unit) { return unit->unitSide() == BattleSide::DEFENDER; });
	ASSERT_NE(target, units.end());
	auto actual = branch->getForUpdate((*target)->unitId());
	auto expected = reference->getForUpdate(actual->unitId());
	const auto parentHealth = parent->battleGetUnitByID(actual->unitId())->getAvailableHealth();
	int64_t damage = actual->getFirstHPleft(), nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*branch, *actual, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	EXPECT_EQ(damage, nativeDamage);
	EXPECT_EQ(actual->save(), expected->save());
	EXPECT_EQ(parent->battleGetUnitByID(actual->unitId())->getAvailableHealth(), parentHealth);
	EXPECT_EQ(sibling->battleGetUnitByID(actual->unitId())->getAvailableHealth(), parentHealth);
	EXPECT_EQ(sibling->forecastSide(BattleSide::DEFENDER), absent);
	EXPECT_EQ(gameState()->saveToMemory(), outside);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, ActualPlayerCannotUsePublicPresenceToReadAnOpposingHeroPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:orrin"));
	auto defender = std::make_shared<CPlayerBattleCallback>(visible->getBattle(), PlayerColor(1));
	EXPECT_TRUE(defender->battleHasHero(BattleSide::ATTACKER));
	const auto hidden = defender->battleGetVisibleHeroPolicy(BattleSide::ATTACKER);
	EXPECT_EQ(hidden.kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(hidden.legacyHero(), nullptr);
	EXPECT_FALSE(hidden.matchesSuppliedHero(nullptr));
	auto scoped = std::make_shared<HypotheticBattle>(environment.get(), defender);
	const auto & state = scoped->forecastSide(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->knowledge, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_FALSE(state->attunementKnown);
	EXPECT_FALSE(state->hero); EXPECT_FALSE(state->mana); EXPECT_FALSE(state->ledger); EXPECT_FALSE(state->perkOwner);
	EXPECT_EQ(state->magicOwner, scoped->getCapturedMagicRules());
	const auto outside = gameState()->saveToMemory();
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*scoped));
	EXPECT_FALSE(scoped->forecastState());
	EXPECT_EQ(state->knowledge, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(gameState()->saveToMemory(), outside);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
}

TEST_F(ForecastNativeAdapterTest, ActualDamageProbeUsesChangedCompactFactsAndPreservesParentRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	forecast::Counters preparation;
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent, &preparation));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent, &preparation));
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(preparation.rootCaptures, 1u);
	EXPECT_EQ(preparation.rootAdmissions, 1u);
#endif
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto roster = branch->battleGetAllUnits(false);
	ASSERT_EQ(roster.size(), 2u);
	auto actor = branch->getForUpdate(roster[0]->unitId());
	auto target = branch->getForUpdate(roster[1]->unitId());
	EXPECT_FALSE(branch->forecastState()->supports(forecast::Event::HeroResource));
	// Passive adventure and Hero-only metadata remain owned without granting a cast action.
	for(const auto * unit : {actor.get(), target.get()})
	{
		const auto guild = unit->getUnstackedBonuses(Selector::type()(BonusType::THIEVES_GUILD_ACCESS));
		ASSERT_EQ(guild->size(), 1u);
		EXPECT_EQ(guild->front()->duration, BonusDuration::PERMANENT);
		EXPECT_EQ(guild->front()->val, 1);
		for(const auto type : {BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN, BonusType::MAX_LEARNABLE_SPELL_LEVEL})
		{
			const auto rows = unit->getUnstackedBonuses(Selector::type()(type));
			ASSERT_EQ(rows->size(), 1u);
			EXPECT_EQ(rows->front()->source, BonusSource::GLOBAL);
			EXPECT_EQ(rows->front()->duration, BonusDuration::PERMANENT);
			EXPECT_EQ(rows->front()->valType, BonusValueType::BASE_NUMBER);
			EXPECT_EQ(rows->front()->val, type == BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN ? 1 : 2);
		}
	}
	const auto initial = target->getAvailableHealth();
	int64_t damage = 37, expectedDamage = damage;
	reference->getForUpdate(target->unitId())->damage(expectedDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*branch, *target, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	EXPECT_EQ(damage, expectedDamage);
	EXPECT_EQ(target->save(), reference->getForUpdate(target->unitId())->save());
	EXPECT_EQ(target->valOfBonuses(BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN), 1);
	EXPECT_EQ(target->valOfBonuses(BonusType::MAX_LEARNABLE_SPELL_LEVEL), 2);
	EXPECT_EQ(target->valOfBonuses(BonusType::THIEVES_GUILD_ACCESS), 1);
	EXPECT_FALSE(branch->forecastState()->supports(forecast::Event::HeroResource));
	EXPECT_EQ(parent->getForUpdate(target->unitId())->getAvailableHealth(), initial);
	const auto * facts = branch->preparedUnitDamageFacts(*target, false);
	ASSERT_NE(facts, nullptr);
	EXPECT_EQ(facts->unit.count, target->getCount());
	EXPECT_EQ(facts->unit.firstHealth, target->getFirstHPleft());
	BattleAttackInfo attack(actor.get(), target.get(), 0, false);
	BattleAttackInfo expected(reference->getForUpdate(actor->unitId()).get(), reference->getForUpdate(target->unitId()).get(), 0, false);
	const auto actualRange = branch->calculateDmgRange(attack);
	const auto expectedRange = reference->calculateDmgRange(expected);
	EXPECT_EQ(actualRange.damage.min, expectedRange.damage.min);
	EXPECT_EQ(actualRange.damage.max, expectedRange.damage.max);
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_GT(branch->forecastState()->work().compactFactHits, 0u);
#endif
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(branch->forecastState()->work().detachedHealthUpdates, 1u);
#endif
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, NativeStrikeAndEveryRoundStayInSyncAcrossTwoForks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	const auto id = branch->battleGetAllUnits(false).front()->unitId();
	auto actor = branch->getForUpdate(id);
	focusMagicAI::afterAttack(*actor, false, true, true, true, branch.get());
	EXPECT_EQ(branch->forecastState()->unit(id)->activation.retaliation.available, actor->counterAttacks.available());
	const auto originalRound = branch->battleGetRound();
	for(int iteration = 0; iteration < 2; ++iteration)
	{
		branch->nextRound();
		EXPECT_EQ(branch->forecastState()->currentRound(), originalRound + iteration + 1);
		EXPECT_EQ(branch->forecastState()->unit(id)->activation.retaliation.available, actor->counterAttacks.available());
	}
	EXPECT_EQ(parent->battleGetRound(), originalRound);
	EXPECT_EQ(parent->forecastState()->currentRound(), originalRound);
}

TEST_F(ForecastNativeAdapterTest, AuthoredNoPerkAenainRosterRetainsWispReachAndFiniteElementalState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto serverRng = projectedHitTest::serialized(*handler->randomizer);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto wispId = CreatureID(CreatureID::decode("new-horizons:wisp"));
	const battle::Unit * wisp = nullptr;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->creatureId() == wispId) wisp = unit;
	ASSERT_NE(wisp, nullptr);
	EXPECT_EQ(wisp->valOfBonuses(BonusType::LONG_REACH), 5);
	EXPECT_TRUE(wisp->hasBonusOfType(BonusType::PASS_THROUGH));
	ASSERT_NE(parent->preparedUnitDamageFacts(*wisp, false), nullptr);
	EXPECT_EQ(parent->forecastState()->ids().size(), 4u);
	EXPECT_EQ(parent->forecastSide(BattleSide::ATTACKER)->knowledge, VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
	EXPECT_EQ(parent->forecastSide(BattleSide::DEFENDER)->knowledge, VisibleHeroPolicy::Knowledge::KNOWN_ABSENT);
	EXPECT_EQ(parent->forecastSide(BattleSide::ATTACKER)->magicOwner, parent->forecastState()->source().magicOwner);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), serverRng);
}

TEST_F(ForecastNativeAdapterTest, OversizedLeadershipRecomputesNativeCurveAcrossCasualtiesAndResurrection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true, true));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto gremlin = CreatureID(CreatureID::decode("core:gremlin"));
	std::shared_ptr<StackWithBonuses> actor, target;
	for(const auto * unit : parent->battleGetAllUnits(false))
	{
		if(unit->creatureId() == gremlin) actor = parent->getForUpdate(unit->unitId());
		if(unit->unitSide() == BattleSide::DEFENDER) target = parent->getForUpdate(unit->unitId());
	}
	ASSERT_TRUE(actor); ASSERT_TRUE(target);
	ASSERT_TRUE(parent->battleCanShoot(actor.get()));
	const auto readout = reference->battleLeadershipReadout(reference->getForUpdate(actor->unitId()).get());
	ASSERT_TRUE(readout.active); ASSERT_GT(readout.capacity, 0); ASSERT_LT(readout.capacity, actor->getCount());
	const auto originalHealth = actor->getAvailableHealth();
	EXPECT_EQ(originalHealth, int64_t(actor->getCount()) * actor->getMaxHealth());
	const auto range = [&]()
	{
		BattleAttackInfo actual(actor.get(), target.get(), 0, true);
		BattleAttackInfo expected(reference->getForUpdate(actor->unitId()).get(), reference->getForUpdate(target->unitId()).get(), 0, true);
		const auto a = parent->calculateDmgRange(actual), b = reference->calculateDmgRange(expected);
		EXPECT_EQ(a.damage.min, b.damage.min); EXPECT_EQ(a.damage.max, b.damage.max);
		const auto * prepared = parent->preparedUnitDamageFacts(*actor, true);
		EXPECT_NE(prepared, nullptr);
		if(prepared && prepared->leadershipEfficiency)
			EXPECT_DOUBLE_EQ(*prepared->leadershipEfficiency, reference->battleLeadershipReadout(expected.attacker).efficiency);
		return a.damage.max;
	};
	const auto originalOffense = range();
	const int64_t survivingCount = std::max<int64_t>(1, readout.capacity - 1);
	int64_t loss = originalHealth - survivingCount * actor->getMaxHealth(), expectedLoss = loss;
	reference->getForUpdate(actor->unitId())->damage(expectedLoss);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *actor, loss, battle::DamageProvenance::OTHER, nullptr));
	EXPECT_EQ(loss, expectedLoss); EXPECT_EQ(actor->getCount(), survivingCount);
	EXPECT_EQ(actor->save(), reference->getForUpdate(actor->unitId())->save());
	const auto reducedOffense = range();
	EXPECT_LE(reducedOffense, originalOffense);
	const auto * belowCap = parent->preparedUnitDamageFacts(*actor, true);
	ASSERT_NE(belowCap, nullptr); ASSERT_TRUE(belowCap->leadershipEfficiency); EXPECT_DOUBLE_EQ(*belowCap->leadershipEfficiency, 1.0);
	int64_t restored = loss, expectedRestored = restored;
	const auto nativeHeal = reference->getForUpdate(actor->unitId())->heal(expectedRestored, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	const auto compactHeal = forecast::NativeAdapter::heal(*parent, *actor, restored, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(compactHeal); EXPECT_EQ(restored, expectedRestored);
	EXPECT_EQ(compactHeal->resurrectedCount, nativeHeal.resurrectedCount);
	EXPECT_EQ(actor->save(), reference->getForUpdate(actor->unitId())->save());
	EXPECT_EQ(actor->getAvailableHealth(), originalHealth);
	EXPECT_EQ(range(), originalOffense);
}

TEST_F(ForecastNativeAdapterTest, SelectedConduitFocusPolicyAndMetamagicDoNotBlanketRejectOrdinaryForecasts)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, true));
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalConduit"));
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage"));
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.conservedFormula"));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = parent->forecastState()->ids().front();
	const auto & captured = parent->forecastState()->definition(id)->leadership;
	if(captured.hero)
	{
		EXPECT_EQ(captured.capabilityOwner, hero->getCapturedCapabilityRules());
		EXPECT_EQ(captured.perkOwner, hero->getPerkState().capturedOwner());
	}
	EXPECT_TRUE(forecast::NativeAdapter::leadershipContextCurrent(*parent));
}

TEST_F(ForecastNativeAdapterTest, OrderLeaseIsOwningAndForksShareUntilExactReplacement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(visible->preparedHeroOrderStates(BattleSide::ATTACKER));
	const auto empty = parent->preparedHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->empty());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	EXPECT_EQ(child->preparedHeroOrderStates(BattleSide::ATTACKER), empty);
	HeroOrderState charge;
	charge.command = HeroCommand::CHARGE;
	charge.issuedRound = child->battleGetRound();
	ASSERT_NO_THROW(child->setHeroOrderStates(BattleSide::ATTACKER, {charge}));
	const auto active = child->preparedHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_TRUE(active);
	ASSERT_EQ(active->size(), 1u);
	EXPECT_NE(active, empty);
	EXPECT_TRUE(empty->empty());
	EXPECT_TRUE(parent->preparedHeroOrderStates(BattleSide::ATTACKER)->empty());
	child->setHeroOrderStates(BattleSide::ATTACKER, {});
	EXPECT_TRUE(child->preparedHeroOrderStates(BattleSide::ATTACKER)->empty());
	EXPECT_EQ(active->front(), charge); // Replaced leases remain alive and immutable.
}

TEST_F(ForecastNativeAdapterTest, MovementProofCannotCrossDivergedSiblingGeometryAtSameRevision)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto left = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *left);
	forecast::NativeAdapter::forkExchange(*parent, *right);
	const auto roster = parent->battleGetAllUnits(false);
	ASSERT_EQ(roster.size(), 2u);
	const auto actor = roster.front()->unitId(), blocker = roster.back()->unitId();
	const auto reach = parent->getReachability(roster.back());
	const auto legal = parent->battleGetAvailableHexes(reach, roster.back(), true);
	std::vector<BattleHex> choices;
	for(const auto hex : legal)
		if(hex != roster.back()->getPosition()) choices.push_back(hex);
	ASSERT_GE(choices.size(), 2u);
	left->moveUnit(blocker, choices[0]);
	right->moveUnit(blocker, choices[1]);
	auto a = *left->forecastState();
	auto b = *right->forecastState();
	ASSERT_EQ(a.revision(), b.revision());
	ASSERT_NE(a.unit(blocker)->position, b.unit(blocker)->position);
	const auto * mover = left->battleGetUnitByID(actor);
	const auto moverReach = left->getReachability(mover);
	const auto destinations = left->battleGetAvailableHexes(moverReach, mover, true);
	ASSERT_FALSE(destinations.empty());
	EXPECT_FALSE(forecast::NativeAdapter::prepareMove(b, actor, *destinations.begin(), *left, nullptr));
	const auto proof = forecast::NativeAdapter::prepareMove(a, actor, *destinations.begin(), *left, nullptr);
	ASSERT_TRUE(proof);
	const auto before = b.revision();
	const auto rejected = forecast::Transitions::move(b, *proof, nullptr);
	EXPECT_EQ(rejected.refusal, forecast::Refusal::Stale);
	EXPECT_EQ(b.revision(), before);
	EXPECT_EQ(b.unit(actor)->position, right->battleGetUnitByID(actor)->getPosition());
	EXPECT_TRUE(forecast::Transitions::move(a, *proof, nullptr));
}

TEST_F(ForecastNativeAdapterTest, AcceptedWaitRemainsOwnedAcrossDamageAndStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	const auto id = branch->battleGetAllUnits(false).front()->unitId();
	auto actor = branch->getForUpdate(id);
	branch->makeWait(actor.get());
	ASSERT_TRUE(actor->waitedThisTurn);
	EXPECT_EQ(branch->forecastState()->unit(id)->activation.waited, actor->waitedThisTurn);
	EXPECT_EQ(branch->forecastState()->unit(id)->activation.waiting, actor->waiting);
	int64_t damage = 1;
	ASSERT_TRUE(forecast::NativeAdapter::damage(*branch, *actor, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	EXPECT_TRUE(actor->waitedThisTurn);
	EXPECT_EQ(branch->forecastState()->unit(id)->activation.waited, actor->waitedThisTurn);
	focusMagicAI::afterAttack(*actor, false, false, true, true, branch.get());
	EXPECT_EQ(branch->forecastState()->unit(id)->activation.waitBonusUsed, actor->battlecraftWaitBonusUsed);
	EXPECT_FALSE(parent->forecastState()->unit(id)->activation.waited);
}

TEST_F(ForecastNativeAdapterTest, ActiveChargeUsesOwningOrderLeaseAndNativeDamageCoefficients)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, true));
	HeroOrderState charge;
	charge.command = HeroCommand::CHARGE;
	charge.issuedRound = parent->battleGetRound();
	ASSERT_GE(charge.issuedRound, 1);
	ASSERT_TRUE(heroCommands::supportedByRules(parent->getHeroCommandRules(), charge.command));
	parent->setHeroOrderStates(BattleSide::ATTACKER, {charge});
	ASSERT_TRUE(forecast::NativeAdapter::ordinaryOrdersSupported(*parent, BattleSide::ATTACKER));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	reference->setHeroOrderStates(BattleSide::ATTACKER, {charge});
	const auto roster = branch->battleGetAllUnits(false);
	const auto actor = std::find_if(roster.begin(), roster.end(), [](const battle::Unit * unit)
		{ return unit->unitSide() == BattleSide::ATTACKER; });
	const auto enemy = std::find_if(roster.begin(), roster.end(), [](const battle::Unit * unit)
		{ return unit->unitSide() == BattleSide::DEFENDER; });
	ASSERT_NE(actor, roster.end()); ASSERT_NE(enemy, roster.end());
	const auto actorId = (*actor)->unitId();
	const auto enemyId = (*enemy)->unitId();
	const auto compare = [&]()
	{
		// Order replacement publishes new owned native views. Rebind both
		// endpoints for this comparison instead of retaining earlier pointers.
		BattleAttackInfo actual(branch->battleGetUnitByID(actorId),
			branch->battleGetUnitByID(enemyId), 4, false);
		BattleAttackInfo expected(reference->getForUpdate(actorId).get(),
			reference->getForUpdate(enemyId).get(), 4, false);
		const auto compact = branch->calculateDmgRange(actual);
		const auto native = reference->calculateDmgRange(expected);
		EXPECT_EQ(compact.damage.min, native.damage.min);
		EXPECT_EQ(compact.damage.max, native.damage.max);
	};
	compare();
	const auto active = branch->preparedHeroOrderStates(BattleSide::ATTACKER);
	charge.consumedUnitIds = {actorId};
	branch->setHeroOrderStates(BattleSide::ATTACKER, {charge});
	reference->setHeroOrderStates(BattleSide::ATTACKER, {charge});
	EXPECT_TRUE(active->front().consumedUnitIds.empty());
	EXPECT_EQ(parent->preparedHeroOrderStates(BattleSide::ATTACKER), active);
	compare();
	ASSERT_TRUE(branch->forecastState());
}

#ifdef NH_PERF_EXPERIMENTS
TEST_F(ForecastNativeAdapterTest, DiagnosticReferenceOverrideRefusesOnlyAtRootAndCountsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * original = std::getenv("NH_PERF_FORCE_REFERENCE");
	const auto previous = original ? std::optional<std::string>(original) : std::nullopt;
	const auto set = [](const char * value)
	{
#ifdef _WIN32
		return _putenv_s("NH_PERF_FORCE_REFERENCE", value ? value : "");
#else
		return value ? setenv("NH_PERF_FORCE_REFERENCE", value, 1) : unsetenv("NH_PERF_FORCE_REFERENCE");
#endif
	};
	const auto restore = vstd::makeScopeGuard([&] { set(previous ? previous->c_str() : nullptr); });
	ASSERT_EQ(set("1"), 0);
	forecast::Counters preparation;
	const auto roster = parent->battleGetAllUnits(false);
	const auto before = parent->getForUpdate(roster.front()->unitId())->save();
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*parent, &preparation));
	EXPECT_FALSE(forecast::NativeAdapter::prepareExchange(*parent, &preparation));
	EXPECT_FALSE(parent->forecastState());
	EXPECT_EQ(preparation.rootCaptures, 1u);
	EXPECT_EQ(preparation.referenceOverrides, 1u);
	EXPECT_EQ(preparation.sourceFallbacks, 0u);
	EXPECT_EQ(parent->getForUpdate(roster.front()->unitId())->save(), before);
	ASSERT_EQ(set(nullptr), 0);
	auto ordinary = std::make_shared<HypotheticBattle>(environment.get(), visible);
	EXPECT_TRUE(forecast::NativeAdapter::prepareExchange(*ordinary));
}
#endif
