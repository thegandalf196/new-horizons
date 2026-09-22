/*
 * NewHorizonsMetamagicTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"

namespace
{
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto arcaneEconomy = "new-horizons:metamagic.arcaneEconomy";
constexpr auto formulaReserve = "new-horizons:metamagic.formulaReserve";
constexpr auto grandMetamagic = "new-horizons:metamagic.grandMetamagic";
}

class NewHorizonsMetamagicTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int rank, std::initializer_list<const char *> perks = {})
	{
		startGame();
		const auto decoded = SecondarySkill::decode(metamagicSkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
		for(const auto perk : perks)
			attackerSideHero->applyPerkSelection({metamagicSkill, perk});

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("new-horizons:counterspell")));
		attackerSideHero->mana = 1000;

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		beginCombat();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate(attacker);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	bool cast(SpellID spell, const CStack * target, bool followup = false, bool grand = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.metamagicGrand = grand;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool decline()
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), BattleAction::makeMetamagicDecline(BattleSide::ATTACKER));
	}

	bool castLandMineFollowup(std::initializer_list<int> hexes)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::LAND_MINE;
		action.metamagicFollowup = true;
		for(const auto hex : hexes)
			action.aimToHex(BattleHex(hex));
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	int followupPower(SpellID spell, const CStack * target, bool grand = false)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setMetamagicFollowup(true);
		event.setMetamagicGrand(grand);
		event.setMetamagicTargetUnitId(target->unitId());
		const auto mechanics = spell.toSpell()->battleMechanics(&event);
		return mechanics->getEffectPower();
	}

	CStack * attacker = nullptr;
	CStack * defender = nullptr;
};

TEST_F(NewHorizonsMetamagicTest, InitialOfferDoesNotConsumeUseAndDeclineIsAuthoritative)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, 1);

	// The immediate sequence blocks every ordinary action until the player
	// accepts a spell or submits the explicit End/Decline command.
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), BattleAction::makeWait(attacker)));
	BattleAction forgedWait = BattleAction::makeWait(attacker);
	forgedWait.side = BattleSide::DEFENDER;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), forgedWait));
	ASSERT_TRUE(decline());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);

	// A forged decline cannot clear an already-resolved offer.
	EXPECT_FALSE(decline());
}

TEST_F(NewHorizonsMetamagicTest, MalformedDeclineStackDoesNotPublishOrClearSequence)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto pendingBefore = battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount;
	const auto sequenceBefore = battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells;

	BattleAction malformed = BattleAction::makeMetamagicDecline(BattleSide::ATTACKER);
	malformed.stackNumber = attacker->unitId();
	bool accepted = false;
	EXPECT_NO_THROW(accepted = gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), malformed));
	EXPECT_FALSE(accepted);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, pendingBefore);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells, sequenceBefore);
	ASSERT_TRUE(decline());
}

TEST_F(NewHorizonsMetamagicTest, AcceptedFollowupConsumesOneUseWithoutAnotherHeroActionOrChain)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));

	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.metamagicUsesConsumed, 1);
	EXPECT_EQ(side.metamagicPendingCount, 0);
	EXPECT_EQ(side.castSpellsCount, 1);

	// The additional cast cannot trigger another sequence and the original
	// Hero Action remains spent.
	EXPECT_FALSE(cast(SpellID::DISPEL, defender));
	BattleAction forged = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
	forged.actionType = EActionType::HERO_SPELL;
	forged.spell = SpellID::DISPEL;
	forged.aimToUnit(defender);
	forged.metamagicFollowup = true;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), forged));
}

TEST_F(NewHorizonsMetamagicTest, GrandMetamagicIsExplicitAndProvidesExactlyTwoExtras)
{
	prepare(3, {grandMetamagic});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);

	ASSERT_TRUE(cast(SpellID::SLOW, defender, true, true));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 1);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicGrandUsed);

	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 1);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, 1);
}

TEST_F(NewHorizonsMetamagicTest, FormulaReserveRefundsOnlyAfterGrandSequenceResolves)
{
	prepare(3, {grandMetamagic, formulaReserve});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto manaBeforeGrand = attackerSideHero->mana;
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true, true));
	const auto manaAfterGrand = attackerSideHero->mana;
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed, false);

	// Ending the second leg still resolves the first Metamagic sequence and
	// grants Formula Reserve's three mana exactly once.
	ASSERT_TRUE(decline());
	EXPECT_EQ(attackerSideHero->mana, manaAfterGrand + 3);
	EXPECT_GT(manaBeforeGrand, manaAfterGrand);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_FALSE(decline());
}

TEST_F(NewHorizonsMetamagicTest, ArcaneEconomyReducesOnlyAcceptedFollowupCost)
{
	prepare(1, {arcaneEconomy});
	const auto ordinaryCost = attackerSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	const auto listedCost = attackerSideHero->getSpellCost(SpellID(SpellID::SLOW).toSpell());
	attackerSideHero->mana = ordinaryCost + std::max(1, listedCost - 2);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto manaBeforeFollowup = attackerSideHero->mana;
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(attackerSideHero->mana,
		manaBeforeFollowup - std::max(1, listedCost - 2));
}

TEST_F(NewHorizonsMetamagicTest, FollowupSpellbookValidationKeepsSelectiveDispelFallback)
{
	prepare(1);
	const auto sorcery = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 2, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({
		"new-horizons:sorceryMagic",
		"new-horizons:sorceryMagic.selectiveDispel"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"));

	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusSource::SPELL_EFFECT, 1,
		BonusSourceID(SpellID(SpellID::BLESS))));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	const auto * dispel = SpellID(SpellID::DISPEL).toSpell();
	spells::detail::ProblemImpl problem;
	// The ordinary smart Dispel has no legal target here; the Selective Dispel
	// fallback must remain available while the Metamagic offer is pending.
	EXPECT_TRUE(dispel->canBeCast(problem, battle(), spells::Mode::HERO,
		attackerSideHero, true));
}

TEST_F(NewHorizonsMetamagicTest, SpellSequencingAndPerfectSequenceModifyDifferentSpellPower)
{
	prepare(3, {newHorizonsMagic::METAMAGIC_SPELL_SEQUENCING.data(),
		newHorizonsMagic::METAMAGIC_PERFECT_SEQUENCE.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	// Haste is Sorcery and Bless is Light, and Bless is not already in the
	// sequence: the two independent rank-Expert modifiers stack (15% + 20%).
	EXPECT_EQ(followupPower(SpellID::BLESS, attacker), 13);
}

TEST_F(NewHorizonsMetamagicTest, MagicArrowFollowupUsesArcaneEconomyInAuthoritativeCost)
{
	prepare(1, {arcaneEconomy});
	const auto magicArrow = SpellID(SpellID::MAGIC_ARROW);
	const auto ordinaryCost = attackerSideHero->getSpellCost(magicArrow.toSpell());
	const auto triggerCost = attackerSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	attackerSideHero->mana = triggerCost + std::max(1, ordinaryCost - 2);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, magicArrow.toSpell());
	preview.setMetamagicFollowup(true);
	preview.setMetamagicTargetUnitId(defender->unitId());
	preview.setOvercharge(0);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(preview.getSpell()->battleMechanics(&preview)->canBeCast(problem));

	const auto manaBefore = attackerSideHero->mana;
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = magicArrow;
	action.metamagicFollowup = true;
	action.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->mana, manaBefore - std::max(1, ordinaryCost - 2));
}

TEST_F(NewHorizonsMetamagicTest, FollowupLogNamesSecondAndThirdMagicArrowDamage)
{
	prepare(3, {grandMetamagic});
	// Keep the second and third legs independently targetable even if the
	// provisional Magic Arrow damage kills an entire ten-unit stack.
	CStack * secondTarget = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(rightHex + 2), 10);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true, true));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, secondTarget, true));

	const auto arrows = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(arrows.size(), 2u);
	ASSERT_GT(arrows[0].damage, 0);
	ASSERT_GT(arrows[0].killed, 0u);
	ASSERT_GT(arrows[1].damage, 0);
	ASSERT_GT(arrows[1].killed, 0u);

	const auto contains = [](const RecordedCast & cast, const std::string & text)
	{
		return std::any_of(cast.logLines.begin(), cast.logLines.end(), [&](const std::string & line)
		{
			return line.find(text) != std::string::npos;
		});
	};
	EXPECT_TRUE(contains(arrows[0], "casts a second Magic Arrow through Metamagic, dealing"));
	EXPECT_TRUE(contains(arrows[0], "dealing " + std::to_string(arrows[0].damage) + " damage"));
	EXPECT_TRUE(contains(arrows[0], "killing " + std::to_string(arrows[0].killed)));
	EXPECT_TRUE(contains(arrows[1], "casts a third Magic Arrow through Metamagic, dealing"));
	EXPECT_TRUE(contains(arrows[1], "dealing " + std::to_string(arrows[1].damage) + " damage"));
	EXPECT_TRUE(contains(arrows[1], "killing " + std::to_string(arrows[1].killed)));
}

TEST_F(NewHorizonsMetamagicTest, FollowupLogReportsAffectedNonDamageOutcome)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));

	const auto slows = server.castsOf(SpellID::SLOW);
	ASSERT_EQ(slows.size(), 1u);
	EXPECT_EQ(slows.front().damage, 0);
	EXPECT_EQ(slows.front().killed, 0u);
	EXPECT_TRUE(std::any_of(slows.front().logLines.begin(), slows.front().logLines.end(), [](const std::string & line)
	{
		return line.find("casts a second Slow through Metamagic, affecting 1 target.") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, FollowupLogDoesNotCallSuccessfulObstacleSpellNoEffect)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(castLandMineFollowup({70, 71}));
	ASSERT_EQ(battle()->obstacles.size(), 2u);

	const auto mines = server.castsOf(SpellID::LAND_MINE);
	ASSERT_EQ(mines.size(), 1u);
	EXPECT_TRUE(std::any_of(mines.front().logLines.begin(), mines.front().logLines.end(), [](const std::string & line)
	{
		return line.find("casts a second Land Mine through Metamagic, resolving successfully") != std::string::npos
			&& line.find("no effect") == std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, SplitFocusAddsPowerOnlyForTheOtherTarget)
{
	prepare(3, {newHorizonsMagic::METAMAGIC_SPLIT_FOCUS.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(followupPower(SpellID::SLOW, attacker), 10);
	EXPECT_EQ(followupPower(SpellID::SLOW, defender), 11);
}

TEST_F(NewHorizonsMetamagicTest, SplitFocusNeedsAValidFirstTarget)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPLIT_FOCUS.data()});
	const auto counterspell = SpellID(SpellID::decode("new-horizons:counterspell"));
	BattleAction first;
	first.actionType = EActionType::HERO_SPELL;
	first.side = BattleSide::ATTACKER;
	first.spell = counterspell;
	first.aimToHex(BattleHex::INVALID);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), first));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicFirstTargetUnitId,
		newHorizonsMagic::INVALID_METAMAGIC_TARGET);
	EXPECT_EQ(followupPower(SpellID::SLOW, defender), 10);
}

TEST_F(NewHorizonsMetamagicTest, EchoedDurationOnlyAffectsTheAdditionalSpell)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	const SpellID slow(SpellID::SLOW);
	spells::BattleCast ordinary(battle(), attackerSideHero, spells::Mode::HERO, slow.toSpell());
	spells::BattleCast followup(battle(), attackerSideHero, spells::Mode::HERO, slow.toSpell());
	followup.setMetamagicFollowup(true);
	followup.setMetamagicTargetUnitId(defender->unitId());
	const auto ordinaryMechanics = slow.toSpell()->battleMechanics(&ordinary);
	const auto followupMechanics = slow.toSpell()->battleMechanics(&followup);
	EXPECT_EQ(followupMechanics->getEffectDuration(), ordinaryMechanics->getEffectDuration() + 1);
}

TEST_F(NewHorizonsMetamagicTest, FocusedPairingIgnoresTwentyPercentOfMagicalReduction)
{
	prepare(1, {newHorizonsMagic::METAMAGIC_FOCUSED_PAIRING.data()});
	ASSERT_TRUE(cast(SpellID::SLOW, defender));
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));

	const SpellID magicArrow(SpellID::MAGIC_ARROW);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, magicArrow.toSpell());
	event.setMetamagicFollowup(true);
	event.setMetamagicTargetUnitId(defender->unitId());
	const auto mechanics = magicArrow.toSpell()->battleMechanics(&event);
	const auto raw = mechanics->getEffectValue();
	EXPECT_EQ(mechanics->adjustEffectValue(defender), raw * 60 / 100);

	// The authoritative BattleSpellCast packet clears the pending sequence
	// before applying effects.  Verify the same snapshot still reaches the
	// server-applied damage path, rather than only the direct mechanics helper.
	const auto healthBefore = defender->getAvailableHealth();
	ASSERT_TRUE(cast(magicArrow, defender, true));
	EXPECT_EQ(healthBefore - defender->getAvailableHealth(), raw * 60 / 100);
}

TEST_F(NewHorizonsMetamagicTest, PendingSequenceFollowsControllerForHypnotizedAction)
{
	prepare(1);
	const auto decoded = SecondarySkill::decode(metamagicSkill);
	ASSERT_GE(decoded, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(decoded), 1, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
	defenderSideHero->mana = 1000;

	activate(defender);
	BattleAction trigger;
	trigger.actionType = EActionType::HERO_SPELL;
	trigger.side = BattleSide::DEFENDER;
	trigger.spell = SpellID::HASTE;
	trigger.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), trigger));
	ASSERT_EQ(battle()->getSide(BattleSide::DEFENDER).metamagicPendingCount, 1);

	// The stack retains ATTACKER as its origin side, but Hypnotize gives the
	// DEFENDER player control.  A unit action must therefore not bypass the
	// defender's pending immediate follow-up by carrying ATTACKER in ba.side.
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	attacker->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(attacker), PlayerColor(1));
	activate(attacker);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(1), BattleAction::makeWait(attacker)));
	EXPECT_EQ(battle()->getSide(BattleSide::DEFENDER).metamagicPendingCount, 1);
}

TEST_F(NewHorizonsMetamagicTest, CountersequenceUsesTheCeiledOnePointSevenFiveMultiplier)
{
	prepare(1, {newHorizonsMagic::METAMAGIC_COUNTERSEQUENCE.data()});
	const auto counterspell = SpellID(SpellID::decode("new-horizons:counterspell"));
	ASSERT_NE(counterspell, SpellID::NONE);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	BattleAction followup;
	followup.actionType = EActionType::HERO_SPELL;
	followup.side = BattleSide::ATTACKER;
	followup.spell = counterspell;
	followup.aimToHex(BattleHex::INVALID);
	followup.metamagicFollowup = true;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), followup));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicCountersequenceArmed);
	const auto manaBeforeEnemySpell = attackerSideHero->mana;

	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
	defenderSideHero->mana = 1000;
	activate(defender);
	BattleAction enemySpell;
	enemySpell.actionType = EActionType::HERO_SPELL;
	enemySpell.side = BattleSide::DEFENDER;
	enemySpell.spell = SpellID::HASTE;
	enemySpell.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), enemySpell));
	EXPECT_EQ(attackerSideHero->mana,
		manaBeforeEnemySpell - newHorizonsMagic::counterspellCost(4, false, true));
	EXPECT_FALSE(defender->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE)))));
}

TEST_F(NewHorizonsMetamagicTest, SpellEchoDataReplacesRetiredSpellBufferAtAdvancedRank)
{
	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & entries = perks["skills"][metamagicSkill]["perks"].Vector();
	const auto found = std::find_if(entries.begin(), entries.end(), [](const JsonNode & entry)
	{
		return entry["id"].String() == "new-horizons:metamagic.spellEcho";
	});
	ASSERT_NE(found, entries.end());
	EXPECT_EQ((*found)["name"].String(), "Spell Echo");
	EXPECT_EQ((*found)["requires"].String(), "advanced");
	EXPECT_EQ((*found)["effect"]["status"].String(), "active");
	EXPECT_NE((*found)["description"].String().find("repeats the first Spell"), std::string::npos);
	EXPECT_NE((*found)["description"].String().find("+25%"), std::string::npos);
	EXPECT_EQ(std::find_if(entries.begin(), entries.end(), [](const JsonNode & entry)
	{
		return entry["id"].String() == "new-horizons:metamagic.spellBuffer";
	}), entries.end());
}

TEST_F(NewHorizonsMetamagicTest, SpellEchoBoostsAdditionalRepeatedSpell)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPELL_ECHO.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	// This shared BattleCast preview is also the path used by BattleAI's
	// hypothetical spell evaluation.  The follow-up repeats the first spell,
	// so Spell Echo adds its 25% Spell Power-derived component.
	EXPECT_EQ(followupPower(SpellID::HASTE, attacker), 12);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker, true));
}

TEST_F(NewHorizonsMetamagicTest, SpellEchoDoesNotBoostADifferentAdditionalSpell)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPELL_ECHO.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(followupPower(SpellID::SLOW, defender), 10);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_TRUE(defender->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::SLOW)))));
}
