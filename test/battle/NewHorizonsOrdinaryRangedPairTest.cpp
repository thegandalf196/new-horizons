/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/battle/NewHorizonsRangefinder.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/serializer/CMemorySerializer.h"
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
		for(const char * key : {"furyOfTheHordeAttack", "furyOfTheHordeDefense", "furyOfTheHordeDamage",
			"orcRageMaximumDamagePercent", "pointBlankShotDamageBonusPercent"})
		{
			if(GetParam() == 1) policy.Struct().erase(key);
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
	static std::shared_ptr<Bonus> historicalEnvelope()
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE,
			BonusSource::HERO_SPECIAL, 255, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:christian"))),
			BonusSubtypeID(), BonusValueType::BASE_NUMBER);
		bonus->stacking = "new-horizons:ordinary-creature-specialty:core:christian";
		bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(CreatureID::decode("core:ballista")).toCreature(), true);
		bonus->parameters = std::make_shared<BonusParameters>(12);
		return bonus;
	}
	void damage(CStack & unit, int64_t amount)
	{
		auto state = unit.acquireState(); const auto before = state->getAvailableHealth();
		state->damage(amount); publish(unit.unitId(), *state, state->getAvailableHealth() - before);
	}
	void restore(CStack & unit)
	{
		auto state = unit.acquireState(); const auto before = state->getAvailableHealth();
		int64_t amount = state->getTotalHealth(); state->heal(amount, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
		publish(unit.unitId(), *state, state->getAvailableHealth() - before);
	}
	void publish(uint32_t id, battle::CUnitState & state, int64_t delta)
	{
		BattleUnitsChanged changed; changed.battleID = BattleID(0);
		UnitChanges update(id, UnitChanges::EOperation::UPDATE); update.data = state.save(); update.healthDelta = delta;
		changed.changedStacks.push_back(std::move(update)); gameHandler->sendAndApply(changed);
	}
	static void limited(CStack & stack, int maximum, int fullDamage)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, maximum, BonusSourceID());
		bonus->parameters = std::make_shared<BonusParameters>(fullDamage);
		stack.addNewBonus(bonus);
	}
};
class RangedDamagePayloadProbe final : public CPlayerBattleCallback
{
public:
	using CPlayerBattleCallback::CPlayerBattleCallback;
	using CBattleInfoCallback::finalizeDamagePayload;
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
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero), GetParam() == 0);
	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	const auto siege = attackerSideHero->getSiegeCapabilities(); ASSERT_TRUE(siege);
	EXPECT_EQ(siege->ballistaDamage, 50 + 2 * siege->siegeRating);
	EXPECT_EQ(machine()->getShootingRangeDistance(), 255);
	EXPECT_EQ(machine()->getRangedFullDamageDistance(), 10);
	EXPECT_FALSE(newHorizonsRangefinder::fullDamageDistance(*machine()));
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*attackerSideHero));
	EXPECT_EQ(newHorizonsAttackControllers::ballistaPhysicalDamagePercent(*battle(), *machine()), GetParam() == 0 ? 200 : 100);
	if(GetParam() == 0) EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, "Point-Blank Shot");
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, IndependentLegacyEnvelopeRetainsExactFamilyLimiterAndEffectRange)
{
	prepare("core:christian");
	const auto original = historicalEnvelope();
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
	const auto original = historicalEnvelope();
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
TEST_P(NewHorizonsOrdinaryRangedPairTest, CurrentChristianHasNoLegacyEnvelopeAndKeepsOrdinaryDistanceQuery)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	const BattleHex from(1, 7), empty(13, 7);
	EXPECT_FALSE(newHorizonsRangefinder::distancePenalty(*source, from, empty, *battle()));
	EXPECT_TRUE(battle()->battleHasDistancePenalty(source, from, empty));
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianMultiplierEntersTheActualPhysicalPrimaryDamagePayloadOnly)
{
	prepare("core:christian");
	auto * source = machine(); auto * target = unit(BattleSide::DEFENDER, 0);
	ASSERT_NE(source, nullptr); ASSERT_NE(target, nullptr);
	RangedDamagePayloadProbe probe(battle(), PlayerColor(0));
	BattleAttackInfo hit(source, target, 0, true);
	DamageAttackInfo primary; probe.finalizeDamagePayload(hit, primary, battle());
	EXPECT_EQ(primary.ordinaryCreatureSpecialtyDamagePercent, GetParam() == 0 ? 200 : 100);
	hit.secondaryAttack = true;
	DamageAttackInfo collateral; probe.finalizeDamagePayload(hit, collateral, battle());
	EXPECT_EQ(collateral.ordinaryCreatureSpecialtyDamagePercent, 100);
	hit.secondaryAttack = false; hit.physicalDamage = false;
	DamageAttackInfo magical; probe.finalizeDamagePayload(hit, magical, battle());
	EXPECT_EQ(magical.ordinaryCreatureSpecialtyDamagePercent, 100);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, IndependentLegacyEnvelopeLeafKeepsItsCodecBoundaryWithoutACurrentProducer)
{
	prepare("core:christian");
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureSpecialtyBonus(*attackerSideHero));
	auto bonus = historicalEnvelope();
	CMemorySerializer unsupported; unsupported.oser.version = static_cast<ESerializationVersion>(1147);
	EXPECT_THROW(unsupported.oser & *bonus, std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty());
	CMemorySerializer historical; historical.oser.version = static_cast<ESerializationVersion>(1148);
	ASSERT_NO_THROW(historical.oser & *bonus);
	historical.iser.version = static_cast<ESerializationVersion>(1148);
	Bonus restored; ASSERT_NO_THROW(historical.iser & restored);
	EXPECT_EQ(restored.toJsonNode(), bonus->toJsonNode());
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
		const bool penalized = distance > 10;
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
	EXPECT_EQ(source->getRangedFullDamageDistance(), 8);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_DISTANCE_PENALTY, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(source->getShootingRangeDistance(), 11);
	EXPECT_FALSE(battle()->battleHasDistancePenalty(source, BattleHex(1, 5), BattleHex(15, 5)));
	EXPECT_EQ(source->getRangedFullDamageDistance(), 8);
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
TEST_P(NewHorizonsOrdinaryRangedPairTest, OrdinaryDoublewideDistanceUsesNearestFootprintWithoutAChristianEnvelope)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr); source->setPosition(BattleHex(1, 7));
	auto * wide = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:centaur")), BattleHex(13, 7), 3);
	ASSERT_NE(wide, nullptr); ASSERT_EQ(wide->getHexes().size(), 2u);
	for(const int tile : {10, 12, 13})
	{
		wide->setPosition(BattleHex(tile, 7));
		int nearest = GameConstants::BFIELD_SIZE;
		for(const auto hex : wide->getHexes()) nearest = std::min<int>(nearest, BattleHex::getDistance(source->getPosition(), hex));
		for(const auto hex : wide->getHexes())
			EXPECT_EQ(battle()->battleHasDistancePenalty(source, source->getPosition(), hex), nearest > 10);
	}
	auto * unrelated = unit(BattleSide::ATTACKER, 0); ASSERT_NE(unrelated, nullptr);
	EXPECT_EQ(unrelated->getRangedFullDamageDistance(), 10);
	EXPECT_FALSE(newHorizonsRangefinder::fullDamageDistance(*unrelated));
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasBothOrcFormsAreMeleeWithFixedStatsAndOwnCasualtyRage)
{
	prepare("core:jabarkas");
	expectCapturedSpecialtyArt("JabarkasCruelVolley", "JabarkasOrcLine", creatureByName("core:orc"));
	auto * target = unit(BattleSide::DEFENDER, 0); ASSERT_NE(target, nullptr);
	for(int slot : {0, 1})
	{
		auto * source = unit(BattleSide::ATTACKER, slot); ASSERT_NE(source, nullptr);
		EXPECT_EQ(source->isShooter(), GetParam() == 1);
		EXPECT_EQ(source->valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK)
			.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), GetParam() == 0 ? 2 : 0);
		EXPECT_EQ(source->valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, PrimarySkill::DEFENSE)
			.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), GetParam() == 0 ? 2 : 0);
		EXPECT_EQ(source->valOfBonuses(Selector::typeSubtype(BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageBoth)
			.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), GetParam() == 0 ? 1 : 0);
		BattleAttackInfo melee(source, target, 0, false);
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(melee), 0);
		damage(*target, target->getMaxHealth());
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(melee), 0);
		damage(*source, source->getMaxHealth());
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(melee), GetParam() == 0 ? 10 : 0);
		melee.retaliation = true;
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(melee), GetParam() == 0 ? 10 : 0);
		restore(*source);
		EXPECT_EQ(source->getCount(), source->unitBaseAmount());
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(melee), 0);
	}
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasActualMeleeUsesAcceptedDamageAndCurrentConversionRejectsShoot)
{
	prepare("core:jabarkas");
	auto * source = unit(BattleSide::ATTACKER, 0); ASSERT_NE(source, nullptr);
	auto * target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(8, 7), 1000);
	ASSERT_NE(target, nullptr); source->setPosition(BattleHex(7, 7));
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
	for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
		source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
	blockRetaliation(source); damage(*source, source->getMaxHealth());
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), source);
	if(GetParam() == 0)
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(source, target)));
	const auto expected = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, false));
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeMeleeAttack(source, target, source->getPosition())));
	const auto actual = health - target->getAvailableHealth();
	EXPECT_GE(actual, expected.damage.min); EXPECT_LE(actual, expected.damage.max);
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
TEST_P(NewHorizonsOrdinaryRangedPairTest, ChristianMultiplierUsesCurrentVisibleOwnerWithoutRetiredPermanentRange)
{
	prepare("core:christian");
	auto * source = machine(); ASSERT_NE(source, nullptr);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwnerHero(source), defenderSideHero);
	EXPECT_EQ(source->getRangedFullDamageDistance(), 10);
	EXPECT_EQ(newHorizonsAttackControllers::ballistaPhysicalDamagePercent(*battle(), *source), 100);
}
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasPreparedNativeConversionSurvivesControlWithoutGraftingToForeignOrcs)
{
	prepare("core:jabarkas");
	auto * source = unit(BattleSide::ATTACKER, 0); ASSERT_NE(source, nullptr);
	auto * target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(8, 7), 10);
	ASSERT_NE(target, nullptr);
	damage(*source, source->getMaxHealth());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwnerHero(source), defenderSideHero);
	EXPECT_EQ(source->isShooter(), GetParam() == 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), BattleAttackInfo(source, target, 0, false)), GetParam() == 0 ? 110 : 100);
	const auto * foreign = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:orc")), BattleHex(14, 7), 10);
	ASSERT_NE(foreign, nullptr); EXPECT_TRUE(foreign->isShooter());
	EXPECT_FALSE(foreign->hasBonusOfType(BonusType::CASUALTY_DAMAGE_PERCENTAGE));
}

