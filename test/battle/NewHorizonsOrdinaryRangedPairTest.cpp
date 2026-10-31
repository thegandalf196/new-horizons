/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/battle/NewHorizonsRangefinder.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include <stdexcept>
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameSettings.h"
#include "../../client/windows/HeroSpecialtyPresentation.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/GameLibrary.h"
#if ENABLE_BATTLE_AI
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsOrdinaryRangedPairTest : public HeroCommandFixture, public ::testing::WithParamInterface<int>
{
protected:
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"cruelVolleyDamagePercent", "rangefinderFullDamageDistance"})
		{
			if(GetParam() == 1) policy.Struct().erase(key);
			if(GetParam() == 2) policy[key].Integer() = 0;
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero, bool bow = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:orc")), 10}, {CreatureID(CreatureID::decode("core:orcChieftain")), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		if(std::string_view(hero) == "core:christian")
			giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		if(bow)
			giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:goldenBow")), ArtifactPosition::MISC1);
		startBattle();
	}
	void expectCapturedSpecialtyArt(const char * selected, const char * historical, CreatureID creature)
	{
		const auto rules = attackerSideHero->getPrimaryGrowthRules();
		const auto bonuses = attackerSideHero->getExportedBonusList().toJsonNode();
		const auto metadata = attackerSideHero->getSpecialtyPresentation();
		const std::string artwork = "newHorizons/heroSpecialties/"
			+ std::string(GetParam() == 1 ? historical : selected);
		ASSERT_EQ(metadata.artwork, artwork);
		EXPECT_EQ(metadata.creature, creature);
		const auto view = heroSpecialtyPresentation(*attackerSideHero);
		EXPECT_EQ(view.animation().getOriginalName(), artwork + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), artwork + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
		EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), rules);
		EXPECT_EQ(attackerSideHero->getExportedBonusList().toJsonNode(), bonuses);
	}
	CStack * machine()
	{
		for(const auto & unit : battle()->stacks)
			if(unit->isBallista() && unit->unitSide() == BattleSide::ATTACKER)
				return unit.get();
		return nullptr;
	}
	CStack * unit(BattleSide side, int slot)
	{
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == side && stack->unitSlot() == SlotID(slot))
				return stack.get();
		return nullptr;
	}
	static void limited(CStack & stack, int maximum, int fullDamage)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, maximum, BonusSourceID());
		bonus->parameters = std::make_shared<BonusParameters>(fullDamage);
		stack.addNewBonus(bonus);
	}
};
#if ENABLE_BATTLE_AI
class RangedPairEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RangedPairEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianActualCaptureAndStartupRetainMasterGunnerAndFixedSiegeOutput)
{
	prepare("core:christian");
	expectCapturedSpecialtyArt("ChristianRangefinder", "ChristianBallistaLine", CreatureID(CreatureID::BALLISTA));
	ASSERT_NE(machine(), nullptr);
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero), GetParam() != 1);
	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
	EXPECT_EQ(machine()->getShootingRangeDistance(), 255);
	EXPECT_EQ(machine()->getRangedFullDamageDistance(), GetParam() == 0 ? 12 : 10);
	if(GetParam() != 1)
		EXPECT_NE(attackerSideHero->getSpecialtyPresentation().description.find(GetParam() == 0
			? "12 hexes" : "10 hexes"), std::string::npos);
	const auto envelope = machine()->getBonus(CSelector([](const Bonus * bonus)
		{ return bonus && newHorizonsRangefinder::isEnvelope(*bonus); }));
	EXPECT_EQ(static_cast<bool>(envelope), GetParam() != 1);
	if(envelope)
	{
		EXPECT_EQ(envelope->val, 255);
		EXPECT_EQ(envelope->parameters->toNumber(), GetParam() == 0 ? 12 : 10);
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ReservedEnvelopeRequiresTheExactProducedFamilyLimiterAndEffectRange)
{
	prepare("core:christian");
	const auto original = newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*attackerSideHero);
	if(GetParam() == 1) { EXPECT_FALSE(original); return; }
	ASSERT_TRUE(original);
	EXPECT_TRUE(newHorizonsRangefinder::isEnvelope(*original));
	for(int mutation = 0; mutation < 5; ++mutation)
	{
		auto malformed = *original;
		if(mutation == 0)
			malformed.limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:orc")).toCreature(), true);
		if(mutation == 1)
			malformed.limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:ballista")).toCreature(), false);
		if(mutation == 2)
			malformed.limiter = std::make_shared<CreatureLevelLimiter>(0);
		if(mutation == 3)
			malformed.limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{original->limiter});
		if(mutation == 4)
			malformed.effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		EXPECT_THROW(newHorizonsRangefinder::isEnvelope(malformed), std::runtime_error) << mutation;
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, MalformedVisibleEnvelopeCannotDisappearFromActualLegalRangeQueries)
{
	prepare("core:christian");
	const auto original = newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*attackerSideHero);
	if(GetParam() == 1) { EXPECT_FALSE(original); return; }
	ASSERT_TRUE(original);
	// This opposing Ballista has no valid Christian marker to mask the malformed one.
	auto * isolated = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:ballista")), BattleHex(14, 7), 1);
	ASSERT_NE(isolated, nullptr);
	limited(*isolated, 11, 8);
	EXPECT_EQ(isolated->getShootingRangeDistance(), 11);
	auto malformed = std::make_shared<Bonus>(*original);
	malformed->limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:ballista")).toCreature(), false);
	isolated->addNewBonus(malformed);
	EXPECT_THROW(newHorizonsRangefinder::legalRangeBonus(*isolated), std::runtime_error);
	EXPECT_THROW(isolated->getShootingRangeDistance(), std::runtime_error);
	EXPECT_THROW(newHorizonsRangefinder::fullDamageDistance(*isolated), std::runtime_error);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, SingleDistanceQuerySafelyReturnsAbsenceOrCapturedPenalty)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	const BattleHex from(1, 7), empty(13, 7);
	const auto result = newHorizonsRangefinder::distancePenalty(*source, from, empty, *battle());
	if(GetParam() == 1)
	{
		EXPECT_FALSE(result.has_value());
		EXPECT_TRUE(battle()->battleHasDistancePenalty(source, from, empty));
	}
	else
	{
		ASSERT_TRUE(result.has_value());
		EXPECT_EQ(*result, GetParam() != 0);
		EXPECT_EQ(battle()->battleHasDistancePenalty(source, from, empty), *result);
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianActualDistanceCallbackAndDamageAgreeAtTenElevenTwelveThirteen)
{
	prepare("core:christian");
	auto * source = machine();
	auto * target = unit(BattleSide::DEFENDER, 0);
	ASSERT_NE(source, nullptr); ASSERT_NE(target, nullptr);
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
	source->setPosition(BattleHex(1, 5));
	target->setPosition(BattleHex(11, 5));
	const auto full = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, true));
	const int initiative = source->getInitiative();
	const int shots = source->shots.available();
	for(int distance : {10, 11, 12, 13})
	{
		target->setPosition(BattleHex(1 + distance, 5));
		const bool penalized = distance > (GetParam() == 0 ? 12 : 10);
		EXPECT_EQ(battle()->battleHasDistancePenalty(source, source->getPosition(), target->getPosition()), penalized);
		const auto actual = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, true));
		// The reflection diagnostic excludes negative lowering factors, including distance.
		EXPECT_EQ(actual.damageBeforeDefense.min, full.damageBeforeDefense.min);
		EXPECT_EQ(actual.damageBeforeDefense.max, full.damageBeforeDefense.max);
		// Ordinary long range multiplies the final factor by 0.5 after additive bonuses.
		// With this uncapped, nonadjacent, no-wall shot, floor(x/2) == floor(floor(x)/2).
		EXPECT_EQ(actual.damage.min, penalized ? std::max<int64_t>(1, full.damage.min / 2) : full.damage.min);
		EXPECT_EQ(actual.damage.max, penalized ? std::max<int64_t>(1, full.damage.max / 2) : full.damage.max);
		const BattleHex empty(1 + distance, 7);
		EXPECT_EQ(battle()->battleHasDistancePenalty(source, BattleHex(1, 7), empty), penalized);
		EXPECT_EQ(source->getInitiative(), initiative);
		EXPECT_EQ(source->shots.available(), shots);
		EXPECT_FALSE(battle()->battleHasWallPenalty(source, source->getPosition(), target->getPosition()));
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianLegalMaximumAndEquipmentWaiverRemainIndependent)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	limited(*source, 11, 8);
	EXPECT_EQ(source->getShootingRangeDistance(), 11);
	EXPECT_EQ(source->getRangedFullDamageDistance(), GetParam() == 0 ? 12 : GetParam() == 2 ? 10 : 8);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_DISTANCE_PENALTY, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(source->getShootingRangeDistance(), 11);
	EXPECT_FALSE(battle()->battleHasDistancePenalty(source, BattleHex(1, 5), BattleHex(15, 5)));
	EXPECT_EQ(source->getRangedFullDamageDistance(), GetParam() == 1 ? 8 : 255);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ActualGoldenBowStillWaivesDistanceAndWalls)
{
	prepare("core:christian", true);
	auto * source = machine(); ASSERT_NE(source, nullptr);
	EXPECT_TRUE(source->hasBonusOfType(BonusType::NO_DISTANCE_PENALTY));
	EXPECT_TRUE(source->hasBonusOfType(BonusType::NO_WALL_PENALTY));
	EXPECT_FALSE(battle()->battleHasDistancePenalty(source, BattleHex(1, 5), BattleHex(15, 5)));
	EXPECT_EQ(source->getRangedFullDamageDistance(), 255);
	EXPECT_EQ(source->getShootingRangeDistance(), 255);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianDoublewideNearestFootprintDoesNotGrantOtherShootersTheEnvelope)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	source->setPosition(BattleHex(1, 7));
	auto * wide = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:centaur")), BattleHex(13, 7), 3);
	ASSERT_NE(wide, nullptr); ASSERT_EQ(wide->getHexes().size(), 2u);
	for(const auto hex : wide->getHexes())
		EXPECT_EQ(battle()->battleHasDistancePenalty(source, source->getPosition(), hex), GetParam() != 0);
	auto * unrelated = unit(BattleSide::ATTACKER, 0); ASSERT_NE(unrelated, nullptr);
	EXPECT_EQ(unrelated->getRangedFullDamageDistance(), 10);
	EXPECT_FALSE(newHorizonsRangefinder::fullDamageDistance(*unrelated));
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasActualDamageUsesVictimDeficitAndRestorationForBothOrcForms)
{
	prepare("core:jabarkas");
	expectCapturedSpecialtyArt("JabarkasCruelVolley", "JabarkasOrcLine", creatureByName("core:orc"));
	auto * target = unit(BattleSide::DEFENDER, 0); ASSERT_NE(target, nullptr);
	target->setPosition(BattleHex(8, 5));
	for(int slot : {0, 1})
	{
		auto * source = unit(BattleSide::ATTACKER, slot); ASSERT_NE(source, nullptr);
		source->setPosition(BattleHex(1, 5));
		BattleAttackInfo attack(source, target, 0, true);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		int64_t damage = target->getMaxHealth(); target->damage(damage);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), GetParam() == 0 ? 120 : 100);
		const auto boosted = battle()->calculateDmgRange(attack);
		auto magical = attack; magical.physicalDamage = false;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), magical), 100);
		auto collateral = attack; collateral.secondaryAttack = true;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), collateral), GetParam() == 0 ? 120 : 100);
		int64_t healing = target->getTotalHealth();
		target->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
		EXPECT_EQ(target->getCount(), target->unitBaseAmount());
		EXPECT_GT(target->getKilled(), 0);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		const auto ordinary = battle()->calculateDmgRange(attack);
		if(GetParam() == 0)
		{
			EXPECT_GE(boosted.damageBeforeDefense.min, ordinary.damageBeforeDefense.min * 120 / 100);
			EXPECT_LE(boosted.damageBeforeDefense.min, ordinary.damageBeforeDefense.min * 120 / 100 + 1);
		}
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasActualServerShotUsesTheSameAcceptedDamageRange)
{
	prepare("core:jabarkas");
	auto * source = unit(BattleSide::ATTACKER, 0); ASSERT_NE(source, nullptr);
	auto * target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(8, 7), 1000);
	ASSERT_NE(target, nullptr);
	source->setPosition(BattleHex(1, 7));
	int64_t casualty = target->getMaxHealth(); target->damage(casualty);
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	ASSERT_TRUE(battle()->battleCanShoot(source, target->getPosition()));
	const auto expected = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, true));
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(source, target)));
	const auto actual = health - target->getAvailableHealth();
	EXPECT_GE(actual, expected.damage.min);
	EXPECT_LE(actual, expected.damage.max);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, CapturedReinitializationIsIdempotentAndWorldRoundtripRetainsIdentity)
{
	prepare("core:christian");
	const auto policy = attackerSideHero->getPrimaryGrowthRules();
	const auto bonuses = attackerSideHero->getExportedBonusList().toJsonNode();
	attackerSideHero->initHero(*gameHandler->randomizer);
	EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), policy);
	EXPECT_EQ(attackerSideHero->getExportedBonusList().toJsonNode(), bonuses);
	CGameState restored; restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id); ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), policy);
	EXPECT_EQ(loaded->getExportedBonusList().toJsonNode(), bonuses);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianPermanentAffiliationSurvivesAllegianceSwitch)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwner(source), PlayerColor(1));
	EXPECT_EQ(source->getRangedFullDamageDistance(), GetParam() == 0 ? 12 : 10);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasControlSwitchDoesNotGraftOriginalHeroPremiumToNewOwner)
{
	prepare("core:jabarkas");
	auto * source = unit(BattleSide::ATTACKER, 0); ASSERT_NE(source, nullptr);
	auto * target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(8, 7), 10);
	ASSERT_NE(target, nullptr);
	int64_t damage = target->getMaxHealth(); target->damage(damage);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwnerHero(source), defenderSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), BattleAttackInfo(source, target, 0, true)), 100);
}
#if ENABLE_BATTLE_AI
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasHypotheticalCacheTracksVictimLossAndFullTemporaryRestoration)
{
	prepare("core:jabarkas"); beginCombat();
	RangedPairEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto source = model->getForUpdate(unit(BattleSide::ATTACKER, 0)->unitId());
	auto target = model->getForUpdate(unit(BattleSide::DEFENDER, 0)->unitId());
	source->setPosition(BattleHex(1, 5)); target->setPosition(BattleHex(8, 5));
	DamageCache cache;
	for(int stage = 0; stage < 3; ++stage)
	{
		if(stage == 1) { int64_t damage = target->getMaxHealth(); target->damage(damage); }
		if(stage == 2) { int64_t healing = target->getTotalHealth(); target->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE); }
		const BattleAttackInfo attack(source.get(), target.get(), 0, true);
		DamageCache historicalFresh;
		auto & currentCache = GetParam() == 0 ? cache : historicalFresh;
		EXPECT_EQ(currentCache.getDamage(source.get(), target.get(), model), model->battleExpectedLuckDamage(attack));
	}
	EXPECT_EQ(unit(BattleSide::DEFENDER, 0)->getCount(), 10);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianHypotheticalCacheAndReadoutTrackDistanceAcrossBoundary)
{
	prepare("core:christian"); beginCombat();
	RangedPairEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto source = model->getForUpdate(machine()->unitId());
	auto target = model->getForUpdate(unit(BattleSide::DEFENDER, 0)->unitId());
	source->setPosition(BattleHex(1, 5));
	DamageCache cache;
	for(int distance : {10, 11, 12, 13, 10})
	{
		target->setPosition(BattleHex(1 + distance, 5));
		EXPECT_EQ(source->getRangedFullDamageDistance(), GetParam() == 0 ? 12 : 10);
		const BattleAttackInfo attack(source.get(), target.get(), 0, true);
		DamageCache historicalFresh;
		auto & currentCache = GetParam() == 0 ? cache : historicalFresh;
		EXPECT_EQ(currentCache.getDamage(source.get(), target.get(), model), model->battleExpectedLuckDamage(attack));
	}
}
#endif
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsOrdinaryRangedPairTest, ::testing::Values(0, 1, 2));
