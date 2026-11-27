/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMaintenanceFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/battle/OrdinarySpecialtyDamage.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisRetainedSpecialties.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
struct SpecialtyPercentObserver
{
	double value = 0;
	bool floatingPoint = false;
	template<typename Value>
	void operator()(const char * name, const Value & input, const char *)
	{
		if(std::string_view(name) != "ordinaryCreatureSpecialtyDamagePercent") return;
		if constexpr(std::is_arithmetic_v<Value>)
		{
			value = static_cast<double>(input);
			floatingPoint = std::is_floating_point_v<Value>;
		}
	}
};
}

class VokialDamagePrecisionTest : public NewHorizonsMaintenanceFixture
{
protected:
	bool historical = false;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsMaintenanceFixture::mapLoaded(map);
		if(!historical) return;
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["revisedSpecialties"].Struct().erase("bloodScent");
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void setup(int casualties = 600)
	{
		ASSERT_NO_FATAL_FAILURE(prepare("core:vokial"));
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:vampireLord"), BattleHex(4, 5), 10);
		ASSERT_NE(actor, nullptr);
		injure(*victim, static_cast<int64_t>(victim->getMaxHealth()) * casualties);
	}
	int64_t raw400Damage(const BattleAttackInfo & attack)
	{
		DamageAttackInfo payload;
		payload.ordinaryCreatureSpecialtyDamagePercent =
			newHorizonsPassiveSpecialties::ordinarySpecialtyDamagePercent(*battle(), attack);
		nativeDamage::Facts facts;
		facts.attacker.count = 1;
		facts.attacker.minDamage = facts.attacker.maxDamage = 400;
		facts.defender.count = 1000;
		facts.defender.firstHealth = facts.defender.maxHealth = 10;
		return nativeDamage::calculate(payload, facts).damage.min;
	}
};

TEST_F(VokialDamagePrecisionTest, LevelTwoCapReachesNativeDamageAsExactlyFiftyTwoAndAHalfPercent)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 500);
	EXPECT_EQ(raw400Damage(attack), 600);
	attackerSideHero->levelUp();
	EXPECT_EQ(newHorizonsHeroes::bloodScentCapPermille(*attackerSideHero), 525);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 525);
	EXPECT_DOUBLE_EQ(newHorizonsPassiveSpecialties::ordinarySpecialtyDamagePercent(*battle(), attack), 152.5);
	EXPECT_EQ(raw400Damage(attack), 610);
	attackerSideHero->levelUp();
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 550);
	EXPECT_EQ(raw400Damage(attack), 620);
}

TEST_F(VokialDamagePrecisionTest, HistoricalCaptureKeepsIntegerRatioAndDoesNotAcquireNewCap)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(setup());
	attackerSideHero->levelUp();
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_FALSE(newHorizonsHeroes::bloodScentUsesPermille(*attackerSideHero));
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(attack), 60);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 600);
	EXPECT_DOUBLE_EQ(newHorizonsPassiveSpecialties::ordinarySpecialtyDamagePercent(*battle(), attack), 160);
	EXPECT_EQ(raw400Damage(attack), 640);
}

TEST_F(VokialDamagePrecisionTest, WoundedRatioAndExcludedAttacksRetainTheirOrdinaryRules)
{
	ASSERT_NO_FATAL_FAILURE(setup(250));
	attackerSideHero->levelUp();
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 250);
	EXPECT_EQ(raw400Damage(attack), 500);
	attack.shooting = true;
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 0);
	attack.shooting = false;
	attack.secondaryAttack = true;
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePermille(attack), 0);
}

TEST(VokialDamagePrecision, NativePayloadRetainsIntegerAndFractionalValuesWithoutTruncation)
{
	DamageAttackInfo payload;
	nativeDamage::Facts facts;
	facts.attacker.count = 1;
	facts.attacker.minDamage = facts.attacker.maxDamage = 400;
	facts.defender.count = 1000;
	facts.defender.firstHealth = facts.defender.maxHealth = 10;
	payload.ordinaryCreatureSpecialtyDamagePercent = 152;
	EXPECT_EQ(nativeDamage::calculate(payload, facts).damage.min, 608);
	payload.ordinaryCreatureSpecialtyDamagePercent = 152.5;
	EXPECT_EQ(nativeDamage::calculate(payload, facts).damage.min, 610);
}

TEST(VokialDamagePrecision, PermilleBonusRepresentationSurvivesBinaryRoundTrip)
{
	Bonus input(BonusDuration::PERMANENT, BonusType::CASUALTY_DAMAGE_PERCENTAGE,
		BonusSource::HERO_SPECIAL, 525, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype(2)));
	CMemorySerializer wire;
	input.serialize(wire.oser);
	Bonus output;
	output.serialize(wire.iser);
	EXPECT_EQ(output.type, BonusType::CASUALTY_DAMAGE_PERCENTAGE);
	EXPECT_EQ(output.subtype, BonusSubtypeID(BonusCustomSubtype(2)));
	EXPECT_EQ(output.val, 525);
}

TEST(VokialDamagePrecision, ScriptDescriptorCarriesWholeAndFractionalNumericValues)
{
	DamageAttackInfo payload;
	SpecialtyPercentObserver observer;
	payload.serializeScript(observer);
	EXPECT_DOUBLE_EQ(observer.value, 100);
	EXPECT_TRUE(observer.floatingPoint);
	payload.ordinaryCreatureSpecialtyDamagePercent = 152.5;
	payload.serializeScript(observer);
	EXPECT_DOUBLE_EQ(observer.value, 152.5);
}
