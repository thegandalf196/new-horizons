/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/SpellEffectPreview.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/battle/NewHorizonsSoulChain.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/spells/NewHorizonsDivineRetribution.h"
#include "../../../lib/combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/SetElementalAttunementState.h"

namespace attunement = newHorizonsElementalAttunement;

class ElementalAttunementCombatTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(const char * basic = nullptr, const char * advanced = nullptr, const char * expert = nullptr,
		int finalRank = 3, int rawPower = 48)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, rawPower, ChangeValueMode::ABSOLUTE);
		const SecondarySkill skill(SecondarySkill::decode(attunement::SKILL_ID));
		attackerSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
		if(basic) attackerSideHero->applyPerkSelection({attunement::SKILL_ID, std::string(attunement::SKILL_ID) + "." + basic});
		if(advanced)
		{
			attackerSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({attunement::SKILL_ID, std::string(attunement::SKILL_ID) + "." + advanced});
		}
		if(expert)
		{
			attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({attunement::SKILL_ID, std::string(attunement::SKILL_ID) + "." + expert});
		}
		attackerSideHero->setSecSkillLevel(skill, finalRank, ChangeValueMode::ABSOLUTE);
		startBattle();
	}
};

TEST_F(ElementalAttunementCombatTest, CorrespondenceIncludesOnlyEightElementalsAndTwoBirds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto [name, bit] : {std::pair{"core:airElemental", 1}, {"core:stormElemental", 1},
		{"core:fireElemental", 2}, {"core:energyElemental", 2}, {"core:waterElemental", 4},
		{"core:iceElemental", 4}, {"core:earthElemental", 8}, {"core:magmaElemental", 8}})
	{
		const auto type = creatureByName(name);
		EXPECT_EQ(attunement::creatureFamilyMask(type), bit);
		for(const uint8_t mask : {1, 2, 4, 8})
			EXPECT_EQ(attunement::passivesForCreature({.rank=3}, mask, type).attuned, mask == bit);
	}
	for(const auto * name : {"core:firebird", "core:phoenix"})
		for(const uint8_t mask : {1, 2, 4, 8, 15})
		{
			const auto output = attunement::passivesForCreature({.rank=3}, mask, creatureByName(name));
			EXPECT_TRUE(output.attuned);
			EXPECT_EQ(output.meleeDamagePercent, 40);
		}
	for(const auto * name : {"core:pikeman", "core:psychicElemental", "core:magicElemental"})
		EXPECT_FALSE(attunement::eligibleCreature(creatureByName(name)));
}

TEST_F(ElementalAttunementCombatTest, RankAndAttackPerksAddWithoutCrossingAttackCategories)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto type = creatureByName("core:fireElemental");
	for(int rank = 1; rank <= 3; ++rank)
	{
		attunement::Profile profile;
		profile.rank = rank;
		EXPECT_EQ(attunement::passivesForCreature(profile, 0, type).meleeDamagePercent, 0);
		EXPECT_EQ(attunement::passivesForCreature(profile, 2, type).meleeDamagePercent, 10+10*rank);
		profile.magmaWeapons = true;
		auto result = attunement::passivesForCreature(profile, 2, type);
		EXPECT_EQ(result.meleeDamagePercent, 30+10*rank);
		EXPECT_EQ(result.rangedDamagePercent, 10+10*rank);
		profile.magmaWeapons = false;
		profile.stormBolts = true;
		result = attunement::passivesForCreature(profile, 2, type);
		EXPECT_EQ(result.meleeDamagePercent, 10+10*rank);
		EXPECT_EQ(result.rangedDamagePercent, 30+10*rank);
	}
}

