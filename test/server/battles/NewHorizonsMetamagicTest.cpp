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
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/Problem.h"

namespace
{
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto arcaneEconomy = "new-horizons:metamagic.arcaneEconomy";
constexpr auto formulaReserve = "new-horizons:metamagic.formulaReserve";
constexpr auto grandMetamagic = "new-horizons:metamagic.grandMetamagic";

SpellID phantomArmySpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::PHANTOM_ARMY_SPELL));
}
}

class NewHorizonsMetamagicTest : public HeroCommandFixture
{
protected:
	bool legacyCloneRoster = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		// Old saved rulesets treated a spell with no `active` marker as enabled.
		// Keep that compatibility path narrowly scoped to the legacy Clone test.
		if(legacyCloneRoster)
			magicRules["spells"]["core:clone"].Struct().erase("active");
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int rank, std::initializer_list<const char *> perks = {}, bool useLegacyCloneRoster = false)
	{
		legacyCloneRoster = useLegacyCloneRoster;
		startGame();
		const auto decoded = SecondarySkill::decode(metamagicSkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
		// Ordinary restoration tests need actual Normal capacity. A zero-Knowledge
		// fixture with a scalar total would now place all funds in Buffer instead.
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		for(const auto perk : perks)
			attackerSideHero->applyPerkSelection({metamagicSkill, perk});

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		attackerSideHero->addSpellToSpellbook(SpellID::PRAYER);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		attackerSideHero->addSpellToSpellbook(SpellID::CHAIN_LIGHTNING);
		attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
		attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
		attackerSideHero->addSpellToSpellbook(phantomArmySpell());
		attackerSideHero->addSpellToSpellbook(SpellID::SUMMON_AIR_ELEMENTAL);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("core:iceBolt")));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("new-horizons:counterspell")));
		setTestSpellPointTotal(attackerSideHero, 1000);

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

	bool castAtHex(SpellID spell, BattleHex target, bool followup = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.aimToHex(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void castNoTargetFollowupDirect(SpellID spell, bool counterspelled = false)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setSpellLevel(3);
		event.setMetamagicFollowup(true);
		if(counterspelled)
			event.setCounterspell(BattleSide::DEFENDER, true);
		event.cast(gameHandler->spellcastEnvironment(), {});
	}

	static std::string creatureName(const CStack * unit, int32_t count)
	{
		MetaString text;
		text.appendRawString("%s");
		unit->addNameReplacement(text, count);
		return text.toString(LIBRARY->staticTexts());
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

	int followupDuration(SpellID spell, const CStack * target)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setMetamagicFollowup(true);
		event.setMetamagicTargetUnitId(target->unitId());
		return spell.toSpell()->battleMechanics(&event)->getEffectDuration();
	}

	void damage(CStack * target, int64_t amount)
	{
		auto state = target->acquireState();
		state->damage(amount);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -amount;
		gameHandler->sendAndApply(change);
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

	// A forged unit action is still rejected independently of the Spell Action.
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

TEST_F(NewHorizonsMetamagicTest, SpellActionSurvivesCreatureWaitAndCanBeSpentLaterInRound)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto before = battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER);
	EXPECT_EQ(before.heroActions, 0u);
	EXPECT_EQ(before.orderActions, 0u);
	EXPECT_EQ(before.spellActions, 1u);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), BattleAction::makeWait(attacker)));
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER), before);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);

	// Return to this side's legal activation window without crossing a round.
	activate(attacker);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker, true));
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).spellActions, 0u);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 1);
}

TEST_F(NewHorizonsMetamagicTest, LegacyPendingMetamagicWithoutManaDoesNotBlockCreatureActions)
{
	useCommands = false;
	prepare(1);
	ASSERT_FALSE(battle()->battleUsesHeroCommands());
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(cast(SpellID::SLOW, defender, true));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), BattleAction::makeWait(attacker)));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
}

