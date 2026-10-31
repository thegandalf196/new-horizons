/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsReactiveDefenseFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/entities/hero/CHero.h"
#include <tuple>
namespace
{
namespace reactive = newHorizonsReactiveDefense;
class SolidarityDamageTypeFixture : public NewHorizonsReactiveDefenseFixture
{
protected:
	void fixedValue(CStack & unit, BonusType type, BonusSubtypeID subtype, int value)
	{
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
				BonusSource::OTHER, value, BonusSourceID(), subtype, bound));
	}
	void controlledDamage(CStack & source, CStack & target)
	{
		fixedValue(source, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK), 0);
		fixedValue(target, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE), 0);
		for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
			fixedValue(source, BonusType::CREATURE_DAMAGE, subtype, 2);
		for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
			source.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	}
	void unrelatedGeneralReduction(CStack & target)
	{
		// Same source category/hero ID, deliberately different stacking key.
		auto other = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::HERO_SPECIAL, 10, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:marius"))),
			BonusCustomSubtype::damageTypeAll, BonusValueType::BASE_NUMBER);
		other->stacking = "test:unrelated-marius-general-protection";
		target.addNewBonus(std::move(other));
	}
	size_t guards(const CStack & target) const
	{
		return target.getUnstackedBonuses(CSelector(reactive::isSolidarityProtection))->size();
	}
	void checkNativeHit(const BattleAttack & packet, const CStack & target, const DamageEstimation & expected, bool secondary)
	{
		const auto hit = std::ranges::find(packet.bsa, target.unitId(), &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, packet.bsa.end());
		EXPECT_EQ(hit->isSecondary(), secondary);
		EXPECT_GE(hit->damageAmount, expected.damage.min);
		EXPECT_LE(hit->damageAmount, expected.damage.max);
	}
};
class SolidarityShotDamageTypeTest : public SolidarityDamageTypeFixture,
	public ::testing::WithParamInterface<std::tuple<const char *, int, int>> {};
