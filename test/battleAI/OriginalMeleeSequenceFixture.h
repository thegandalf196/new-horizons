/*
 * OriginalMeleeSequenceFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/OriginalMeleeSequence.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/CRandomGenerator.h"

class OriginalMeleeSequenceTest : public ProjectedTargetHitFixture
{
protected:
	std::shared_ptr<HypotheticBattle> known;
	struct UnitOracle
	{
		uint32_t id;
		int64_t health;
		int count;
		BattleHex position;
		int counters;
		bool defended;
		int attack;
		JsonNode saved;
	};
	struct Oracle
	{
		bool complete;
		std::vector<heroOrderValue::MeleeHitReceipt> hits;
		std::vector<UnitOracle> units;
	};

	heroOrderValue::BraceMeleeAction action() const
	{
		return {incoming->unitId(), braced->unitId(), BattleHex(92), braced->getPosition()};
	}

	void releaseKnown()
	{
		known.reset();
		child.reset();
		live.reset();
	}

	void refreshLegacyKnown()
	{
		releaseKnown();
		auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		ASSERT_EQ(allKnowing->battleGetMySide(), BattleSide::ALL_KNOWING);
		known = std::make_shared<HypotheticBattle>(environment.get(), allKnowing);
		ASSERT_FALSE(known->initialExportSubject());
	}

	virtual void refreshKnown()
	{
		releaseKnown();
		// Test-only explicit authoritative perspective, not a player callback bypass.
		auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		ASSERT_EQ(allKnowing->battleGetMySide(), BattleSide::ALL_KNOWING);
		const auto origin = InitialExportBattleSubject::capture(allKnowing, *battle());
		known = std::make_shared<HypotheticBattle>(environment.get(), origin);
		ASSERT_TRUE(known->initialExportSubject());
	}

	void prepareSequence(bool brace = true, bool retaliate = true, bool lastStand = false,
		bool lethal = false, int extra = 0, int incomingCount = 100)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(lastStand, lethal ? 100 : 10, lethal ? 1 : incomingCount));
		releaseKnown();
		forceMaximumDamage(braced);
		forceMaximumDamage(incoming);
		if(!retaliate)
		{
			blockRetaliation(braced);
			blockRetaliation(incoming);
		}
		if(extra)
			incoming->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, extra, BonusSourceID()));
		if(brace)
		{
			activate(braced->unitId());
			ASSERT_TRUE(issue(HeroCommand::BRACE));
		}
		activate(incoming->unitId());
		ASSERT_NO_FATAL_FAILURE(refreshKnown());
	}

	heroOrderValue::OriginalMeleeSequence project()
	{
		if(known && !known->initialExportSubject())
			return heroOrderValue::projectLegacyOriginalMeleeSequence(environment.get(), known, *battle(), action());
		return heroOrderValue::projectOriginalMeleeSequence(environment.get(), known, action());
	}

	void retireKnown(heroOrderValue::OriginalMeleeSequence & result)
	{
		result.branch.reset();
		releaseKnown();
	}

	void compareNative(heroOrderValue::OriginalMeleeSequence & result)
	{
		ASSERT_TRUE(result.meleeSequenceComplete());
		ASSERT_TRUE(result.branch);
		const auto frozen = freezeOracle(result);
		retireKnown(result);
		ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	}

	Oracle freezeOracle(const heroOrderValue::OriginalMeleeSequence & result) const
	{
		Oracle frozen{result.meleeSequenceComplete(), result.hits, {}};
		for(const CStack * original : {incoming, braced})
		{
			const auto * unit = result.branch->battleGetUnitByID(original->unitId());
			const auto state = unit->acquireState();
			frozen.units.push_back({unit->unitId(), unit->getAvailableHealth(), unit->getCount(),
				unit->getPosition(), state->counterAttacks.available(), unit->defended(),
				unit->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)), state->save()});
		}
		return frozen; // Own values only: no branch, provider, unit or graph aliases.
	}

	void compareNativeFrozen(const Oracle & frozen)
	{
		ASSERT_TRUE(frozen.complete);
		ASSERT_FALSE(known);
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
		ASSERT_EQ(server.attacks.size(), frozen.hits.size());
		std::map<uint32_t, JsonNode> lastPacketState;
		for(size_t index = 0; index < frozen.hits.size(); ++index)
		{
			const auto & receipt = frozen.hits[index];
			const auto & actual = server.attacks[index];
			EXPECT_EQ(actual.stackAttacking, receipt.actorId);
			EXPECT_EQ(actual.counter(), receipt.damageRetaliation);
			ASSERT_EQ(actual.bsa.size(), 1);
			EXPECT_EQ(actual.bsa.front().stackAttacked, receipt.victimId);
			EXPECT_EQ(actual.bsa.front().damageAmount, receipt.appliedDamage);
			for(const auto & change : actual.attackerChanges.changedStacks)
				lastPacketState[change.id] = change.data;
			for(const auto & hit : actual.bsa)
				lastPacketState[hit.stackAttacked] = hit.newState.data;
		}
		for(const auto & projected : frozen.units)
		{
			const auto * original = battle()->battleGetUnitByID(projected.id);
			ASSERT_NE(original, nullptr);
			EXPECT_EQ(projected.health, original->getAvailableHealth());
			EXPECT_EQ(projected.count, original->getCount());
			EXPECT_EQ(projected.position, original->getPosition());
			EXPECT_EQ(projected.counters, original->acquireState()->counterAttacks.available());
			EXPECT_EQ(projected.attack, original->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)));
			// The real command can already advance to another activation. Compare
			// activation-local fields at the attack packet boundary, not afterward.
			ASSERT_TRUE(lastPacketState.contains(original->unitId()));
			for(const std::string key : {"battlecraftWaitBonusUsed", "veteranPhysicalDamageSinceActivation"})
				EXPECT_EQ(projected.saved["state"][key], lastPacketState.at(original->unitId())["state"][key]);
		}
	}
};
