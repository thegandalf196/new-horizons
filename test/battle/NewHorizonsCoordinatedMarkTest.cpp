/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsCoordinatedMarkFixture.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../server/battles/BattleStartSnapshotFixture.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include <cmath>
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/MeleeReturnDestination.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../server/battles/BattlePrimaryStrikeAftermath.h"
namespace
{
namespace marks = newHorizonsCoordinatedMarks;
class NewHorizonsCoordinatedMarkTest : public NewHorizonsCoordinatedMarkFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	bool uncappedProtection = false;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsCoordinatedMarkFixture::mapLoaded(map);
		if(uncappedProtection)
		{
			JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
			magic.Struct().erase("physicalDamageReductionCapPercent");
			magic.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
	}
	void prepare(const char * hero, bool upgraded = false)
	{
		NewHorizonsCoordinatedMarkFixture::prepare(hero, GetParam(), upgraded);
	}
	void publishShot()
	{
		const BattleAttackInfo attack(actor, victim, 0, true);
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), attack, 1));
	}
	void publishReturn()
	{
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), marks::completedReturn(*battle(), *actor, *victim, true));
	}
	void controlledDefenseConsumer(bool frenzy)
	{
		prepare("core:ingham"); auto * friendUnit = ally();
		const int defense = frenzy ? 29 : 19;
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		{
			friendUnit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK), bound));
			friendUnit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 8, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound));
			victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, defense, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				friendUnit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 10, BonusSourceID(), subtype, bound));
		}
		if(frenzy) friendUnit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::IN_FRENZY, BonusSource::OTHER, 100, BonusSourceID()));
		ASSERT_EQ(friendUnit->getCount(), 10); ASSERT_EQ(friendUnit->getAttack(false), 10);
		ASSERT_EQ(friendUnit->getDefense(false), 8); ASSERT_EQ(victim->getDefense(false), defense);
		const double factor = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR);
		const double cap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR_CAP);
		const int attackRating = frenzy ? 18 : 10;
		const auto expected = [&](int ignored)
		{
			return static_cast<int64_t>(std::max(1.0, std::floor(100 * (1 - std::min(factor * (defense - ignored - attackRating), cap)))));
		};
		const BattleAttackInfo attack(friendUnit, victim, 0, false);
		const auto baseline = battle()->calculateDmgRange(attack);
		EXPECT_EQ(baseline.damage.min, expected(0)); EXPECT_EQ(baseline.damage.max, expected(0));
		publishShot(); const auto marked = battle()->calculateDmgRange(attack);
		const int ignored = GetParam() == 0 ? defense * 25 / 100 : 0;
		EXPECT_EQ(ignored, GetParam() == 0 ? (frenzy ? 7 : 4) : 0);
		EXPECT_EQ(marked.damage.min, expected(ignored)); EXPECT_EQ(marked.damage.max, expected(ignored));
		EXPECT_EQ(marked.damageBeforeDefense.min, 100); EXPECT_EQ(marked.damageBeforeDefense.max, 100);
		EXPECT_EQ(friendUnit->getAttack(false), 10); EXPECT_EQ(friendUnit->getDefense(false), 8);
		EXPECT_EQ(victim->getDefense(false), defense);
	}
	void protectionConsumer(bool capped, bool shooting)
	{
		uncappedProtection = !capped;
		prepare("core:lorelei");
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(battle()->getMagicRules()));
		EXPECT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(battle()->getMagicRules()) >= 0, capped);
		auto * next = addStack(BattleSide::ATTACKER, creatureByName(shooting ? "core:archer" : "core:swordsman"), BattleHex(9, 5), 10);
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		{
			next->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK), bound));
			victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				next->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 100, BonusSourceID(), subtype, bound));
		}
		const BattleAttackInfo hit(next, victim, 0, shooting);
		ASSERT_TRUE(hit.physicalDamage);
		const auto raw = battle()->calculateDmgRange(hit);
		ASSERT_EQ(raw.damage.min, raw.damage.max);
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::OTHER, 50, BonusSourceID(), BonusCustomSubtype::damageTypeAll));
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::SPELL_EFFECT, 20, BonusSourceID(SpellID(shooting ? SpellID::AIR_SHIELD : SpellID::SHIELD)),
			shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee));
		const auto protectedHit = battle()->calculateDmgRange(hit);
		EXPECT_EQ(protectedHit.damage.min, static_cast<int64_t>(std::floor(raw.damage.min * .4)));
		publishReturn(); ASSERT_EQ(marks::hasMark(*victim), GetParam() == 0);
		const auto marked = battle()->calculateDmgRange(hit);
		const double fraction = GetParam() == 0 ? .52 : .4;
		EXPECT_EQ(marked.damage.min, static_cast<int64_t>(std::floor(raw.damage.min * fraction)));
		EXPECT_EQ(marked.damage.max, marked.damage.min);
		EXPECT_EQ(marked.damageBeforeDefense.min, raw.damageBeforeDefense.min);
		EXPECT_EQ(victim->getDefense(false), 10);
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), hit, 1));
		EXPECT_FALSE(marks::hasMark(*victim));
		EXPECT_EQ(battle()->calculateDmgRange(hit).damage.min, protectedHit.damage.min);
	}
	void mixedLegacyPayload(int existingIgnore)
	{
		prepare("core:lorelei"); auto * next = ally();
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				next->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 100, BonusSourceID(), subtype, bound));
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::OTHER, 50, BonusSourceID(), BonusCustomSubtype::damageTypeAll));
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::SPELL_EFFECT, 20, BonusSourceID(SpellID(SpellID::SHIELD)), BonusCustomSubtype::damageTypeMelee));
		publishReturn();
		DamageAttackInfo payload;
		payload.attacker = next; payload.defender = victim;
		payload.attackerHex = next->getPosition(); payload.defenderHex = victim->getPosition();
		payload.intrinsicPhysicalDamageReductionPercent = 20;
		payload.runicEnduranceReductionPercent = 10; payload.ironHideReductionPercent = 30;
		payload.coordinatedPhysicalProtectionIgnorePercent = marks::physicalProtectionIgnorePercent(*battle(), BattleAttackInfo(next, victim, 0, false));
		// Exercise the transient calculator contract, without inventing a learned hero grant.
		payload.legacyPhysicalDamageReductionIgnorePercent = existingIgnore;
		payload.physicalDamageReductionIgnorePercent = std::min(100, existingIgnore + payload.coordinatedPhysicalProtectionIgnorePercent);
		const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator(); ASSERT_NE(calculator, nullptr);
		const auto actual = calculator->calculate(*battle(), payload);
		const double remaining = 1 - std::clamp(existingIgnore, 0, 100) / 100.0;
		const double protectedFraction = .4 * (1 - .2 * remaining) * (1 - .1 * remaining) * (1 - .3 * remaining);
		const double lorelei = payload.coordinatedPhysicalProtectionIgnorePercent / 100.0;
		const auto expected = static_cast<int64_t>(std::floor(1000 * (1 - (1 - protectedFraction) * (1 - lorelei))));
		EXPECT_EQ(actual.damage.min, expected); EXPECT_EQ(actual.damage.max, expected);
		EXPECT_EQ(actual.damageBeforeDefense.min, 1000);
		EXPECT_EQ(payload.physicalDamageReductionIgnorePercent, std::min(100, existingIgnore + (GetParam() == 0 ? 20 : 0)));
	}
	CStack * ally() { return addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(9, 5), 10); }
};
}
TEST_P(NewHorizonsCoordinatedMarkTest, ActualMonkShotPublishesOneCapturedSideMark)
{
	prepare("core:ingham"); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	const auto effects = victim->getUnstackedBonuses(CSelector(marks::isMark));
	ASSERT_EQ(effects->size(), GetParam() == 0 ? 1u : 0u);
	if(!effects->empty())
	{
		EXPECT_EQ(effects->front()->val, 25); EXPECT_EQ(effects->front()->turnsRemain, 1);
		EXPECT_EQ(effects->front()->parameters->toVector(), (std::vector<int32_t>{0, static_cast<int32_t>(actor->unitId())}));
	}
}
TEST_P(NewHorizonsCoordinatedMarkTest, ZealotShotRefreshesOneMarkWithoutStacking)
{
	prepare("core:ingham", true); publishShot(); publishShot();
	EXPECT_EQ(victim->getUnstackedBonuses(CSelector(marks::isMark))->size(), GetParam() == 0 ? 1u : 0u);
}
TEST_P(NewHorizonsCoordinatedMarkTest, ConsecrationIsCreatureDefensePenetrationAndOnlyNextAlliedMelee)
{
	prepare("core:ingham"); auto * friendUnit = ally(); publishShot();
	BattleAttackInfo melee(friendUnit, victim, 0, false);
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), melee), GetParam() == 0 ? 25 : 0);
	EXPECT_EQ(marks::physicalProtectionIgnorePercent(*battle(), melee), 0);
	auto shot = melee; shot.shooting = true;
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), shot), 0);
	EXPECT_TRUE(marks::consumed(*battle(), shot).empty());
	melee.retaliation = true;
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), melee), GetParam() == 0 ? 25 : 0);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), melee, 1));
	EXPECT_FALSE(marks::hasMark(*victim));
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), melee), 0);
}
TEST_P(NewHorizonsCoordinatedMarkTest, AcceptedZeroDamageConsumesButCollateralOrMagicalHitsDoNot)
{
	prepare("core:ingham"); auto * friendUnit = ally(); publishShot();
	BattleAttackInfo attack(friendUnit, victim, 0, false);
	for(int exclusion = 0; exclusion < 4; ++exclusion)
	{
		auto excluded = attack;
		if(exclusion == 0) excluded.secondaryAttack = true;
		if(exclusion == 1) excluded.physicalDamage = false;
		if(exclusion == 2) excluded.bracePreemptive = true;
		if(exclusion == 3) excluded.cleaveDamagePercent = 50;
		EXPECT_TRUE(marks::consumed(*battle(), excluded).empty());
	}
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), attack, 0));
	EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkTest, DifferentEnemyArmyCannotUseCapturedMark)
{
	prepare("core:ingham"); publishShot();
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:swordsman"), BattleHex(9, 5), 10);
	EXPECT_TRUE(marks::consumed(*battle(), BattleAttackInfo(enemy, victim, 0, false)).empty());
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), BattleAttackInfo(enemy, victim, 0, false)), 0);
}
TEST_P(NewHorizonsCoordinatedMarkTest, ActualHarpyAttackAndReturnMarksOnlyAfterReturning)
{
	prepare("core:lorelei"); const auto origin = actor->getPosition(); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(4, 5), true)));
	ASSERT_TRUE(actor->alive()); ASSERT_TRUE(victim->alive()); EXPECT_EQ(actor->getPosition(), origin);
	EXPECT_EQ(marks::hasMark(*victim), GetParam() == 0);
	EXPECT_EQ(observed.sourcePositionsAtPublication.size(), GetParam() == 0 ? 1u : 0u);
	if(!observed.sourcePositionsAtPublication.empty()) EXPECT_EQ(observed.sourcePositionsAtPublication.front(), origin);
}
TEST_P(NewHorizonsCoordinatedMarkTest, HarpyHagNoReturnActionDoesNotMark)
{
	prepare("core:lorelei", true); beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(4, 5), false)));
	EXPECT_FALSE(marks::hasMark(*victim)); EXPECT_TRUE(observed.sourcePositionsAtPublication.empty());
}
TEST_P(NewHorizonsCoordinatedMarkTest, FeatheredFeintRequiresDifferentAttackerAndIgnoresProtectionOnly)
{
	prepare("core:lorelei"); auto * friendUnit = ally(); publishReturn();
	BattleAttackInfo own(actor, victim, 0, false), next(friendUnit, victim, 0, false);
	EXPECT_EQ(marks::physicalProtectionIgnorePercent(*battle(), own), 0);
	EXPECT_TRUE(marks::consumed(*battle(), own).empty());
	EXPECT_EQ(marks::physicalProtectionIgnorePercent(*battle(), next), GetParam() == 0 ? 20 : 0);
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), next), 0);
	next.shooting = true;
	EXPECT_EQ(marks::physicalProtectionIgnorePercent(*battle(), next), GetParam() == 0 ? 20 : 0);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), next, 1));
	EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkTest, IncompleteReturnDeadOrStoppedActorCannotMark)
{
	prepare("core:lorelei");
	EXPECT_TRUE(marks::completedReturn(*battle(), *actor, *victim, false).add.empty());
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(marks::completedReturn(*battle(), *actor, *victim, true).add.empty());
	actor->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	int64_t lethal = actor->getAvailableHealth(); actor->damage(lethal);
	EXPECT_TRUE(marks::completedReturn(*battle(), *actor, *victim, true).add.empty());
}
TEST_P(NewHorizonsCoordinatedMarkTest, MarksExpireAtRoundEndAndCurrentIdentityRoundtrips)
{
	prepare("core:ingham");
	beginCombat();
	ASSERT_GT(battle()->battleGetRound(), 0);
	publishShot();
	const auto policy = attackerSideHero->getPrimaryGrowthRules();
	const auto original = victim->getUnstackedBonuses(CSelector(marks::isMark))->toJsonNode();
	NativeBonusGraphAccess access;
	// CURRENT stays1151. Presence includes explicitzero and must refuse before bytes.
	CMemorySerializer current;
	ASSERT_EQ(current.oser.version, ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	if(GetParam() == 1)
		EXPECT_NO_THROW(current.oser & *gameState());
	else
	{
		EXPECT_THROW(current.oser & *gameState(), std::runtime_error);
		EXPECT_TRUE(current.extractBuffer().empty());
	}
	// The existing public world codec performs the normal preflight and load-tree reinit.
	// This explicit reserved-version test is not loadReplaySnapshot/write activation.
	constexpr auto version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	CMemorySerializer world;
	world.oser.version = version;
	ASSERT_NO_THROW(world.oser & *gameState());
	CGameState restored; restored.preInit(LIBRARY);
	world.iser.version = version; world.iser.loadingGamestate = true; world.iser.cb = &restored;
	ASSERT_NO_THROW(world.iser & restored);
	const auto * hero = restored.getHero(attackerSideHero->id); ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), policy);
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero), GetParam() != 1);
	// World snapshots omit battles; preserve the marker through the real descriptor codec.
	ASSERT_EQ(restored.getBattle(BattleID(0)), nullptr);
	CMemorySerializer descriptor;
	descriptor.oser.version = version;
	ASSERT_NO_THROW(battleStartFixture::writeDescriptors(*battle(), descriptor));
	descriptor.iser.version = version; descriptor.iser.cb = &restored;
	auto combat = std::make_unique<BattleInfo>(&restored);
	ASSERT_NO_THROW(descriptor.iser & *combat);
	const auto * loaded = combat->getStack(victim->unitId(), false); ASSERT_NE(loaded, nullptr);
	BonusList decoded;
	loaded->getExportedBonusList().getBonuses(decoded, CSelector(marks::isMark));
	EXPECT_EQ(decoded.toJsonNode(), original);
	ASSERT_NO_THROW(combat->localInit());
	EXPECT_EQ(marks::hasMark(*loaded), GetParam() == 0);
	EXPECT_EQ(loaded->getUnstackedBonuses(CSelector(marks::isMark))->toJsonNode(), original);
	battle()->nextRound(); EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkTest, ActualDamagePipelineAppliesRelativePhysicalProtectionIgnore)
{
	prepare("core:lorelei"); auto * friendUnit = ally();
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusCustomSubtype::damageTypeAll));
	const BattleAttackInfo attack(friendUnit, victim, 0, false);
	const auto baseline = battle()->calculateDmgRange(attack);
	publishReturn();
	const auto marked = battle()->calculateDmgRange(attack);
	EXPECT_EQ(marked.damageBeforeDefense.min, baseline.damageBeforeDefense.min);
	EXPECT_EQ(marked.damageBeforeDefense.max, baseline.damageBeforeDefense.max);
	if(GetParam() == 0) EXPECT_GT(marked.damage.min, baseline.damage.min);
	else EXPECT_EQ(marked.damage.min, baseline.damage.min);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), newHorizonsOrdinaryStrike::plan(*battle(), attack, 1));
	EXPECT_EQ(battle()->calculateDmgRange(attack).damage.min, baseline.damage.min);
}
TEST_P(NewHorizonsCoordinatedMarkTest, SpentMarkIsRemovedEvenIfItsAcceptedStrikeKilledTheVictim)
{
	prepare("core:ingham"); auto * friendUnit = ally(); publishShot();
	const auto prepared = newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(friendUnit, victim, 0, false), 1);
	int64_t lethal = victim->getAvailableHealth(); victim->damage(lethal); ASSERT_FALSE(victim->alive());
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), prepared);
	EXPECT_FALSE(marks::hasMark(*victim));
}
TEST_P(NewHorizonsCoordinatedMarkTest, PublishedBeneficiarySurvivesSourceControlChangeButAttackerMustStillBeAllied)
{
	prepare("core:ingham"); auto * friendUnit = ally(); publishShot();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	const BattleAttackInfo allied(friendUnit, victim, 0, false);
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), allied), GetParam() == 0 ? 25 : 0);
	EXPECT_FALSE(marks::consecratingShot(*battle(), BattleAttackInfo(actor, victim, 0, true)));
	friendUnit->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(
		SpellID(SpellID::decode("new-horizons:puppetMaster")), PlayerColor(1))));
	EXPECT_EQ(battle()->battleGetActionController(friendUnit), PlayerColor(1));
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), allied), GetParam() == 0 ? 25 : 0);
	friendUnit->removeBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	friendUnit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(marks::creatureDefenseIgnorePercent(*battle(), allied), 0);
	EXPECT_TRUE(marks::consumed(*battle(), allied).empty());
}
TEST_P(NewHorizonsCoordinatedMarkTest, CurrentCreatureFormControlsCreationWhileCapturedIdentityRemains)
{
	prepare("core:ingham");
	actor->beginBattleForm(creatureByName("core:peasant"), 1);
	EXPECT_FALSE(marks::consecratingShot(*battle(), BattleAttackInfo(actor, victim, 0, true)));
	EXPECT_EQ(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero), GetParam() != 1);
}
TEST_P(NewHorizonsCoordinatedMarkTest, CompletedHomeCommitStillMarksWhenItsLandingKillsTheHarpy)
{
	prepare("core:lorelei", true);
	addStack(BattleSide::ATTACKER, creatureByName("core:swordsman"), BattleHex(2, 3), 1);
	const auto origin = actor->getPosition(); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	int64_t wound = actor->getAvailableHealth() - 1; actor->damage(wound);
	addReturnMoat(origin);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(4, 5), true)));
	ASSERT_FALSE(actor->alive()); EXPECT_EQ(actor->getPosition(), origin);
	EXPECT_EQ(marks::hasMark(*victim), GetParam() == 0);
	if(GetParam() == 0)
	{
		ASSERT_EQ(observed.sourceAliveAtPublication.size(), 1u);
		EXPECT_FALSE(observed.sourceAliveAtPublication.front());
		EXPECT_EQ(observed.sourcePositionsAtPublication.front(), origin);
	}
}
TEST_P(NewHorizonsCoordinatedMarkTest, ConsecratedTargetRealDamageConsumerFloorsCreatureDefenseWithoutChangingStats)
{
	ASSERT_NO_FATAL_FAILURE(controlledDefenseConsumer(false));
}
TEST_P(NewHorizonsCoordinatedMarkTest, ConsecratedTargetDoesNotPenetrateFrenzyOwnDefenseTrade)
{
	ASSERT_NO_FATAL_FAILURE(controlledDefenseConsumer(true));
}
TEST_P(NewHorizonsCoordinatedMarkTest, HistoricalUncappedMeleePenetratesShieldAndGeneralProtectionOnce)
{
	ASSERT_NO_FATAL_FAILURE(protectionConsumer(false, false));
}
TEST_P(NewHorizonsCoordinatedMarkTest, HistoricalUncappedPhysicalShotPenetratesAirShieldAndGeneralProtectionOnce)
{
	ASSERT_NO_FATAL_FAILURE(protectionConsumer(false, true));
}
TEST_P(NewHorizonsCoordinatedMarkTest, CappedMeleeProtectionConsumerRetainsItsExistingResult)
{
	ASSERT_NO_FATAL_FAILURE(protectionConsumer(true, false));
}
TEST_P(NewHorizonsCoordinatedMarkTest, CappedPhysicalShotProtectionConsumerRetainsItsExistingResult)
{
	ASSERT_NO_FATAL_FAILURE(protectionConsumer(true, true));
}
TEST_P(NewHorizonsCoordinatedMarkTest, HistoricalRealLichMagicalShotDoesNotUseOrConsumeFeatheredFeint)
{
	uncappedProtection = true;
	prepare("core:lorelei");
	ASSERT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(battle()->getMagicRules()), -1);
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(battle()->getMagicRules()));
	auto * lich = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(9, 5), 10);
	const BattleAttackInfo shot(lich, victim, 0, true); ASSERT_FALSE(shot.physicalDamage);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusCustomSubtype::damageTypeAll));
	const auto before = battle()->calculateDmgRange(shot);
	publishReturn();
	const auto after = battle()->calculateDmgRange(shot);
	EXPECT_EQ(after.damage.min, before.damage.min); EXPECT_EQ(after.damage.max, before.damage.max);
	EXPECT_TRUE(marks::consumed(*battle(), shot).empty());
	EXPECT_EQ(marks::hasMark(*victim), GetParam() == 0);
}
TEST_P(NewHorizonsCoordinatedMarkTest, LegacyCalculatorPayloadKeepsExisting37ScopeBeforeNewMark20)
{
	ASSERT_NO_FATAL_FAILURE(mixedLegacyPayload(37));
}
TEST_P(NewHorizonsCoordinatedMarkTest, LegacyCalculatorPayloadKeepsExisting90IdentityWhenCappedSumOverflows)
{
	ASSERT_NO_FATAL_FAILURE(mixedLegacyPayload(90));
}
TEST_P(NewHorizonsCoordinatedMarkTest, LegacyCalculatorPayloadClampsExistingIgnoreWithoutLosingMarkScope)
{
	ASSERT_NO_FATAL_FAILURE(mixedLegacyPayload(137));
}
INSTANTIATE_TEST_SUITE_P(CurrentHistoricalZero, NewHorizonsCoordinatedMarkTest, ::testing::Values(0, 1, 2));
