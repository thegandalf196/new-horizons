/*
 * NewHorizonsEntangleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file, in main folder
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/CGameHandler.h"

namespace
{
constexpr auto entangleKey = "new-horizons:entangle";
constexpr auto natureMagicSkill = "new-horizons:natureMagic";
constexpr auto rootcallerPerk = "new-horizons:natureMagic.rootcaller";
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto arcaneAcquisitionPerk = "new-horizons:metamagic.arcaneAcquisition";
constexpr auto echoedDurationPerk = "new-horizons:metamagic.echoedDuration";

SpellID entangleSpell()
{
	return SpellID(SpellID::decode(entangleKey));
}

JsonNode legacyMagicRules(const int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
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
		auto & spells = rules["spells"].Struct();
		for(auto it = spells.begin(); it != spells.end();)
		{
			if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
				it = spells.erase(it);
			else
				++it;
		}
		for(auto & [factionId, faction] : rules["factions"].Struct())
		{
			(void)factionId;
			faction["major"] = faction["preferredA"];
			faction["minor"] = faction["preferredB"];
			faction.Struct().erase("preferredA");
			faction.Struct().erase("preferredB");
		}
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

class NewHorizonsEntangleTest : public BattleTestFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * rooted = nullptr;
	CStack * friendly = nullptr;
	CStack * farFriendly = nullptr;
	CStack * flyer = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		const auto rules = magicRulesVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? JsonNode(JsonPath::builtin("config/newHorizonsMagic"))
			: legacyMagicRules(magicRulesVersion);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const int spellPower = 0, const int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION)
	{
		magicRulesVersion = magicVersion;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(entangleSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);

		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::TELEPORT);
		setTestSpellPointTotal(defenderSideHero, 100);

		startBattle();
		removeDeployedUnits();
		rooted = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(9, 5), 20);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 20);
		farFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 20);
		flyer = addStack(BattleSide::DEFENDER, creatureByName("core:phoenix"), BattleHex(12, 5), 5);
		ASSERT_NE(rooted, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(farFriendly, nullptr);
		ASSERT_NE(flyer, nullptr);
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

	bool cast(const BattleSide side, const SpellID spell, const CStack * target, const bool followup = false)
	{
		if(!activateHeroActionSide(side))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.aimToUnit(target);
		if(spell == SpellID::TELEPORT)
			action.aimToHex(BattleHex(10, 5));
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(side), action);
	}

	bool castEntangle(const CStack * target = nullptr, const bool followup = false)
	{
		return cast(BattleSide::ATTACKER, entangleSpell(), target ? target : rooted, followup);
	}

	bool submit(const CStack * stack, const BattleAction & action)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	TConstBonusListPtr entangleBonuses(const CStack * stack) const
	{
		return stack->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(entangleSpell())).And(Selector::type()(BonusType::BIND_EFFECT)));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	void addClassicBind(CStack * stack)
	{
		Bonus bind(BonusDuration::N_TURNS, BonusType::BIND_EFFECT,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BIND)));
		bind.turnsRemain = 2;
		stack->addNewBonus(std::make_shared<Bonus>(bind));
	}
};

TEST_F(NewHorizonsEntangleTest, AuthoritativeCastCostsFourAndOnlyRootsAnEnemyGroundStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = entangleSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spell, MasteryLevel::NONE), 4);

	spells::BattleCast castContext(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	spells::detail::ProblemImpl problem;
	const auto mechanics = spell.toSpell()->battleMechanics(&castContext);
	EXPECT_TRUE(mechanics->canBeCastAt(spells::Target{battle::Destination(rooted)}, problem));
	spells::detail::ProblemImpl friendlyProblem;
	EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{battle::Destination(friendly)}, friendlyProblem));
	spells::detail::ProblemImpl flyingProblem;
	EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{battle::Destination(flyer)}, flyingProblem));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto initiativeBefore = rooted->getInitiative();
	const auto healthBefore = rooted->getAvailableHealth();
	ASSERT_TRUE(castEntangle());
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 4);
	ASSERT_EQ(entangleBonuses(rooted)->size(), 1u);
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 1);
	EXPECT_EQ(rooted->getMovementRange(), 0);
	EXPECT_EQ(rooted->getInitiative(), initiativeBefore);
	EXPECT_EQ(rooted->getAvailableHealth(), healthBefore);
}

TEST_F(NewHorizonsEntangleTest, SchoolRankScalesOnlyTheCappedSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(75));
	const auto nature = SecondarySkill(SecondarySkill::decode(natureMagicSkill));
	ASSERT_TRUE(nature.hasValue());
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	ASSERT_TRUE(castEntangle());
	ASSERT_EQ(entangleBonuses(rooted)->size(), 1u);
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 2)
		<< "Expert Nature's 145% coefficient raises only the 75 / 100 Spell Power term across its floor";
}

TEST_F(NewHorizonsEntangleTest, RootcallerAddsOneAfterBaseCapAndEchoedDurationAddsOneMore)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	const auto nature = SecondarySkill(SecondarySkill::decode(natureMagicSkill));
	ASSERT_TRUE(nature.hasValue());
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({natureMagicSkill, rootcallerPerk});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(natureMagicSkill, rootcallerPerk));

	const auto metamagic = SecondarySkill(SecondarySkill::decode(metamagicSkill));
	ASSERT_TRUE(metamagic.hasValue());
	attackerSideHero->setSecSkillLevel(metamagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({metamagicSkill, arcaneAcquisitionPerk});
	attackerSideHero->setSecSkillLevel(metamagic, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({metamagicSkill, echoedDurationPerk});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(metamagicSkill, echoedDurationPerk));

	// Echoed Duration is a Metamagic follow-up, not an automatic modifier on an
	// ordinary first cast. Arcane Acquisition grants it after a qualifying cast.
	ASSERT_TRUE(cast(BattleSide::ATTACKER, SpellID::HASTE, friendly));
	ASSERT_TRUE(castEntangle(rooted, true));
	ASSERT_EQ(entangleBonuses(rooted)->size(), 1u);
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 4)
		<< "The ordinary base cap remains 2, Rootcaller raises it to 3, then Echoed Duration raises it to 4";
}

TEST_F(NewHorizonsEntangleTest, BoundStackStillShootsCanRetaliateAndCanUseNonmovementAbility)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// The melee unit at (8, 5) blocks this shot lane; leave a clear corridor for
	// the rooted archer while retaining adjacency in the dedicated melee test.
	BattleStackMoved reposition;
	reposition.battleID = BattleID(0);
	reposition.stack = friendly->unitId();
	reposition.tilesToMove.insert(BattleHex(8, 7));
	gameHandler->sendAndApply(reposition);
	ASSERT_TRUE(castEntangle());
	ASSERT_TRUE(battle()->battleCanShoot(rooted, farFriendly->getPosition()));
	EXPECT_TRUE(rooted->ableToRetaliate());

	BattleAttackInfo retaliation(rooted, friendly, 0, false);
	retaliation.retaliation = true;
	EXPECT_GT(battle()->calculateDmgRange(retaliation).damage.max, 0);

	rooted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	spells::Target target{battle::Destination(farFriendly)};
	EXPECT_TRUE(submit(rooted, BattleAction::makeCreatureSpellcast(rooted, target, SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(entangleBonuses(rooted)->empty());
}

TEST_F(NewHorizonsEntangleTest, BoundStackCanWaitDefendAndMakeAnAdjacentMeleeAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castEntangle());
	EXPECT_FALSE(submit(rooted, BattleAction::makeMove(rooted, BattleHex(10, 5))));
	EXPECT_EQ(rooted->getMovementRange(), 0);

	ASSERT_TRUE(submit(rooted, BattleAction::makeWait(rooted)));
	EXPECT_FALSE(entangleBonuses(rooted)->empty());

	// A second real marker avoids needing a second Hero Action; the fixture is
	// checking the ordinary DEFEND action path while the same effect is present.
	Bonus marker(BonusDuration::N_TURNS, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT,
		0, BonusSourceID(entangleSpell()));
	marker.turnsRemain = 1;
	flyer->addNewBonus(std::make_shared<Bonus>(marker));
	ASSERT_TRUE(submit(flyer, BattleAction::makeDefend(flyer)));
	EXPECT_FALSE(entangleBonuses(flyer)->empty());
}

TEST_F(NewHorizonsEntangleTest, BoundStackCanMakeAnAdjacentMeleeAttackWithoutMoving)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castEntangle());
	const auto originalPosition = rooted->getPosition();
	const auto attack = BattleAction::makeMeleeAttack(rooted, friendly->getPosition(), rooted->getPosition(), false);
	ASSERT_TRUE(submit(rooted, attack));
	EXPECT_EQ(rooted->getPosition(), originalPosition);
	EXPECT_FALSE(entangleBonuses(rooted)->empty());
}

TEST_F(NewHorizonsEntangleTest, RealDisplacementClearsOnlyEntangleAndPreservesClassicBind)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castEntangle());
	addClassicBind(rooted);
	const auto classicSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BIND))).And(Selector::type()(BonusType::BIND_EFFECT));
	ASSERT_TRUE(rooted->hasBonus(classicSelector));

	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = rooted->unitId();
	move.tilesToMove.insert(BattleHex(10, 5));
	gameHandler->sendAndApply(move);

	EXPECT_EQ(rooted->getPosition(), BattleHex(10, 5));
	EXPECT_TRUE(entangleBonuses(rooted)->empty());
	EXPECT_TRUE(rooted->hasBonus(classicSelector));
}

TEST_F(NewHorizonsEntangleTest, ActualTeleportBreaksEntangle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	ASSERT_TRUE(castEntangle());
	ASSERT_FALSE(entangleBonuses(rooted)->empty());

	ASSERT_TRUE(activateHeroActionSide(BattleSide::DEFENDER));
	ASSERT_FALSE(entangleBonuses(rooted)->empty())
		<< "The marker must still be live at teleport time, not merely have expired while advancing turns";
	ASSERT_TRUE(cast(BattleSide::DEFENDER, SpellID::TELEPORT, rooted));
	EXPECT_NE(rooted->getPosition(), BattleHex(9, 5));
	EXPECT_TRUE(entangleBonuses(rooted)->empty());
}

TEST_F(NewHorizonsEntangleTest, RefreshSnapshotsTheNewDurationAndExpiresNormally)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	ASSERT_TRUE(castEntangle());
	ASSERT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 2);
	while(battle()->getRound() == 0)
		advanceRound();
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 2);
	// The first next-round packet starts round one; a second one moves the
	// shared spell-action budget to the subsequent round, matching Crusade's
	// authoritative refresh fixture.
	advanceRound();
	ASSERT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 1);
	ASSERT_TRUE(castEntangle());
	ASSERT_EQ(entangleBonuses(rooted)->size(), 1u);
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 2);
	advanceRound();
	ASSERT_EQ(entangleBonuses(rooted)->size(), 1u);
	EXPECT_EQ(entangleBonuses(rooted)->front()->turnsRemain, 1);
	advanceRound();
	EXPECT_TRUE(entangleBonuses(rooted)->empty());
}

TEST_F(NewHorizonsEntangleTest, V1SavedRosterDoesNotAdmitEntangle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, newHorizonsMagic::RULESET_VERSION));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), entangleSpell()));
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castEntangle());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(cast(BattleSide::ATTACKER, SpellID::HASTE, friendly))
		<< "Rejected V1 spell requests must not consume the attacker's Hero Action";
}

TEST_F(NewHorizonsEntangleTest, V2SavedRosterWithEntangleRowCannotCastV3Mechanic)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), entangleSpell()));
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castEntangle()) << "Stale V2 requests must be rejected before mana/action spending";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(cast(BattleSide::ATTACKER, SpellID::HASTE, friendly))
		<< "Rejected V2 spell requests must not consume the attacker's Hero Action";
}
