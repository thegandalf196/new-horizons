/*
 * OwnedExportMeleeFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "OriginalMeleeSequenceFixture.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Limiters.h"

class OwnedExportMeleeTest : public OriginalMeleeSequenceTest
{
protected:
	std::shared_ptr<InitialExportBattleSubject> seed;
	void refreshKnown() override
	{
		// This fixture begins its originating-history lease at captureOwned(),
		// after its explicitly authored native setup, not during prepareSequence.
		ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	}
	void captureOwned()
	{
		known.reset();
		child.reset();
		live.reset();
		auto authorized = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		seed = InitialExportBattleSubject::capture(authorized, *battle());
	}
	void startOwned()
	{
		known = std::make_shared<HypotheticBattle>(environment.get(), seed);
		ASSERT_NE(known->initialExportSubject(), nullptr);
	}
	Bonus aura(BonusDuration::Type duration, BonusType type = BonusType::PRIMARY_SKILL)
	{
		Bonus value(duration, type, BonusSource::OTHER, type == BonusType::PRIMARY_SKILL ? 4 : 1,
			BonusSourceID(), type == BonusType::PRIMARY_SKILL ? BonusSubtypeID(PrimarySkill::ATTACK) : BonusSubtypeID());
		value.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		// These tests exercise creature effects, not a changed hero rating/policy.
		// Native collection filters hero-applicable inherited A/D; other types
		// would alter the hero view and correctly fail the full identity witness.
		value.limiter = std::make_shared<CreatureLevelLimiter>();
		return value;
	}
	InitialExportWorld::ExportHandle addOrigin(CStack & source, const Bonus & record)
	{
		return seed->world().add(seed->world().sourceAt(seed->occurrence(source.unitId())), record);
	}
	void retire(heroOrderValue::OriginalMeleeSequence & result)
	{
		result.branch.reset();
		known.reset();
		seed.reset(); // All private query providers are gone before native dispatch.
	}
	void compareAfterNativeAdd(heroOrderValue::OriginalMeleeSequence & result, CStack & source, const Bonus & record)
	{
		const auto frozen = freezeOracle(result);
		retire(result);
		source.addNewBonus(std::make_shared<Bonus>(record));
		ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	}
};