TEST_F(ElementalAttunementCombatTest, CurrentTagsIncludeBeneficialSpellsButExcludeArcaneAndChainArea)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & rules = battle()->getMagicRules();
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::FIREBALL), 2);
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::FROST_RING), 4);
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::LIGHTNING_BOLT), 1);
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::STONE_SKIN), 8);
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::MAGIC_ARROW), 0);
	EXPECT_EQ(attunement::spellElementMask(rules, SpellID::ARMAGEDDON), 0);
	EXPECT_FALSE(attunement::elementalAreaDamage(rules, SpellID::CHAIN_LIGHTNING));
	for(const auto spell : {SpellID::FIREBALL, SpellID::FROST_RING, SpellID::INFERNO, SpellID::METEOR_SHOWER})
		EXPECT_TRUE(attunement::elementalAreaDamage(rules, spell));
}

TEST_F(ElementalAttunementCombatTest, PrimalVigilanceAddsAllowanceWithoutRefreshingConsumedRetaliations)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalAttunement", "rebirthChain"));
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(80), 10);
	ASSERT_EQ(unit->counterAttacks.available(), 1);
	unit->counterAttacks.use();
	EXPECT_EQ(unit->counterAttacks.available(), 0);
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	EXPECT_EQ(unit->counterAttacks.available(), 1);
	unit->counterAttacks.use();
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	EXPECT_EQ(unit->counterAttacks.available(), 0);
	unit->counterAttacks.reset();
	EXPECT_EQ(unit->counterAttacks.available(), 2);
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(unit->counterAttacks.isLimited());
}

TEST_F(ElementalAttunementCombatTest, NewArrivalsInheritFlagsAndWaterVeilIsIndependentSpellMitigation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("primalBurst"));
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	auto * fire = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(80), 10);
	auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(82), 10);
	EXPECT_TRUE(fire->elementalAttunementPassives().attuned);
	EXPECT_FALSE(water->elementalAttunementPassives().attuned);
	EXPECT_EQ(fire->elementalAttunementPassives().spellReductionBasisPoints, 2000);
	EXPECT_EQ(water->elementalAttunementPassives().spellReductionBasisPoints, 0);
	const auto sources = spells::collectMagicalDamageReductionSources(*SpellID(SpellID::MAGIC_ARROW).toSpell(), fire, 0, 0, true);
	EXPECT_NE(std::find(sources.begin(), sources.end(), 2000), sources.end());
	EXPECT_EQ(spells::calculateMagicalDamageReductionBasisPoints(100, {6000, 2000}, {}).damageWithPenetration, 32);
}

TEST_F(ElementalAttunementCombatTest, GreaterEssenceAndPrimordialHostAffectEligibleFamiliesWithoutAttunement)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalAttunement", "greaterEssence", "phoenixSpark"));
	const auto fire = creatureByName("core:fireElemental");
	newHorizonsHeroes::LeadershipSlotCapacity eligible{100, 10, 10};
	attunement::applyLeadershipPolicy(eligible, *attackerSideHero, fire);
	EXPECT_EQ(eligible.requirement, 8);
	EXPECT_EQ(eligible.maximum, 12);
	newHorizonsHeroes::LeadershipSlotCapacity unrelated{100, 10, 10};
	attunement::applyLeadershipPolicy(unrelated, *attackerSideHero, creatureByName("core:pikeman"));
	EXPECT_EQ(unrelated.requirement, 10);
	auto * unit = addStack(BattleSide::ATTACKER, fire, BattleHex(80), 10);
	EXPECT_FALSE(unit->elementalAttunementPassives().attuned);
	EXPECT_EQ(unit->elementalAttunementPassives().maximumHealthPercent, 25);
	EXPECT_EQ(unit->getCount(), 10);
	const auto maximum = unit->getMaxHealth();
	EXPECT_EQ(maximum, attunement::maximumHealth(*unit, 1));
	EXPECT_EQ(unit->getAvailableHealth(), int64_t(maximum) * 10);
}