#if ENABLE_BATTLE_AI
TEST_P(NewHorizonsOrdinaryRangedPairTest, JabarkasHypotheticalCacheTracksOwnCasualtiesAndFullTemporaryRestoration)
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
		if(stage == 1) { int64_t damage = source->getMaxHealth(); source->damage(damage); }
		if(stage == 2) { int64_t healing = source->getTotalHealth(); source->heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE); }
		const BattleAttackInfo attack(source.get(), target.get(), 0, GetParam() == 1);
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
		EXPECT_EQ(source->getRangedFullDamageDistance(), 10);
		const BattleAttackInfo attack(source.get(), target.get(), 0, true);
		DamageCache historicalFresh;
		auto & currentCache = GetParam() == 0 ? cache : historicalFresh;
		EXPECT_EQ(currentCache.getDamage(source.get(), target.get(), model), model->battleExpectedLuckDamage(attack));
	}
}
#endif
INSTANTIATE_TEST_SUITE_P(CurrentAndAuthoredOmission, NewHorizonsOrdinaryRangedPairTest, ::testing::Values(0, 1));

TEST(OrdinaryRangedReplacementPolicies, ExplicitZeroFixedRecipesRejectWithoutInventingLegacyRules)
{
	for(const char * key : {"pointBlankShotDamageBonusPercent", "furyOfTheHordeAttack", "furyOfTheHordeDefense", "furyOfTheHordeDamage", "orcRageMaximumDamagePercent"})
	{
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 0;
		EXPECT_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, true), std::runtime_error);
	}
}
