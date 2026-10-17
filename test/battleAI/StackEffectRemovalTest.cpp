/*
 * StackEffectRemovalTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/Propagators.h"

TEST(StackEffectRemovalTest, ExactLegacyComparedFieldsAndIntentionallyIgnoredMetadata)
{
	const Bonus selected(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 2, BonusSourceID());
	EXPECT_TRUE(battle::matchesStackEffectRemoval(selected, selected));
	for(unsigned field = 0; field < 8; ++field)
	{
		SCOPED_TRACE(field);
		Bonus candidate(selected);
		switch(field)
		{
		case 0: candidate.duration = BonusDuration::UNTIL_ATTACK; break;
		case 1: candidate.type = BonusType::LUCK; break;
		case 2: candidate.subtype = BonusSubtypeID(PrimarySkill::DEFENSE); break;
		case 3: candidate.source = BonusSource::SPELL_EFFECT; break;
		case 4: candidate.val = 3; break;
		case 5: candidate.sid = BonusSourceID(SpellID(SpellID::HASTE)); break;
		case 6: candidate.valType = BonusValueType::PERCENT_TO_ALL; break;
		case 7: candidate.effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT; break;
		}
		EXPECT_FALSE(battle::matchesStackEffectRemoval(selected, candidate));
	}
	Bonus ignored(selected);
	ignored.turnsRemain = 31;
	ignored.stacking = "different";
	ignored.propagator = std::make_shared<IPropagator>();
	ignored.appliedByEnemy = true;
	ignored.spellCasterOwner = PlayerColor(1);
	EXPECT_TRUE(battle::matchesStackEffectRemoval(selected, ignored));
	Bonus confusion(selected);
	confusion.type = BonusType::CONFUSION_PENDING;
	Bonus foreign(confusion);
	foreign.spellCasterOwner = PlayerColor(1);
	EXPECT_FALSE(battle::matchesStackEffectRemoval(confusion, foreign));
}