TEST_F(NewHorizonsMetamagicTest, UnspentSpellActionExpiresAndOneHeroActionReturnsNextRound)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).spellActions, 1u);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	const auto counts = battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER);
	EXPECT_EQ(counts.heroActions, 1u);
	EXPECT_EQ(counts.orderActions, 0u);
	EXPECT_EQ(counts.spellActions, 0u);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
	activate(attacker);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).heroActions, 0u);
	EXPECT_EQ(battle()->battleHeroActionAllowanceCounts(BattleSide::ATTACKER).spellActions, 1u);
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
	const auto manaBeforeGrand = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true, true));
	const auto manaAfterGrand = attackerSideHero->getManaAvailable();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed, false);

	// Ending the second leg still resolves the first Metamagic sequence and
	// grants Formula Reserve's three mana exactly once.
	ASSERT_TRUE(decline());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaAfterGrand + 3);
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
	setTestSpellPointTotal(attackerSideHero, ordinaryCost + std::max(1, listedCost - 2));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto manaBeforeFollowup = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	EXPECT_EQ(attackerSideHero->getManaAvailable(),
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
	setTestSpellPointTotal(attackerSideHero, triggerCost + std::max(1, ordinaryCost - 2));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, magicArrow.toSpell());
	preview.setMetamagicFollowup(true);
	preview.setMetamagicTargetUnitId(defender->unitId());
	preview.setOvercharge(0);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(preview.getSpell()->battleMechanics(&preview)->canBeCast(problem));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = magicArrow;
	action.metamagicFollowup = true;
	action.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - std::max(1, ordinaryCost - 2));
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
	EXPECT_TRUE(contains(arrows[0], "damage to Pikemen (" + std::to_string(arrows[0].killed) + " killed)"));
	EXPECT_TRUE(contains(arrows[1], "casts a third Magic Arrow through Metamagic, dealing"));
	EXPECT_TRUE(contains(arrows[1], "dealing " + std::to_string(arrows[1].damage) + " damage"));
	EXPECT_TRUE(contains(arrows[1], "damage to Pikemen (" + std::to_string(arrows[1].killed) + " killed)"));
}

