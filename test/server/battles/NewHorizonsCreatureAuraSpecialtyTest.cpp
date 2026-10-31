/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/GameSettings.h"
#include <tuple>

namespace
{
using Aura = std::tuple<const char *, const char *, const char *, const char *, int>;
class NewHorizonsCreatureAuraSpecialtyTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<Aura>
{
protected:
	bool historical = false;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			for(const char * key : {"livingBastionDefense", "chillingPresenceReductionPercent", "boneFormationDefense", "ogreWarcallerDamagePercent"})
				rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase(key);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(std::get<0>(GetParam()))), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
	}
	int value(const CStack * unit) const
	{
		if(std::string(std::get<0>(GetParam())) == "core:vey")
			return unit->valOfBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST, BonusCustomSubtype::damageTypeMelee));
		return std::string(std::get<0>(GetParam())) == "core:charna"
			? unit->valOfBonuses(BonusType::GENERAL_ATTACK_REDUCTION) : unit->getDefense(false);
	}
	static void control(CStack * unit)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
			BonusSource::OTHER, 1, BonusSourceID()));
	}
};
TEST_P(NewHorizonsCreatureAuraSpecialtyTest, SameArmyAuraUsesLivingNeighborsAndDoesNotStackOrTransferWithControl)
{
	prepare();
	startBattle();
	const auto & [hero, recipient, sourceCreature, title, coefficient] = GetParam();
	const bool hostile = std::string(hero) == "core:charna";
	const auto recipientSide = hostile ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	auto * subject = addStack(recipientSide, creatureByName(recipient), BattleHex(leftHex), 10);
	ASSERT_NE(subject, nullptr);
	const int baseline = value(subject);
	const int sourceHex = leftHex + 1;
	auto * source = addStack(BattleSide::ATTACKER, creatureByName(sourceCreature), BattleHex(sourceHex), 1);
	EXPECT_EQ(value(subject) - baseline, coefficient);
	control(source);
	EXPECT_EQ(value(subject) - baseline, coefficient) << "Source control retains its native passive army affiliation";
	control(subject);
	EXPECT_EQ(value(subject) - baseline, coefficient) << "Recipient control retains its native passive army affiliation";
	auto * second = addStack(BattleSide::ATTACKER, creatureByName(sourceCreature), BattleHex(leftHex + 17), 1);
	EXPECT_EQ(value(subject) - baseline, coefficient);
	int64_t firstHealth = source->getAvailableHealth();
	source->health.damage(firstHealth);
	source->nodeHasChanged();
	EXPECT_EQ(value(subject) - baseline, coefficient);
	int64_t secondHealth = second->getAvailableHealth();
	second->health.damage(secondHealth);
	second->nodeHasChanged();
	EXPECT_EQ(value(subject), baseline);
	auto * otherArmy = addStack(BattleSide::DEFENDER, creatureByName(sourceCreature), BattleHex(sourceHex), 1);
	ASSERT_NE(otherArmy, nullptr);
	EXPECT_EQ(value(subject), baseline);
}
TEST_P(NewHorizonsCreatureAuraSpecialtyTest, HistoricalAndCurrentIdentityCapturesRemainSeparate)
{
	historical = true;
	prepare();
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	const auto id = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}
TEST_P(NewHorizonsCreatureAuraSpecialtyTest, CurrentIdentityAndStartingDevelopmentSurviveWorldRoundtrip)
{
	prepare();
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, std::get<3>(GetParam()));
	const auto id = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(loaded->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(loaded->getSpecialtyPresentation().name, std::get<3>(GetParam()));
}
INSTANTIATE_TEST_SUITE_P(ThreePassiveIdentities, NewHorizonsCreatureAuraSpecialtyTest, ::testing::Values(
	Aura{"core:ryland", "core:pikeman", "core:dendroidSoldier", "Living Bastion", 3},
	Aura{"core:charna", "core:pikeman", "core:wraith", "Chilling Presence", 10},
	Aura{"core:galthran", "core:skeletonWarrior", "core:walkingDead", "Bone Formation", 3},
	Aura{"core:vey", "core:pikeman", "core:ogreMage", "Ogre Warcaller", 10}));
}
