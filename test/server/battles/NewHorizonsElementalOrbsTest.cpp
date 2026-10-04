/*
 * NewHorizonsElementalOrbsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
struct ElementalOrbCase
{
	const char * spellKey;
	const char * artifactKey;
	SpellDamageElement element;
	const char * testName;
	bool targetByHex;
};

SpellID spellFromKey(const char * key)
{
	return SpellID(SpellID::decode(key));
}

ArtifactID artifactFromKey(const char * key)
{
	return ArtifactID(ArtifactID::decode(key));
}

std::shared_ptr<Bonus> spellDamageReduction(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
}

int64_t multipliedBy125Percent(int64_t value)
{
	return value / 100 * 125 + value % 100 * 125 / 100;
}

class ElementalDamageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ElementalDamageEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsElementalOrbDamageTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<ElementalOrbCase>
{
protected:
	const CSpell * spell = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(const ElementalOrbCase & testCase)
	{
		startGame();
		spell = spellFromKey(testCase.spellKey).toSpell();
		ASSERT_NE(spell, nullptr) << testCase.spellKey;
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(attackerSideHero, artifactFromKey(testCase.artifactKey), ArtifactPosition::MISC1);
		attackerSideHero->addSpellToSpellbook(spell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 23, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		const auto artifactId = artifactFromKey(testCase.artifactKey);
		ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::MISC1), nullptr);
		EXPECT_EQ(attackerSideHero->getArt(ArtifactPosition::MISC1)->getTypeId(), artifactId);
		EXPECT_EQ(attackerSideHero->getElementalSpellDamageBonus(testCase.element), 25);

		const auto saved = gameState()->saveToMemory();
		CGameState restored;
		restored.preInit(LIBRARY);
		restored.loadFromMemory(saved);
		const auto * restoredHero = restored.getHero(attackerSideHero->id);
		ASSERT_NE(restoredHero, nullptr);
		ASSERT_NE(restoredHero->getArt(ArtifactPosition::MISC1), nullptr);
		EXPECT_EQ(restoredHero->getArt(ArtifactPosition::MISC1)->getTypeId(), artifactId);
		EXPECT_EQ(restoredHero->getElementalSpellDamageBonus(testCase.element), 25);

		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		ASSERT_NE(target, nullptr);
		beginCombat();
	}

	spells::Target destinationFor(const ElementalOrbCase & testCase) const
	{
		spells::Target destination;
		if(testCase.targetByHex)
			destination.emplace_back(spells::Destination(target->getPosition()));
		else
			destination.emplace_back(target);
		return destination;
	}

	void submitCast(const ElementalOrbCase & testCase)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		if(testCase.targetByHex)
			action.aimToHex(target->getPosition());
		else
			action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}
};

class NewHorizonsElementalOrbControlTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};
}

TEST_P(NewHorizonsElementalOrbDamageTest, MatchingOrbBoostsForecastAndAcceptedCastThenRemovalRestoresBaseline)
{
	const auto testCase = GetParam();
	prepare(testCase);
	ASSERT_EQ(spell->getDamageElement(), testCase.element);

	target->addNewBonus(spellDamageReduction(20));
	spells::BattleCast actualCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&actualCast);
	spells::detail::ProblemImpl problem;
	const auto destination = destinationFor(testCase);
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const int64_t afterReduction = rawDamage * 80 / 100;
	const int64_t expectedBoostedDamage = multipliedBy125Percent(afterReduction);
	EXPECT_EQ(mechanics->adjustEffectValue(target), expectedBoostedDamage)
		<< "Elemental damage is a final multiplier after ordinary magical reduction";

	const auto healthBefore = target->getAvailableHealth();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	ElementalDamageEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	spells::BattleCast forecast(&projected, attackerSideHero, spells::Mode::HERO, spell);
	forecast.castEval(projected.getServerCallback(), destination);
	const auto * forecastTarget = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(forecastTarget, nullptr);
	EXPECT_EQ(healthBefore - forecastTarget->getAvailableHealth(), expectedBoostedDamage);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore) << "A preview must not mutate the live unit";

	submitCast(testCase);
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedBoostedDamage);

	gameHandler->removeArtifact(ArtifactLocation(attackerSideHero->id, ArtifactPosition::MISC1));
	EXPECT_EQ(attackerSideHero->getElementalSpellDamageBonus(testCase.element), 0);
	advanceRound();
	spells::BattleCast unboostedCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	mechanics = spell->battleMechanics(&unboostedCast);
	const auto unboostedRawDamage = mechanics->getEffectValue();
	ASSERT_GT(unboostedRawDamage, 0);
	const int64_t expectedUnboostedDamage = unboostedRawDamage * 80 / 100;
	const auto unboostedHealthBefore = target->getAvailableHealth();
	submitCast(testCase);
	EXPECT_EQ(unboostedHealthBefore - target->getAvailableHealth(), expectedUnboostedDamage);
}

INSTANTIATE_TEST_SUITE_P(AllFourElements, NewHorizonsElementalOrbDamageTest,
	::testing::Values(
		ElementalOrbCase{"core:fireball", "core:orbOfTempestuousFire", SpellDamageElement::FIRE, "Fire", false},
		ElementalOrbCase{"core:iceBolt", "core:orbOfDrivingRain", SpellDamageElement::WATER, "Water", false},
		ElementalOrbCase{"core:lightningBolt", "core:orbOfTheFirmament", SpellDamageElement::AIR, "Air", false},
		ElementalOrbCase{"core:meteorShower", "core:orbOfSilt", SpellDamageElement::EARTH, "Earth", true}),
	[](const ::testing::TestParamInfo<ElementalOrbCase> & info)
	{
		return info.param.testName;
	});

TEST_F(NewHorizonsElementalOrbControlTest, TagsMatchTheAuthoredElementAndDoNotInferFromMagicSchool)
{
	const std::array<std::pair<const char *, SpellDamageElement>, 12> positiveTags{{
		{"core:fireball", SpellDamageElement::FIRE},
		{"core:landMine", SpellDamageElement::FIRE},
		{"core:landMineTrigger", SpellDamageElement::FIRE},
		{"core:fireWall", SpellDamageElement::FIRE},
		{"core:fireWallTrigger", SpellDamageElement::FIRE},
		{"core:inferno", SpellDamageElement::FIRE},
		{"core:iceBolt", SpellDamageElement::WATER},
		{"core:frostRing", SpellDamageElement::WATER},
		{"core:lightningBolt", SpellDamageElement::AIR},
		{"core:chainLightning", SpellDamageElement::AIR},
		{"new-horizons:masterChainLightning", SpellDamageElement::AIR},
		{"core:meteorShower", SpellDamageElement::EARTH},
	}};
	for(const auto & [spellKey, element] : positiveTags)
	{
		const auto * spell = spellFromKey(spellKey).toSpell();
		ASSERT_NE(spell, nullptr) << spellKey;
		EXPECT_EQ(spell->getDamageElement(), element) << spellKey;
	}

	for(const auto * spellKey : {"core:magicArrow", "core:armageddon", "core:implosion",
		"new-horizons:disintegrate", "new-horizons:stormOfDaggers"})
	{
		const auto * spell = spellFromKey(spellKey).toSpell();
		ASSERT_NE(spell, nullptr) << spellKey;
		EXPECT_EQ(spell->getDamageElement(), SpellDamageElement::NONE) << spellKey;
	}

	const auto * implosion = spellFromKey("core:implosion").toSpell();
	ASSERT_NE(implosion, nullptr);
	EXPECT_TRUE(implosion->hasSchool(SpellSchool::EARTH));
}

TEST_F(NewHorizonsElementalOrbControlTest, EarthOrbDoesNotBoostEarthSchoolImplosionWithoutAnEarthDamageTag)
{
	startGame();
	const auto implosionId = spellFromKey("core:implosion");
	const auto * implosion = implosionId.toSpell();
	ASSERT_NE(implosion, nullptr);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	giveArtifact(attackerSideHero, artifactFromKey("core:orbOfSilt"), ArtifactPosition::MISC1);
	attackerSideHero->addSpellToSpellbook(implosionId);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 23, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	EXPECT_EQ(implosion->getDamageElement(), SpellDamageElement::NONE);
	EXPECT_TRUE(implosion->hasSchool(SpellSchool::EARTH));
	EXPECT_EQ(attackerSideHero->getElementalSpellDamageBonus(SpellDamageElement::EARTH), 25);

	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	beginCombat();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, implosion);
	auto mechanics = implosion->battleMechanics(&cast);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	EXPECT_EQ(mechanics->adjustEffectValue(target), rawDamage)
		<< "An Earth school spell remains untagged unless its authored damage element is Earth";

	const auto before = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = implosionId;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), rawDamage);
}

TEST_F(NewHorizonsElementalOrbControlTest, ElementalMultiplierIsAppliedAfterReductionAndBeforeCreatureDamageCap)
{
	startGame();
	const auto fireballId = spellFromKey("core:fireball");
	const auto * fireball = fireballId.toSpell();
	ASSERT_NE(fireball, nullptr);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	giveArtifact(attackerSideHero, artifactFromKey("core:orbOfTempestuousFire"), ArtifactPosition::MISC1);
	attackerSideHero->addSpellToSpellbook(fireballId);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 23, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:behemoth"), BattleHex(rightHex), 1);
	ASSERT_NE(target, nullptr);
	// Keep the attacker hero as the current action controller for this focused cast test.
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1, BonusSourceID()));
	target->addNewBonus(spellDamageReduction(20));
	beginCombat();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fireball);
	const auto mechanics = fireball->battleMechanics(&cast);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const int64_t reducedDamage = rawDamage * 80 / 100;
	const int64_t boostedDamage = multipliedBy125Percent(reducedDamage);
	const int64_t maximumHealth = target->getMaxHealth();
	ASSERT_GT(maximumHealth, 0);
	const int64_t capThreshold = reducedDamage + 1;
	const int capPercent = static_cast<int>((capThreshold * 100 + maximumHealth - 1) / maximumHealth);
	ASSERT_GE(capPercent, 1);
	ASSERT_LE(capPercent, 100);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::DAMAGE_RECEIVED_CAP,
		BonusSource::OTHER, capPercent, BonusSourceID()));
	const int64_t cappedDamage = maximumHealth * capPercent / 100;
	ASSERT_GT(cappedDamage, reducedDamage);
	ASSERT_LT(cappedDamage, boostedDamage);
	EXPECT_EQ(mechanics->adjustEffectValue(target), cappedDamage);

	const auto before = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = fireballId;
	action.aimToUnit(target);
	spells::detail::ProblemImpl problem;
	std::vector<std::string> problemMessages;
	const bool castable = mechanics->canBeCast(problem);
	problem.getAll(problemMessages);
	ASSERT_TRUE(castable) << testing::PrintToString(problemMessages);
	problemMessages.clear();
	const auto actionTarget = action.getTarget(battle());
	const bool targetable = mechanics->canBeCastAt(actionTarget, problem);
	problem.getAll(problemMessages);
	ASSERT_TRUE(targetable)
		<< testing::PrintToString(problemMessages);
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK)
		<< testing::PrintToString(problemMessages);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleGetOwner(active), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(before - target->getAvailableHealth(), cappedDamage);
}

TEST_F(NewHorizonsElementalOrbControlTest, FireWallTriggerAppliesCastTimeDamageSnapshotAndElementOnce)
{
	startGame();
	const auto * fireWall = spellFromKey("core:fireWall").toSpell();
	const auto triggerId = spellFromKey("core:fireWallTrigger");
	const auto * trigger = triggerId.toSpell();
	ASSERT_NE(fireWall, nullptr);
	ASSERT_NE(trigger, nullptr);
	giveArtifact(attackerSideHero, artifactFromKey("core:orbOfTempestuousFire"), ArtifactPosition::MISC1);
	EXPECT_EQ(attackerSideHero->getElementalSpellDamageBonus(SpellDamageElement::FIRE), 25);
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	beginCombat();

	SpellCreatedObstacle obstacle;
	obstacle.ID = fireWall->getId().getNum();
	obstacle.trigger = triggerId;
	obstacle.casterSide = BattleSide::ATTACKER;
	obstacle.casterSpellPower = 83;
	obstacle.casterPowerDivisor = 10;
	obstacle.minimalDamage = 83;
	obstacle.damageSnapshot = true;
	spells::ObstacleCasterProxy obstacleCaster(PlayerColor(0), attackerSideHero, obstacle);
	spells::BattleCast triggerCast(battle(), &obstacleCaster, spells::Mode::PASSIVE, trigger);
	auto mechanics = trigger->battleMechanics(&triggerCast);
	ASSERT_EQ(trigger->getDamageElement(), SpellDamageElement::FIRE);
	EXPECT_EQ(mechanics->getEffectValue(), 83);
	EXPECT_EQ(mechanics->adjustEffectValue(target), 103)
		<< "The saved cast-time raw value receives the Fire Orb once, not again through hero spell bonuses";

	target->addNewBonus(spellDamageReduction(20));
	EXPECT_EQ(mechanics->adjustEffectValue(target), 82)
		<< "Recipient magical reduction is applied before the final elemental multiplier";
}

TEST(NewHorizonsElementalOrbsWireTest, ExplicitSubtypeRoundTripsAndCannotBeSilentlyDownsaved)
{
	Bonus outgoing(BonusDuration::PERMANENT, BonusType::ELEMENTAL_SPELL_DAMAGE,
		BonusSource::ARTIFACT, 25, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(SpellDamageElement::WATER))));
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);
	Bonus restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.type, BonusType::ELEMENTAL_SPELL_DAMAGE);
	EXPECT_EQ(restored.val, 25);
	EXPECT_EQ(restored.subtype.getNum(), static_cast<int32_t>(SpellDamageElement::WATER));

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE;
	EXPECT_THROW(oldWriter.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}