TEST_F(NewHorizonsMetamagicTest, FollowupDamageLogUsesAuthoritativePacketValuesAcrossRebirth)
{
	prepare(1);
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	ASSERT_GT(casts.front().damage, 0);
	ASSERT_GT(casts.front().killed, 0u);
	ASSERT_TRUE(defender->alive());
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		const auto packetOutcome = std::to_string(casts.front().damage) + " damage to Pikemen ("
			+ std::to_string(casts.front().killed) + " killed)";
		return line.find(packetOutcome) != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, FollowupHealingLogUsesCappedAuthoritativeHealthDelta)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	damage(attacker, 1);
	ASSERT_EQ(attacker->getAvailableHealth(), attacker->getTotalHealth() - 1);
	ASSERT_TRUE(cast(SpellID::CURE, attacker, true));

	const auto casts = server.castsOf(SpellID::CURE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Cure through Metamagic, restoring 1 health to Pikemen.")
			!= std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, FollowupResurrectionLogUsesAuthoritativeCountAndHealth)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto healthBefore = attacker->getAvailableHealth();
	damage(attacker, healthBefore);
	ASSERT_FALSE(attacker->alive());
	ASSERT_TRUE(cast(SpellID::RESURRECTION, attacker, true));
	const auto restored = attacker->getAvailableHealth();
	const auto resurrected = attacker->getCount();
	ASSERT_GT(restored, 0);
	ASSERT_GT(resurrected, 0);

	const auto casts = server.castsOf(SpellID::RESURRECTION);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find("casts a second Resurrection through Metamagic, restoring "
			+ std::to_string(restored) + " health to Pikemen (" + std::to_string(resurrected)
			+ " resurrected).") != std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, FollowupResurrectionAggregatesPartialCasualtiesPerTarget)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto countBeforeDamage = attacker->getCount();
	damage(attacker, attacker->getMaxHealth() * 5);
	const auto countBeforeHealing = attacker->getCount();
	const auto healthBeforeHealing = attacker->getAvailableHealth();
	ASSERT_LT(countBeforeHealing, countBeforeDamage);
	ASSERT_TRUE(cast(SpellID::RESURRECTION, attacker, true));
	const auto restored = attacker->getAvailableHealth() - healthBeforeHealing;
	const auto resurrected = attacker->getCount() - countBeforeHealing;
	ASSERT_GT(restored, 0);
	ASSERT_GT(resurrected, 0);

	const auto casts = server.castsOf(SpellID::RESURRECTION);
	ASSERT_EQ(casts.size(), 1u);
	const auto causalLines = std::ranges::count_if(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Resurrection through Metamagic") != std::string::npos;
	});
	EXPECT_EQ(causalLines, 1);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find("restoring " + std::to_string(restored) + " health to Pikemen ("
			+ std::to_string(resurrected) + " resurrected)") != std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, CounterspelledHealingFollowupRecordsNoHealingOutcome)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	damage(attacker, 1);
	const auto healthBefore = attacker->getAvailableHealth();
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(cast(SpellID::CURE, attacker, true));
	EXPECT_EQ(attacker->getAvailableHealth(), healthBefore);

	const auto casts = server.castsOf(SpellID::CURE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Cure through Metamagic, but the spell was counterspelled.")
			!= std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, FollowupSummonLogUsesFinalAuthoritativeIdentityAndCountOnce)
{
	prepare(1);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	castNoTargetFollowupDirect(SpellID::SUMMON_AIR_ELEMENTAL);

	const CStack * summoned = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks())
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isSummoned() && !unit->isClone())
			summoned = unit;
	ASSERT_NE(summoned, nullptr);
	ASSERT_GT(summoned->getCount(), 0);

	const auto casts = server.castsOf(SpellID::SUMMON_AIR_ELEMENTAL);
	ASSERT_EQ(casts.size(), 1u);
	const auto expected = "casts a second Air Elemental through Metamagic, summoning "
		+ std::to_string(summoned->getCount()) + " " + creatureName(summoned, summoned->getCount()) + ".";
	EXPECT_EQ(std::ranges::count_if(casts.front().logLines, [&](const std::string & line)
	{
		return line.find("through Metamagic, summoning") != std::string::npos;
	}), 1);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find(expected) != std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, LegacySavedCloneRosterFollowupLogUsesFinalIdentityAndCountOnce)
{
	prepare(1, {}, true);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::CLONE, attacker, true));

	const CStack * clone = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks())
		if(unit->unitSide() == BattleSide::ATTACKER && unit->isClone())
			clone = unit;
	ASSERT_NE(clone, nullptr);
	ASSERT_EQ(clone->creatureId(), attacker->creatureId());
	ASSERT_EQ(clone->getCount(), attacker->getCount());

	const auto casts = server.castsOf(SpellID::CLONE);
	ASSERT_EQ(casts.size(), 1u);
	const auto expected = "casts a second Clone through Metamagic, creating a clone of "
		+ std::to_string(clone->getCount()) + " " + creatureName(clone, clone->getCount()) + ".";
	EXPECT_EQ(std::ranges::count_if(casts.front().logLines, [&](const std::string & line)
	{
		return line.find("through Metamagic, creating a clone of") != std::string::npos;
	}), 1);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find(expected) != std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, FollowupPhantomArmyLogReportsResolvedStackIntegrityAndDurationOnce)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(phantomArmySpell(), attacker, true));

	const battle::Unit * phantom = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks())
		if(unit->unitSide() == BattleSide::ATTACKER && unit->getPhantomInitialIntegrity() > 0)
			phantom = unit;
	ASSERT_NE(phantom, nullptr);
	ASSERT_EQ(phantom->getCount(), attacker->getCount());
	ASSERT_GT(phantom->getPhantomIntegrity(), 0);
	EXPECT_EQ(phantom->getPhantomIntegrity(), phantom->getPhantomInitialIntegrity());

	const auto casts = server.castsOf(phantomArmySpell());
	ASSERT_EQ(casts.size(), 1u);
	const auto expected = "casts a second Phantom Army through Metamagic, creating a phantom stack of "
		+ std::to_string(phantom->getCount()) + " " + creatureName(dynamic_cast<const CStack *>(phantom), phantom->getCount())
		+ " with " + std::to_string(phantom->getPhantomIntegrity()) + "/"
		+ std::to_string(phantom->getPhantomInitialIntegrity()) + " integrity for "
		+ std::to_string(newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS) + " rounds.";
	EXPECT_EQ(std::ranges::count_if(casts.front().logLines, [](const std::string & line)
	{
		return line.find("through Metamagic, creating a phantom stack of") != std::string::npos;
	}), 1);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [&](const std::string & line)
	{
		return line.find(expected) != std::string::npos;
	})) << ::testing::PrintToString(casts.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, CounterspelledPhantomArmyFollowupLogsNoCreationOutcome)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto stackCountBefore = battle()->battleGetAllStacks().size();
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(cast(phantomArmySpell(), attacker, true));
	EXPECT_EQ(battle()->battleGetAllStacks().size(), stackCountBefore);

	const auto casts = server.castsOf(phantomArmySpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Phantom Army through Metamagic, but the spell was counterspelled.")
			!= std::string::npos;
	}));
	EXPECT_TRUE(std::ranges::none_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("phantom stack") != std::string::npos || line.find("integrity for") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, CounterspelledSummonFollowupAddsNoUnitOrSummonOutcome)
{
	prepare(1);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto unitCountBefore = battle()->battleGetAllStacks().size();
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 1000);
	castNoTargetFollowupDirect(SpellID::SUMMON_AIR_ELEMENTAL, true);
	EXPECT_EQ(battle()->battleGetAllStacks().size(), unitCountBefore);

	const auto casts = server.castsOf(SpellID::SUMMON_AIR_ELEMENTAL);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Air Elemental through Metamagic, but the spell was counterspelled.")
			!= std::string::npos;
	}));
	EXPECT_TRUE(std::ranges::none_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("summoning") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, FollowupChainLightningLogNamesEveryDamagedStack)
{
	prepare(1);
	addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex + 2), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(rightHex + 3), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:monk"), BattleHex(rightHex + 4), 100);

	struct Before
	{
		const CStack * unit;
		int64_t health;
		int32_t count;
	};
	std::vector<Before> before;
	for(const auto * unit : battle()->battleGetAllStacks(true))
		before.push_back({unit, unit->getAvailableHealth(), unit->getCount()});

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::CHAIN_LIGHTNING, defender, true));
	const auto casts = server.castsOf(SpellID::CHAIN_LIGHTNING);
	ASSERT_EQ(casts.size(), 1u);
	const auto lineIt = std::ranges::find_if(casts.front().logLines, [](const std::string & candidate)
	{
		return candidate.find("through Metamagic, dealing") != std::string::npos;
	});
	ASSERT_NE(lineIt, casts.front().logLines.end());
	const auto & line = *lineIt;

	int damagedStacks = 0;
	size_t previousOutcomePosition = 0;
	for(const auto & injuryPack : server.injuries)
	{
		for(const auto & injury : injuryPack.stacks)
		{
			if(injury.damageAmount <= 0)
				continue;
			const auto snapshot = std::ranges::find(before, injury.stackAttacked,
				[](const Before & value) { return value.unit->unitId(); });
			ASSERT_NE(snapshot, before.end());
			++damagedStacks;
			const auto outcome = std::to_string(injury.damageAmount) + " damage to "
				+ creatureName(snapshot->unit, snapshot->count) + " ("
				+ std::to_string(injury.killedAmount) + " killed)";
			const auto outcomePosition = line.find(outcome);
			EXPECT_NE(outcomePosition, std::string::npos) << outcome << " missing from: " << line;
			if(damagedStacks > 1 && outcomePosition != std::string::npos)
			{
				EXPECT_GT(outcomePosition, previousOutcomePosition) << "Target outcomes must retain packet order";
			}
			previousOutcomePosition = outcomePosition;
		}
	}
	EXPECT_EQ(damagedStacks, 4);
}

