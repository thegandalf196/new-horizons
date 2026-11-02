/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/mock_battle_Unit.h"
#include "../../lib/battle/TimeStopState.h"
#include "../../lib/battle/StackEffectRecordRules.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include <map>
#include <stdexcept>
namespace
{
class ClassifiedUnit : public UnitMock
{
public:
	std::map<SpellID, bool> classified;
	mutable std::vector<SpellID> observations;
	std::optional<bool> legacyTimeStopSpellMatch(SpellID spell) const override
	{
		observations.push_back(spell);
		if(!classified.contains(spell)) throw std::logic_error("Unbound owned spell identity");
		return classified.at(spell);
	}
	bool isTimeStopped() const override { return true; } // Rule-input stub; not a production clock shadow.
};
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
Bonus record(BonusType type, BonusSource source, SpellID spell)
{
	return Bonus(BonusDuration::ONE_BATTLE, type, source, 1, BonusSourceID(spell));
}
}
TEST(OwnedTimeStopRecordClassifierTest, DedicatedAndNonSpellPathsDoNotConsultAnySpellDomain)
{
	ClassifiedUnit unit; WithoutLibrary absent;
	EXPECT_TRUE(timeStopState::isStateBonus(record(BonusType::TIME_STOP, BonusSource::OTHER, SpellID(12)), unit));
	EXPECT_FALSE(timeStopState::isStateBonus(record(BonusType::NONE, BonusSource::OTHER, SpellID(12)), unit));
	EXPECT_TRUE(unit.observations.empty());
}
TEST(OwnedTimeStopRecordClassifierTest, CapturedFalseAndUnknownRemainDistinctBeforeStateTypeFiltering)
{
	ClassifiedUnit unit; unit.classified.emplace(SpellID(12), false); unit.classified.emplace(SpellID(13), true);
	WithoutLibrary absent;
	EXPECT_FALSE(timeStopState::isStateBonus(record(BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, SpellID(12)), unit));
	EXPECT_FALSE(timeStopState::isStateBonus(record(BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, SpellID(13)), unit));
	EXPECT_THROW(timeStopState::isStateBonus(record(BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, SpellID(14)), unit), std::logic_error);
	EXPECT_EQ(unit.observations, (std::vector<SpellID>{SpellID(12), SpellID(13), SpellID(14)}));
	for(const auto type : {BonusType::NONE, BonusType::NOT_ACTIVE, BonusType::INVINCIBLE})
		EXPECT_TRUE(timeStopState::isStateBonus(record(type, BonusSource::SPELL_EFFECT, SpellID(13)), unit));
	EXPECT_FALSE(stackEffectRecordRules::incomingProtected(unit, record(BonusType::NONE, BonusSource::SPELL_EFFECT, SpellID(13))));
	EXPECT_TRUE(stackEffectRecordRules::incomingProtected(unit, record(BonusType::NONE, BonusSource::SPELL_EFFECT, SpellID(12))));
}
TEST(OwnedTimeStopRecordClassifierTest, DefaultSubjectPreservesExistingNativeRecognition)
{
	UnitMock native;
	const SpellID timeStop(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL)); ASSERT_TRUE(timeStop.hasValue());
	for(const auto spell : {timeStop, SpellID(SpellID::FORTUNE)})
		for(const auto type : {BonusType::NONE, BonusType::NOT_ACTIVE, BonusType::INVINCIBLE, BonusType::STACKS_SPEED})
		{
			const auto value = record(type, BonusSource::SPELL_EFFECT, spell);
			EXPECT_EQ(timeStopState::isStateBonus(value, native), timeStopState::isStateBonus(value));
		}
}
