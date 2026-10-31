/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMeleeSpecialtyFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include <tuple>
namespace
{
class SorshaDamageTypeFixture : public NewHorizonsMeleeSpecialtyFixture
{
protected:
	int physicalPolicy = 0;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsMeleeSpecialtyFixture::mapLoaded(map);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(physicalPolicy == 1) magic.Struct().erase("physicalDamageReductionCapPercent");
		magic.setOverrideFlag(true); map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void fixedValue(CStack & unit, BonusType type, BonusSubtypeID subtype, int value)
	{
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
				BonusSource::OTHER, value, BonusSourceID(), subtype, bound));
	}
	void controlledSource(CStack & source)
	{
		fixedValue(source, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK), 0);
		fixedValue(*actor, BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE), 0);
		fixedValue(source, BonusType::STACKS_INITIATIVE_BASE, BonusSubtypeID(), 500);
		for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
			fixedValue(source, BonusType::CREATURE_DAMAGE, subtype, 2);
		for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
			source.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	}
	void unrelatedGeneralReduction()
	{
		auto other = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::HERO_SPECIAL, 10, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:sorsha"))),
			BonusCustomSubtype::damageTypeAll, BonusValueType::BASE_NUMBER);
		other->stacking = "test:unrelated-sorsha-general-protection";
		actor->addNewBonus(std::move(other));
	}
	void completeNativeSequence(int captureMode)
	{
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
		ASSERT_TRUE(actor->alive()); ASSERT_TRUE(victim->alive());
		EXPECT_EQ(observed.shieldPublications, captureMode == 0 ? 1 : 0);
		ASSERT_EQ(actor->getUnstackedBonuses(CSelector(newHorizonsShieldAndSword::isProtection))->size(), captureMode == 0 ? 1u : 0u);
	}
	void expectNativeHit(const CStack & source, const DamageEstimation & range, bool shot, bool magical, bool secondary)
	{
		const auto packet = std::ranges::find(observed.attacks, source.unitId(), &BattleAttack::stackAttacking);
		ASSERT_NE(packet, observed.attacks.end()); EXPECT_EQ(packet->shot(), shot); EXPECT_EQ(packet->spellLike(), magical);
		const auto hit = std::ranges::find(packet->bsa, actor->unitId(), &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, packet->bsa.end()); EXPECT_EQ(hit->isSecondary(), secondary);
		EXPECT_GE(hit->damageAmount, range.damage.min); EXPECT_LE(hit->damageAmount, range.damage.max);
	}
};
class SorshaShotDamageTypeTest : public SorshaDamageTypeFixture,
	public ::testing::WithParamInterface<std::tuple<int, int, int>> {};
class SorshaMeleeDamageTypeTest : public SorshaDamageTypeFixture,
	public ::testing::WithParamInterface<std::tuple<int, int>> {};
}
TEST_P(SorshaShotDamageTypeTest, CompletedSequenceGuardDoesNotProtectMagicalMainOrSplashButProtectsPhysicalShots)
{
	const auto [kind, policy, captureMode] = GetParam(); physicalPolicy = policy;
	prepare("core:sorsha", captureMode);
	const bool magical = kind < 4; const bool secondary = magical && kind % 2 != 0;
	const auto key = kind < 2 ? "core:magog" : kind < 4 ? "core:lich" : "core:archer";
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName(key), BattleHex(10, 5), 100);
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(3, 4), 100);
	ASSERT_NE(shooter, nullptr); ASSERT_NE(other, nullptr); controlledSource(*shooter);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_EQ(shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK), magical);
	BattleAttackInfo shot(shooter, actor, 0, true); shot.secondaryAttack = secondary;
	ASSERT_EQ(shot.physicalDamage, !magical);
	const auto plain = battle()->calculateDmgRange(shot);
	unrelatedGeneralReduction();
	const auto before = battle()->calculateDmgRange(shot); ASSERT_LT(before.damage.max, plain.damage.max);
	ASSERT_NO_FATAL_FAILURE(completeNativeSequence(captureMode));
	const auto after = battle()->calculateDmgRange(shot);
	if(magical || captureMode != 0)
	{
		EXPECT_EQ(after.damage.min, before.damage.min); EXPECT_EQ(after.damage.max, before.damage.max);
	}
	else { EXPECT_LT(after.damage.min, before.damage.min); EXPECT_LT(after.damage.max, before.damage.max); }
	const auto * aim = secondary ? other : actor;
	if(secondary)
	{
		const auto ability = shooter->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
		ASSERT_NE(ability, nullptr); ASSERT_TRUE(ability->subtype.hasValue());
		const auto * spell = ability->subtype.as<SpellID>().toSpell();
		spells::BattleCast event(battle(), shooter, spells::Mode::SPELL_LIKE_ATTACK, spell);
		event.setSpellLevel(ability->val);
		battle::Target targets; targets.emplace_back(aim, aim->getPosition());
		ASSERT_TRUE(vstd::contains(spell->battleMechanics(&event)->getAffectedStacks(targets), actor));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), shooter);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeShotAttack(shooter, aim)));
	ASSERT_NO_FATAL_FAILURE(expectNativeHit(*shooter, after, true, magical, secondary));
}
TEST_P(SorshaMeleeDamageTypeTest, CompletedSequenceGuardRetainsPhysicalMeleeAndOtherGeneralProtection)
{
	const auto [policy, captureMode] = GetParam(); physicalPolicy = policy; prepare("core:sorsha", captureMode);
	auto * source = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(2, 5), 100);
	ASSERT_NE(source, nullptr); controlledSource(*source); blockRetaliation(source);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const BattleAttackInfo hit(source, actor, 0, false); ASSERT_TRUE(hit.physicalDamage);
	const auto plain = battle()->calculateDmgRange(hit);
	unrelatedGeneralReduction();
	const auto before = battle()->calculateDmgRange(hit); ASSERT_LT(before.damage.max, plain.damage.max);
	ASSERT_NO_FATAL_FAILURE(completeNativeSequence(captureMode));
	const auto after = battle()->calculateDmgRange(hit);
	if(captureMode == 0) { EXPECT_LT(after.damage.min, before.damage.min); EXPECT_LT(after.damage.max, before.damage.max); }
	else { EXPECT_EQ(after.damage.min, before.damage.min); EXPECT_EQ(after.damage.max, before.damage.max); }
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(source, actor, source->getPosition())));
	ASSERT_NO_FATAL_FAILURE(expectNativeHit(*source, after, false, false, false));
}
INSTANTIATE_TEST_SUITE_P(MagogLichMainSplashArcherCurrentLegacyPdrAndCaptureModes, SorshaShotDamageTypeTest,
	::testing::Combine(::testing::Values(0, 1, 2, 3, 4), ::testing::Values(0, 1), ::testing::Values(0, 1, 2)));
INSTANTIATE_TEST_SUITE_P(CurrentLegacyPdrAndCaptureModes, SorshaMeleeDamageTypeTest,
	::testing::Combine(::testing::Values(0, 1), ::testing::Values(0, 1, 2)));