TEST_F(NewHorizonsMetamagicTest, FollowupAreaDamageLogNamesEveryDamagedStack)
{
	prepare(1);
	const auto * archer = addStack(BattleSide::DEFENDER,
		creatureByName("core:archer"), BattleHex(rightHex + 1), 100);
	const auto * griffin = addStack(BattleSide::DEFENDER,
		creatureByName("core:griffin"), BattleHex(rightHex + GameConstants::BFIELD_WIDTH), 100);
	const std::array<const CStack *, 4> targets{attacker, defender, archer, griffin};
	std::map<uint32_t, std::pair<int64_t, int32_t>> before;
	for(const auto * target : targets)
		before[target->unitId()] = {target->getAvailableHealth(), target->getCount()};

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(castAtHex(SpellID::FIREBALL, defender->getPosition(), true));
	const auto casts = server.castsOf(SpellID::FIREBALL);
	ASSERT_EQ(casts.size(), 1u);
	const auto lineIt = std::ranges::find_if(casts.front().logLines, [](const std::string & candidate)
	{
		return candidate.find("through Metamagic, dealing") != std::string::npos;
	});
	ASSERT_NE(lineIt, casts.front().logLines.end());

	int damagedStacks = 0;
	for(const auto * target : targets)
	{
		const auto [health, count] = before.at(target->unitId());
		const auto damage = health - target->getAvailableHealth();
		if(damage <= 0)
			continue;
		++damagedStacks;
		const auto killed = count - target->getCount();
		const auto outcome = std::to_string(damage) + " damage to "
			+ creatureName(target, count) + " (" + std::to_string(killed) + " killed)";
		EXPECT_NE(lineIt->find(outcome), std::string::npos) << outcome << " missing from: " << *lineIt;
	}
	EXPECT_EQ(damagedStacks, 4);
}

