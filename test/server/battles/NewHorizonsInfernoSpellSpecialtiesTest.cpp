/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../../../lib/spells/NewHorizonsInfernoSpecialties.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
class InfernoSpellSpecialtiesTest : public CastleSpecialtyTest
{
protected:
	void arena(const char * hero, int level)
	{
		ASSERT_NO_FATAL_FAILURE(specialist(hero, level, false));
		ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::FIREBALL, "new-horizons:havocMagic"));
		ASSERT_NO_FATAL_FAILURE(emptyField());
		active = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(1, 5), 100, 1000);
		enemy = place(BattleSide::DEFENDER, "core:halberdier", BattleHex(9, 5), 1000, 100);
		ASSERT_TRUE(active && enemy);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
	}
	CStack * active = nullptr;
	CStack * enemy = nullptr;
};
}
TEST_F(InfernoSpellSpecialtiesTest, KindlingUsesCompletedFireSequenceAndExactHalfPercentCap)
{
	arena("core:xyron", 1);
	const auto bonus = [&] { return infernoSpellSpecialties::kindlingBonusBasisPoints(*battle(), BattleSide::ATTACKER, SpellID::FIREBALL); };
	EXPECT_EQ(bonus(), 0);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(bonus(), 250);
	attackerSideHero->level = 3;
	EXPECT_EQ(bonus(), 750);
	attackerSideHero->level = 20;
	EXPECT_EQ(bonus(), 1000);
	battle()->getSide(BattleSide::ATTACKER).usedSpellsHistory.push_back(SpellID::BLESS);
	EXPECT_EQ(bonus(), 0);
	battle()->getSide(BattleSide::ATTACKER).usedSpellsHistory.push_back(SpellID::FIREBALL);
	EXPECT_EQ(bonus(), 1000);
}
TEST_F(InfernoSpellSpecialtiesTest, LegacyCaptureHasNoKindling)
{
	stripped = {"kindling"};
	arena("core:xyron", 20);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(infernoSpellSpecialties::kindlingBonusBasisPoints(*battle(), BattleSide::ATTACKER, SpellID::FIREBALL), 0);
}
TEST_F(InfernoSpellSpecialtiesTest, BloodfireAlliesKeepHealthAndGainAttack)
{
	arena("core:ash", 10);
	auto * ally = place(BattleSide::ATTACKER, "core:pikeman", BattleHex(8, 5), 20, 500);
	const auto health = ally->getAvailableHealth();
	const auto attack = ally->getAttack(false);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	EXPECT_EQ(ally->getAvailableHealth(), health);
	EXPECT_EQ(ally->getAttack(false), attack + 4);
}
TEST_F(InfernoSpellSpecialtiesTest, BurnAwayRefreshBecomesNewestAndOrderSurvivesSerialization)
{
	arena("core:xarfax", 1);
	Bonus first(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::DEFENSE));
	first.turnsRemain = 2;
	Bonus second = first;
	second.sid = BonusSourceID(SpellID(SpellID::SHIELD));
	second.val = 2;
	SetStackEffect initial;
	initial.battleID = BattleID(0);
	initial.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{first, second});
	gameHandler->sendAndApply(initial);
	first.turnsRemain = 4;
	SetStackEffect refresh;
	refresh.battleID = BattleID(0);
	refresh.toUpdate.emplace_back(enemy->unitId(), std::vector<Bonus>{first});
	infernoSpellSpecialties::preserveRefreshChronology(*battle(), refresh);
	ASSERT_EQ(refresh.toRemove.size(), 1);
	ASSERT_EQ(refresh.toAdd.size(), 1);
	EXPECT_TRUE(refresh.toUpdate.empty());
	gameHandler->sendAndApply(refresh);
	auto local = battle()->getLocalSpellEffects(enemy->unitId());
	ASSERT_EQ(local.size(), 2);
	EXPECT_EQ(local.back().sid, first.sid);
	EXPECT_EQ(local.back().turnsRemain, 4);
	// Local spell-effect vectors are the ordinary persisted representation.
	CMemorySerializer storage;
	storage.oser & local;
	std::vector<Bonus> restored;
	storage.iser & restored;
	ASSERT_EQ(restored.size(), 2);
	EXPECT_EQ(restored.front().sid, second.sid);
	EXPECT_EQ(restored.back().sid, first.sid);
	SetStackEffect restore;
	restore.battleID = BattleID(0);
	restore.toRemove.emplace_back(enemy->unitId(), local);
	restore.toAdd.emplace_back(enemy->unitId(), restored);
	gameHandler->sendAndApply(restore);
	ASSERT_TRUE(submit(spellAt(BattleSide::ATTACKER, SpellID::FIREBALL, enemy)));
	local = battle()->getLocalSpellEffects(enemy->unitId());
	ASSERT_EQ(local.size(), 1);
	EXPECT_EQ(local.front().sid, second.sid);
}