class SolidarityMeleeDamageTypeTest : public SolidarityDamageTypeFixture,
	public ::testing::WithParamInterface<std::tuple<int, int>> {};
}
TEST_P(SolidarityShotDamageTypeTest, PublishedGuardLeavesMagicalMainAndSplashUnchangedButProtectsPhysicalShots)
{
	const auto [creature, policy, mode] = GetParam();
	prepare(true, mode, false, policy);
	enemy->setPosition(BattleHex(12, 2));
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName(creature), BattleHex(10, 5), 100);
	auto * splash = addStack(BattleSide::ATTACKER, creatureByName("core:demon"), BattleHex(3, 4), 100);
	ASSERT_NE(shooter, nullptr); ASSERT_NE(splash, nullptr);
	controlledDamage(*shooter, *subject); controlledDamage(*shooter, *splash);
	fixedValue(*shooter, BonusType::STACKS_INITIATIVE_BASE, BonusSubtypeID(), 2000);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	ASSERT_TRUE(shooter->isShooter());
	const bool magical = shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK);
	const BattleAttackInfo mainInfo(shooter, subject, 0, true);
	ASSERT_EQ(mainInfo.physicalDamage, !magical);
	if(magical)
	{
		const auto ability = shooter->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
		ASSERT_NE(ability, nullptr); ASSERT_TRUE(ability->subtype.hasValue());
		const auto * spell = ability->subtype.as<SpellID>().toSpell();
		spells::BattleCast event(battle(), shooter, spells::Mode::SPELL_LIKE_ATTACK, spell);
		event.setSpellLevel(ability->val);
		battle::Target targets; targets.emplace_back(subject, subject->getPosition());
		ASSERT_TRUE(vstd::contains(spell->battleMechanics(&event)->getAffectedStacks(targets), splash));
	}
	const auto plain = battle()->calculateDmgRange(mainInfo);
	unrelatedGeneralReduction(*subject); unrelatedGeneralReduction(*splash);
	const auto before = battle()->calculateDmgRange(mainInfo);
	ASSERT_LT(before.damage.max, plain.damage.max) << "Unrelated same-source protection keeps its historical behavior";
	BattleAttackInfo splashInfo(shooter, splash, 0, true); splashInfo.secondaryAttack = true;
	const auto splashBefore = battle()->calculateDmgRange(splashInfo);
	injure({{ally, ally->getAvailableHealth()}});
	ASSERT_FALSE(ally->alive());
	ASSERT_EQ(guards(*subject), mode == 0 ? 1u : 0u);
	ASSERT_EQ(guards(*splash), mode == 0 ? 1u : 0u);
	const auto after = battle()->calculateDmgRange(mainInfo);
	const auto splashAfter = battle()->calculateDmgRange(splashInfo);
	if(magical || mode != 0)
	{
		EXPECT_EQ(after.damage.min, before.damage.min); EXPECT_EQ(after.damage.max, before.damage.max);
		EXPECT_EQ(splashAfter.damage.min, splashBefore.damage.min); EXPECT_EQ(splashAfter.damage.max, splashBefore.damage.max);
	}
	else
	{
		EXPECT_LT(after.damage.min, before.damage.min); EXPECT_LT(after.damage.max, before.damage.max);
	}
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeShotAttack(shooter, subject)));
	const auto packet = std::ranges::find(server.attacks, shooter->unitId(), &BattleAttack::stackAttacking);
	ASSERT_NE(packet, server.attacks.end());
	ASSERT_TRUE(packet->shot()); EXPECT_EQ(packet->spellLike(), magical);
	ASSERT_NO_FATAL_FAILURE(checkNativeHit(*packet, *subject, after, false));
	if(magical) ASSERT_NO_FATAL_FAILURE(checkNativeHit(*packet, *splash, splashAfter, true));
}
TEST_P(SolidarityMeleeDamageTypeTest, PublishedGuardRetainsPhysicalMeleeProtectionAndOtherContributors)
{
	const auto [policy, mode] = GetParam(); prepare(true, mode, false, policy);
	controlledDamage(*enemy, *subject);
	fixedValue(*enemy, BonusType::STACKS_INITIATIVE_BASE, BonusSubtypeID(), 2000);
	blockRetaliation(enemy);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), enemy);
	const BattleAttackInfo hit(enemy, subject, 0, false); ASSERT_TRUE(hit.physicalDamage);
	const auto plain = battle()->calculateDmgRange(hit);
	unrelatedGeneralReduction(*subject);
	const auto before = battle()->calculateDmgRange(hit); ASSERT_LT(before.damage.max, plain.damage.max);
	injure({{ally, ally->getAvailableHealth()}}); ASSERT_FALSE(ally->alive());
	ASSERT_EQ(guards(*subject), mode == 0 ? 1u : 0u);
	const auto after = battle()->calculateDmgRange(hit);
	if(mode == 0) { EXPECT_LT(after.damage.min, before.damage.min); EXPECT_LT(after.damage.max, before.damage.max); }
	else { EXPECT_EQ(after.damage.min, before.damage.min); EXPECT_EQ(after.damage.max, before.damage.max); }
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(enemy, subject, enemy->getPosition())));
	const auto packet = std::ranges::find(server.attacks, enemy->unitId(), &BattleAttack::stackAttacking);
	ASSERT_NE(packet, server.attacks.end()); EXPECT_FALSE(packet->shot()); EXPECT_FALSE(packet->spellLike());
	ASSERT_NO_FATAL_FAILURE(checkNativeHit(*packet, *subject, after, false));
}
INSTANTIATE_TEST_SUITE_P(ActualShooterCurrentAndLegacyPdrCurrentHistoricalZero, SolidarityShotDamageTypeTest,
	::testing::Combine(::testing::Values("core:magog", "core:lich", "core:archer"), ::testing::Values(0, 1), ::testing::Values(0, 1, 2)));
INSTANTIATE_TEST_SUITE_P(CurrentAndLegacyPdrCurrentHistoricalZero, SolidarityMeleeDamageTypeTest,
	::testing::Combine(::testing::Values(0, 1), ::testing::Values(0, 1, 2)));
