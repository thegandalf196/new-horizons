/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include <map>
#include <stdexcept>

namespace
{
class CountedStack final : public CStack
{
public:
	mutable int ordinaryQueries = 0;
	mutable int markerQueries = 0;
	mutable int identityQueries = 0;
	bool capturedDomain = true;
	std::map<SpellID, bool> captured;

	explicit CountedStack(const CStackBasicDescriptor & descriptor)
		: CStack(&descriptor, PlayerColor(0), 7, BattleSide::ATTACKER) {}

	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override
	{
		++ordinaryQueries;
		return CStack::getAllBonuses(selector, key);
	}
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(
		const CSelector & selector, const std::string & key = "", bool unstacked = false) const override
	{
		++markerQueries;
		return CStack::getBonusesBeforeCreatureAbilitySuppression(selector, key, unstacked);
	}
	std::optional<bool> legacyTimeStopSpellMatch(SpellID spell) const override
	{
		++identityQueries;
		if(!capturedDomain)
			return std::nullopt;
		if(!captured.contains(spell))
			throw std::logic_error("Unclassified captured spell identity");
		return captured.at(spell);
	}
	void resetQueries() const
	{
		ordinaryQueries = markerQueries = identityQueries = 0;
	}
};

std::shared_ptr<Bonus> marker(BonusType type, BonusSource source = BonusSource::OTHER,
	BonusSourceID identity = BonusSourceID(), int value = 1)
{
	return std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, source, value, identity);
}

class UnitTimeStopQueryTest : public testing::Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 3};
	CountedStack subject{descriptor};
	const SpellID legacy{42000000};

	void expectQuery(bool stopped)
	{
		subject.resetQueries();
		EXPECT_EQ(subject.isTimeStopped(), stopped);
		EXPECT_EQ(subject.markerQueries, 1);
		EXPECT_EQ(subject.ordinaryQueries, 0);
	}

	void expectSuppressionFallback(bool stopped)
	{
		subject.resetQueries();
		EXPECT_EQ(subject.isTimeStopped(), stopped);
		EXPECT_EQ(subject.markerQueries, 3);
		EXPECT_EQ(subject.ordinaryQueries, 1);
	}
};
}

TEST_F(UnitTimeStopQueryTest, AbsentAndUnrelatedSpellEffectsNeedOneLimiterCorrectQuery)
{
	expectQuery(false);
	subject.addNewBonus(marker(BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, BonusSourceID(legacy)));
	expectQuery(false);
	EXPECT_EQ(subject.identityQueries, 0);
}

TEST_F(UnitTimeStopQueryTest, DedicatedPresenceIncludesZeroAndRetainsPriorityOverMalformedLegacy)
{
	const auto malformed = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy));
	subject.addNewBonus(malformed);
	EXPECT_THROW(subject.isTimeStopped(), std::logic_error);
	const auto dedicated = marker(BonusType::TIME_STOP, BonusSource::OTHER, BonusSourceID(), 0);
	subject.addNewBonus(dedicated);
	expectQuery(true);
	EXPECT_EQ(subject.identityQueries, 0);
	subject.removeBonus(dedicated);
	EXPECT_THROW(subject.isTimeStopped(), std::logic_error);
}

TEST_F(UnitTimeStopQueryTest, CapturedFalseAndTrueRemainDistinctWithoutCachingCurrentState)
{
	subject.captured.emplace(legacy, false);
	subject.addNewBonus(marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy)));
	expectQuery(false);
	EXPECT_EQ(subject.identityQueries, 1);
	subject.captured.at(legacy) = true;
	expectQuery(true);
	EXPECT_EQ(subject.identityQueries, 1);
}

TEST_F(UnitTimeStopQueryTest, NonSpellAndMissingIdentityMarkersDoNotConsultCapturedDomain)
{
	subject.addNewBonus(marker(BonusType::NONE, BonusSource::OTHER, BonusSourceID(legacy)));
	subject.addNewBonus(marker(BonusType::NONE, BonusSource::SPELL_EFFECT));
	expectQuery(false);
	EXPECT_EQ(subject.identityQueries, 0);
}

TEST_F(UnitTimeStopQueryTest, NativeIdentityFallbackPreservesRecognizedAndUnrelatedSpells)
{
	subject.capturedDomain = false;
	const SpellID timeStop(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
	ASSERT_TRUE(timeStop.hasValue());
	const auto unrelated = marker(BonusType::NONE, BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::FORTUNE)));
	subject.addNewBonus(unrelated);
	expectQuery(false);
	const auto clock = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(timeStop));
	subject.addNewBonus(clock);
	expectQuery(true);
	subject.removeBonus(clock);
	expectQuery(false);
}

TEST_F(UnitTimeStopQueryTest, SuppressionKeepsMarkersForeignAbilitiesAndNativeDefenses)
{
	subject.captured.emplace(legacy, true);
	subject.addNewBonus(marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy)));
	subject.addNewBonus(marker(BonusType::ADDITIONAL_ATTACK, BonusSource::CREATURE_ABILITY,
		BonusSourceID(CreatureID(0))));
	const auto foreign = marker(BonusType::ADDITIONAL_ATTACK, BonusSource::CREATURE_ABILITY,
		BonusSourceID(CreatureID(1)));
	subject.addNewBonus(foreign);
	subject.addNewBonus(marker(BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY,
		BonusSourceID(CreatureID(0)), 20));
	for(int level : {1, 2})
	{
		const auto suppression = marker(BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::FORGETFULNESS)), level);
		subject.addNewBonus(suppression);
		expectSuppressionFallback(true);
		const auto attacks = subject.getBonuses(Selector::type()(BonusType::ADDITIONAL_ATTACK));
		ASSERT_EQ(attacks->size(), 1u);
		EXPECT_EQ((*attacks)[0], foreign);
		EXPECT_EQ(subject.valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 20);
		subject.removeBonus(suppression);
	}
}

