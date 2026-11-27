/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMaintenanceFixture.h"
#include "../../../lib/combatScripts/NativeHellfireBatteryDamage.h"
#include "../../../lib/combatScripts/NativeDamageFacts.h"
#include "../../../lib/combatScripts/NativeDamageFactors.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"

class NewHorizonsHellfireBatteryTest : public NewHorizonsMaintenanceFixture
{
protected:
	bool historical = false;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsMaintenanceFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			rules["revisedSpecialties"].Struct().erase("hellfireBattery");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	DamageAttackInfo shot() const
	{
		DamageAttackInfo result;
		result.attacker = actor;
		result.defender = victim;
		result.shooting = true;
		return result;
	}
	DamageEstimation flat(int damage, double physicalFactor = 1, int share = 200, int cap = 0, int boost = 0)
	{
		auto info = shot();
		info.machineBaseDamage = damage;
		info.executionerDamagePercent = boost;
		nativeDamage::Facts facts;
		facts.attacker.count = 1;
		facts.attacker.minDamage = facts.attacker.maxDamage = damage;
		facts.attacker.bonuses.set(static_cast<size_t>(BonusType::SIEGE_WEAPON));
		facts.defender.count = victim->getCount();
		facts.defender.firstHealth = victim->getFirstHPleft();
		facts.defender.maxHealth = victim->getMaxHealth();
		facts.defender.phantomIntegrity = victim->getPhantomIntegrity();
		facts.defender.values[static_cast<size_t>(BonusType::DAMAGE_RECEIVED_CAP)] = cap;
		const double common = nativeDamage::commonDamageFactor(nativeDamage::finalDamageFactors(info, facts),
			nativeDamage::foldDamageModifiers(info, facts, true));
		return nativeDamage::hellfireBattery::calculate(*battle(), info, facts, physicalFactor, 1, share, common);
	}
	void fireReduction(int percent)
	{
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, percent,
			BonusSourceID(), BonusSubtypeID(SpellSchool::FIRE)));
	}
};

TEST_F(NewHorizonsHellfireBatteryTest, NeutralTargetReceivesOneBudgetAtZeroHalfAndFullConversion)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	for(const int damage : {1, 3, 51, 100, 101})
		for(const int share : {0, 200, 500, 1000})
		{
			const auto result = flat(damage, 1, share);
			EXPECT_EQ(result.damage.min, damage);
			EXPECT_EQ(result.damage.max, damage);
		}
}
TEST_F(NewHorizonsHellfireBatteryTest, OddOutgoingBudgetIsRoundedBeforeItsFireAllocation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	EXPECT_EQ(flat(3, 1.5, 200, 0, 50).damage.min, 4);
	EXPECT_EQ(flat(3, 1.5, 500, 0, 50).damage.min, 4);
	fireReduction(100);
	EXPECT_EQ(flat(3, 1.5, 500, 0, 50).damage.min, 2);
}
TEST_F(NewHorizonsHellfireBatteryTest, PhysicalDefenseChangesOnlyRemainingPhysicalShare)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	EXPECT_EQ(flat(100, 0.25).damage.min, 40); // 80 physical at 25%, plus 20 Fire.
	EXPECT_EQ(flat(100, 2).damage.min, 180);
	EXPECT_EQ(flat(1, 0.25).damage.min, 1);
}
TEST_F(NewHorizonsHellfireBatteryTest, FireProtectionChangesOnlyConvertedShareAndCanStopFullConversion)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	fireReduction(50);
	EXPECT_EQ(flat(100).damage.min, 90);
	fireReduction(100);
	EXPECT_EQ(flat(100).damage.min, 80);
	EXPECT_EQ(flat(100, 1, 1000).damage.min, 0);
	EXPECT_EQ(flat(100, 1, 1000).kills.min, 0);
}
TEST_F(NewHorizonsHellfireBatteryTest, CombinedHitHasOneCapAndCombinedCasualtyEstimate)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	const auto ordinary = flat(100);
	EXPECT_EQ(ordinary.kills.min, 10);
	const auto capped = flat(100, 1, 200, 100);
	EXPECT_EQ(capped.damage.min, victim->getMaxHealth());
	EXPECT_EQ(capped.kills.min, 1);
}
TEST_F(NewHorizonsHellfireBatteryTest, PhantomUsesPhysicalQuarterAndFireDoubleOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId();
	info.count = 10;
	info.type = victim->creatureId();
	info.side = BattleSide::DEFENDER;
	info.position = BattleHex(10, 5);
	info.summoned = true;
	info.phantomIntegrity = 50;
	info.phantomDuration = newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS;
	BattleUnitsChanged pack;
	pack.battleID = BattleID(0);
	pack.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(pack.changedStacks.back().data);
	gameHandler->sendAndApply(pack);
	victim = battle()->getStack(info.id);
	ASSERT_NE(victim, nullptr);
	EXPECT_EQ(flat(100, 0.25).damage.min, 60);
	EXPECT_EQ(flat(100, 0.25).kills.min, 10);
	EXPECT_EQ(flat(50, 0.25).damage.min, 30);
	EXPECT_EQ(flat(50, 0.25).kills.min, 0);
}
TEST_F(NewHorizonsHellfireBatteryTest, MissingCaptureKeepsHistoricalBallistaSpecialtyAndHasNoFireComponent)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	EXPECT_EQ(nativeDamage::hellfireBattery::firePermille(*battle(), shot()), 0);
	EXPECT_FALSE(newHorizonsHeroes::revisedSpecialtyRetiresHistorical(*attackerSideHero));
}
TEST_F(NewHorizonsHellfireBatteryTest, CurrentCaptureReplacesGenericSpecialtyAndOnlyChangesBallistaShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	EXPECT_GT(nativeDamage::hellfireBattery::firePermille(*battle(), shot()), 0);
	EXPECT_TRUE(newHorizonsHeroes::revisedSpecialtyRetiresHistorical(*attackerSideHero));
	auto melee = shot(); melee.shooting = false;
	EXPECT_EQ(nativeDamage::hellfireBattery::firePermille(*battle(), melee), 0);
	auto magical = shot(); magical.physicalDamage = false;
	EXPECT_EQ(nativeDamage::hellfireBattery::firePermille(*battle(), magical), 0);
	auto ordinary = shot(); ordinary.attacker = upgraded;
	EXPECT_EQ(nativeDamage::hellfireBattery::firePermille(*battle(), ordinary), 0);
}
TEST_F(NewHorizonsHellfireBatteryTest, AuthoritativeShotMatchesSharedForecastWithOneAttackAndNoSpellCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pyre"));
	fireReduction(100);
	forceActorFirst();
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto expected = battle()->calculateDmgRange(BattleAttackInfo(actor, victim, 0, true));
	ASSERT_EQ(expected.damage.min, expected.damage.max);
	const auto health = victim->getAvailableHealth();
	const auto ammunition = actor->shots.available();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto attacks = server.attacks.size();
	const auto casts = server.casts.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, victim)));
	EXPECT_EQ(health - victim->getAvailableHealth(), expected.damage.min);
	EXPECT_EQ(actor->shots.available(), ammunition - 1);
	EXPECT_EQ(server.attacks.size(), attacks + 1);
	EXPECT_EQ(server.casts.size(), casts);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}