TEST_F(NewHorizonsMetamagicTest, FollowupTimedEffectLogNamesSpellTargetAndAuthoritativeDuration)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto expectedDuration = followupDuration(SpellID(SpellID::SLOW), defender);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));

	const auto slows = server.castsOf(SpellID::SLOW);
	ASSERT_EQ(slows.size(), 1u);
	EXPECT_EQ(slows.front().damage, 0);
	EXPECT_EQ(slows.front().killed, 0u);
	EXPECT_TRUE(std::any_of(slows.front().logLines.begin(), slows.front().logLines.end(), [&](const std::string & line)
	{
		return line.find("casts a second Slow through Metamagic, applying Slow to Pikemen for "
			+ std::to_string(expectedDuration) + (expectedDuration == 1 ? " turn." : " turns.")) != std::string::npos;
	})) << ::testing::PrintToString(slows.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, FollowupDispelLogNamesRemovedSpellTargetAndRemainingDuration)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto haste = attacker->getBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE))));
	ASSERT_NE(haste, nullptr);
	ASSERT_TRUE(Bonus::NTurns(haste.get()));
	const auto remainingDuration = haste->turnsRemain;

	ASSERT_TRUE(cast(SpellID::DISPEL, attacker, true));
	const auto dispels = server.castsOf(SpellID::DISPEL);
	ASSERT_EQ(dispels.size(), 1u);
	EXPECT_FALSE(attacker->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_TRUE(std::ranges::any_of(dispels.front().logLines, [&](const std::string & line)
	{
		return line.find("casts a second Dispel through Metamagic, removing Haste from Pikemen with "
			+ std::to_string(remainingDuration)
			+ (remainingDuration == 1 ? " turn remaining." : " turns remaining.")) != std::string::npos;
	})) << ::testing::PrintToString(dispels.front().logLines);
}

TEST_F(NewHorizonsMetamagicTest, FollowupMultiBonusSpellProducesOneCausalEffectOutcome)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto expectedDuration = followupDuration(SpellID(SpellID::PRAYER), attacker);
	ASSERT_TRUE(cast(SpellID::PRAYER, attacker, true));

	const auto prayers = server.castsOf(SpellID::PRAYER);
	ASSERT_EQ(prayers.size(), 1u);
	const auto line = std::ranges::find_if(prayers.front().logLines, [](const std::string & candidate)
	{
		return candidate.find("casts a second Prayer through Metamagic") != std::string::npos;
	});
	ASSERT_NE(line, prayers.front().logLines.end()) << ::testing::PrintToString(prayers.front().logLines);
	const auto outcome = "applying Prayer to Pikemen for " + std::to_string(expectedDuration)
		+ (expectedDuration == 1 ? " turn" : " turns");
	const auto first = line->find(outcome);
	ASSERT_NE(first, std::string::npos) << *line;
	EXPECT_EQ(line->find(outcome, first + outcome.size()), std::string::npos) << *line;
	EXPECT_EQ(line->find("refreshing Prayer"), std::string::npos) << *line;
}

TEST_F(NewHorizonsMetamagicTest, FollowupMixedDamageAndStatusLogReportsBothOutcomes)
{
	prepare(1);
	const SpellID iceBolt(SpellID::decode("core:iceBolt"));
	ASSERT_NE(iceBolt, SpellID::NONE);
	const auto * target = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(rightHex + 2), 100);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(iceBolt, target, true));

	const auto casts = server.castsOf(iceBolt);
	ASSERT_EQ(casts.size(), 1u);
	ASSERT_GT(casts.front().damage, 0);
	const auto line = std::ranges::find_if(casts.front().logLines, [](const std::string & candidate)
	{
		return candidate.find("casts a second Ice Bolt through Metamagic") != std::string::npos;
	});
	ASSERT_NE(line, casts.front().logLines.end()) << ::testing::PrintToString(casts.front().logLines);
	EXPECT_NE(line->find("dealing " + std::to_string(casts.front().damage) + " damage to Pikemen"),
		std::string::npos) << *line;
	EXPECT_NE(line->find("and applying Ice Bolt to Pikemen"), std::string::npos) << *line;
}

