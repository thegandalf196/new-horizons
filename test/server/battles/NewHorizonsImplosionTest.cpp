/*
 * NewHorizonsImplosionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsImplosion.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/battle/BattleDisplacementCause.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
const newHorizonsImplosion::Rules DEFAULT{1200, 10, 3000, 3, 1};

class NewHorizonsImplosionTest : public HeroCommandFixture
{
protected:
	bool legacy = false;
	bool lethalProfile = false;
	CStack * active = nullptr;
	CStack * primary = nullptr;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_TRUE(newHorizonsImplosion::hasRules(rules)) << "Requires the shipped captured Implosion profile";
		if(legacy)
			rules["spells"]["core:implosion"].Struct().erase("implosion");
		if(lethalProfile)
		{
			rules["spells"]["core:implosion"]["implosion"]["baseBasisPoints"].Integer() = 10000;
			rules["spells"]["core:implosion"]["implosion"]["maximumBasisPoints"].Integer() = 10000;
		}
		rules.setOverrideFlag(true);
		newHorizonsMagic::validateRules(rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int power = 0, MasteryLevel::Type rank = MasteryLevel::ADVANCED)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
			rank, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:griffin"), BattleHex(3, 5), 200);
		primary = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(10, 5), 1000);
		Bonus initiative(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
		active->addNewBonus(std::make_shared<Bonus>(initiative));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		ASSERT_FALSE(battle()->battleIsFinished().has_value());
	}

	void injure(CStack * unit, int64_t amount)
	{
		const auto before = unit->getAvailableHealth();
		auto state = unit->acquireState();
		state->damage(amount);
		UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(changed);
		ASSERT_EQ(unit->getAvailableHealth(), before - amount);
	}

	bool cast()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::IMPLOSION;
		action.aimToUnit(primary);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	int64_t expectedDamage() const
	{
		const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		return mechanics->adjustEffectValue(primary);
	}
};
}

TEST(NewHorizonsImplosionFormulaTest, CurrentHealthAndOneFinalFloor)
{
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 50, 10000, 0, 0), 1700);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 100, 10000, 0, 0), 2200);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 150, 10000, 0, 0), 2700);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 180, 10000, 0, 0), 3000);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 1000, 0, 10000, 0, 0), 120);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 500, 0, 10000, 0, 0), 60);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 999, 50, 11500, 0, 0), 177);
}

TEST(NewHorizonsImplosionFormulaTest, RankAndWarcastingScaleOnlyPowerTerm)
{
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 50, 10000, 0, 0), 1700);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 50, 14500, 0, 0), 1925);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 0, 14500, 200, 25), 1200);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 10000, 50, 14500, 100, 25), 3000);
}

TEST(NewHorizonsImplosionFormulaTest, CapAndHugeHealthAreOverflowSafe)
{
	const auto health = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, health, std::numeric_limits<int32_t>::max(),
		1000000, 200, 100), health / 10 * 3 + health % 10 * 3 / 10);
	EXPECT_EQ(newHorizonsImplosion::damage(DEFAULT, 1, 0, 10000, 0, 0), 0);
	EXPECT_THROW(newHorizonsImplosion::damage(DEFAULT, 100, -1, 10000, 0, 0), std::invalid_argument);
}

TEST_F(NewHorizonsImplosionTest, ActualPaidCastUsesCurrentAggregateHealthAndPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000));
	ASSERT_NO_FATAL_FAILURE(injure(primary, 200));
	ASSERT_EQ(primary->getAvailableHealth(), 800);
	const auto before = primary->getAvailableHealth();
	const auto predicted = expectedDamage();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto position = primary->getPosition();
	ASSERT_GT(predicted, 0);
	ASSERT_TRUE(cast());
	EXPECT_EQ(primary->getAvailableHealth(), before - predicted);
	EXPECT_EQ(primary->getPosition(), position);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsImplosionTest, ResolvedDamagePullsBothSidesButNotPrimaryOrOutsideRadius)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(8, 5), 10);
	auto * outside = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), 10);
	const auto outsidePosition = outside->getPosition();
	const auto primaryPosition = primary->getPosition();
	ASSERT_TRUE(cast());
	EXPECT_EQ(enemy->getPosition(), BattleHex(11, 5));
	EXPECT_EQ(friendly->getPosition(), BattleHex(9, 5));
	EXPECT_EQ(outside->getPosition(), outsidePosition);
	EXPECT_EQ(primary->getPosition(), primaryPosition);
	EXPECT_EQ(enemy->getAvailableHealth(), 10);
	EXPECT_EQ(friendly->getAvailableHealth(), 10);
	EXPECT_TRUE(server.attacks.empty());
}

TEST_F(NewHorizonsImplosionTest, ClosestFirstAndClockwiseFromNorthEastAreStable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto center = primary->getPosition();
	const auto ne = center.cloneInDirection(BattleHex::TOP_RIGHT).cloneInDirection(BattleHex::TOP_RIGHT);
	const auto east = center.cloneInDirection(BattleHex::RIGHT).cloneInDirection(BattleHex::RIGHT);
	auto * eastUnit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), east, 10);
	auto * neUnit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), ne, 10);
	auto * nearUnit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"),
		center.cloneInDirection(BattleHex::LEFT), 10);
	const auto order = newHorizonsImplosion::pullOrder(*battle(), primary->unitId(), center, 3);
	ASSERT_EQ(order.size(), 3u);
	EXPECT_EQ(order[0], nearUnit->unitId());
	EXPECT_EQ(order[1], neUnit->unitId());
	EXPECT_EQ(order[2], eastUnit->unitId());
	ASSERT_TRUE(cast());
	EXPECT_EQ(neUnit->getPosition(), center.cloneInDirection(BattleHex::TOP_RIGHT));
	EXPECT_EQ(eastUnit->getPosition(), center.cloneInDirection(BattleHex::RIGHT));
	EXPECT_EQ(nearUnit->getPosition(), center.cloneInDirection(BattleHex::LEFT));
}

TEST_F(NewHorizonsImplosionTest, BlockedCloserEndpointLeavesStackUnmoved)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(11, 5), 10);
	const auto before = unit->getPosition();
	ASSERT_FALSE(newHorizonsImplosion::pullDestination(*battle(), *unit, primary->getPosition()));
	ASSERT_TRUE(cast());
	EXPECT_EQ(unit->getPosition(), before);
}

TEST_F(NewHorizonsImplosionTest, DoubleWideRadiusAndDestinationUseWholeFootprint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// A defender's tail is to the right: head is outside radius3 while its tail
	// is inside. A single-hex head-only radius check would wrongly exclude it.
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:cavalier"), BattleHex(6, 5), 10);
	ASSERT_TRUE(unit->doubleWide());
	ASSERT_EQ(unit->getHexes().size(), 2u);
	ASSERT_EQ(BattleHex::getDistance(primary->getPosition(), unit->getPosition()), 4);
	ASSERT_EQ(BattleHex::getDistance(primary->getPosition(), BattleHex(7, 5)), 3);
	ASSERT_TRUE(unit->coversPos(BattleHex(7, 5)));
	const auto ids = newHorizonsImplosion::pullOrder(*battle(), primary->unitId(), primary->getPosition(), 3);
	EXPECT_EQ(std::count(ids.begin(), ids.end(), unit->unitId()), 1);
	const auto destination = newHorizonsImplosion::pullDestination(*battle(), *unit, primary->getPosition());
	ASSERT_TRUE(destination);
	EXPECT_TRUE(battle()->battleCanForciblyDisplace(unit, *destination, BattleDisplacementCause::MAGICAL));
	ASSERT_TRUE(cast());
	EXPECT_EQ(unit->getPosition(), *destination);
}

TEST_F(NewHorizonsImplosionTest, CapturedCenterSurvivesPrimaryDeath)
{
	// Configurable 100% fixture profile solely proves the post-damage lifetime seam.
	lethalProfile = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	const auto origin = primary->getPosition();
	ASSERT_TRUE(cast());
	EXPECT_FALSE(primary->alive());
	EXPECT_EQ(primary->getPosition(), origin);
	EXPECT_EQ(unit->getPosition(), BattleHex(11, 5));
}

TEST_F(NewHorizonsImplosionTest, RoundedZeroDamageStillResolvesPull)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(primary, 999));
	ASSERT_EQ(primary->getAvailableHealth(), 1);
	ASSERT_EQ(expectedDamage(), 0);
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	ASSERT_TRUE(cast());
	EXPECT_EQ(primary->getAvailableHealth(), 1);
	EXPECT_EQ(unit->getPosition(), BattleHex(11, 5));
}

TEST_F(NewHorizonsImplosionTest, AbsoluteImmunityRejectsWithoutDamagePullOrPayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	primary->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::IMPLOSION))));
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	const auto before = primary->save();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast());
	EXPECT_EQ(primary->save(), before);
	EXPECT_EQ(unit->getPosition(), BattleHex(12, 5));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsImplosionTest, AbsentCapturedProfileKeepsGenericDamageAndNoPull)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(1000));
	EXPECT_FALSE(newHorizonsImplosion::hasRules(battle()->getMagicRules()));
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	const auto before = primary->getAvailableHealth();
	const auto predicted = expectedDamage();
	ASSERT_TRUE(cast());
	EXPECT_EQ(primary->getAvailableHealth(), std::max<int64_t>(0, before - predicted));
	EXPECT_EQ(unit->getPosition(), BattleHex(12, 5));
}

TEST_F(NewHorizonsImplosionTest, OptionalProfileRejectsNullUnknownAndUnsupportedVersion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto rules = battle()->getMagicRules();
	EXPECT_NO_THROW(newHorizonsMagic::validateImplosionSerialization(rules, true));
	EXPECT_THROW(newHorizonsMagic::validateImplosionSerialization(rules, false), std::runtime_error);
	rules["spells"]["core:implosion"]["implosion"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::validateImplosionSerialization(rules, false), std::runtime_error);
	rules = battle()->getMagicRules();
	rules["spells"]["core:implosion"]["implosion"]["unknown"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = battle()->getMagicRules();
	rules["spells"]["core:implosion"].Struct().erase("implosion");
	EXPECT_NO_THROW(newHorizonsMagic::validateImplosionSerialization(rules, false));
}

TEST_F(NewHorizonsImplosionTest, ExistingMagicalReductionAppliesAfterCurrentHealthFormula)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto unmitigated = expectedDamage();
	ASSERT_EQ(unmitigated, 120);
	primary->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSource::OTHER, 5000,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	const auto mitigated = expectedDamage();
	EXPECT_EQ(mitigated, 60);
	const auto health = primary->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_EQ(primary->getAvailableHealth(), health - mitigated);
}

TEST_F(NewHorizonsImplosionTest, TimeStoppedPrimaryDoesNotResolveDamageOrPull)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	primary->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(primary->isTimeStopped());
	auto * unit = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	const auto before = primary->save();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast());
	EXPECT_EQ(primary->save(), before);
	EXPECT_EQ(unit->getPosition(), BattleHex(12, 5));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsImplosionTest, AuthenticSpellLockSealsSecondaryWhileOtherAndUnrelatedResistanceStillPull)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * locked = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(8, 5), 10);
	auto * resistant = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(10, 3), 10);
	const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	ASSERT_NE(lock, SpellID::NONE);
	auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock));
	marker->turnsRemain = 2;
	locked->addNewBonus(marker);
	resistant->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto lockedBefore = locked->save();
	const auto resistantDestination = newHorizonsImplosion::pullDestination(*battle(), *resistant, primary->getPosition());
	ASSERT_TRUE(resistantDestination);
	EXPECT_FALSE(newHorizonsImplosion::pullDestination(*battle(), *locked, primary->getPosition()));
	const auto ordered = newHorizonsImplosion::pullOrder(*battle(), primary->unitId(), primary->getPosition(), 3);
	EXPECT_EQ(std::count(ordered.begin(), ordered.end(), locked->unitId()), 0);
	ASSERT_TRUE(cast());
	EXPECT_EQ(locked->save(), lockedBefore);
	EXPECT_EQ(marker->turnsRemain, 2);
	EXPECT_EQ(ordinary->getPosition(), BattleHex(9, 5));
	EXPECT_EQ(resistant->getPosition(), *resistantDestination);
}
