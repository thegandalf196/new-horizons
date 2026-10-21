/*
 * NewHorizonsVinesProjectionReceiptTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/mock_spells_Mechanics.h"
#include "../mock/mock_battle_Unit.h"
#include "../server/battles/NewHorizonsElementalTerrainFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsSpellcraft.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"

namespace
{
using namespace testing;
using Implementation = spells::effects::ForecastImplementation;
using Prepared = spells::effects::Effects::EffectsToApply;

class ReceiptEffect final : public spells::effects::Effect
{
public:
	Implementation implementation;
	explicit ReceiptEffect(Implementation value) : implementation(value) {}
	Implementation forecastImplementation(const spells::Mechanics *) const override { return implementation; }
	void adjustAffectedHexes(BattleHexArray &, const spells::Mechanics *, const spells::Target &) const override {}
	void apply(ServerCallback *, const spells::Mechanics *, const spells::Target &) const override {}
	spells::Target filterTarget(const spells::Mechanics *, const spells::Target & target) const override { return target; }
	spells::Target transformTarget(const spells::Mechanics *, const spells::Target &,
		const spells::Target & target) const override { return target; }
private:
	void initImpl(JsonNode) override {}
};

class ReceiptEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ReceiptEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

std::map<uint32_t, JsonNode> unitsSnapshot(const HypotheticBattle & state)
{
	// Unit state scalars only; bonus roots and their derived controllers are not serialized here.
	std::map<uint32_t, JsonNode> result;
	for(const auto * unit : state.battleGetAllUnits(true))
		result.emplace(unit->unitId(), unit->acquireState()->save());
	return result;
}

template<typename Generator>
std::vector<std::byte> randomState(const Generator & generator)
{
	CMemorySerializer memory;
	memory.oser & generator;
	return memory.extractBuffer();
}
}

TEST(NewHorizonsVinesProjectionReceipt, KeepsEffectOrderEmptyListsAndNormalizedOwnedDestinations)
{
	NiceMock<spells::MechanicsMock> mechanics;
	NiceMock<UnitMock> first, second;
	ON_CALL(first, unitId()).WillByDefault(Return(7));
	ON_CALL(first, getPosition()).WillByDefault(Return(BattleHex(8, 5)));
	ON_CALL(second, unitId()).WillByDefault(Return(9));
	ON_CALL(second, getPosition()).WillByDefault(Return(BattleHex(9, 5)));
	ReceiptEffect damage(Implementation::CANONICAL_DAMAGE), timed(Implementation::CANONICAL_TIMED);
	damage.name = "directDamage";
	timed.name = "slowingVines";
	Prepared prepared{{&damage, {spells::Destination(&first), spells::Destination(&second)}}, {&timed, {}}};
	const auto receipt = newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0);
	ASSERT_TRUE(receipt);
	ASSERT_EQ(receipt->effects.size(), 2u);
	EXPECT_TRUE(receipt->effects[1].destinations.empty());
	ASSERT_EQ(receipt->effects[0].destinations.size(), 2u);
	EXPECT_EQ(receipt->effects[0].destinations[0].unitId, 7u);
	EXPECT_EQ(receipt->effects[0].destinations[1].unitId, 9u);
	std::reverse(prepared[0].second.begin(), prepared[0].second.end());
	const auto reversed = newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0);
	ASSERT_TRUE(reversed);
	EXPECT_NE(*receipt, *reversed) << "An unordered recipient set loses damage-event order";
	ON_CALL(first, getPosition()).WillByDefault(Return(BattleHex(3, 3)));
	EXPECT_EQ(receipt->effects[0].destinations[0].hex, BattleHex(8, 5));
	EXPECT_FALSE(mechanics.vengefulVinesProjectionReceipt());
}

TEST(NewHorizonsVinesProjectionReceipt, UnknownChangedAndNonNormalizedEffectsFailClosed)
{
	NiceMock<spells::MechanicsMock> mechanics;
	NiceMock<UnitMock> unit;
	ON_CALL(unit, getPosition()).WillByDefault(Return(BattleHex(8, 5)));
	ReceiptEffect damage(Implementation::CANONICAL_DAMAGE), timed(Implementation::CANONICAL_TIMED);
	Prepared prepared{{&damage, {spells::Destination(&unit)}}, {&timed, {}}};
	ASSERT_TRUE(newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0));
	damage.implementation = Implementation::UNKNOWN;
	EXPECT_FALSE(newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0));
	damage.implementation = Implementation::CANONICAL_DAMAGE;
	ASSERT_TRUE(newHorizonsVengefulVines::supportsProjection(mechanics, prepared));
	timed.implementation = Implementation::UNKNOWN;
	EXPECT_FALSE(newHorizonsVengefulVines::supportsProjection(mechanics, prepared))
		<< "A dependency loaded during apply invalidates the pending certificate";
	timed.implementation = Implementation::CANONICAL_TIMED;
	prepared[0].second = {spells::Destination(&unit, BattleHex(9, 5))};
	EXPECT_FALSE(newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0));
	prepared[0].second = {spells::Destination(BattleHex(8, 5))};
	EXPECT_FALSE(newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0));
	prepared.emplace_back(&damage, spells::Target{});
	EXPECT_FALSE(newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0));
}

TEST(NewHorizonsVinesProjectionReceipt, EqualRecipientListsDoNotEraseResolvedConcentration)
{
	NiceMock<spells::MechanicsMock> mechanics;
	ReceiptEffect damage(Implementation::CANONICAL_DAMAGE), timed(Implementation::CANONICAL_TIMED);
	const Prepared prepared{{&damage, {}}, {&timed, {}}};
	const auto ordinary = newHorizonsVengefulVines::captureProjection(mechanics, prepared, 0);
	const auto concentrated = newHorizonsVengefulVines::captureProjection(mechanics, prepared, 20);
	ASSERT_TRUE(ordinary);
	ASSERT_TRUE(concentrated);
	EXPECT_EQ(ordinary->effects, concentrated->effects);
	EXPECT_NE(*ordinary, *concentrated)
		<< "Intended-stack counting can differ from final health-filtered targets";
}

class NewHorizonsVinesProjectionIntegration : public NewHorizonsElementalTerrainFixture
{
protected:
	std::shared_ptr<ReceiptEnvironment> environment;
	std::shared_ptr<CBattleCallback> callback;
	std::shared_ptr<CBattleInfoCallback> projectionSource;
	const battle::Unit * first = nullptr;
	const battle::Unit * second = nullptr;
	const BattleHex firstHex{8, 5}, secondHex{9, 5};
	SpellID vines() const { return SpellID(SpellID::decode(std::string(newHorizonsVengefulVines::SPELL_KEY))); }

	void prepare(bool concentration = false, bool rebirth = false)
	{
		startGame();
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		if(rebirth)
		{
			defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
			defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth")),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		}
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), rebirth ? 1 : 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), rebirth ? 1 : 1000));
		if(concentration)
		{
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
				MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", std::string(newHorizonsSpellcraft::CONCENTRATION)});
			attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", std::string(newHorizonsSpellcraft::EXTEND_SPELL)});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:spellcraft", std::string(newHorizonsSpellcraft::CONCENTRATION)));
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(vines());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::DEFENDER)
			{
				if(unit->unitSlot() == SlotID(0)) first = unit;
				if(unit->unitSlot() == SlotID(1)) second = unit;
			}
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		for(const auto & [unit, hex] : std::array{std::pair{first, firstHex}, std::pair{second, secondHex}})
		{
			BattleStackMoved move;
			move.battleID = BattleID(0);
			move.stack = unit->unitId();
			move.distance = 0;
			move.teleporting = true;
			move.tilesToMove.insert(hex);
			gameHandler->sendAndApply(move);
		}
		beginCombat();
		// The Rebirth order witness intentionally knows both hero profiles. A
		// player-0 forecast must not invent the hidden defender's death reactions.
		callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
		callback->onBattleStarted(battle());
		projectionSource = callback->getBattle(BattleID(0));
		if(rebirth)
		{
			projectionSource = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
			ASSERT_EQ(projectionSource->battleGetMySide(), BattleSide::ALL_KNOWING);
			ASSERT_EQ(projectionSource->battleGetFightingHero(BattleSide::DEFENDER), defenderSideHero);
			ASSERT_TRUE(newHorizonsElementalRebirth::activeProfile(defenderSideHero));
			ASSERT_GT(first->getBattleStartMaximumAggregateHP(), 0);
			ASSERT_GT(second->getBattleStartMaximumAggregateHP(), 0);
		}
		environment = std::make_shared<ReceiptEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> branch() const
	{
		return std::make_shared<HypotheticBattle>(environment.get(), projectionSource);
	}

	spells::Target target(const std::initializer_list<BattleHex> & hexes) const
	{
		spells::Target result;
		for(const auto hex : hexes) result.emplace_back(hex);
		return result;
	}

	std::unique_ptr<spells::Mechanics> project(HypotheticBattle & state, const spells::Target & aim) const
	{
		spells::BattleCast cast(&state, attackerSideHero, spells::Mode::HERO, vines().toSpell());
		auto mechanics = vines().toSpell()->battleMechanics(&cast);
		std::vector<Implementation> implementations;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			const auto implementation = effect.forecastImplementation(mechanics.get());
			EXPECT_NE(implementation, Implementation::UNKNOWN)
				<< "Registered Vines effect must attest its actual loaded closure: " << effect.name;
			implementations.push_back(implementation);
			return false;
		});
		EXPECT_EQ(implementations, (std::vector<Implementation>{
			Implementation::CANONICAL_DAMAGE, Implementation::CANONICAL_TIMED}));
		mechanics->castEval(state.getServerCallback(), aim);
		return mechanics;
	}
};

TEST_F(NewHorizonsVinesProjectionIntegration, DistinctGeometryKeepsOrderedInputsAndActualProjectedUnitsEqual)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto one = branch(), two = branch();
	const auto liveHealth = first->getAvailableHealth();
	const auto serverRng = randomState(*gameHandler->randomizer);
	const auto fixtureRng = randomState(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto firstCast = project(*one, target({firstHex, secondHex, BattleHex(10, 5)}));
	const auto secondCast = project(*two, target({firstHex, secondHex, BattleHex(9, 6)}));
	ASSERT_TRUE(firstCast->vengefulVinesProjectionReceipt());
	ASSERT_TRUE(secondCast->vengefulVinesProjectionReceipt());
	EXPECT_EQ(firstCast->vengefulVinesProjectionReceipt(), secondCast->vengefulVinesProjectionReceipt());
	EXPECT_EQ(unitsSnapshot(*one), unitsSnapshot(*two));
	EXPECT_EQ(one->hasCompletedHeroSpellCast(BattleSide::ATTACKER), two->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(first->getAvailableHealth(), liveHealth);
	EXPECT_EQ(randomState(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(randomState(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(NewHorizonsVinesProjectionIntegration, ReversedRecipientsChangeActualRebirthIdentityAllocation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	auto one = branch(), two = branch();
	const auto serverRng = randomState(*gameHandler->randomizer);
	const auto fixtureRng = randomState(dynamic_cast<CRandomGenerator &>(*getRNG()));
	ASSERT_TRUE(one->captureElementalRebirthSource(*one->battleGetUnitByID(first->unitId())));
	ASSERT_TRUE(one->captureElementalRebirthSource(*one->battleGetUnitByID(second->unitId())));
	const auto firstCast = project(*one, target({firstHex, secondHex, BattleHex(10, 5)}));
	const auto reverseCast = project(*two, target({secondHex, firstHex, BattleHex(7, 5)}));
	ASSERT_TRUE(firstCast->vengefulVinesProjectionReceipt());
	ASSERT_TRUE(reverseCast->vengefulVinesProjectionReceipt());
	EXPECT_NE(firstCast->vengefulVinesProjectionReceipt(), reverseCast->vengefulVinesProjectionReceipt());
	ASSERT_EQ(one->getElementalRebirthSpawnUnitIds().size(), 2u);
	ASSERT_EQ(two->getElementalRebirthSpawnUnitIds().size(), 2u);
	const auto allocatedFirst = *one->getElementalRebirthSpawnUnitIds().begin();
	EXPECT_EQ(one->battleGetUnitByID(allocatedFirst)->getPosition(), firstHex);
	EXPECT_EQ(two->battleGetUnitByID(allocatedFirst)->getPosition(), secondHex);
	EXPECT_NE(unitsSnapshot(*one), unitsSnapshot(*two));
	EXPECT_TRUE(first->alive());
	EXPECT_TRUE(second->alive());
	EXPECT_EQ(randomState(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(randomState(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}

TEST_F(NewHorizonsVinesProjectionIntegration, ConcentrationChildReceiptSurvivesAndFailedCallClearsIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto state = branch();
	const auto aim = target({firstHex, BattleHex(7, 5), BattleHex(7, 6)});
	auto mechanics = project(*state, aim);
	const auto receipt = mechanics->vengefulVinesProjectionReceipt();
	ASSERT_TRUE(receipt);
	EXPECT_GT(receipt->concentrationBonusPercent, 0);
	ASSERT_EQ(receipt->effects[0].destinations.size(), 1u);
	EXPECT_EQ(receipt->effects[0].destinations[0].unitId, first->unitId());
	EXPECT_EQ(state->getExtendSpellLastRound(BattleSide::ATTACKER), state->battleGetRound());
	mechanics->castEval(state->getServerCallback(), target({firstHex, firstHex, firstHex}));
	EXPECT_FALSE(mechanics->vengefulVinesProjectionReceipt());
}

TEST_F(NewHorizonsVinesProjectionIntegration, ActualControlledRecipientsKeepCanonicalAllegianceFiltering)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto ordinary = branch(), controlled = branch(), hypnotized = branch();
	const auto liveUnits = unitsSnapshot(*ordinary);
	const std::map<uint32_t, int64_t> liveHealth{
		{first->unitId(), first->getAvailableHealth()}, {second->unitId(), second->getAvailableHealth()}};
	const auto * friendly = ordinary->battleActiveUnit();
	ASSERT_NE(friendly, nullptr);
	ASSERT_EQ(friendly->unitSide(), BattleSide::ATTACKER);
	const auto friendlyId = friendly->unitId();
	ordinary->moveUnit(friendlyId, BattleHex(10, 5));
	controlled->moveUnit(friendlyId, BattleHex(10, 5));
	hypnotized->moveUnit(friendlyId, BattleHex(10, 5));
	Bonus hypnosis;
	hypnosis.type = BonusType::HYPNOTIZED;
	hypnosis.duration = BonusDuration::ONE_BATTLE;
	hypnotized->addUnitBonus(first->unitId(), {hypnosis});
	const auto * hypnosisTarget = hypnotized->battleGetUnitByID(first->unitId());
	ASSERT_EQ(hypnotized->battleGetOwner(hypnosisTarget), PlayerColor(0));
	ASSERT_TRUE(hypnotized->battleMatchOwner(PlayerColor(0), hypnosisTarget, false))
		<< "Legacy spell-polarity filtering uses the target's original army side";
	const SpellID puppet(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	controlled->addUnitBonus(first->unitId(), {newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(0))});
	controlled->addUnitBonus(friendlyId, {newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))});
	ASSERT_EQ(controlled->battleGetActionController(controlled->battleGetUnitByID(first->unitId())), PlayerColor(0));
	ASSERT_EQ(controlled->battleGetActionController(controlled->battleGetUnitByID(friendlyId)), PlayerColor(1));
	ASSERT_EQ(controlled->battleGetOwner(controlled->battleGetUnitByID(first->unitId())), PlayerColor(1));
	ASSERT_EQ(controlled->battleGetOwner(controlled->battleGetUnitByID(friendlyId)), PlayerColor(0));
	const auto friendlyHealth = friendly->getAvailableHealth();
	const auto friendlySpeed = friendly->getMovementRange();
	const auto aim = target({firstHex, secondHex, BattleHex(10, 5)});
	const auto normal = project(*ordinary, aim);
	const auto redirected = project(*controlled, aim);
	const auto hypnosisCast = project(*hypnotized, aim);
	ASSERT_TRUE(normal->vengefulVinesProjectionReceipt());
	const auto receipt = redirected->vengefulVinesProjectionReceipt();
	ASSERT_TRUE(receipt);
	const auto hypnosisReceipt = hypnosisCast->vengefulVinesProjectionReceipt();
	ASSERT_TRUE(hypnosisReceipt);
	ASSERT_EQ(receipt->effects.size(), 2u);
	for(const auto & effect : receipt->effects)
	{
		// Puppet changes action authority, never the smart spell's allegiance.
		ASSERT_EQ(effect.destinations.size(), 2u);
		EXPECT_EQ(effect.destinations[0].unitId, first->unitId());
		EXPECT_EQ(effect.destinations[1].unitId, second->unitId());
		EXPECT_EQ(effect.destinations[0].hex, firstHex);
		EXPECT_EQ(effect.destinations[1].hex, secondHex);
	}
	EXPECT_EQ(normal->vengefulVinesProjectionReceipt(), receipt);
	EXPECT_EQ(receipt, hypnosisReceipt);
	for(const auto * enemy : {first, second})
	{
		const auto * after = controlled->battleGetUnitByID(enemy->unitId());
		EXPECT_LT(after->getAvailableHealth(), enemy->getAvailableHealth());
		EXPECT_EQ(after->getAvailableHealth(), ordinary->battleGetUnitByID(enemy->unitId())->getAvailableHealth());
		EXPECT_EQ(after->getMovementRange(), enemy->getMovementRange() - 2);
		const auto * hypnotizedAfter = hypnotized->battleGetUnitByID(enemy->unitId());
		EXPECT_EQ(hypnotizedAfter->getAvailableHealth(), after->getAvailableHealth());
		EXPECT_EQ(hypnotizedAfter->getMovementRange(), after->getMovementRange());
		EXPECT_EQ(enemy->getAvailableHealth(), liveHealth.at(enemy->unitId()));
	}
	EXPECT_EQ(controlled->battleGetUnitByID(friendlyId)->getAvailableHealth(), friendlyHealth);
	EXPECT_EQ(controlled->battleGetUnitByID(friendlyId)->getMovementRange(), friendlySpeed);
	EXPECT_EQ(hypnotized->battleGetUnitByID(friendlyId)->getAvailableHealth(), friendlyHealth);
	EXPECT_EQ(hypnotized->battleGetUnitByID(friendlyId)->getMovementRange(), friendlySpeed);
	EXPECT_FALSE(first->isHypnotized());
	EXPECT_FALSE(newHorizonsPuppetMaster::hasControlMarker(first));
	EXPECT_EQ(unitsSnapshot(*branch()), liveUnits);
	// Saved CUnitState scalars omit bonus roots; observe the retained control
	// context directly instead of treating that snapshot as a complete state.
	EXPECT_EQ(ordinary->battleGetActionController(ordinary->battleGetUnitByID(first->unitId())), PlayerColor(1));
	EXPECT_EQ(controlled->battleGetActionController(controlled->battleGetUnitByID(first->unitId())), PlayerColor(0));
	EXPECT_EQ(ordinary->battleGetActionController(ordinary->battleGetUnitByID(friendlyId)), PlayerColor(0));
	EXPECT_EQ(controlled->battleGetActionController(controlled->battleGetUnitByID(friendlyId)), PlayerColor(1));
	for(const auto id : {first->unitId(), friendlyId})
	{
		EXPECT_FALSE(newHorizonsPuppetMaster::hasValidControlMarker(*ordinary, ordinary->battleGetUnitByID(id)));
		EXPECT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*controlled, controlled->battleGetUnitByID(id)));
	}
	EXPECT_TRUE(hypnotized->battleGetUnitByID(first->unitId())->isHypnotized());
	EXPECT_EQ(hypnotized->battleGetOwner(hypnotized->battleGetUnitByID(first->unitId())), PlayerColor(0));
}

TEST_F(NewHorizonsVinesProjectionIntegration, NegatedCastNeverPublishesReusableReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = branch();
	spells::BattleCast cast(state.get(), attackerSideHero, spells::Mode::HERO, vines().toSpell());
	cast.setCounterspell(BattleSide::DEFENDER, true);
	auto mechanics = vines().toSpell()->battleMechanics(&cast);
	mechanics->castEval(state->getServerCallback(), target({firstHex, secondHex, BattleHex(10, 5)}));
	EXPECT_FALSE(mechanics->vengefulVinesProjectionReceipt());
}
