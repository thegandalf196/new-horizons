/*
 * NewHorizonsOverwatchTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
class OverwatchEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OverwatchEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class OverwatchCallback final : public CBattleCallback
{
public:
	OverwatchCallback() : CBattleCallback(PlayerColor(1), nullptr) {}
};

/// Detached binary descriptors preserve the exact new sidecar fields without
/// detaching or attaching the original authoritative stack graph.
class OverwatchDescriptorWriter
{
	BinarySerializer & encoder;
public:
	using Version = ESerializationVersion;
	static constexpr bool saving = true;
	explicit OverwatchDescriptorWriter(BinarySerializer & value) : encoder(value) {}
	bool hasFeature(Version version) const { return encoder.hasFeature(version); }
	template<typename T> OverwatchDescriptorWriter & operator&(T & value) { encoder & value; return *this; }
	OverwatchDescriptorWriter & operator&(std::vector<std::unique_ptr<CStack>> & units)
	{
		std::vector<std::unique_ptr<CStack>> descriptors;
		for(const auto & unit : units)
		{
			CStackBasicDescriptor base(unit->unitType()->getId(), unit->unitBaseAmount());
			auto copy = std::make_unique<CStack>(&base, unit->unitOwner(), static_cast<int>(unit->unitId()),
				unit->unitSide(), unit->unitSlot());
			copy->initialPosition = unit->initialPosition;
			copy->battlecraftOverwatchReadyRound = unit->battlecraftOverwatchReadyRound;
			copy->battlecraftOverwatchUsedRound = unit->battlecraftOverwatchUsedRound;
			for(const auto & bonus : unit->getExportedBonusList())
				copy->addNewBonus(std::make_shared<Bonus>(*bonus));
			descriptors.push_back(std::move(copy));
		}
		encoder & descriptors;
		return *this;
	}
};
}

class NewHorizonsOverwatchTest : public HeroCommandFixture
{
protected:
	static constexpr auto skillKey = "new-horizons:battlecraft";
	static constexpr auto perkKey = "new-horizons:battlecraft.overwatch";
	CStack * shooter = nullptr;
	CStack * mover = nullptr;
	CStack * secondMover = nullptr;
	std::vector<std::byte> beforeBattle;
	bool grantGhostWalk = false;
	bool selectVeiledMovement = false;
	bool attackerOverwatch = false;
	bool legacyPerkRules = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the production-active New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(legacyPerkRules)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
			return;
		}
		if(selectVeiledMovement)
		{
			bool foundVeiled = false;
			for(const auto & perk : perks["skills"][std::string(newHorizonsShroud::SKILL_ID)]["perks"].Vector())
				if(perk["id"].String() == newHorizonsShroud::VEILED_MOVEMENT_PERK_ID)
				{
					ASSERT_EQ(perk["effect"]["status"].String(), "active");
					foundVeiled = true;
				}
			ASSERT_TRUE(foundVeiled);
		}
		bool registered = false;
		for(const auto & perk : perks["skills"][skillKey]["perks"].Vector())
			if(perk["id"].String() == perkKey)
			{
				ASSERT_EQ(perk["effect"]["status"].String(), "active")
					<< "Candidate is not accepted until the shipped registry is active";
				registered = true;
			}
		ASSERT_TRUE(registered);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	void prepare(bool selected = true, int moverCount = 1000, int shooterCount = 100,
		bool counterfireMover = false)
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode(skillKey));
		ASSERT_GE(skill.getNum(), 0);
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			const auto lookup = [this](const std::string & key) { return defenderSideHero->getPerkSkillRank(key); };
			bool accepted = false;
			for(uint64_t seed = 0; seed < 4096 && !accepted; ++seed)
			{
				const auto offers = defenderSideHero->getPerkState().prepareOffer(lookup, seed);
				for(size_t index = 0; index < offers.size(); ++index)
					if(offers[index].selection.skillId == skillKey && offers[index].selection.perkId == perkKey)
					{
						gameHandler->levelUpHero(defenderSideHero, offers, index, seed, false);
						accepted = true;
						break;
					}
			}
			ASSERT_TRUE(accepted) << "Active Overwatch must be legally offered";
		}
		ASSERT_EQ(newHorizonsBattlecraft::hasOverwatch(defenderSideHero), selected);
		if(grantGhostWalk)
		{
			const SecondarySkill shroud(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID)));
			ASSERT_TRUE(shroud.hasValue());
			attackerSideHero->setSecSkillLevel(shroud, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			if(selectVeiledMovement)
				attackerSideHero->applyPerkSelection({std::string(newHorizonsShroud::SKILL_ID),
					std::string(newHorizonsShroud::VEILED_MOVEMENT_PERK_ID)});
			ASSERT_EQ(newHorizonsShroud::hasVeiledMovement(attackerSideHero), selectVeiledMovement);
		}
		if(attackerOverwatch)
		{
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({skillKey, perkKey});
			ASSERT_TRUE(newHorizonsBattlecraft::hasOverwatch(attackerSideHero));
		}
		if(counterfireMover)
		{
			const SecondarySkill archery(SecondarySkill::decode("new-horizons:archery"));
			ASSERT_GE(archery.getNum(), 0);
			attackerSideHero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:archery", "new-horizons:archery.counterfire"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:archery", "new-horizons:archery.counterfire"));
		}
		beforeBattle = gameState()->saveToMemory();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), shooterCount);
		mover = addStack(BattleSide::ATTACKER,
			creatureByName(counterfireMover ? "core:archer" : "core:pikeman"), BattleHex(2, 5), moverCount);
		secondMover = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 6), 1000);
		ASSERT_NE(shooter, nullptr);
		ASSERT_NE(mover, nullptr);
		ASSERT_NE(secondMover, nullptr);
		forceMaximumDamage(shooter);
		beginCombat();
	}

	void activate(CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	bool wait(CStack * unit)
	{
		activate(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(unit->unitSide()), BattleAction::makeWait(unit));
	}

	bool move(CStack * unit, BattleHex destination)
	{
		activate(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(unit->unitSide()), BattleAction::makeMove(unit, destination));
	}

	size_t shotsRecorded() const
	{
		return std::ranges::count_if(server.attacks, [this](const BattleAttack & attack)
			{ return attack.stackAttacking == shooter->unitId() && attack.shot(); });
	}

	void relocate(CStack * unit, BattleHex destination, bool teleporting)
	{
		BattleStackMoved pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.tilesToMove.insert(destination);
		pack.teleporting = teleporting;
		gameHandler->sendAndApply(pack);
	}
};

TEST_F(NewHorizonsOverwatchTest, SelectedVeiledMovementSuppressesLiveDetachedAndHazardReactions)
{
	grantGhostWalk = selectVeiledMovement = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	EXPECT_TRUE(battle()->battleGetOverwatchReactors(mover, BattleHex(3, 5), BattleHex(4, 5)).empty());
	const auto ammo = shooter->shots.available();
	const auto health = mover->getAvailableHealth();
	OverwatchEnvironment environment(gameState());
	auto callback = std::make_shared<OverwatchCallback>();
	callback->onBattleStarted(battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	EXPECT_TRUE(child->projectVoluntaryMovement(mover->unitId(), BattleHex(4, 5)).empty());
	EXPECT_EQ(child->battleGetUnitByID(mover->unitId())->getPosition(), BattleHex(4, 5));
	EXPECT_EQ(child->battleGetUnitByID(mover->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(child->getForUpdate(shooter->unitId())->shots.available(), ammo);
	EXPECT_EQ(parent->battleGetUnitByID(mover->unitId())->getPosition(), BattleHex(2, 5));
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(mover->getPosition(), BattleHex(4, 5));
	EXPECT_EQ(mover->getAvailableHealth(), health);
	EXPECT_EQ(shooter->shots.available(), ammo);
	EXPECT_EQ(shotsRecorded(), 0u);
	EXPECT_TRUE(battle()->battleOverwatchReady(shooter));
	EXPECT_EQ(shooter->battlecraftOverwatchUsedRound, -1);
}

TEST_F(NewHorizonsOverwatchTest, UnselectedVeiledMovementDoesNotHideOrdinaryGhostWalk)
{
	grantGhostWalk = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	ASSERT_GT(newHorizonsShroud::rank(attackerSideHero), 0);
	ASSERT_FALSE(newHorizonsShroud::hasVeiledMovement(attackerSideHero));
	EXPECT_TRUE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	const auto ammo = shooter->shots.available();
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(shotsRecorded(), 1u);
	EXPECT_EQ(shooter->shots.available(), ammo - 1);
}

TEST_F(NewHorizonsOverwatchTest, VeilingUsesCurrentControllerRatherThanOriginalArmySide)
{
	grantGhostWalk = selectVeiledMovement = attackerOverwatch = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	const auto shooterControl = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(shooterControl);
	mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwnerHero(mover), defenderSideHero);
	ASSERT_EQ(battle()->battleGetOwnerHero(shooter), attackerSideHero);
	ASSERT_FALSE(newHorizonsShroud::hasVeiledMovement(defenderSideHero));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	// Vanilla Hypnotize targeting compares the actor's controller against the
	// target's original army. Switching both units therefore makes this target
	// ineligible before Veiled Movement is considered.
	ASSERT_FALSE(battle()->battleMatchOwner(shooter, mover));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	shooter->removeBonus(shooterControl);
	ASSERT_EQ(battle()->battleGetOwnerHero(shooter), defenderSideHero);
	ASSERT_EQ(battle()->battleGetOwnerHero(mover), defenderSideHero);
	ASSERT_TRUE(battle()->battleMatchOwner(shooter, mover));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	EXPECT_TRUE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	EXPECT_EQ(battle()->battleGetOverwatchReactors(mover, BattleHex(3, 5), BattleHex(4, 5)),
		(std::vector<uint32_t>{shooter->unitId()}));
}

TEST_F(NewHorizonsOverwatchTest, AbsentSavedPerkRulesDoNotAdmitVeiledMovementOrOverwatch)
{
	legacyPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_FALSE(newHorizonsShroud::hasVeiledMovement(attackerSideHero));
	EXPECT_FALSE(newHorizonsShroud::hasVeiledMovement(nullptr));
	ASSERT_TRUE(wait(shooter));
	const auto ammo = shooter->shots.available();
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(shotsRecorded(), 0u);
	EXPECT_EQ(shooter->shots.available(), ammo);
}

TEST_F(NewHorizonsOverwatchTest, AcceptedWaitReactsOnFirstEntryAtHalfDamageSpendsAmmoAndOnlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(battle()->battleOverwatchReady(shooter));
	ASSERT_TRUE(wait(shooter));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	const auto round = battle()->getRound();
	const auto ammo = shooter->shots.available();
	const auto counters = shooter->counterAttacks.available();
	ASSERT_TRUE(move(mover, BattleHex(3, 5)));
	EXPECT_EQ(shotsRecorded(), 0u);
	EXPECT_TRUE(battle()->battleOverwatchReady(shooter));
	BattleAttackInfo expected(shooter, mover, 0, true);
	expected.retaliation = true;
	expected.attackerPos = shooter->getPosition();
	expected.defenderPos = BattleHex(4, 5);
	expected.archeryRangedDamageMultiplierPercent = newHorizonsBattlecraft::OVERWATCH_DAMAGE_PERCENT;
	const auto damage = battle()->calculateDmgRange(expected).damage.max;
	const auto health = mover->getAvailableHealth();
	ASSERT_GT(damage, 0);
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(shotsRecorded(), 1u);
	EXPECT_EQ(mover->getAvailableHealth(), health - damage);
	EXPECT_EQ(shooter->shots.available(), ammo - 1);
	EXPECT_EQ(shooter->counterAttacks.available(), counters);
	EXPECT_EQ(shooter->battlecraftOverwatchUsedRound, round);
	EXPECT_EQ(shooter->battlecraftOverwatchReadyRound, -1);
	EXPECT_FALSE(battle()->battleOverwatchReady(shooter));
	ASSERT_TRUE(move(secondMover, BattleHex(4, 6)));
	EXPECT_EQ(shotsRecorded(), 1u);
}

TEST_F(NewHorizonsOverwatchTest, DelayedActivationAndNewRoundExpireReadiness)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	activate(shooter);
	EXPECT_FALSE(battle()->battleOverwatchReady(shooter));
	EXPECT_EQ(shooter->battlecraftOverwatchReadyRound, -1);
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(shotsRecorded(), 0u);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	ASSERT_TRUE(wait(shooter));
	ASSERT_TRUE(battle()->battleOverwatchReady(shooter));
	gameHandler->sendAndApply(next);
	EXPECT_EQ(shooter->battlecraftOverwatchReadyRound, -1);
	EXPECT_EQ(shooter->battlecraftOverwatchUsedRound, -1);
}

TEST_F(NewHorizonsOverwatchTest, ForcedRelocationAndAlreadyInsideMovementDoNotTrigger)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	relocate(mover, BattleHex(4, 5), true);
	EXPECT_EQ(mover->getPosition(), BattleHex(4, 5));
	EXPECT_EQ(shotsRecorded(), 0u);
	EXPECT_TRUE(battle()->battleOverwatchReady(shooter));
	ASSERT_TRUE(move(mover, BattleHex(5, 5)));
	EXPECT_EQ(shotsRecorded(), 0u);
	EXPECT_TRUE(battle()->battleOverwatchReady(shooter));
}

TEST_F(NewHorizonsOverwatchTest, LethalEntryStopsMoverBeforeRequestedDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1, 1000));
	ASSERT_TRUE(wait(shooter));
	ASSERT_TRUE(move(mover, BattleHex(6, 5)));
	EXPECT_FALSE(mover->alive());
	EXPECT_EQ(mover->getPosition(), BattleHex(4, 5));
	EXPECT_NE(mover->getPosition(), BattleHex(6, 5));
	EXPECT_EQ(shotsRecorded(), 1u);
}

TEST_F(NewHorizonsOverwatchTest, LimitedRangeAndLegalTargetControlsUseSharedPredicate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	EXPECT_EQ(battle()->battleGetOverwatchReactors(mover, BattleHex(3, 5), BattleHex(4, 5)),
		(std::vector<uint32_t>{shooter->unitId()}));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, secondMover, BattleHex(4, 6), BattleHex(5, 6)));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, shooter, BattleHex(3, 5), BattleHex(4, 5)));
	Bonus limited(BonusDuration::ONE_BATTLE, BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 4, BonusSourceID());
	SetStackEffect apply;
	apply.battleID = BattleID(0);
	apply.toAdd.emplace_back(shooter->unitId(), std::vector<Bonus>{limited});
	gameHandler->sendAndApply(apply);
	EXPECT_EQ(newHorizonsBattlecraft::overwatchRange(shooter), 4);
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, mover, BattleHex(3, 5), BattleHex(4, 5)));
	EXPECT_TRUE(battle()->battleCanOverwatch(shooter, mover, BattleHex(9, 5), BattleHex(10, 5)));
	shooter->shots.use(shooter->shots.available());
	EXPECT_FALSE(battle()->battleOverwatchReady(shooter));
	EXPECT_FALSE(battle()->battleCanOverwatch(shooter, mover, BattleHex(9, 5), BattleHex(10, 5)));
}

TEST_F(NewHorizonsOverwatchTest, UnselectedPerkDoesNotArmOrReact)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_TRUE(wait(shooter));
	EXPECT_FALSE(battle()->battleOverwatchReady(shooter));
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(shotsRecorded(), 0u);
}

TEST_F(NewHorizonsOverwatchTest, CounterReactionCannotStartCounterfireRecursion)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1000, 100, true));
	ASSERT_TRUE(wait(shooter));
	const auto targetAmmo = mover->shots.available();
	const auto targetHealth = mover->getAvailableHealth();
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	ASSERT_EQ(shotsRecorded(), 1u);
	EXPECT_LT(mover->getAvailableHealth(), targetHealth);
	EXPECT_EQ(mover->shots.available(), targetAmmo);
	EXPECT_EQ(mover->archeryCounterfireRound, -1);
	EXPECT_EQ(std::ranges::count_if(server.attacks, [this](const BattleAttack & attack)
		{ return attack.stackAttacking == mover->unitId(); }), 0);
	const auto reaction = std::ranges::find_if(server.attacks, [this](const BattleAttack & attack)
		{ return attack.stackAttacking == shooter->unitId(); });
	ASSERT_NE(reaction, server.attacks.end());
	EXPECT_TRUE(reaction->counter());
	EXPECT_TRUE(reaction->shot());
}

TEST_F(NewHorizonsOverwatchTest, StateCopiesAndVersionedUpdateRoundTripRejectOldProtocol)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	const auto round = battle()->getRound();
	auto state = shooter->acquireState();
	auto copy = shooter->acquireState();
	*copy = *state;
	EXPECT_EQ(copy->battlecraftOverwatchReadyRound, round);
	copy->load(state->save());
	EXPECT_EQ(copy->battlecraftOverwatchReadyRound, round);
	auto legacy = state->save();
	legacy["state"].Struct().erase("battlecraftOverwatchReadyRound");
	legacy["state"].Struct().erase("battlecraftOverwatchUsedRound");
	copy->load(legacy);
	EXPECT_EQ(copy->battlecraftOverwatchReadyRound, -1);
	EXPECT_EQ(copy->battlecraftOverwatchUsedRound, -1);
	auto invalid = state->save();
	invalid["state"]["battlecraftOverwatchUsedRound"] = JsonNode(-2);
	EXPECT_THROW(copy->load(invalid), std::runtime_error);
	BattleUnitsChanged pack;
	pack.battleID = BattleID(0);
	pack.changedStacks.emplace_back(shooter->unitId(), UnitChanges::EOperation::UPDATE);
	pack.changedStacks.back().data = state->save();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REBIRTH_CHAIN;
	EXPECT_THROW(old.oser & pack, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer oldUpdate;
	oldUpdate.oser.version = ESerializationVersion::NEW_HORIZONS_REBIRTH_CHAIN;
	EXPECT_THROW(oldUpdate.oser & pack.changedStacks.front(), std::runtime_error);
	EXPECT_TRUE(oldUpdate.extractBuffer().empty());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & pack;
	CMemorySerializer reader(current.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	reader.iser.cb = gameState().get();
	BattleUnitsChanged decoded;
	reader.iser & decoded;
	ASSERT_EQ(decoded.changedStacks.size(), 1u);
	EXPECT_EQ(decoded.changedStacks.front().data["state"]["battlecraftOverwatchReadyRound"].Integer(), round);
}

TEST_F(NewHorizonsOverwatchTest, BattleStartRestoreRebindPreservesReadyAndUsedMarkers)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1, 1000));
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 8), 10), nullptr);
	ASSERT_TRUE(wait(shooter));
	const auto round = battle()->getRound();
	const auto id = shooter->unitId();
	const auto restoreAndCheck = [this, id, round](bool ready)
	{
		auto replica = std::make_shared<CGameState>();
		replica->preInit(LIBRARY);
		replica->loadFromMemory(beforeBattle);
		BattleStart outgoing;
		outgoing.battleID = BattleID(0);
		CMemorySerializer snapshot;
		OverwatchDescriptorWriter descriptorWriter(snapshot.oser);
		battle()->serialize(descriptorWriter);
		snapshot.iser.cb = replica.get();
		outgoing.info = std::make_unique<BattleInfo>(replica.get());
		snapshot.iser & *outgoing.info;
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::CURRENT;
		wire.oser & outgoing;
		wire.iser.version = ESerializationVersion::CURRENT;
		wire.iser.cb = replica.get();
		BattleStart incoming;
		wire.iser & incoming;
		RecordingGameServer restoredServer;
		restoredServer.gameState = replica;
		auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
		restoredHandler->sendAndApply(incoming);
		auto * restored = replica->getBattle(BattleID(0));
		ASSERT_NE(restored, nullptr);
		const auto * restoredShooter = restored->getStack(id, false);
		ASSERT_NE(restoredShooter, nullptr);
		EXPECT_EQ(restoredShooter->battlecraftOverwatchReadyRound, ready ? round : -1);
		EXPECT_EQ(restoredShooter->battlecraftOverwatchUsedRound, ready ? -1 : round);
		EXPECT_EQ(restored->battleOverwatchReady(restoredShooter), ready);
		EXPECT_EQ(battle()->battleOverwatchReady(shooter), ready)
			<< "Replica rebinding must not mutate the original authoritative battle";
	};
	ASSERT_NO_FATAL_FAILURE(restoreAndCheck(true));
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	ASSERT_FALSE(mover->alive());
	ASSERT_TRUE(secondMover->alive()) << "Retain a live opposing army so combat continues";
	ASSERT_EQ(shooter->battlecraftOverwatchUsedRound, round);
	// Death naturally consumes unrelated Veteran history. A bare activation
	// packet is not the server's complete activation flow and cannot clear it.
	// Test the Used marker without weakening the broader binary-save guard.
	ASSERT_NO_FATAL_FAILURE(restoreAndCheck(false));
}

TEST_F(NewHorizonsOverwatchTest, DetachedMovementMatchesLiveDamageAmmoMarkersAndKeepsParentIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(wait(shooter));
	const auto id = shooter->unitId();
	const auto moverId = mover->unitId();
	const auto ammo = shooter->shots.available();
	const auto health = mover->getAvailableHealth();
	OverwatchEnvironment environment(gameState());
	auto callback = std::make_shared<OverwatchCallback>();
	callback->onBattleStarted(battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto hits = child->projectVoluntaryMovement(moverId, BattleHex(4, 5));
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_EQ(hits.front().shooterId, id);
	EXPECT_EQ(hits.front().position, BattleHex(4, 5));
	const auto projectedShooter = child->getForUpdate(id);
	const auto * projectedMover = child->battleGetUnitByID(moverId);
	ASSERT_NE(projectedMover, nullptr);
	EXPECT_EQ(projectedShooter->shots.available(), ammo - 1);
	EXPECT_FALSE(child->battleOverwatchReady(projectedShooter.get()));
	EXPECT_TRUE(parent->battleOverwatchReady(parent->battleGetUnitByID(id)));
	EXPECT_TRUE(battle()->battleOverwatchReady(shooter));
	EXPECT_EQ(mover->getAvailableHealth(), health);
	EXPECT_EQ(shooter->shots.available(), ammo);
	ASSERT_TRUE(move(mover, BattleHex(4, 5)));
	EXPECT_EQ(mover->getAvailableHealth(), projectedMover->getAvailableHealth());
	EXPECT_EQ(health - mover->getAvailableHealth(), hits.front().healthLoss);
	EXPECT_EQ(shooter->shots.available(), projectedShooter->shots.available());
	EXPECT_EQ(shooter->battlecraftOverwatchUsedRound, projectedShooter->battlecraftOverwatchUsedRound);
}

TEST_F(NewHorizonsOverwatchTest, LethalAttackForecastHasNoOutgoingStrikeAndSelectedReplaySpendsOneAmmo)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1, 1000));
	auto * intendedTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(6, 5), 1000);
	ASSERT_NE(intendedTarget, nullptr);
	ASSERT_TRUE(wait(shooter));
	const auto ammo = shooter->shots.available();
	const auto targetHealth = intendedTarget->getAvailableHealth();
	OverwatchEnvironment environment(gameState());
	auto callback = std::make_shared<OverwatchCallback>();
	callback->onBattleStarted(battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	DamageCache damageCache;
	damageCache.buildDamageCache(parent, BattleSide::ATTACKER);
	const BattleAttackInfo incoming(parent->battleGetUnitByID(mover->unitId()),
		parent->battleGetUnitByID(intendedTarget->unitId()), 0, false);
	const auto possibility = AttackPossibility::evaluate(incoming, BattleHex(5, 5), damageCache, parent);
	ASSERT_NE(possibility.attackerState, nullptr);
	EXPECT_FALSE(possibility.attackerState->alive());
	EXPECT_EQ(possibility.defenderDamageReduce, 0);
	EXPECT_TRUE(possibility.fortuneStrikes.empty());
	ASSERT_EQ(possibility.overwatchHits.size(), 1u);
	EXPECT_TRUE(parent->battleGetUnitByID(mover->unitId())->alive());
	EXPECT_TRUE(parent->battleOverwatchReady(parent->battleGetUnitByID(shooter->unitId())));
	EXPECT_TRUE(mover->alive());
	EXPECT_EQ(shooter->shots.available(), ammo);
	EXPECT_EQ(intendedTarget->getAvailableHealth(), targetHealth);
	auto replay = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, replay, damageCache);
	EXPECT_FALSE(replay->battleGetUnitByID(mover->unitId())->alive());
	EXPECT_EQ(replay->getForUpdate(shooter->unitId())->shots.available(), ammo - 1);
	EXPECT_EQ(replay->battleGetUnitByID(intendedTarget->unitId())->getAvailableHealth(), targetHealth);
	EXPECT_TRUE(mover->alive());
	EXPECT_EQ(shooter->shots.available(), ammo);
}
