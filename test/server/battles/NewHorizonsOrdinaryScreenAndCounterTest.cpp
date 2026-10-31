/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <set>
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/spells/CSpell.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/ProjectedAdjacencyBonuses.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsOrdinaryScreenAndCounterTest : public HeroCommandFixture
{
protected:
	bool historical = false;
	bool zero = false;
	CStack * actor = nullptr;
	CStack * ally = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercent", "ridersExploitDamagePercent"})
			if(historical)
				rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase(key);
			else if(zero)
				rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 0;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero, const char * creature)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(creature)), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 10);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 6), 1000);
	}
	static Bonus marker(BonusType type)
	{
		return Bonus(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID());
	}
};
#if ENABLE_BATTLE_AI
template<typename Random>
auto screenRandomBytes(Random & random)
{
	CMemorySerializer memory;
	memory.oser & random;
	return memory.extractBuffer();
}
class ScreenProbeUpdater final : public IUpdater
{
	std::shared_ptr<unsigned> calls;
public:
	explicit ScreenProbeUpdater(std::shared_ptr<unsigned> counter) : calls(std::move(counter)) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext &) const override
	{
		++*calls;
		return bonus;
	}
};
std::shared_ptr<unsigned> installScreenProbe(CStack & target)
{
	auto calls = std::make_shared<unsigned>(0);
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK,
		BonusSource::OTHER, 1, BonusSourceID());
	bonus->updater = std::make_shared<ScreenProbeUpdater>(calls);
	target.addNewBonus(bonus);
	return calls;
}
class ScreenCounterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ScreenCounterEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PrismaticScreenUsesDirectMdrInBothCapturedModesAndDoesNotStack)
{
	prepare("core:pasis", "new-horizons:wispUpgrade");
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	for(bool current : {false, true})
		EXPECT_EQ(spell->adjustRawDamage(defenderSideHero, ally, 10000, 0, 0, 100, current, current, false), 8500);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
	addStack(BattleSide::ATTACKER, creatureByName("new-horizons:wisp"), BattleHex(5, 5), 10);
	EXPECT_EQ(spell->adjustRawDamage(defenderSideHero, ally, 10000, 0, 0, 100, true, true, false), 8500);
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 10000);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PrismaticScreenKeepsNativeArmyUnderHypnotize)
{
	prepare("core:pasis", "new-horizons:wisp");
	actor->addNewBonus(std::make_shared<Bonus>(marker(BonusType::HYPNOTIZED)));
	ally->addNewBonus(std::make_shared<Bonus>(marker(BonusType::HYPNOTIZED)));
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	EXPECT_EQ(spell->adjustRawDamage(defenderSideHero, ally, 10000, 0, 0, 100, true, true, false), 8500);
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 10000);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, EvasiveFlightProtectsOnlyOrdinaryPrimaryCounterDamage)
{
	prepare("core:korbac", "core:fireDragonFly");
	BattleAttackInfo attack(victim, actor, 0, false);
	const auto unprotected = battle()->calculateDmgRange(attack);
	attack.retaliation = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), attack), 20);
	const auto protectedRange = battle()->calculateDmgRange(attack);
	EXPECT_LT(protectedRange.damage.min, unprotected.damage.min);
	EXPECT_LT(protectedRange.damage.max, unprotected.damage.max);
	for(int exclusion = 0; exclusion < 5; ++exclusion)
	{
		auto excluded = attack;
		if(exclusion == 0) excluded.shooting = true;
		if(exclusion == 1) excluded.secondaryAttack = true;
		if(exclusion == 2) excluded.bracePreemptive = true;
		if(exclusion == 3) excluded.preemptiveDamagePercent = 50;
		if(exclusion == 4) excluded.physicalDamage = false;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), excluded), 0);
	}
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, RidersExploitReadsAllowanceRatherThanAttackBlockingOrTemporaryInability)
{
	prepare("core:tyraxor", "core:hobgoblinWolfRider");
	BattleAttackInfo attack(actor, victim, 0, false);
	actor->addNewBonus(std::make_shared<Bonus>(marker(BonusType::BLOCKS_RETALIATION)));
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	victim->addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(), 1)));
	ASSERT_FALSE(victim->ableToRetaliate());
	ASSERT_TRUE(victim->counterAttacks.canUse());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	victim->removeBonuses(Selector::type()(BonusType::PHYSICAL_AFFLICTION));
	const auto ordinary = battle()->calculateDmgRange(attack);
	victim->counterAttacks.use();
	ASSERT_FALSE(victim->counterAttacks.canUse());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	const auto boosted = battle()->calculateDmgRange(attack);
	EXPECT_GT(boosted.damage.min, ordinary.damage.min);
	EXPECT_GT(boosted.damage.max, ordinary.damage.max);
	victim->addNewBonus(std::make_shared<Bonus>(marker(BonusType::UNLIMITED_RETALIATIONS)));
	EXPECT_TRUE(victim->counterAttacks.canUse());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, HistoricalAbsenceDoesNotReinterpretExistingPasisCapture)
{
	historical = true;
	prepare("core:pasis", "new-horizons:wisp");
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(SpellID(SpellID::MAGIC_ARROW).toSpell()->adjustRawDamage(defenderSideHero, ally,
		10000, 0, 0, 100, true, true, false), 10000);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ExplicitZeroKeepsReplacementWithoutGrantingScreen)
{
	zero = true;
	prepare("core:pasis", "new-horizons:wisp");
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(SpellID(SpellID::MAGIC_ARROW).toSpell()->adjustRawDamage(defenderSideHero, ally,
		10000, 0, 0, 100, true, true, false), 10000);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedScreenTracksOnlyBranchPositionsWithoutChangingLiveNeighbors)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	branch->getForUpdate(actor->unitId())->setPosition(BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedCounterAllowanceIsBranchLocalAndUsesSharedDamageConsumer)
{
	prepare("core:tyraxor", "core:goblinWolfRider");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto source = branch->getForUpdate(actor->unitId());
	const auto target = branch->getForUpdate(victim->unitId());
	BattleAttackInfo projected(source.get(), target.get(), 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*branch, projected), 100);
	target->counterAttacks.use();
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*branch, projected), 120);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), BattleAttackInfo(actor, victim, 0, false)), 100);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedEvasiveFlightUsesEffectiveRecipientOwner)
{
	prepare("core:korbac", "core:serpentFly");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto fly = branch->getForUpdate(actor->unitId());
	const auto counter = branch->getForUpdate(victim->unitId());
	BattleAttackInfo reaction(counter.get(), fly.get(), 0, false);
	reaction.retaliation = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*branch, reaction), 20);
	fly->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	counter->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*branch, reaction), 0);
}
#endif

TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, EachNewOptionalKeyIncludingZeroRejects1144Capture)
{
	const auto context = [](ESerializationVersion version)
	{
		CMemorySerializer wire;
		wire.oser.version = version;
		return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
	};
	JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	auto & policy = development["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const std::set<std::string> legacy{"version", "closeQuartersDrill", "marshSharpshooter",
		"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"};
	std::erase_if(policy.Struct(), [&](const auto & row) { return !legacy.contains(row.first); });
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)));
	for(const char * key : {"prismaticScreenReductionPercent", "evasiveFlightReductionPercent", "ridersExploitDamagePercent"})
	{
		policy[key].Integer() = 0;
		EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES)), std::runtime_error);
		EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::NEW_HORIZONS_ADJACENCY_FILTERS)), std::runtime_error);
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(development, context(ESerializationVersion::CURRENT)));
		policy.Struct().erase(key);
	}
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, AllowanceCacheGateSurvivesEffectiveOwnerTransfer)
{
	prepare("core:tyraxor", "core:goblinWolfRider");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto source = branch->getForUpdate(actor->unitId());
	const auto target = branch->getForUpdate(victim->unitId());
	target->counterAttacks.use();
	BattleAttackInfo attack(source.get(), target.get(), 0, false);
	ASSERT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*branch, attack), 120);
	DamageCache cache;
	cache.cacheDamage(source.get(), target.get(), branch);
	source->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	target->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	ASSERT_EQ(branch->battleGetOwner(source.get()), PlayerColor(1));
	ASSERT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*branch, attack), 100);
	EXPECT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*branch));
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), branch, &timing), branch->battleExpectedLuckDamage(attack));
	EXPECT_EQ(timing.dynamicBypasses, 1u);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, HistoricalAllowanceAbsenceDoesNotActivateNewCachePolicy)
{
	historical = true;
	prepare("core:tyraxor", "core:goblinWolfRider");
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*battle()));
}
#endif

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedScreenEmitterMovementReevaluatesCachedDamageInBothDirections)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	branch->moveUnit(actor->unitId(), BattleHex(3, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedScreenRecipientMovementReevaluatesCachedDamageInBothDirections)
{
	prepare("core:pasis", "new-horizons:wispUpgrade");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	branch->moveUnit(ally->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	branch->moveUnit(ally->unitId(), BattleHex(4, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	EXPECT_EQ(ally->getPosition(), BattleHex(4, 5));
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedOverlappingScreensDoNotStackAndDeadLastSourceDoesNotProtect)
{
	prepare("core:pasis", "new-horizons:wisp");
	const auto * extra = addStack(BattleSide::ATTACKER, creatureByName("new-horizons:wispUpgrade"), BattleHex(5, 5), 10);
	ASSERT_NE(extra, nullptr);
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	const auto lastSource = branch->getForUpdate(extra->unitId());
	int64_t lethalDamage = lastSource->getAvailableHealth();
	lastSource->damage(lethalDamage);
	ASSERT_FALSE(lastSource->alive());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	EXPECT_TRUE(extra->alive());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PlayerVisibleScreenQueriesPreserveSiblingLiveStateAndBothRandomStreams)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto parentRecipient = parent->getForUpdate(ally->unitId());
	parent->getForUpdate(actor->unitId());
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	const auto childRecipient = child->getForUpdate(ally->unitId());
	const auto siblingRecipient = sibling->getForUpdate(ally->unitId());
	child->getForUpdate(actor->unitId());
	const auto siblingEmitter = sibling->getForUpdate(actor->unitId());
	const auto liveBefore = gameState()->saveToMemory();
	const auto siblingBefore = siblingRecipient->save();
	const auto siblingEmitterBefore = siblingEmitter->save();
	const auto serverRandom = screenRandomBytes(*gameHandler->randomizer);
	const auto aiRandom = screenRandomBytes(CRandomGenerator::getDefault());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*child, *childRecipient, 10000), 8500);
	child->moveUnit(actor->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*child, *childRecipient, 10000), 10000);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*parent, *parentRecipient, 10000), 8500);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*sibling, *siblingRecipient, 10000), 8500);
	EXPECT_EQ(siblingRecipient->save(), siblingBefore);
	EXPECT_EQ(siblingEmitter->save(), siblingEmitterBefore);
	EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
	EXPECT_EQ(screenRandomBytes(*gameHandler->randomizer), serverRandom);
	EXPECT_EQ(screenRandomBytes(CRandomGenerator::getDefault()), aiRandom);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedSourceMorphUsesExistingFamilyFilterAndRestoresItsScreen)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const auto source = branch->getForUpdate(actor->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	source->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	source->endBattleForm();
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	EXPECT_FALSE(actor->hasBattleForm());
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ProjectedHypnotizedScreenRetainsOriginalArmyRelationship)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const auto source = branch->getForUpdate(actor->unitId());
	source->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	recipient->addUnitBonus({marker(BonusType::HYPNOTIZED)});
	ASSERT_EQ(branch->battleGetOwner(source.get()), PlayerColor(1));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 8500);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, HistoricalScreenAbsenceStaysAbsentAcrossProjectedMovement)
{
	historical = true;
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	branch->moveUnit(actor->unitId(), BattleHex(3, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ExplicitZeroScreenStaysZeroAcrossProjectedMovement)
{
	zero = true;
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	branch->moveUnit(actor->unitId(), BattleHex(3, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
}
#endif

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PositiveScreenCertificateRequiresExactAddressOwnerAndLivingSource)
{
	prepare("core:pasis", "new-horizons:wisp");
	auto original = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticSourceBinding binding(original, *original);
	ASSERT_NE(binding.adjacencySources(original), nullptr);
	auto other = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	const std::shared_ptr<CBattleInfoCallback> sameOwnerDifferentAddress(original, other.get());
	EXPECT_EQ(binding.adjacencySources(sameOwnerDifferentAddress), nullptr);
	auto holder = std::make_shared<int>(0);
	const std::shared_ptr<CBattleInfoCallback> sameAddressDifferentOwner(holder, original.get());
	EXPECT_EQ(binding.adjacencySources(sameAddressDifferentOwner), nullptr);
	const std::weak_ptr<CPlayerBattleCallback> retired = original;
	// The same-control alias still owns the real callback after its primary handle resets.
	original.reset();
	EXPECT_FALSE(retired.expired());
	// A fresh binding has no ownership lease; the independently owned alias cannot revive it.
	auto ephemeral = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticSourceBinding retiredBinding(ephemeral, *ephemeral);
	ASSERT_NE(retiredBinding.adjacencySources(ephemeral), nullptr);
	const std::shared_ptr<CBattleInfoCallback> retiredAlias(holder, ephemeral.get());
	const std::weak_ptr<CPlayerBattleCallback> weak = ephemeral;
	ephemeral.reset();
	ASSERT_TRUE(weak.expired());
	EXPECT_EQ(retiredBinding.adjacencySources(retiredAlias), nullptr);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, AncestorBaseRebindRetiresDescendantPositiveScreenCertificate)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	parent->getForUpdate(ally->unitId());
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	const auto recipient = child->getForUpdate(ally->unitId());
	ASSERT_NE(ProjectedAdjacencyBonuses::query(*recipient, Selector::all, {}, false), nullptr);
	auto replacement = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	static_cast<BattleProxy &>(*parent) = static_cast<const BattleProxy &>(*replacement);
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*recipient, Selector::all, {}, false), nullptr);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ally, 10000), 8500);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PositiveScreenOpaqueSelectorAndSameKeyCompetitionKeepStackingAndRawOccurrences)
{
	prepare("core:pasis", "new-horizons:wisp");
	const std::string key = "new-horizons:ordinary-creature-specialty:core:pasis";
	Bonus competing(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER,
		25, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY), BonusValueType::BASE_NUMBER);
	competing.stacking = key;
	// Compete within the native layer; recipient-local ADD intentionally does not restack it.
	ally->addNewBonus(std::make_shared<Bonus>(competing));
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const CSelector sameKey([key](const Bonus * bonus) { return bonus->stacking == key; });
	const CSelector opaque([key](const Bonus * bonus) { return bonus->stacking == key && bonus->val == 15; });
	const auto stacked = recipient->getBonusesBeforeCreatureAbilitySuppression(sameKey);
	const auto raw = recipient->getBonusesBeforeCreatureAbilitySuppression(sameKey, {}, true);
	ASSERT_EQ(stacked->size(), 1u);
	EXPECT_EQ(stacked->front()->val, 25);
	ASSERT_GE(raw->size(), 2u);
	EXPECT_EQ(raw->size(), ally->getUnstackedBonuses(sameKey)->size());
	EXPECT_TRUE(recipient->getBonusesBeforeCreatureAbilitySuppression(opaque)->empty());
	const auto heldWeaker = recipient->getBonusesBeforeCreatureAbilitySuppression(opaque, {}, true);
	ASSERT_FALSE(heldWeaker->empty());
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	const auto away = recipient->getBonusesBeforeCreatureAbilitySuppression(sameKey, {}, true);
	ASSERT_EQ(away->size(), 1u);
	EXPECT_EQ(away->front()->val, 25);
	EXPECT_TRUE(recipient->getBonusesBeforeCreatureAbilitySuppression(opaque, {}, true)->empty());
	EXPECT_EQ(heldWeaker->front()->val, 15);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, NestedScreenRecipientOverlayRefreshAndRemovalPreserveParentMembership)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto source = parent->getForUpdate(ally->unitId());
	Bonus local(BonusDuration::N_TURNS, BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::SPELL_EFFECT,
		10, BonusSourceID(SpellID(SpellID::PROTECTION_FROM_AIR)), BonusSubtypeID(SpellSchool::ANY));
	local.turnsRemain = 2;
	local.stacking = "screen-overlay-characterization";
	source->addUnitBonus({local});
	local.val = 99;
	local.turnsRemain = 5;
	source->updateUnitBonus({local});
	const CSelector selected([](const Bonus * bonus) { return bonus->stacking == "screen-overlay-characterization"; });
	const auto held = source->getBonusesBeforeCreatureAbilitySuppression(selected);
	ASSERT_EQ(held->size(), 1u);
	EXPECT_EQ(held->front()->val, 10);
	EXPECT_EQ(held->front()->turnsRemain, 5);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	const auto recipient = child->getForUpdate(ally->unitId());
	recipient->removeUnitBonus(selected);
	EXPECT_TRUE(recipient->getBonusesBeforeCreatureAbilitySuppression(selected)->empty());
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*child, *recipient, 10000), 8500);
	EXPECT_EQ(source->getBonusesBeforeCreatureAbilitySuppression(selected)->size(), 1u);
	EXPECT_EQ(held->front()->val, 10);
	EXPECT_EQ(held->front()->turnsRemain, 5);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, FrozenDamageScreenSnapshotDoesNotFollowLaterEmitterMovement)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	auto retained = std::make_shared<StackWithBonuses>(branch.get(), recipient.get());
	retained->freezeDamageScoringBonusSnapshot();
	ASSERT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *retained, 10000), 8500);
	branch->moveUnit(actor->unitId(), BattleHex(12, 5));
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *recipient, 10000), 10000);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*branch, *retained, 10000), 8500);
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*retained, Selector::all, {}, false), nullptr);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, HiddenPasisSourceDeniesProjectionBeforeAnyNativeUpdaterOrSelector)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const auto calls = installScreenProbe(*ally);
	const CSelector forbidden([](const Bonus *) -> bool { throw std::runtime_error("Denied source ran selector"); });
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*recipient, forbidden, {}, false), nullptr);
	EXPECT_EQ(*calls, 0u);
	(void)ally->getAllBonuses(Selector::all);
	EXPECT_GT(*calls, 0u); // Positive witness: the installed native updater is observable.
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, HiddenEnemyRecipientDeniesProjectionBeforeAnyNativeUpdaterOrSelector)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto own = branch->getForUpdate(ally->unitId());
	ASSERT_NE(ProjectedAdjacencyBonuses::query(*own, Selector::all, {}, false), nullptr);
	const auto recipient = branch->getForUpdate(victim->unitId());
	const auto calls = installScreenProbe(*victim);
	const CSelector forbidden([](const Bonus *) -> bool { throw std::runtime_error("Denied recipient ran selector"); });
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*recipient, forbidden, {}, false), nullptr);
	EXPECT_EQ(*calls, 0u);
	(void)victim->getAllBonuses(Selector::all);
	EXPECT_GT(*calls, 0u);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, ZeroScreenCapabilityDeniesDirectLaneWithoutNativeUpdaterOrSelector)
{
	zero = true;
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const auto calls = installScreenProbe(*ally);
	const CSelector forbidden([](const Bonus *) -> bool { throw std::runtime_error("Zero policy ran selector"); });
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*recipient, forbidden, {}, false), nullptr);
	EXPECT_EQ(*calls, 0u);
}
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, MissingScreenCapabilityDeniesDirectLaneWithoutNativeUpdaterOrSelector)
{
	historical = true;
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto recipient = branch->getForUpdate(ally->unitId());
	const auto calls = installScreenProbe(*ally);
	const CSelector forbidden([](const Bonus *) -> bool { throw std::runtime_error("Missing policy ran selector"); });
	EXPECT_EQ(ProjectedAdjacencyBonuses::query(*recipient, forbidden, {}, false), nullptr);
	EXPECT_EQ(*calls, 0u);
}
#endif

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsOrdinaryScreenAndCounterTest, PositiveScreenPreservesSinglePersonalVeteranCohesionAcrossNestedViews)
{
	prepare("core:pasis", "new-horizons:wisp");
	ScreenCounterEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto original = parent->getForUpdate(ally->unitId());
	original->veteranCohesionEarned = true;
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	const auto recipient = child->getForUpdate(ally->unitId());
	const CSelector personal([](const Bonus * bonus)
		{ return bonus->stacking == "new-horizons:discipline.veteranCohesion"; });
	ASSERT_EQ(recipient->getBonusesBeforeCreatureAbilitySuppression(personal)->size(), 1u);
	recipient->veteranCohesionEarned = false;
	EXPECT_TRUE(recipient->getBonusesBeforeCreatureAbilitySuppression(personal)->empty());
	recipient->veteranCohesionEarned = true;
	const auto normalized = recipient->getBonusesBeforeCreatureAbilitySuppression(personal, {}, true);
	ASSERT_EQ(normalized->size(), 1u);
	EXPECT_EQ(normalized->front()->val, 2);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*child, *recipient, 10000), 8500);
	EXPECT_EQ(original->getBonusesBeforeCreatureAbilitySuppression(personal)->size(), 1u);
}
#endif
