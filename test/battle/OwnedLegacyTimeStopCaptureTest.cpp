/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedLegacyTimeStopCapture.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/GameLibrary.h"
#include <limits>
#include <stdexcept>
namespace
{
std::shared_ptr<Bonus> marker(SpellID id)
{
	return std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(id));
}
}
TEST(OwnedLegacyTimeStopCaptureTest, CapturesCanonicalAndNeededOtherIdentityBeforeAnyLibraryRetirement)
{
	const SpellID timeStop(SpellID::decode(newHorizonsSorcery::TIME_STOP_SPELL)); ASSERT_TRUE(timeStop.hasValue());
	const auto captured = battle::ownedFrameCapture::captureLegacyTimeStopIdentities({marker(timeStop), marker(SpellID(SpellID::FORTUNE))});
	ASSERT_TRUE(captured.canonical); EXPECT_EQ(*captured.canonical, timeStop);
	EXPECT_TRUE(captured.classified.at(timeStop)); EXPECT_FALSE(captured.classified.at(SpellID(SpellID::FORTUNE)));
	EXPECT_FALSE(captured.classified.contains(SpellID(SpellID::CURSE)));
}
TEST(OwnedLegacyTimeStopCaptureTest, InvalidPresentIdentityRetainsTheNativeLookupFailure)
{
	const SpellID invalid(std::numeric_limits<int32_t>::max());
	// The source service's getObjectImpl wraps an invalid index as runtime_error.
	EXPECT_THROW(invalid.toSpell(), std::runtime_error);
	EXPECT_THROW(battle::ownedFrameCapture::captureLegacyTimeStopIdentities({marker(invalid)}), std::runtime_error);
}
TEST(OwnedLegacyTimeStopCaptureTest, TraversesSharedNestedBonusIdentitiesWithoutMutatingNativeRecords)
{
	auto root = marker(SpellID(SpellID::FORTUNE));
	auto child = marker(SpellID(SpellID::CURSE));
	BonusParametersOnCombatEvent effects;
	effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, false});
	effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, true});
	root->parameters = std::make_shared<BonusParameters>(effects);
	const auto parameters = root->parameters;
	const auto captured = battle::ownedFrameCapture::captureLegacyTimeStopIdentities({root});
	EXPECT_FALSE(captured.classified.at(SpellID(SpellID::CURSE)));
	EXPECT_EQ(root->parameters, parameters);
	const auto * unchanged = parameters->combatEffects(); ASSERT_NE(unchanged, nullptr);
	ASSERT_EQ(unchanged->effects.size(), 2u);
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(unchanged->effects[0]).bonus, child);
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(unchanged->effects[1]).bonus, child);
}
TEST(OwnedLegacyTimeStopCaptureTest, IncludesOtherEffectTypesNeededByCanonicalRecordProtection)
{
	auto other = marker(SpellID(SpellID::CURSE));
	other->type = BonusType::MAGIC_RESISTANCE;
	const auto captured = battle::ownedFrameCapture::captureLegacyTimeStopIdentities({other});
	EXPECT_TRUE(captured.classified.contains(SpellID(SpellID::CURSE)));
	EXPECT_FALSE(captured.classified.at(SpellID(SpellID::CURSE)));
}