TEST_F(ElementalAttunementCombatTest, ConvergenceIncreasesActualPowerOnceAndStoneboundUsesItsFinalValue)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", "adaptiveElement", "perfectConvergence"));
	for(int i = 0; i < 5; ++i)
		EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 60);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 10);
	EXPECT_EQ(ordinary->elementalAttunementPassives().physicalReductionBasisPoints, 1500);
	EXPECT_FALSE(ordinary->elementalAttunementPassives().attuned);
	const auto profile = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::MAGIC_ARROW);
	EXPECT_EQ(profile.factionSpellDamageBonus, 15);
	EXPECT_FALSE(newHorizonsElementalRebirth::activeProfile(attackerSideHero));
}

TEST_F(ElementalAttunementCombatTest, ConduitAndCataclysmAreAdditiveWithoutRebornStacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", "elementalCataclysm"));
	const auto fireball = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::FIREBALL);
	const auto arrow = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::MAGIC_ARROW);
	const auto chain = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, SpellID::CHAIN_LIGHTNING);
	EXPECT_EQ(fireball.factionSpellDamageBonus, 40);
	EXPECT_EQ(arrow.factionSpellDamageBonus, 15);
	EXPECT_EQ(chain.factionSpellDamageBonus, 15);
	const auto captured = battle::ProjectedUnitEnvironmentState::capture(*battle());
	EXPECT_EQ(captured.inputs(BattleSide::ATTACKER).elementalAttunementProfile, battle()->getElementalAttunementProfile(BattleSide::ATTACKER));
	EXPECT_EQ(captured.state(BattleSide::ATTACKER).elementalAttunementMask, 0);
}

TEST_F(ElementalAttunementCombatTest, AcceptedManualHeroCastActivatesAfterEffectsAndRejectedCastDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	auto * fire = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(80), 100);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 100);
	beginCombat();
	ASSERT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::FIREBALL, victim));
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 2);
	EXPECT_TRUE(fire->elementalAttunementPassives().attuned);
	const auto before = battle()->getElementalAttunementMask(BattleSide::ATTACKER);
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::FIREBALL, victim));
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), before);
}

TEST_F(ElementalAttunementCombatTest, CompletionPacketRejectsStaleAndWrongElementWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetElementalAttunementState completion;
	completion.battleID = BattleID(0);
	completion.side = BattleSide::ATTACKER;
	completion.spellID = SpellID::FIREBALL;
	completion.beforeMask = 0;
	completion.afterMask = 4;
	EXPECT_THROW(gameHandler->sendAndApply(completion), std::runtime_error);
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
	completion.afterMask = 2;
	ASSERT_NO_THROW(gameHandler->sendAndApply(completion));
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 2);
	EXPECT_THROW(gameHandler->sendAndApply(completion), std::runtime_error);
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 2);
}

TEST_F(ElementalAttunementCombatTest, OpponentObserverCannotReadHiddenProfileThroughEnvironment)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalAttunement", "adaptiveElement"));
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(80), 10);
	CPlayerBattleCallback owner(battle(), attackerSideHero->getOwner());
	CPlayerBattleCallback opponent(battle(), defenderSideHero->getOwner());
	const battle::LiveUnitEnvironmentContext authority(*battle());
	const battle::LiveUnitEnvironmentContext ownView(owner);
	const battle::LiveUnitEnvironmentContext enemyView(opponent);
	EXPECT_EQ(ownView.elementalAttunementProfile(BattleSide::ATTACKER),
		authority.elementalAttunementProfile(BattleSide::ATTACKER));
	EXPECT_EQ(enemyView.elementalAttunementProfile(BattleSide::ATTACKER), attunement::Profile{});
	EXPECT_EQ(battle::UnitEnvironmentQueries(ownView).unitElementalAttunement(unit).meleeDamagePercent, 60);
	const auto nativeFacts = battle::UnitEnvironmentQueries(authority).unitElementalAttunement(unit);
	EXPECT_EQ(battle::UnitEnvironmentQueries(enemyView).unitElementalAttunement(unit), nativeFacts);
	EXPECT_EQ(nativeFacts.meleeDamagePercent, 60);
	EXPECT_EQ(nativeFacts.physicalReductionBasisPoints, 1200);
	// An ID is not authority to observe another occurrence's hidden policy.
	const auto foreign = unit->acquireState();
	ASSERT_EQ(foreign->unitId(), unit->unitId());
	EXPECT_THROW(enemyView.committedElementalAttunement(foreign.get()), std::invalid_argument);
	const auto captured = battle::ProjectedUnitEnvironmentState::capture(*battle());
	EXPECT_EQ(captured.inputs(BattleSide::ATTACKER).elementalAttunementProfile,
		authority.elementalAttunementProfile(BattleSide::ATTACKER));
}