TEST_F(NewHorizonsMetamagicTest, FollowupLongerDurationRefreshIsReportedAsUpdate)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto expectedDuration = followupDuration(SpellID(SpellID::HASTE), attacker);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker, true));

	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 2u);
	EXPECT_TRUE(std::ranges::any_of(casts.back().logLines, [&](const std::string & line)
	{
		return line.find("casts a second Haste through Metamagic, refreshing Haste on Pikemen for "
			+ std::to_string(expectedDuration)
			+ (expectedDuration == 1 ? " turn." : " turns.")) != std::string::npos;
	})) << ::testing::PrintToString(casts.back().logLines);
}

TEST_F(NewHorizonsMetamagicTest, CounterspelledStatusCounterPreservesExistingEffect)
{
	prepare(1);
	auto haste = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 3,
		BonusSourceID(SpellID(SpellID::HASTE)));
	haste->turnsRemain = 3;
	defender->addNewBonus(haste);
	ASSERT_TRUE(defender->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE)))));

	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));

	EXPECT_TRUE(defender->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE)))));
	const auto slows = server.castsOf(SpellID::SLOW);
	ASSERT_EQ(slows.size(), 1u);
	EXPECT_TRUE(std::ranges::any_of(slows.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Slow through Metamagic, but the spell was counterspelled.")
			!= std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, ResistedFollowupKeepsResistanceOutcomeWithoutDamage)
{
	prepare(1);
	// A full 100% bonus makes the target invalid before casting; the fixture's
	// fixed RNG seed makes this 99% resistance outcome deterministic.
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 99, BonusSourceID()));
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));

	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().damage, 0);
	EXPECT_EQ(casts.front().killed, 0u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Magic Arrow through Metamagic, resisted by 1 target.") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, CounterspelledFollowupKeepsCounterspellOutcomeWithoutDamage)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 1000);
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));

	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.counterspellNegated);
	EXPECT_EQ(casts.front().damage, 0);
	EXPECT_EQ(casts.front().killed, 0u);
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("casts a second Magic Arrow through Metamagic, but the spell was counterspelled.") != std::string::npos;
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

TEST_F(NewHorizonsMetamagicTest, SpellActionDoesNotBlockControlledHypnotizedStack)
{
	prepare(1);
	const auto decoded = SecondarySkill::decode(metamagicSkill);
	ASSERT_GE(decoded, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(decoded), 1, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10, ChangeValueMode::ABSOLUTE);
	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
	setTestSpellPointTotal(defenderSideHero, 1000);

	activate(defender);
	BattleAction trigger;
	trigger.actionType = EActionType::HERO_SPELL;
	trigger.side = BattleSide::DEFENDER;
	trigger.spell = SpellID::HASTE;
	trigger.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), trigger));
	ASSERT_EQ(battle()->getSide(BattleSide::DEFENDER).metamagicPendingCount, 1);

	// The stack retains ATTACKER as its origin side, but Hypnotize gives the
	// DEFENDER player control. Its creature activation must leave the
	// controlling hero's independent Spell Action untouched.
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	attacker->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(attacker), PlayerColor(1));
	activate(attacker);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(
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
	const auto manaBeforeEnemySpell = attackerSideHero->getManaAvailable();

	giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
	setTestSpellPointTotal(defenderSideHero, 1000);
	activate(defender);
	BattleAction enemySpell;
	enemySpell.actionType = EActionType::HERO_SPELL;
	enemySpell.side = BattleSide::DEFENDER;
	enemySpell.spell = SpellID::HASTE;
	enemySpell.aimToUnit(defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), enemySpell));
	EXPECT_EQ(attackerSideHero->getManaAvailable(),
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
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 2u);
	EXPECT_TRUE(std::ranges::any_of(casts.back().logLines, [](const std::string & line)
	{
		return line.find("casts a second Haste through Metamagic, causing no status change.")
			!= std::string::npos;
	})) << ::testing::PrintToString(casts.back().logLines);
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
