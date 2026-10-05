/*
 * NewHorizonsVengefulVinesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/CGameHandler.h"

#include <array>
#include <set>

namespace
{
constexpr auto natureMagicSkill = "new-horizons:natureMagic";

SpellID vengefulVinesSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsVengefulVines::SPELL_KEY)));
}

JsonNode magicRulesAtVersion(const int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	if(version >= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return rules;

	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		spell.Struct().erase("heroAccess");
		spell.Struct().erase("restoration");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		rules.Struct().erase("spellPoints");
		rules.Struct().erase("mageGuildGeneration");
		rules.Struct().erase("physicalDamageReductionCapPercent");
		rules.Struct().erase("warcasting");
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("active");
			spell.Struct().erase("directDamage");
			spell.Struct().erase("cureAfflictions");
		}
	}
	return rules;
}
}

class NewHorizonsVengefulVinesTest : public BattleTestFixture
{
protected:
	enum class StackLayout
	{
		DOUBLE_WIDE,
		THREE_ENEMIES,
		FRIENDLY_IN_FOOTPRINT
	};

	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	BattleHex origin{7, 4};
	BattleHexArray selectedHexes;
	CStack * enemy = nullptr;
	CStack * secondEnemy = nullptr;
	CStack * thirdEnemy = nullptr;
	CStack * doubleWideEnemy = nullptr;
	CStack * friendly = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesAtVersion(magicRulesVersion));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const int spellPower = 100,
		const int rulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION,
		const StackLayout layout = StackLayout::DOUBLE_WIDE)
	{
		magicRulesVersion = rulesVersion;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(vengefulVinesSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		selectedHexes.clear();
		selectedHexes.insert(origin);
		selectedHexes.insert(origin.cloneInDirection(BattleHex::RIGHT, false));
		selectedHexes.insert(selectedHexes.back().cloneInDirection(BattleHex::RIGHT, false));
		ASSERT_EQ(selectedHexes.size(), 3u);

		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), selectedHexes[0], 1000);
		if(layout == StackLayout::THREE_ENEMIES)
		{
			secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), selectedHexes[1], 1000);
			thirdEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), selectedHexes[2], 1000);
			doubleWideEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:phoenix"), BattleHex(3, 5), 10);
			friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(13, 5), 1000);
		}
		else if(layout == StackLayout::FRIENDLY_IN_FOOTPRINT)
		{
			secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
			doubleWideEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:phoenix"), BattleHex(4, 5), 10);
			friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), selectedHexes[1], 1000);
		}
		else
		{
			secondEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
			doubleWideEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:phoenix"), selectedHexes[1], 10);
			friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(13, 5), 1000);
		}
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(secondEnemy, nullptr);
		ASSERT_NE(doubleWideEnemy, nullptr);
		ASSERT_NE(friendly, nullptr);
		if(layout == StackLayout::THREE_ENEMIES)
		{
			ASSERT_NE(thirdEnemy, nullptr);
		}
		if(layout != StackLayout::FRIENDLY_IN_FOOTPRINT)
		{
			ASSERT_TRUE(doubleWideEnemy->doubleWide());
			if(layout == StackLayout::DOUBLE_WIDE)
			{
				ASSERT_EQ(doubleWideEnemy->occupiedHex(), selectedHexes[2]);
			}
		}
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

	bool activateHeroActionSide(const BattleSide side)
	{
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitSide() == side)
				return true;
			const auto defend = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), defend))
				return false;
		}
		return false;
	}

	BattleAction vinesAction(const std::vector<BattleHex> & selection) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = vengefulVinesSpell();
		for(const auto & hex : selection)
			action.aimToHex(hex);
		return action;
	}

	spells::Target vinesTarget(const std::vector<BattleHex> & selection) const
	{
		spells::Target target;
		for(const auto & hex : selection)
			target.emplace_back(hex);
		return target;
	}

	bool castVines(const bool followup = false)
	{
		if(!activateHeroActionSide(BattleSide::ATTACKER))
			return false;
		auto action = vinesAction(selectedHexes.toVector());
		action.metamagicFollowup = followup;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castHaste()
	{
		if(!activateHeroActionSide(BattleSide::ATTACKER))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(friendly);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	TConstBonusListPtr vinesMovementBonuses(const CStack * stack) const
	{
		return stack->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(vengefulVinesSpell())).And(Selector::type()(BonusType::STACKS_MOVEMENT_RANGE)));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};

TEST(NewHorizonsVengefulVinesGeometryTest, RequiresThreeDistinctConnectedPlayableLocations)
{
	const BattleHex origin(7, 4);
	const auto right = origin.cloneInDirection(BattleHex::RIGHT, false);
	const auto right2 = right.cloneInDirection(BattleHex::RIGHT, false);
	const auto left = origin.cloneInDirection(BattleHex::LEFT, false);
	const auto topRight = origin.cloneInDirection(BattleHex::TOP_RIGHT, false);

	const auto makeTarget = [](const std::vector<BattleHex> & selected)
	{
		spells::Target target;
		for(const auto & hex : selected)
			target.emplace_back(hex);
		return target;
	};

	const auto line = std::vector<BattleHex>{origin, right, right2};
	const auto bent = std::vector<BattleHex>{origin, right, right.cloneInDirection(BattleHex::BOTTOM_RIGHT, false)};
	const auto triangle = std::vector<BattleHex>{origin, right, topRight};
	const auto thirdTouchesFirst = std::vector<BattleHex>{origin, right, left};
	for(const auto & shape : {line, bent, triangle, thirdTouchesFirst})
	{
		const auto target = makeTarget(shape);
		const auto footprint = newHorizonsVengefulVines::footprint(target);
		ASSERT_EQ(footprint.size(), 3u);
		EXPECT_EQ(footprint[0], shape[0]);
		EXPECT_EQ(footprint[1], shape[1]);
		EXPECT_EQ(footprint[2], shape[2]);
	}
	EXPECT_EQ(BattleHex::mutualPosition(right, left), BattleHex::NONE)
		<< "A new selection need only touch any earlier selection, not the most recent one.";

	EXPECT_TRUE(newHorizonsVengefulVines::footprint(makeTarget({origin, right})).empty());
	EXPECT_TRUE(newHorizonsVengefulVines::footprint(makeTarget({origin, right, right2, topRight})).empty());
	EXPECT_TRUE(newHorizonsVengefulVines::footprint(makeTarget({origin, right, origin})).empty());
	EXPECT_TRUE(newHorizonsVengefulVines::footprint(makeTarget({origin, right, BattleHex(10, 4)})).empty());
	EXPECT_TRUE(newHorizonsVengefulVines::footprint(makeTarget({BattleHex(0, 4), right, right2})).empty());

	const auto & triples = newHorizonsVengefulVines::connectedTriples();
	ASSERT_FALSE(triples.empty());
	std::set<std::array<int, 3>> distinctSets;
	for(const auto & triple : triples)
	{
		ASSERT_EQ(triple.size(), 3u);
		const std::array<int, 3> ids{triple[0].toInt(), triple[1].toInt(), triple[2].toInt()};
		auto key = ids;
		std::ranges::sort(key);
		EXPECT_TRUE(distinctSets.insert(key).second) << "AI enumeration must not repeat an equivalent footprint.";
		EXPECT_EQ(newHorizonsVengefulVines::footprint(makeTarget(triple.toVector())), triple);
	}
	EXPECT_EQ(distinctSets.size(), triples.size());
}

TEST_F(NewHorizonsVengefulVinesTest, NormalDamageUsesTwentyPlusElevenTenthsOfSpellPowerAndAffectsEachEnemyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = vengefulVinesSpell();
	ASSERT_TRUE(newHorizonsVengefulVines::enabled(attackerSideHero->getMagicRules(), spell));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spell, MasteryLevel::NONE), 5);

	spells::BattleCast castContext(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&castContext);
	EXPECT_EQ(mechanics->getTargetTypes(), (std::vector<spells::AimType>{
		spells::AimType::LOCATION, spells::AimType::LOCATION, spells::AimType::LOCATION}));
	EXPECT_EQ(mechanics->getEffectValue(), 130);
	const auto target = vinesTarget(selectedHexes.toVector());
	EXPECT_TRUE(mechanics->canBeCastAt(target));
	EXPECT_EQ(mechanics->getAffectedStacks(target).size(), 2u)
		<< "The selected triple intersects one single-hex stack and both occupied Phoenix hexes; the Phoenix resolves once.";

	const auto enemyBefore = enemy->getAvailableHealth();
	const auto secondEnemyBefore = secondEnemy->getAvailableHealth();
	const auto doubleWideBefore = doubleWideEnemy->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto movementBefore = enemy->getMovementRange();
	const auto initiativeBefore = enemy->getInitiative();

	ASSERT_TRUE(castVines());
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 130);
	EXPECT_EQ(secondEnemy->getAvailableHealth(), secondEnemyBefore)
		<< "A hostile stack outside the selected locations is not affected.";
	EXPECT_EQ(doubleWideBefore - doubleWideEnemy->getAvailableHealth(), 130);
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(enemy->getMovementRange(), movementBefore - 2);
	EXPECT_EQ(enemy->getInitiative(), initiativeBefore);
	ASSERT_EQ(vinesMovementBonuses(enemy)->size(), 1u);
	EXPECT_EQ(vinesMovementBonuses(enemy)->front()->turnsRemain, 2);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 5);
	EXPECT_TRUE(vinesMovementBonuses(friendly)->empty());
}

TEST_F(NewHorizonsVengefulVinesTest, SelectedFriendlyHexDoesNotReplaceEnemyOnlyTargeting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, newHorizonsMagic::CURRENT_RULESET_VERSION,
		StackLayout::FRIENDLY_IN_FOOTPRINT));
	const auto spell = vengefulVinesSpell();
	spells::BattleCast castContext(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&castContext);
	const auto target = vinesTarget(selectedHexes.toVector());
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	const auto affected = mechanics->getAffectedStacks(target);
	ASSERT_EQ(affected.size(), 1u);
	EXPECT_EQ(affected.front()->unitId(), enemy->unitId());

	const auto enemyBefore = enemy->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(castVines());
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 130);
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_TRUE(vinesMovementBonuses(friendly)->empty());
}

TEST_F(NewHorizonsVengefulVinesTest, NatureRankScalesOnlyDamageAndTheFixedSpeedPenaltyExpiresAfterTwoRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto nature = SecondarySkill(SecondarySkill::decode(natureMagicSkill));
	ASSERT_TRUE(nature.hasValue());

	const auto spell = vengefulVinesSpell();
	const std::array<int64_t, 4> expectedDamage{130, 146, 163, 179};
	const std::array<int, 4> expectedCoefficient{100, 115, 130, 145};
	for(size_t rank = 0; rank < expectedDamage.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(nature, static_cast<MasteryLevel::Type>(rank), ChangeValueMode::ABSOLUTE);
		spells::BattleCast castContext(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&castContext);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(
			battle()->getMagicRules(), attackerSideHero, spell), expectedCoefficient[rank]);
		EXPECT_EQ(mechanics->getEffectValue(), expectedDamage[rank]);
	}

	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DURATION, BonusSource::OTHER, 4, BonusSourceID()));
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto movementBefore = enemy->getMovementRange();
	const auto initiativeBefore = enemy->getInitiative();
	ASSERT_TRUE(castVines());
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 179);
	EXPECT_EQ(enemy->getMovementRange(), movementBefore - 2)
		<< "School rank and hero Spell Duration do not rewrite the fixed Speed penalty or duration.";
	EXPECT_EQ(enemy->getInitiative(), initiativeBefore);
	ASSERT_EQ(vinesMovementBonuses(enemy)->size(), 1u);
	EXPECT_EQ(vinesMovementBonuses(enemy)->front()->turnsRemain, 2);

	while(battle()->getRound() == 0)
		advanceRound();
	ASSERT_EQ(vinesMovementBonuses(enemy)->size(), 1u);
	EXPECT_EQ(vinesMovementBonuses(enemy)->front()->turnsRemain, 2);
	advanceRound();
	ASSERT_EQ(vinesMovementBonuses(enemy)->size(), 1u);
	EXPECT_EQ(vinesMovementBonuses(enemy)->front()->turnsRemain, 1);
	advanceRound();
	EXPECT_TRUE(vinesMovementBonuses(enemy)->empty());
}

TEST_F(NewHorizonsVengefulVinesTest, MalformedAndEdgeSelectionsAreRejectedWithoutSpendingManaOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto actionsBefore = server.startedActions.size();
	const auto spell = vengefulVinesSpell();

	spells::BattleCast castContext(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&castContext);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{battle::Destination(origin)}, problem));
	EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{
		battle::Destination(origin), battle::Destination(origin.cloneInDirection(BattleHex::RIGHT, false))}, problem));
	EXPECT_FALSE(mechanics->canBeCastAt(vinesTarget({origin,
		origin.cloneInDirection(BattleHex::RIGHT, false), BattleHex(10, 4)}), problem));

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		vinesAction(std::vector<BattleHex>{origin, origin.cloneInDirection(BattleHex::RIGHT, false)})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		vinesAction(std::vector<BattleHex>{origin, origin.cloneInDirection(BattleHex::RIGHT, false), BattleHex(10, 4)})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		vinesAction(std::vector<BattleHex>{origin, origin.cloneInDirection(BattleHex::RIGHT, false), origin})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		vinesAction(std::vector<BattleHex>{BattleHex(14, 5), BattleHex(15, 5), BattleHex(14, 4)})));
	BattleAction unitTarget = vinesAction(selectedHexes.toVector());
	unitTarget.target.front().unitValue = enemy->unitId();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), unitTarget));

	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	EXPECT_TRUE(server.castsOf(spell).empty());
	EXPECT_TRUE(castVines()) << "A rejected malformed request must preserve the action for a valid cast.";
}

TEST_F(NewHorizonsVengefulVinesTest, ResistanceAndSpellImmunityExcludeOnlyTheirOwnStacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, newHorizonsMagic::CURRENT_RULESET_VERSION,
		StackLayout::THREE_ENEMIES));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	secondEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(vengefulVinesSpell())));
	const auto resistantBefore = enemy->getAvailableHealth();
	const auto immuneBefore = secondEnemy->getAvailableHealth();
	const auto receptiveBefore = thirdEnemy->getAvailableHealth();

	ASSERT_TRUE(castVines());
	EXPECT_EQ(enemy->getAvailableHealth(), resistantBefore);
	EXPECT_EQ(secondEnemy->getAvailableHealth(), immuneBefore);
	EXPECT_EQ(receptiveBefore - thirdEnemy->getAvailableHealth(), 130);
	EXPECT_TRUE(vinesMovementBonuses(enemy)->empty());
	EXPECT_TRUE(vinesMovementBonuses(secondEnemy)->empty());
	EXPECT_FALSE(vinesMovementBonuses(thirdEnemy)->empty());
}

TEST_F(NewHorizonsVengefulVinesTest, MetamagicEchoedDurationAddsOneToTheFixedTwoRoundPenalty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	constexpr auto metamagicSkill = "new-horizons:metamagic";
	constexpr auto arcaneAcquisitionPerk = "new-horizons:metamagic.arcaneAcquisition";
	constexpr auto echoedDurationPerk = "new-horizons:metamagic.echoedDuration";
	const auto metamagic = SecondarySkill(SecondarySkill::decode(metamagicSkill));
	ASSERT_TRUE(metamagic.hasValue());
	attackerSideHero->setSecSkillLevel(metamagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({metamagicSkill, arcaneAcquisitionPerk});
	attackerSideHero->setSecSkillLevel(metamagic, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({metamagicSkill, echoedDurationPerk});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(metamagicSkill, echoedDurationPerk));

	ASSERT_TRUE(castHaste());
	ASSERT_TRUE(castVines(true));
	ASSERT_EQ(vinesMovementBonuses(enemy)->size(), 1u);
	EXPECT_EQ(vinesMovementBonuses(enemy)->front()->turnsRemain, 3);
}

TEST_F(NewHorizonsVengefulVinesTest, SavedV2RosterCannotUseTheV3EffectAndDoesNotConsumeTheHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
	const auto spell = vengefulVinesSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	EXPECT_FALSE(newHorizonsVengefulVines::enabled(attackerSideHero->getMagicRules(), spell));
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto actionsBefore = server.startedActions.size();

	EXPECT_FALSE(castVines()) << "The installed V3 effect must not activate in a saved V2 roster.";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	EXPECT_TRUE(castHaste()) << "A stale Vengeful Vines request must leave the shared Hero Action available.";
}