TEST_F(ElementalAttunementCombatTest, ConduitBookDamageAgreesWithNativeRecipientPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", nullptr, nullptr, 3, 200));
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 100);
	const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *arrow, battle());
	ASSERT_TRUE(preview.primary);
	EXPECT_EQ(preview.primary->value, 253); // (20 + 200) * 1.15, once.
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, arrow);
	const auto mechanics = arrow->battleMechanics(&event);
	EXPECT_EQ(mechanics->adjustEffectValue(target), preview.primary->value);
}

TEST_F(ElementalAttunementCombatTest, PhysicalHeroPoisonCapturesConduitButCreaturePoisonDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", nullptr, nullptr, 3, 0));
	const SpellID heroPoisonSpell(SpellID::decode("new-horizons:poison"));
	ASSERT_TRUE(heroPoisonSpell.hasValue());
	const auto & schools = battle()->getMagicRules()["spells"]["new-horizons:poison"]["schools"].Vector();
	ASSERT_EQ(schools.size(), 1u);
	ASSERT_EQ(schools.front().String(), "new-horizons:nature");
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(heroPoisonSpell);
	setTestSpellPointTotal(attackerSideHero, 1000);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(82), 1000);
	beginCombat();
	ASSERT_TRUE(newHorizonsMagic::physicalPoisonEnabled(battle()->getMagicRules(), heroPoisonSpell));
	ASSERT_FALSE(newHorizonsMagic::physicalPoisonEnabled(battle()->getMagicRules(), SpellID::POISON));
	ASSERT_TRUE(castOn(attackerSideHero, heroPoisonSpell, target));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 23); // Fixed20 and Conduit15%; no SP.
	auto heroPoison = target->acquireState();
	for(const int64_t expected : {23, 34, 46})
	{
		EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(heroPoison.get()), expected);
		newHorizonsBulwark::advancePhysicalPoison(heroPoison.get());
	}
	EXPECT_EQ(heroPoison->physicalPoisonBaseDamage, 0);
	auto creaturePoison = target->acquireState();
	newHorizonsBulwark::clearPhysicalPoison(creaturePoison.get());
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(creaturePoison.get(), 20, 7));
	EXPECT_EQ(creaturePoison->physicalPoisonSpellOrigin(), BattleSide::NONE);
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(creaturePoison.get()), 20);
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
}

TEST_F(ElementalAttunementCombatTest, PlagueCapturesConduitInItsStoredTickWithoutRepeatedScaling)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", nullptr, nullptr, 3, 0));
	const SpellID plague(SpellID::decode("new-horizons:plague"));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(plague);
	setTestSpellPointTotal(attackerSideHero, 1000);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 1000);
	beginCombat();
	ASSERT_TRUE(castOn(attackerSideHero, plague, target));
	const auto markers = target->getBonuses(newHorizonsPlague::markerSelector(battle()->getMagicRules()));
	ASSERT_EQ(markers->size(), 1);
	const auto marker = markers->front();
	EXPECT_EQ(marker->val, 28); // Fixed25, then one Conduit15% floor.
	ASSERT_TRUE(marker->parameters);
	const auto captured = marker->parameters->toJsonNode()["mdrPenetration"];
	EXPECT_EQ(captured["factionSpellDamageBonusPercent"].Integer(), 15);
	EXPECT_TRUE(captured["factionSpellDamageCapturedInMagnitude"].Bool());
	for(int tick = 0; tick < 3; ++tick)
		EXPECT_EQ(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER,
			target, marker->val, &captured), 28);
}

