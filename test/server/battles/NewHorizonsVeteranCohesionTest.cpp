/*
 * NewHorizonsVeteranCohesionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/NewHorizonsDiscipline.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
class CohesionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CohesionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

struct CohesionPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature <= Version::NEW_HORIZONS_SWIFT_REBIRTH; }
	template<typename T> CohesionPrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached packet payload");
	}
};

class NewHorizonsVeteranCohesionTest : public HeroCommandFixture
{
protected:
	CStack * victim = nullptr;
	CStack * enemy = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped-active New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void select(const std::string & perk)
	{
		const auto lookup = [this](const std::string & skill) { return attackerSideHero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t i = 0; i < offers.size(); ++i)
				if(offers[i].selection.skillId == newHorizonsDiscipline::SKILL && offers[i].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, i, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsDiscipline::SKILL, perk));
					return;
				}
		}
		FAIL() << "No legal active Discipline offer " << perk;
	}
	void prepare(bool perk = true, int count = 100, const std::string & creature = "core:peasant")
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode(newHorizonsDiscipline::SKILL));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(newHorizonsDiscipline::STEADFAST));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(perk)
			ASSERT_NO_FATAL_FAILURE(select(newHorizonsDiscipline::VETERAN_COHESION));
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(creature), count));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 10000));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSlot() == SlotID(0))
			{
				if(unit->unitSide() == BattleSide::ATTACKER)
					victim = battle()->getStack(unit->unitId(), false);
				else
					enemy = battle()->getStack(unit->unitId(), false);
			}
		ASSERT_NE(victim, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		ASSERT_EQ(newHorizonsDiscipline::hasVeteranCohesion(attackerSideHero), perk);
		ASSERT_EQ(victim->getBattleStartMaximumAggregateHP(), perk ? victim->getAvailableHealth() : 0);
	}
	void injureTo(int64_t remaining)
	{
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.attackerID = enemy->unitId();
		hit.stackAttacked = victim->unitId();
		hit.damageAmount = victim->getAvailableHealth() - remaining;
		ASSERT_GE(hit.damageAmount, 0);
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), victim->acquireState());
		gameHandler->sendAndApply(packet);
		ASSERT_EQ(victim->getAvailableHealth(), remaining);
	}
	void healFully()
	{
		auto state = victim->acquireState();
		auto amount = victim->getBattleStartMaximumAggregateHP();
		const auto restored = state->heal(amount,
			EHealLevel::RESURRECT, EHealPower::PERMANENT).healedHealthPoints;
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		auto & change = packet.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = restored;
		gameHandler->sendAndApply(packet);
	}
};
}

TEST_F(NewHorizonsVeteranCohesionTest, LiveStrictHalfUsesBattleStartBasisAndHealingNeverRearms)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto basis = victim->getBattleStartMaximumAggregateHP();
	const int raw = victim->valOfBonuses(BonusType::MORALE);
	ASSERT_NO_FATAL_FAILURE(injureTo(basis / 2));
	EXPECT_FALSE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw);
	ASSERT_NO_FATAL_FAILURE(injureTo(basis / 2 - 1));
	EXPECT_TRUE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw + 2);
	ASSERT_NO_FATAL_FAILURE(healFully());
	EXPECT_EQ(victim->getAvailableHealth(), basis);
	ASSERT_NO_FATAL_FAILURE(injureTo(basis / 2 - 1));
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw + 2);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	EXPECT_TRUE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw + 2);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto limits = newHorizonsMagic::moraleLimits(battle()->getMagicRules());
	ASSERT_TRUE(limits);
	EXPECT_EQ(victim->moraleVal(), limits->second);
}

TEST_F(NewHorizonsVeteranCohesionTest, OddAggregateThresholdAndLethalReceiptSurviveRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 101));
	const auto basis = victim->getBattleStartMaximumAggregateHP();
	const auto ceiling = basis / 2 + basis % 2;
	const int raw = victim->valOfBonuses(BonusType::MORALE);
	ASSERT_NO_FATAL_FAILURE(injureTo(ceiling));
	EXPECT_FALSE(victim->veteranCohesionEarned);
	ASSERT_NO_FATAL_FAILURE(injureTo(0));
	EXPECT_TRUE(victim->veteranCohesionEarned);
	ASSERT_NO_FATAL_FAILURE(healFully());
	EXPECT_TRUE(victim->alive());
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw + 2);
}

TEST_F(NewHorizonsVeteranCohesionTest, SameRankUnselectedAndNonNormalAddedStackDoNotEarn)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const int raw = victim->valOfBonuses(BonusType::MORALE);
	ASSERT_NO_FATAL_FAILURE(injureTo(victim->getAvailableHealth() / 3));
	EXPECT_FALSE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw);
	ASSERT_NO_FATAL_FAILURE(select(newHorizonsDiscipline::VETERAN_COHESION));
	auto * added = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(2, 2), 100);
	added->captureBattleStartMaximumAggregateHP();
	EXPECT_EQ(added->getBattleStartMaximumAggregateHP(), 0);
	auto state = added->acquireState();
	auto damage = state->getAvailableHealth() * 3 / 4;
	state->damage(damage);
	EXPECT_FALSE(state->veteranCohesionEarned);
}

TEST_F(NewHorizonsVeteranCohesionTest, DetachedParentChildrenAndLiveStackHaveIndependentOnceOnlyReceipts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CohesionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto branch = child->getForUpdate(victim->unitId());
	const auto basis = branch->getBattleStartMaximumAggregateHP();
	const int raw = branch->valOfBonuses(BonusType::MORALE);
	auto damage = branch->getAvailableHealth() - basis / 2 + 1;
	branch->damage(damage);
	EXPECT_TRUE(branch->veteranCohesionEarned);
	EXPECT_EQ(branch->valOfBonuses(BonusType::MORALE), raw + 2);
	EXPECT_FALSE(parent->getForUpdate(victim->unitId())->veteranCohesionEarned);
	EXPECT_FALSE(sibling->getForUpdate(victim->unitId())->veteranCohesionEarned);
	EXPECT_FALSE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw);
	// A materialized untouched child cannot inherit a later parent's reward.
	auto parentUnit = parent->getForUpdate(victim->unitId());
	damage = parentUnit->getAvailableHealth() - basis / 2 + 1;
	parentUnit->damage(damage);
	EXPECT_EQ(sibling->getForUpdate(victim->unitId())->valOfBonuses(BonusType::MORALE), raw);
	auto healing = basis;
	branch->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	damage = branch->getAvailableHealth() - basis / 2 + 1;
	branch->damage(damage);
	EXPECT_EQ(branch->valOfBonuses(BonusType::MORALE), raw + 2);
}

TEST_F(NewHorizonsVeteranCohesionTest, NonLivingImmunityAndOrdinaryMoraleCapRemainEffective)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 100, "core:earthElemental"));
	const int raw = victim->valOfBonuses(BonusType::MORALE);
	const int damageBoost = victim->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST);
	ASSERT_NO_FATAL_FAILURE(injureTo(victim->getBattleStartMaximumAggregateHP() / 2 - 1));
	EXPECT_TRUE(victim->veteranCohesionEarned);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), raw + 2);
	EXPECT_EQ(victim->moraleVal(), 0);
	EXPECT_EQ(victim->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST), damageBoost);
}

TEST_F(NewHorizonsVeteranCohesionTest, TypedMonotonicStateCurrentRoundTripAndOlderZeroPrefixGuards)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injureTo(victim->getBattleStartMaximumAggregateHP() / 2 - 1));
	auto state = victim->acquireState();
	const auto saved = state->save();
	ASSERT_TRUE(battle::hasVeteranCohesionState(saved));
	auto invalid = saved;
	invalid["state"]["veteranCohesionEarned"].Integer() = 1;
	EXPECT_THROW(state->load(invalid), std::runtime_error);
	EXPECT_TRUE(state->veteranCohesionEarned);
	auto attemptedReset = saved;
	attemptedReset["state"]["veteranCohesionEarned"].Bool() = false;
	state->load(attemptedReset);
	EXPECT_TRUE(state->veteranCohesionEarned);
	CStackBasicDescriptor base(victim->unitType()->getId(), victim->unitBaseAmount());
	CStack descriptor(&base, victim->unitOwner(), victim->unitId(), victim->unitSide());
	descriptor.veteranCohesionEarned = true;
	CMemorySerializer current;
	current.oser & descriptor;
	CStack restored;
	current.iser.cb = gameState().get();
	current.iser & restored;
	EXPECT_TRUE(restored.veteranCohesionEarned);
	CohesionPrefixProbe stackProbe;
	EXPECT_THROW(descriptor.serialize(stackProbe), std::runtime_error);
	EXPECT_EQ(stackProbe.fields, 0);
	UnitChanges change(victim->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = saved;
	CohesionPrefixProbe unitProbe;
	EXPECT_THROW(change.serialize(unitProbe), std::runtime_error);
	EXPECT_EQ(unitProbe.fields, 0);
	BattleUnitsChanged packet;
	packet.changedStacks.push_back(change);
	CohesionPrefixProbe packetProbe;
	EXPECT_THROW(packet.serialize(packetProbe), std::runtime_error);
	EXPECT_EQ(packetProbe.fields, 0);
	CohesionPrefixProbe battleProbe;
	EXPECT_THROW(battle()->serialize(battleProbe), std::runtime_error);
	EXPECT_EQ(battleProbe.fields, 0);
}
