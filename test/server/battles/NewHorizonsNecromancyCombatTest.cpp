/*
 * NewHorizonsNecromancyCombatTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/battle/NewHorizonsNecromancyCombat.h"
#include "../../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"

namespace
{
template<typename Value>
std::vector<std::byte> serialized(Value & value)
{
	CMemorySerializer output;
	output.oser & value;
	return output.extractBuffer();
}

class NecromancyEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit NecromancyEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

std::shared_ptr<Bonus> marker(BonusType type)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, 1, BonusSourceID());
}
}

class NewHorizonsNecromancyCombatTest : public HeroCommandFixture
{
protected:
	bool currentPolicy = true;
	bool legacyMdr = false;
	bool useGraveplate = false;
	bool legacyPdr = false;
	CStack * skeleton = nullptr;
	CStack * warrior = nullptr;
	CStack * ordinary = nullptr;
	CStack * magicalShooter = nullptr;
	CStack * physicalSource = nullptr;
	std::shared_ptr<NecromancyEnvironment> environment;
	std::shared_ptr<CBattleCallback> callback;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		if(!currentPolicy)
			perks.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		perks.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(legacyMdr)
			magic["spells"].Struct().erase("new-horizons:holyArmor");
		if(legacyPdr)
			magic.Struct().erase("physicalDamageReductionCapPercent");
		magic.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}

	CStack * at(BattleSide side, SlotID slot)
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == side && unit->unitSlot() == slot)
				return battle()->getStack(unit->unitId(), false);
		return nullptr;
	}

	void prepare()
	{
		startGame();
		ASSERT_EQ(newHorizonsNecromancy::policy(defenderSideHero->getPerkState().rules).has_value(), currentPolicy);
		defenderSideHero->setHeroType(HeroTypeID(72));
		ASSERT_TRUE(defenderSideHero->usesNewHorizonsNecromancy());
		const auto skillId = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
		ASSERT_GE(skillId, 0);
		const auto skill = SecondarySkill(skillId);
		defenderSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(defenderSideHero->getNewHorizonsNecromancyRank(), 3);
		const auto * advanced = useGraveplate ? newHorizonsNecromancy::DEATH_LORD_ID
			: newHorizonsNecromancy::SOUL_HARVESTER_ID;
		for(const auto * perk : {newHorizonsNecromancy::DARK_CONVERSION_ID,
			advanced, newHorizonsNecromancy::CORPSE_PRESERVATION_ID})
		{
			defenderSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, perk});
			ASSERT_TRUE(defenderSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, perk));
		}
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), CreatureID(CreatureID::SKELETON), 200));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:skeletonWarrior"), 200));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(2), creatureByName("core:zombie"), 200));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:magog"), 10));
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		skeleton = at(BattleSide::DEFENDER, SlotID(0));
		warrior = at(BattleSide::DEFENDER, SlotID(1));
		ordinary = at(BattleSide::DEFENDER, SlotID(2));
		magicalShooter = at(BattleSide::ATTACKER, SlotID(0));
		ASSERT_NE(skeleton, nullptr);
		ASSERT_NE(warrior, nullptr);
		ASSERT_NE(ordinary, nullptr);
		ASSERT_NE(magicalShooter, nullptr);
		forceMaximumDamage(magicalShooter);
		if(useGraveplate)
			physicalSource = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(7, 7), 2);
		beginCombat();
		callback = std::make_shared<CBattleCallback>(PlayerColor(1), nullptr);
		callback->onBattleStarted(battle());
		environment = std::make_shared<NecromancyEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> forecast()
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}

	// Calculator arithmetic witness; action legality is covered separately by the native melee test.
	DamageEstimation physicalDamage(CStack * target, int cap = 80, int armorer = 0, int order = 0,
		int intrinsic = -1, int output = 100, bool physical = true)
	{
		DamageAttackInfo info;
		info.attacker = physicalSource;
		info.defender = target;
		info.attackerHex = physicalSource->getPosition();
		info.defenderHex = target->getPosition();
		info.physicalDamage = physical;
		info.physicalDamageReductionCapPercent = cap;
		info.intrinsicPhysicalDamageReductionPercent = intrinsic >= 0 ? intrinsic
			: target->intrinsicPhysicalDamageReductionPercent();
		info.newHorizonsArmorerReductionPercent = armorer;
		info.heroOrderDamageReductionPercents = {order};
		info.heroOrderDamageReductionPercent = order;
		info.activationOutputPercent = output;
		const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
		if(!calculator)
			throw std::runtime_error("Graveplate control requires the native damage calculator");
		return calculator->calculate(*battle(), info);
	}
};

TEST_F(NewHorizonsNecromancyCombatTest, BothSkeletonFormsGainMovementOnlyAndOneNormallyRestrictedCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * target : {skeleton, warrior})
	{
		EXPECT_EQ(target->intrinsicMagicalDamageReductionPercent(), 30);
		EXPECT_EQ(target->getMovementRange(), target->getInitiative() + 2);
		EXPECT_EQ(target->counterAttacks.available(), 2);
		target->counterAttacks.use();
		EXPECT_EQ(target->counterAttacks.available(), 1);
		target->counterAttacks.use();
		EXPECT_FALSE(target->ableToRetaliate());
		target->counterAttacks.reset();
		EXPECT_EQ(target->counterAttacks.available(), 2);
		target->addNewBonus(marker(BonusType::NO_RETALIATION));
		EXPECT_EQ(target->counterAttacks.available(), 0);
		EXPECT_FALSE(target->ableToRetaliate());
	}
	EXPECT_EQ(ordinary->intrinsicMagicalDamageReductionPercent(), 0);
	EXPECT_EQ(ordinary->counterAttacks.available(), 1);
	EXPECT_EQ(ordinary->getMovementRange(), ordinary->getInitiative());
}

TEST_F(NewHorizonsNecromancyCombatTest, FrozenAndBindStillZeroMovementAndLegacyInitiativeFallbackDoesNotGainSpeed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// A private provider has Speed but no explicit Initiative, exercising the legacy fallback.
	CBonusSystemNode speedOnly(BonusNodeType::STACK_BATTLE, true);
	speedOnly.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 5, BonusSourceID()));
	battle::CUnitStateDetached fallback(skeleton, &speedOnly);
	fallback.localInit(skeleton);
	EXPECT_EQ(fallback.getInitiative(), 5);
	EXPECT_EQ(fallback.getMovementRange(), 7u);
	const auto beforeInitiative = skeleton->getInitiative();
	skeleton->addNewBonus(marker(BonusType::BIND_EFFECT));
	EXPECT_EQ(skeleton->getMovementRange(), 0u);
	EXPECT_EQ(skeleton->getInitiative(), beforeInitiative);
	warrior->addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::makeFrozenMarker(BonusSourceID(), 0)));
	EXPECT_EQ(warrior->getMovementRange(), 0u);
}

TEST_F(NewHorizonsNecromancyCombatTest, AbsentCapturedPolicyRetainsAllHistoricalCombatOutputs)
{
	currentPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto unchanged = forecast();
	EXPECT_FALSE(unchanged->stackStates.contains(skeleton->unitId()));
	for(auto * target : {skeleton, warrior, ordinary})
	{
		EXPECT_EQ(target->intrinsicMagicalDamageReductionPercent(), 0);
		EXPECT_FALSE(newHorizonsNecromancy::needsCombatPassiveSnapshot(*target));
		EXPECT_EQ(target->getMovementRange(), target->getInitiative());
		EXPECT_EQ(target->counterAttacks.available(), 1);
		EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 100), 100);
		EXPECT_EQ(newHorizonsNecromancy::adjustUnmitigatedMagicalDamage(*target, 100), 100);
	}
}

TEST_F(NewHorizonsNecromancyCombatTest, BonewardIsIndependentFromSpellwardAndNeverChanceResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * fireball = SpellID(SpellID::FIREBALL).toSpell();
	ASSERT_NE(fireball, nullptr);
	for(auto * target : {skeleton, warrior})
	{
		EXPECT_EQ(fireball->adjustRawDamage(nullptr, target, 100, 0, 0, 100, true, true, false), 70);
		EXPECT_EQ(fireball->adjustRawDamage(nullptr, target, 100, 0, 0, 100, true, true, false, 2000), 56);
		EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 100), 70);
		EXPECT_EQ(newHorizonsNecromancy::adjustUnmitigatedMagicalDamage(*target, 100), 70);
		EXPECT_EQ(target->magicResistance(), ordinary->magicResistance());
	}
	EXPECT_EQ(fireball->adjustRawDamage(nullptr, ordinary, 100, 0, 0, 100, true, true, false), 100);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ordinary, 100), 100);
	const auto warcasting = SecondarySkill::decode("new-horizons:warcasting");
	ASSERT_GE(warcasting, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(warcasting), 1, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.spellward"});
	const auto spellward = battle()->battleGetPerkMagicalReductionBasisPoints(skeleton);
	ASSERT_EQ(spellward, 1000);
	EXPECT_EQ(fireball->adjustRawDamage(nullptr, skeleton, 100, 0, 0, 100, true, true, false, spellward), 63);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *skeleton, 100), 63);
}

TEST_F(NewHorizonsNecromancyCombatTest, NativeSpellUsesSameMitigatedValueAsDetachedForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto projected = forecast();
	auto target = projected->getForUpdate(skeleton->unitId());
	const auto * fireball = SpellID(SpellID::FIREBALL).toSpell();
	spells::BattleCast live(battle(), attackerSideHero, spells::Mode::HERO, fireball);
	const auto mechanics = fireball->battleMechanics(&live);
	const auto expected = mechanics->adjustEffectValue(skeleton);
	ASSERT_GT(expected, 0);
	spells::BattleCast detached(projected.get(), attackerSideHero, spells::Mode::HERO, fireball);
	const auto beforeProjected = target->getAvailableHealth();
	detached.castEval(projected->getServerCallback(), {spells::Destination(target.get())});
	EXPECT_EQ(beforeProjected - target->getAvailableHealth(), expected);
	const auto beforeLive = skeleton->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::FIREBALL, skeleton));
	EXPECT_EQ(beforeLive - skeleton->getAvailableHealth(), expected);
}

TEST_F(NewHorizonsNecromancyCombatTest, MagicalShotPredictionAndAppliedDamageAgreeWhilePhysicalDamageIsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(magicalShooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	BattleAttackInfo attack(magicalShooter, skeleton, 0, true);
	ASSERT_FALSE(attack.physicalDamage);
	const auto predicted = battle()->battleEstimateDamage(attack);
	ASSERT_EQ(predicted.damage.min, predicted.damage.max);
	auto projected = forecast();
	const auto * projectedMage = projected->battleGetUnitByID(magicalShooter->unitId());
	const auto * projectedTarget = projected->battleGetUnitByID(skeleton->unitId());
	const auto preview = projected->battleEstimateDamage(BattleAttackInfo(projectedMage, projectedTarget, 0, true));
	EXPECT_EQ(preview.damage.min, predicted.damage.min);
	DamageEstimation raw;
	raw.damage = {100, 100};
	newHorizonsNecromancy::adjustCreatureDamage(attack, raw);
	EXPECT_EQ(raw.damage.min, 70);
	EXPECT_EQ(raw.damage.max, 70);
	raw.damage = {100, 100};
	attack.physicalDamage = true;
	newHorizonsNecromancy::adjustCreatureDamage(attack, raw);
	EXPECT_EQ(raw.damage.min, 100);
	EXPECT_EQ(raw.damage.max, 100);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = magicalShooter->unitId();
	gameHandler->sendAndApply(active);
	const auto before = skeleton->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(magicalShooter, skeleton)));
	EXPECT_EQ(before - skeleton->getAvailableHealth(), predicted.damage.min);
}

TEST_F(NewHorizonsNecromancyCombatTest, CapturedInputsAndCurrentControllerKeepParentChildSiblingAndCacheIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto serverRng = serialized(*gameHandler->randomizer);
	const auto defaultRng = serialized(CRandomGenerator::getDefault());
	auto parent = forecast();
	auto first = parent->getForUpdate(skeleton->unitId());
	const auto nativeHealth = skeleton->getAvailableHealth();
	const auto captured = battle::ProjectedUnitEnvironmentState::capture(*battle());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
		0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(skeleton->intrinsicMagicalDamageReductionPercent(), 0);
	EXPECT_EQ(captured.inputs(BattleSide::DEFENDER).necromancy.magicalReductionPercent, 30);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto changed = child->getForUpdate(skeleton->unitId());
	EXPECT_EQ(changed->intrinsicMagicalDamageReductionPercent(), 30);
	EXPECT_EQ(changed->counterAttacks.available(), 2);
	changed->counterAttacks.use();
	EXPECT_EQ(first->counterAttacks.available(), 2);
	DamageCache baseline;
	baseline.buildDamageCache(parent, BattleSide::DEFENDER);
	DamageCache current(&baseline);
	current.buildDamageCache(child, BattleSide::DEFENDER);
	const auto * shooter = child->battleGetUnitByID(magicalShooter->unitId());
	changed->beginBattleForm(creatureByName("core:zombie"), 2);
	EXPECT_EQ(changed->intrinsicMagicalDamageReductionPercent(), 0);
	const bool formShot = child->battleCanShoot(shooter, changed->getPosition());
	ASSERT_TRUE(formShot);
	const auto formDamage = child->battleExpectedLuckDamage(BattleAttackInfo(shooter, changed.get(), 0, formShot));
	EXPECT_EQ(current.getDamage(shooter, changed.get(), child), formDamage);
	changed->endBattleForm();
	EXPECT_EQ(changed->intrinsicMagicalDamageReductionPercent(), 30);
	changed->addUnitBonus(std::vector<Bonus>{*marker(BonusType::HYPNOTIZED)});
	EXPECT_EQ(changed->intrinsicMagicalDamageReductionPercent(), 0);
	EXPECT_EQ(changed->counterAttacks.available(), 0);
	EXPECT_EQ(sibling->getForUpdate(skeleton->unitId())->intrinsicMagicalDamageReductionPercent(), 30);
	const bool currentShot = child->battleCanShoot(shooter, changed->getPosition());
	const auto expected = child->battleExpectedLuckDamage(BattleAttackInfo(shooter, changed.get(), 0, currentShot));
	EXPECT_EQ(current.getDamage(shooter, changed.get(), child), expected);
	EXPECT_EQ(first->intrinsicMagicalDamageReductionPercent(), 30);
	EXPECT_EQ(skeleton->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(serialized(CRandomGenerator::getDefault()), defaultRng);
}

TEST_F(NewHorizonsNecromancyCombatTest, ObservingEnemyDoesNotImportHiddenPassivesFromInitialExportWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(skeleton->intrinsicMagicalDamageReductionPercent(), 30);
	auto observer = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	observer->onBattleStarted(battle());
	auto hidden = std::make_shared<HypotheticBattle>(environment.get(), observer->getBattle(BattleID(0)));
	const auto * viewed = hidden->battleGetUnitByID(skeleton->unitId());
	ASSERT_NE(viewed, nullptr);
	EXPECT_EQ(viewed->intrinsicMagicalDamageReductionPercent(), 0);
	EXPECT_EQ(viewed->getMovementRange(), viewed->getInitiative());
	EXPECT_EQ(viewed->acquireState()->counterAttacks.available(), 1);
	EXPECT_EQ(skeleton->intrinsicMagicalDamageReductionPercent(), 30);
}

TEST_F(NewHorizonsNecromancyCombatTest, NewPassiveWithHistoricalMdrReducesMagicalDamageOnly)
{
	legacyMdr = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	const auto * fireball = SpellID(SpellID::FIREBALL).toSpell();
	for(auto * target : {skeleton, warrior})
	{
		ASSERT_EQ(target->intrinsicMagicalDamageReductionPercent(), 30);
		EXPECT_EQ(fireball->adjustRawDamage(nullptr, target, 100, 0, 0, 100, false, false, false), 70);
		EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 100), 70);
		BattleAttackInfo physical(magicalShooter, target, 0, false);
		ASSERT_TRUE(physical.physicalDamage);
		DamageEstimation raw;
		raw.damage = {100, 100};
		newHorizonsNecromancy::adjustCreatureDamage(physical, raw);
		EXPECT_EQ(raw.damage.min, 100);
		EXPECT_EQ(raw.damage.max, 100);
	}
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ordinary, 100), 100);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(fireball->adjustRawDamage(nullptr, skeleton, 100, 0, 0, 100, false, false, false), 56);
	// Historical abilities ignored ordinary MDR; add only Boneward to that path.
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *skeleton, 100), 70);
}

TEST_F(NewHorizonsNecromancyCombatTest, HistoricalMdrWithoutNewPassiveRetainsItsOriginalReductions)
{
	legacyMdr = true;
	currentPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	ASSERT_EQ(skeleton->intrinsicMagicalDamageReductionPercent(), 0);
	const auto * fireball = SpellID(SpellID::FIREBALL).toSpell();
	EXPECT_EQ(fireball->adjustRawDamage(nullptr, skeleton, 100, 0, 0, 100, false, false, false), 100);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *skeleton, 100), 100);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(fireball->adjustRawDamage(nullptr, skeleton, 100, 0, 0, 100, false, false, false), 80);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *skeleton, 100), 100);
}

TEST_F(NewHorizonsNecromancyCombatTest, GraveplateIsLegalAlternativeToTombGuardAndIndependentCappedPhysicalSource)
{
	useGraveplate = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(defenderSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::DEATH_LORD_ID));
	ASSERT_FALSE(defenderSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::SOUL_HARVESTER_ID));
	for(auto * target : {skeleton, warrior})
	{
		ASSERT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 20);
		EXPECT_EQ(target->counterAttacks.available(), 1);
		ASSERT_EQ(physicalDamage(target, 80, 0, 0, 0).damage.min, 100);
		EXPECT_EQ(physicalDamage(target).damage.min, 80);
		EXPECT_EQ(physicalDamage(target, 80, 25, 30).damage.min, 42);
		EXPECT_EQ(physicalDamage(target, 80, 100).damage.min, 20)
			<< "Graveplate belongs before the cap, never after the already capped result";
		EXPECT_EQ(physicalDamage(target, 80, 0, 30, -1, 9).damage.min, 5)
			<< "Independent sources and activation output reach one final floor";
		EXPECT_EQ(physicalDamage(target, 80, 0, 0, -1, 100, false).damage.min, 100);
		EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *target, 100), 70);
	}
	EXPECT_EQ(ordinary->intrinsicPhysicalDamageReductionPercent(), 0);
	EXPECT_EQ(physicalDamage(ordinary).damage.min, 100);
}

TEST_F(NewHorizonsNecromancyCombatTest, GraveplateCoexistsWithHistoricalPhysicalRulesWithoutReinterpretingOldSources)
{
	useGraveplate = true;
	legacyPdr = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(battle()->getMagicRules()), -1);
	EXPECT_EQ(physicalDamage(skeleton, -1, 0, 0, 0).damage.min, 100);
	EXPECT_EQ(physicalDamage(skeleton, -1).damage.min, 80);
	EXPECT_EQ(physicalDamage(skeleton, -1, 25, 30).damage.min, 42);
	EXPECT_EQ(physicalDamage(skeleton, -1, 25, 30, 0).damage.min, 52);
}

TEST_F(NewHorizonsNecromancyCombatTest, HistoricalNecromancyCaptureDoesNotAcquireGraveplate)
{
	useGraveplate = true;
	currentPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(skeleton->intrinsicPhysicalDamageReductionPercent(), 0);
	EXPECT_EQ(physicalDamage(skeleton).damage.min, 100);
	EXPECT_EQ(physicalDamage(skeleton, 80, 25, 30).damage.min, 52);
	EXPECT_EQ(physicalDamage(skeleton, -1, 25, 30).damage.min, 52);
}

TEST_F(NewHorizonsNecromancyCombatTest, GraveplateNativePhysicalHitMatchesDetachedForecast)
{
	useGraveplate = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(8, 7), 200);
	ASSERT_NE(target, nullptr);
	ASSERT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 20);
	BattleAttackInfo physical(physicalSource, target, 0, false);
	const auto native = battle()->battleEstimateDamage(physical);
	ASSERT_EQ(native.damage.min, native.damage.max);
	auto projected = forecast();
	const auto * from = projected->battleGetUnitByID(physicalSource->unitId());
	const auto * to = projected->battleGetUnitByID(target->unitId());
	const auto preview = projected->battleEstimateDamage(BattleAttackInfo(from, to, 0, false));
	EXPECT_EQ(preview.damage.min, native.damage.min);
	EXPECT_EQ(to->intrinsicPhysicalDamageReductionPercent(), 20);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = physicalSource->unitId();
	gameHandler->sendAndApply(active);
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(physicalSource, target->getPosition(), physicalSource->getPosition())));
	EXPECT_EQ(health - target->getAvailableHealth(), native.damage.min);
}

TEST_F(NewHorizonsNecromancyCombatTest, GraveplateSnapshotVisibilityAndCacheFollowControllerAndForm)
{
	useGraveplate = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = forecast();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto target = child->getForUpdate(skeleton->unitId());
	ASSERT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 20);
	DamageCache baseline;
	baseline.buildDamageCache(parent, BattleSide::DEFENDER);
	DamageCache current(&baseline);
	current.buildDamageCache(child, BattleSide::DEFENDER);
	const auto * from = child->battleGetUnitByID(physicalSource->unitId());
	target->beginBattleForm(creatureByName("core:zombie"), 2);
	EXPECT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 0);
	EXPECT_EQ(current.getDamage(from, target.get(), child),
		child->battleExpectedLuckDamage(BattleAttackInfo(from, target.get(), 0, false)));
	target->endBattleForm();
	EXPECT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 20);
	target->addUnitBonus(std::vector<Bonus>{*marker(BonusType::HYPNOTIZED)});
	EXPECT_EQ(target->intrinsicPhysicalDamageReductionPercent(), 0);
	EXPECT_EQ(sibling->getForUpdate(skeleton->unitId())->intrinsicPhysicalDamageReductionPercent(), 20);
	EXPECT_EQ(skeleton->intrinsicPhysicalDamageReductionPercent(), 20);
	auto observer = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	observer->onBattleStarted(battle());
	auto hidden = std::make_shared<HypotheticBattle>(environment.get(), observer->getBattle(BattleID(0)));
	EXPECT_EQ(hidden->battleGetUnitByID(skeleton->unitId())->intrinsicPhysicalDamageReductionPercent(), 0);
}