TEST_F(ElementalAttunementCombatTest, SoulChainUsesSavedOffenseOnceAlongsideIndependentMitigation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit"));
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 1000);
	JsonNode captured;
	captured["factionSpellDamageBonusPercent"].Integer() = 15;
	EXPECT_EQ(newHorizonsSoulChain::adjustedEchoDamage(*battle(), BattleSide::ATTACKER,
		target, 1000, 2000, &captured), 230);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(newHorizonsSoulChain::adjustedEchoDamage(*battle(), BattleSide::ATTACKER,
		target, 1000, 2000, &captured), 115);
	EXPECT_EQ(newHorizonsSoulChain::adjustedEchoDamage(*battle(), BattleSide::ATTACKER,
		target, 1000, 2000, nullptr), 100);
}

TEST_F(ElementalAttunementCombatTest, TriggeredHeroStatusesUseTheirOwnCaptureInsteadOfCurrentCreatureArmyPerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit"));
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 1000);
	JsonNode parameters;
	parameters["mdrPenetration"]["factionSpellDamageBonusPercent"].Integer() = 15;
	CombatEventPayload payload;
	const nativeEvents::Context heroEvent{*gameHandler->spellEnv, *battle(), source, target, parameters, payload};
	for(const auto * key : {"new-horizons:hexOfPain", "new-horizons:shadowGift", "core:fireShield"})
	{
		const auto * spell = SpellID(SpellID::decode(key)).toSpell();
		ASSERT_NE(spell, nullptr);
		EXPECT_EQ(nativeEvents::adjustDamage(heroEvent, *spell, *source, *target, 100), 115);
	}
	JsonNode absentCapture;
	const nativeEvents::Context creatureEvent{*gameHandler->spellEnv, *battle(), source, target, absentCapture, payload};
	const auto * fireShield = SpellID(SpellID::FIRE_SHIELD).toSpell();
	EXPECT_EQ(nativeEvents::adjustDamage(creatureEvent, *fireShield, *source, *target, 100), 100);
}

TEST_F(ElementalAttunementCombatTest, DivineRetributionKeepsOffenseInItsSavedProtectionAndDelayedPayout)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", nullptr, nullptr, 3, 0));
	const SpellID divine(SpellID::decode("new-horizons:divineRetribution"));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(divine);
	setTestSpellPointTotal(attackerSideHero, 1000);
	auto * protectedUnit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 1000);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 1000);
	beginCombat();
	ASSERT_TRUE(castOn(attackerSideHero, divine, protectedUnit));
	const auto protections = protectedUnit->getBonusesOfType(BonusType::DIVINE_RETRIBUTION);
	ASSERT_EQ(protections->size(), 1);
	Bonus original(*protections->front());
	ASSERT_TRUE(original.parameters);
	CMemorySerializer memory;
	memory.oser & original;
	Bonus restored;
	memory.iser & restored;
	ASSERT_TRUE(restored.parameters);
	const auto captured = restored.parameters->toJsonNode()["mdrPenetration"];
	EXPECT_EQ(newHorizonsMetamagic::capturedOffensiveProfile(&captured).factionSpellDamageBonus, 15);
	EXPECT_EQ(newHorizonsDivineRetribution::recipientDamage(*battle(), target, 100,
		attackerSideHero->getOwner(), &captured), 115);
	EXPECT_EQ(newHorizonsDivineRetribution::recipientDamage(*battle(), target, 100,
		attackerSideHero->getOwner(), nullptr), 100);
	EXPECT_EQ(battle()->getElementalAttunementMask(BattleSide::ATTACKER), 0);
}