TEST_F(UnitTimeStopQueryTest, WarmNativeLimiterReevaluatesAfterEnablingBonusRemoval)
{
	const auto clock = marker(BonusType::TIME_STOP);
	clock->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::GUARDIAN_SPIRIT);
	subject.addNewBonus(clock);
	expectQuery(false);
	const auto guardian = marker(BonusType::GUARDIAN_SPIRIT);
	subject.addNewBonus(guardian);
	expectQuery(true);
	subject.removeBonus(guardian);
	expectQuery(false);
}

TEST_F(UnitTimeStopQueryTest, SharedStackingNameCannotMergeDedicatedAndLegacyTypes)
{
	subject.captured.emplace(legacy, false);
	const auto old = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy));
	const auto dedicated = marker(BonusType::TIME_STOP, BonusSource::OTHER, BonusSourceID(), 0);
	old->stacking = dedicated->stacking = "time-stop-type-separation";
	subject.addNewBonus(old);
	subject.addNewBonus(dedicated);
	expectQuery(true);
	EXPECT_EQ(subject.identityQueries, 0);
	subject.removeBonus(dedicated);
	expectQuery(false);
	EXPECT_EQ(subject.identityQueries, 1);
}

TEST_F(UnitTimeStopQueryTest, DetachedViewObservesNativeMarkerRemovalWithoutAnOrdinaryQuery)
{
	const auto clock = marker(BonusType::TIME_STOP);
	subject.addNewBonus(clock);
	battle::CUnitStateDetached detached(&subject, &subject);
	subject.resetQueries();
	EXPECT_TRUE(detached.isTimeStopped());
	EXPECT_EQ(subject.ordinaryQueries, 0);
	subject.removeBonus(clock);
	EXPECT_FALSE(detached.isTimeStopped());
	EXPECT_EQ(subject.ordinaryQueries, 0);
	EXPECT_FALSE(subject.isTimeStopped());
}

TEST_F(UnitTimeStopQueryTest, ActiveSuppressionSelectsLegacySpellSourceBeforeCrossSourceStacking)
{
	subject.captured.emplace(legacy, true);
	const auto spell = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy));
	const auto other = marker(BonusType::NONE, BonusSource::OTHER, BonusSourceID(), 2);
	spell->stacking = other->stacking = "legacy-source-collision";
	subject.addNewBonus(spell);
	subject.addNewBonus(other);
	expectQuery(false); // Ordinary no-suppression cache stacks globally first.
	for(int level : {1, 2})
	{
		const auto suppression = marker(BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::FORGETFULNESS)), level);
		subject.addNewBonus(suppression);
		expectSuppressionFallback(true); // Existing active path selects source, then stacks.
		EXPECT_EQ(subject.identityQueries, 1);
		subject.removeBonus(suppression);
		expectQuery(false);
	}
}

TEST_F(UnitTimeStopQueryTest, UnrecognizedSuppressionMarkerKeepsOriginalInactiveStackingSemantics)
{
	subject.captured.emplace(legacy, true);
	const auto spell = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy));
	const auto other = marker(BonusType::NONE, BonusSource::OTHER, BonusSourceID(), 2);
	spell->stacking = other->stacking = "legacy-source-collision";
	subject.addNewBonus(spell);
	subject.addNewBonus(other);
	subject.addNewBonus(marker(BonusType::CREATURE_ABILITY_SUPPRESSION)); // Wrong source/SID.
	expectSuppressionFallback(false); // Fallback does not invent active Forgetfulness.
	EXPECT_EQ(subject.identityQueries, 0);
}

TEST_F(UnitTimeStopQueryTest, DedicatedMarkerWinsBeforeSuppressionFallbackAndMalformedLegacy)
{
	subject.addNewBonus(marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(legacy)));
	subject.addNewBonus(marker(BonusType::TIME_STOP, BonusSource::OTHER, BonusSourceID(), 0));
	subject.addNewBonus(marker(BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::FORGETFULNESS))));
	expectQuery(true);
	EXPECT_EQ(subject.identityQueries, 0);
}

TEST_F(UnitTimeStopQueryTest, InheritedSuppressionRemovalPreservesDetachedSourceCollisionSemantics)
{
	const SpellID timeStop(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL));
	ASSERT_TRUE(timeStop.hasValue());
	subject.captured.emplace(timeStop, true);
	const auto spell = marker(BonusType::NONE, BonusSource::SPELL_EFFECT, BonusSourceID(timeStop));
	const auto other = marker(BonusType::NONE, BonusSource::OTHER, BonusSourceID(), 2);
	spell->stacking = other->stacking = "legacy-source-collision";
	subject.addNewBonus(spell);
	subject.addNewBonus(other);
	CBonusSystemNode parent(BonusNodeType::HERO);
	subject.attachTo(parent);
	battle::CUnitStateDetached detached(&subject, &subject);
	EXPECT_FALSE(detached.isTimeStopped());
	const auto suppression = marker(BonusType::CREATURE_ABILITY_SUPPRESSION, BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::FORGETFULNESS)));
	parent.addNewBonus(suppression);
	expectSuppressionFallback(true);
	EXPECT_TRUE(detached.isTimeStopped());
	parent.removeBonus(suppression);
	expectQuery(false);
	EXPECT_FALSE(detached.isTimeStopped());
	subject.detachFrom(parent);
}
