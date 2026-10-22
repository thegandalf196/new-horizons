/*
 * NewHorizonsSpellLockIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include <limits>

namespace
{
Bonus timed(SpellID spell, BonusType type, int value = 1)
{
	Bonus result(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, BonusSourceID(spell));
	result.turnsRemain = 2;
	return result;
}
}

TEST(NewHorizonsSpellLockIdentityTest, UnrelatedOrMalformedSourcesDoNotRequireOptionalLockDefinition)
{
	NativeBonusGraphAccess access;
	CBonusSystemNode bearer(BonusNodeType::STACK_BATTLE);
	for(const auto type : {BonusType::NONE, BonusType::MAGIC_RESISTANCE})
	{
		bearer.addNewBonus(std::make_shared<Bonus>(timed(SpellID::HASTE, type, 100)));
		bearer.addNewBonus(std::make_shared<Bonus>(timed(SpellID::NONE, type, 100)));
		bearer.addNewBonus(std::make_shared<Bonus>(timed(SpellID(std::numeric_limits<int32_t>::max()), type, 100)));
		auto wrongType = timed(SpellID::HASTE, type, 100);
		wrongType.sid = BonusSourceID(CreatureID(0));
		bearer.addNewBonus(std::make_shared<Bonus>(wrongType));
	}
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(bearer));
	EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(bearer), std::nullopt);
}

TEST(NewHorizonsSpellLockIdentityTest, CoreTimedPolarityPreservationNeedsNoInstalledLock)
{
	auto haste = timed(SpellID::HASTE, BonusType::STACKS_SPEED);
	auto slow = timed(SpellID::SLOW, BonusType::STACKS_SPEED);
	EXPECT_TRUE(newHorizonsSorcery::isPreservedSpellLockEffect(&haste, true));
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&haste, false));
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&slow, true));
	EXPECT_TRUE(newHorizonsSorcery::isPreservedSpellLockEffect(&slow, false));
	haste.source = BonusSource::OTHER;
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&haste, true));
	haste = timed(SpellID::NONE, BonusType::STACKS_SPEED);
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&haste, true));
	haste.sid = BonusSourceID(SpellID(std::numeric_limits<int32_t>::max()));
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&haste, true));
	EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(nullptr, true));
}

TEST(NewHorizonsSpellLockIdentityTest, InstalledMarkersMatchSourceIdQueriesWithAndWithoutStackingCollisions)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires installed Spell Lock content";
	NativeBonusGraphAccess access;
	const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	for(const bool collision : {false, true})
	{
		SCOPED_TRACE(::testing::Message() << "same-key collision=" << collision);
		CBonusSystemNode bearer(BonusNodeType::STACK_BATTLE);
		auto resistance = timed(lock, BonusType::MAGIC_RESISTANCE, 100);
		resistance.stacking = "spell-lock-identity-test";
		auto foreignResistance = timed(SpellID::HASTE, BonusType::MAGIC_RESISTANCE, 999);
		foreignResistance.stacking = collision ? resistance.stacking : "unrelated-resistance-test";
		auto polarity = timed(lock, BonusType::NONE, 0);
		polarity.stacking = "spell-lock-polarity-test";
		auto foreignPolarity = timed(SpellID::HASTE, BonusType::NONE, 10);
		foreignPolarity.stacking = collision ? polarity.stacking : "unrelated-polarity-test";
		bearer.addNewBonus(std::make_shared<Bonus>(foreignResistance));
		bearer.addNewBonus(std::make_shared<Bonus>(foreignPolarity));
		bearer.addNewBonus(std::make_shared<Bonus>(resistance));
		bearer.addNewBonus(std::make_shared<Bonus>(polarity));

		// Preserve the original source-ID queries, including native cache stacking.
		const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(lock));
		const auto originalResistance = bearer.getBonuses(source.And(Selector::type()(BonusType::MAGIC_RESISTANCE)));
		const bool originalLocked = originalResistance && vstd::contains_if(*originalResistance, [](const std::shared_ptr<Bonus> & bonus)
		{
			return bonus && Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0;
		});
		std::optional<bool> originalPolarity;
		const auto originalMarkers = bearer.getBonuses(source.And(Selector::type()(BonusType::NONE)));
		if(originalMarkers)
		{
			for(const auto & bonus : *originalMarkers)
			{
				if(bonus && Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0)
				{
					originalPolarity = bonus->val > 0;
					break;
				}
			}
		}
		EXPECT_EQ(newHorizonsSorcery::isSpellLocked(bearer), originalLocked);
		EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(bearer), originalPolarity);
		EXPECT_EQ(originalLocked, !collision);
		EXPECT_EQ(originalPolarity, collision ? std::nullopt : std::optional<bool>(false));
		EXPECT_FALSE(newHorizonsSorcery::isPreservedSpellLockEffect(&polarity, false));
	}
}

TEST(NewHorizonsSpellLockIdentityTest, ExpiredAndPermanentInstalledMarkersKeepTheirExistingExclusion)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires installed Spell Lock content";
	NativeBonusGraphAccess access;
	CBonusSystemNode bearer(BonusNodeType::STACK_BATTLE);
	const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	for(const auto type : {BonusType::NONE, BonusType::MAGIC_RESISTANCE})
	{
		auto expired = std::make_shared<Bonus>(timed(lock, type, 100));
		bearer.addNewBonus(expired);
		expired->turnsRemain = 0;
		bearer.nodeHasChanged();
		auto permanent = timed(lock, type, 100);
		permanent.duration = BonusDuration::PERMANENT;
		bearer.addNewBonus(std::make_shared<Bonus>(permanent));
	}
	EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(bearer));
	EXPECT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(bearer), std::nullopt);
}