TEST_F(ElementalAttunementCombatTest, AuthoritativeControlPacketsPreserveOriginalArmyHealthAndUsedRetaliations)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", "greaterEssence"));
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 8);
	const auto type = creatureByName("core:earthElemental");
	const int baseHealth = type.toCreature()->getBaseHitPoints();
	ASSERT_GT(baseHealth, 7);
	int position = 80;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		for(const bool wounded : {false, true})
		{
			auto * unit = addStack(side, type, BattleHex(position), 10);
			position += 2;
			const bool ownArmy = side == BattleSide::ATTACKER;
			const int maximum = ownArmy ? baseHealth * 125 / 100 : baseHealth;
			const int wound = wounded ? 7 : 0;
			if(wounded)
			{
				int64_t amount = wound;
				unit->damage(amount);
				unit->counterAttacks.use();
			}
			// These expectations come from the accepted rule and original health
			// ledger, not another observer executing the same implementation.
			const auto requireUnchanged = [&]()
			{
				EXPECT_EQ(unit->unitSide(), side);
				EXPECT_EQ(unit->getMaxHealth(), maximum);
				EXPECT_EQ(unit->getCount(), 10);
				EXPECT_EQ(unit->getFirstHPleft(), maximum - wound);
				EXPECT_EQ(unit->getAvailableHealth(), 10LL * maximum - wound);
				EXPECT_EQ(unit->counterAttacks.total(), 1);
				EXPECT_EQ(unit->counterAttacks.total() - unit->counterAttacks.available(), wounded ? 1 : 0);
				EXPECT_EQ(unit->elementalAttunementPassives().meleeDamagePercent, ownArmy ? 40 : 0);
			};
			requireUnchanged();
			const auto ordinaryOwner = unit->unitOwner();
			const auto controller = ownArmy ? defenderSideHero->getOwner() : attackerSideHero->getOwner();
			const auto puppet = newHorizonsPuppetMaster::controlMarker(
				SpellID(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID))), controller);
			for(const auto & control : {Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
				BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE))), puppet})
			{
				SetStackEffect apply;
				apply.battleID = BattleID(0);
				apply.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{control});
				ASSERT_NO_THROW(gameHandler->sendAndApply(apply));
				EXPECT_EQ(battle()->battleGetActionController(unit), controller);
				EXPECT_EQ(battle()->battleGetOwner(unit), control.type == BonusType::HYPNOTIZED ? controller : ordinaryOwner);
				requireUnchanged();
				SetStackEffect remove;
				remove.battleID = BattleID(0);
				remove.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{control});
				ASSERT_NO_THROW(gameHandler->sendAndApply(remove));
				EXPECT_EQ(battle()->battleGetActionController(unit), ordinaryOwner);
				requireUnchanged();
			}
		}
}

TEST_F(ElementalAttunementCombatTest, AuthoritativeHypnotizePreservesOriginalArmiesAdditionalRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("elementalConduit", "rebirthChain"));
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(80), 10);
	unit->counterAttacks.use();
	const auto requireOneRemaining = [&]()
	{
		EXPECT_EQ(unit->elementalAttunementPassives().additionalRetaliations, 1);
		EXPECT_EQ(unit->counterAttacks.total(), 2);
		EXPECT_EQ(unit->counterAttacks.available(), 1);
	};
	requireOneRemaining();
	const Bonus control(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	SetStackEffect apply;
	apply.battleID = BattleID(0);
	apply.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{control});
	ASSERT_NO_THROW(gameHandler->sendAndApply(apply));
	EXPECT_EQ(battle()->battleGetActionController(unit), defenderSideHero->getOwner());
	requireOneRemaining();
	SetStackEffect remove;
	remove.battleID = BattleID(0);
	remove.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{control});
	ASSERT_NO_THROW(gameHandler->sendAndApply(remove));
	EXPECT_EQ(battle()->battleGetActionController(unit), attackerSideHero->getOwner());
	requireOneRemaining();
}
