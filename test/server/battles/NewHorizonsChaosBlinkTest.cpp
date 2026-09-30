/*
 * NewHorizonsChaosBlinkTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsBlink.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <numeric>

namespace
{
SpellID blinkSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsBlink::SPELL_ID)));
}

bool containsHex(const std::vector<BattleHex> & hexes, const BattleHex & expected)
{
	return std::ranges::find(hexes, expected) != hexes.end();
}

class NewHorizonsChaosBlinkTest : public BattleTestFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(blinkSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int32_t spellPower = 0, bool blinkmaster = false,
		const std::string & targetCreature = "core:pikeman")
	{
		startGame();
		if(blinkmaster)
		{
			const auto chaosMagic = SecondarySkill::decode(newHorizonsBlink::CHAOS_SKILL);
			ASSERT_GE(chaosMagic, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagic), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			// Acquire through the normal saved perk selector after making its
			// Basic-rank prerequisite legitimate in this fixture.
			attackerSideHero->applyPerkSelection({newHorizonsBlink::CHAOS_SKILL,
				newHorizonsBlink::BLINKMASTER_PERK});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsBlink::CHAOS_SKILL,
				newHorizonsBlink::BLINKMASTER_PERK));
		}

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(blinkSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName(targetCreature), BattleHex(8, 5), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 20);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	bool activateHeroActionSide(BattleSide side)
	{
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitSide() == side)
				return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool castBlink(CStack * target)
	{
		if(!activateHeroActionSide(BattleSide::ATTACKER))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = blinkSpell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::ATTACKER), action);
	}

	std::optional<newHorizonsBlink::Preview> preview(const CStack * target, int32_t spellPower = -1)
	{
		const auto * spell = blinkSpell().toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		if(spellPower >= 0)
			cast.setEffectPower(spellPower);
		const auto mechanics = spell->battleMechanics(&cast);
		return newHorizonsBlink::preview(*mechanics, target);
	}
};
}

TEST(NewHorizonsChaosBlinkRulesTest, RadiusUsesScaledSpellPowerTermAndCapsWithoutLegacyDivisor)
{
	using newHorizonsBlink::radiusFor;
	EXPECT_EQ(radiusFor(0, 10000, 0), 2);
	EXPECT_EQ(radiusFor(99, 10000, 0), 2);
	EXPECT_EQ(radiusFor(100, 10000, 0), 3);
	EXPECT_EQ(radiusFor(200, 10000, 0), 4);
	EXPECT_EQ(radiusFor(87, 11500, 0), 3);
	EXPECT_EQ(radiusFor(86, 11500, 0), 2);
	EXPECT_EQ(radiusFor(84, 10000, 20), 3);
	EXPECT_EQ(radiusFor(83, 10000, 20), 2);
	EXPECT_EQ(radiusFor(std::numeric_limits<int32_t>::max(), 10000, 1000), 4);
}

TEST(NewHorizonsChaosBlinkRulesTest, OutcomeDistributionIsUniformOrTwoIndependentDrawsWithReplacement)
{
	const BattleHex origin(8, 5);
	newHorizonsBlink::Preview ordinary;
	ordinary.legalDestinations = {BattleHex(8, 3), BattleHex(8, 7), BattleHex(10, 5)};
	const auto uniform = newHorizonsBlink::outcomeDistribution(ordinary, origin);
	ASSERT_EQ(uniform.size(), ordinary.legalDestinations.size());
	for(const auto & outcome : uniform)
	{
		EXPECT_EQ(outcome.weight, 1u);
		EXPECT_EQ(outcome.totalWeight, 3u);
	}

	ordinary.blinkmaster = true;
	const auto selected = newHorizonsBlink::outcomeDistribution(ordinary, origin);
	ASSERT_EQ(selected.size(), 3u);
	EXPECT_EQ(selected[0].totalWeight, 9u);
	EXPECT_EQ(selected[0].weight + selected[1].weight + selected[2].weight, 9u);
	EXPECT_EQ(selected[0].weight, 5u);
	EXPECT_EQ(selected[1].weight, 1u);
	EXPECT_EQ(selected[2].weight, 3u);
	EXPECT_EQ(newHorizonsBlink::fartherDestination(origin, BattleHex(8, 7), BattleHex(8, 3)), BattleHex(8, 3));
}

TEST_F(NewHorizonsChaosBlinkTest, PreviewExcludesOriginIgnoresRouteBlockerAndSortsDestinations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const BattleHex origin = friendly->getPosition();
	const BattleHex throughBlocker(9, 5);
	const BattleHex destination(10, 5);
	auto * routeBlocker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), throughBlocker, 1);
	ASSERT_NE(routeBlocker, nullptr);

	const auto singleWide = preview(friendly);
	ASSERT_TRUE(singleWide);
	EXPECT_FALSE(containsHex(singleWide->legalDestinations, origin));
	EXPECT_TRUE(containsHex(singleWide->legalDestinations, destination))
		<< "Only the endpoint footprint is checked; intervening occupied hexes are ignored.";
	for(size_t index = 1; index < singleWide->legalDestinations.size(); ++index)
		EXPECT_LT(singleWide->legalDestinations[index - 1].toInt(), singleWide->legalDestinations[index].toInt());
}

TEST_F(NewHorizonsChaosBlinkTest, PreviewChecksDoubleWideLandingFootprint)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, "core:centaur"));
	ASSERT_TRUE(friendly->doubleWide());
	const BattleHex wideDestination(10, 5);
	const BattleHex landingTail = battle::Unit::occupiedHex(wideDestination, true, BattleSide::ATTACKER);
	ASSERT_TRUE(landingTail.isAvailable());
	const auto openFootprint = preview(friendly);
	ASSERT_TRUE(openFootprint);
	ASSERT_TRUE(containsHex(openFootprint->legalDestinations, wideDestination));
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), landingTail, 1), nullptr);
	const auto blockedFootprint = preview(friendly);
	ASSERT_TRUE(blockedFootprint);
	EXPECT_FALSE(containsHex(blockedFootprint->legalDestinations, wideDestination));
}

TEST_F(NewHorizonsChaosBlinkTest, BasicChaosSkillLegitimatelyActivatesBlinkmasterAndChangesSharedPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare(87, true));
	const auto blinkPreview = preview(friendly);
	ASSERT_TRUE(blinkPreview);
	EXPECT_EQ(blinkPreview->radius, 3);
	EXPECT_TRUE(blinkPreview->blinkmaster);
	const auto outcomes = newHorizonsBlink::outcomeDistribution(*blinkPreview, friendly->getPosition());
	ASSERT_EQ(outcomes.size(), blinkPreview->legalDestinations.size());
	const uint64_t totalWeight = std::accumulate(outcomes.begin(), outcomes.end(), uint64_t{0},
		[](uint64_t total, const auto & outcome) { return total + outcome.weight; });
	EXPECT_EQ(totalWeight, static_cast<uint64_t>(blinkPreview->legalDestinations.size())
		* blinkPreview->legalDestinations.size());
	EXPECT_TRUE(std::ranges::all_of(outcomes, [](const auto & outcome)
		{ return outcome.totalWeight > 0 && outcome.weight > 0; }));
}

TEST_F(NewHorizonsChaosBlinkTest, FriendlyBlinkIgnoresResistanceAndMirrorAndOnlyClearsEntangleBind)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));

	const auto entangle = SpellID(SpellID::decode("new-horizons:entangle"));
	Bonus entangleBind(BonusDuration::N_TURNS, BonusType::BIND_EFFECT,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(entangle));
	entangleBind.turnsRemain = 2;
	friendly->addNewBonus(std::make_shared<Bonus>(entangleBind));
	Bonus classicBind(BonusDuration::N_TURNS, BonusType::BIND_EFFECT,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BIND)));
	classicBind.turnsRemain = 2;
	friendly->addNewBonus(std::make_shared<Bonus>(classicBind));

	const auto classicBindSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BIND))).And(Selector::type()(BonusType::BIND_EFFECT));
	const auto entangleBindSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(entangle)).And(Selector::type()(BonusType::BIND_EFFECT));
	const auto origin = friendly->getPosition();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto attacksBefore = friendly->counterAttacks.available();

	ASSERT_TRUE(castBlink(friendly));
	EXPECT_NE(friendly->getPosition(), origin);
	EXPECT_FALSE(friendly->moved());
	EXPECT_EQ(friendly->counterAttacks.available(), attacksBefore);
	EXPECT_FALSE(friendly->hasBonus(entangleBindSelector));
	EXPECT_TRUE(friendly->hasBonus(classicBindSelector));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 4);
	EXPECT_EQ(enemy->getPosition(), BattleHex(14, 5)) << "A friendly Mirror must not reflect Blink.";
}

TEST_F(NewHorizonsChaosBlinkTest, HostileBlinkStillUsesOrdinaryMagicResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto origin = enemy->getPosition();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto round = battle()->battleGetRound();
	const auto actionBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(round);

	EXPECT_FALSE(castBlink(enemy));
	EXPECT_EQ(enemy->getPosition(), origin);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round), actionBefore);
}

TEST_F(NewHorizonsChaosBlinkTest, FriendlyBlinkStillRespectsAbsoluteSpellImmunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto immunity = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(blinkSpell()));
	immunity->parameters = std::make_shared<BonusParameters>(1);
	friendly->addNewBonus(std::move(immunity));

	const auto origin = friendly->getPosition();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto round = battle()->battleGetRound();
	const auto actionBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(round);

	EXPECT_FALSE(castBlink(friendly));
	EXPECT_EQ(friendly->getPosition(), origin);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round), actionBefore);
}

TEST_F(NewHorizonsChaosBlinkTest, HostileMagicMirrorReflectsBlinkOntoTheFriendlySide)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));
	const auto friendlyOrigin = friendly->getPosition();
	const auto enemyOrigin = enemy->getPosition();
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(castBlink(enemy));
	EXPECT_EQ(enemy->getPosition(), enemyOrigin);
	EXPECT_NE(friendly->getPosition(), friendlyOrigin)
		<< "A reflected hostile cast should resolve on the caster's friendly stack.";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 4);
}

TEST_F(NewHorizonsChaosBlinkTest, EmptySelectedLandingRingRejectsBeforeManaOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const BattleHex origin = friendly->getPosition();
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable() || hex == origin || BattleHex::getDistance(origin, hex) > 2
			|| battle()->battleGetUnitByPos(hex, true))
			continue;
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 1), nullptr);
	}
	EXPECT_FALSE(preview(friendly));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto round = battle()->battleGetRound();
	const auto actionBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(round);
	EXPECT_FALSE(castBlink(friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round), actionBefore);
}
