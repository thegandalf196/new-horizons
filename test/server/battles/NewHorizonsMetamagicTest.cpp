/*
 * NewHorizonsMetamagicTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "BattleStartSnapshotFixture.h"
#include "HeroCommandFixture.h"

#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/Problem.h"

#include <limits>

namespace
{
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto arcaneEconomy = "new-horizons:metamagic.arcaneEconomy";
constexpr auto formulaReserve = "new-horizons:metamagic.formulaReserve";
constexpr auto spellBuffer = "new-horizons:metamagic.spellBuffer";
constexpr auto grandMetamagic = "new-horizons:metamagic.grandMetamagic";
constexpr auto arcaneAcquisition = "new-horizons:metamagic.arcaneAcquisition";
constexpr auto echoedDuration = "new-horizons:metamagic.echoedDuration";
constexpr auto sorceryMagicSkill = "new-horizons:sorceryMagic";
constexpr auto spellbinderPerk = "new-horizons:sorceryMagic.spellbinder";

SpellID phantomArmySpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::PHANTOM_ARMY_SPELL));
}

SpellID spellLockSpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
}
}

class NewHorizonsMetamagicTest : public HeroCommandFixture
{
protected:
	bool legacyCloneRoster = false;
	bool legacySchoolRankRules = false;

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
		if(legacySchoolRankRules)
		{
			magicRules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
			magicRules.Struct().erase("schoolRankPowerCoefficientPercent");
			newHorizonsMagic::validateRules(magicRules);
		}
		// Old saved rulesets treated a spell with no `active` marker as enabled.
		// Keep that compatibility path narrowly scoped to the legacy Clone test.
		if(legacyCloneRoster)
			magicRules["spells"]["core:clone"].Struct().erase("active");
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int rank, std::initializer_list<const char *> perks = {}, bool useLegacyCloneRoster = false,
		int32_t combatManaBonus = 0)
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
		std::array<std::string, 4> selectedPerTier;
		const JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto perk : perks)
		{
			const auto & definitions = perkRules["skills"][metamagicSkill]["perks"].Vector();
			const auto definition = std::find_if(definitions.begin(), definitions.end(), [perk](const JsonNode & entry)
			{
				return entry["id"].String() == perk;
			});
			ASSERT_NE(definition, definitions.end()) << "Missing Metamagic perk " << perk;
			const auto & required = (*definition)["requires"].String();
			const int tier = required == "basic" ? 1 : required == "advanced" ? 2 : required == "expert" ? 3 : 0;
			ASSERT_GT(tier, 0) << "Invalid Metamagic perk tier for " << perk;
			ASSERT_TRUE(selectedPerTier[tier].empty()) << "Two fixture perks occupy tier " << tier;
			selectedPerTier[tier] = perk;
		}
		// Tests that exercise a later-tier perk must still model the canonical
		// Basic -> Advanced -> Expert selection order. These defaults have no
		// effect on the scenarios that require them.
		if(rank >= 2 && selectedPerTier[1].empty())
			selectedPerTier[1] = arcaneAcquisition;
		if(rank >= 3 && selectedPerTier[2].empty())
			selectedPerTier[2] = echoedDuration;
		for(int tier = 1; tier <= 3; ++tier)
			if(!selectedPerTier[tier].empty())
				attackerSideHero->applyPerkSelection({metamagicSkill, selectedPerTier[tier]});

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
		attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->addSpellToSpellbook(SpellID::RESURRECTION);
		attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
		attackerSideHero->addSpellToSpellbook(phantomArmySpell());
		attackerSideHero->addSpellToSpellbook(SpellID::SUMMON_AIR_ELEMENTAL);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("core:iceBolt")));
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode("new-horizons:counterspell")));
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(combatManaBonus > 0)
		{
			Bonus combatMana;
			combatMana.type = BonusType::COMBAT_MANA_BONUS;
			combatMana.val = combatManaBonus;
			GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
			gameHandler->sendAndApply(grantCombatMana);
		}

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
		BattleAction retired = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
		retired.metamagicDecline = true;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), retired);
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

	bool castFireWallFollowup(BattleHex target, BattleHex::EDir direction)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::FIRE_WALL;
		action.metamagicFollowup = true;
		action.aimToHex(target);
		action.spellFireWallDirection = direction;
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

	void setSpellLockDurationTestPower()
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	}

	void grantSpellbinder()
	{
		const auto decoded = SecondarySkill::decode(sorceryMagicSkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({sorceryMagicSkill, spellbinderPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(sorceryMagicSkill, spellbinderPerk));
	}

	void applySpellLockScript(const CStack * target, bool followup = false)
	{
		const auto spell = spellLockSpell();
		ASSERT_NE(spell, SpellID::NONE);
		ASSERT_NE(spell.toSpell(), nullptr);
		ASSERT_TRUE(spell.toSpell()->hasBattleEffects());
		ASSERT_NE(gameState()->getMagicRules()["spells"].Struct().count(newHorizonsSorcery::SPELL_LOCK_SPELL), 0u);
		ASSERT_TRUE(gameState()->getMagicRules()["spells"][newHorizonsSorcery::SPELL_LOCK_SPELL]["active"].Bool());
		ASSERT_TRUE(spell.toSpell()->isCommonHeroSpell());

		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setSpellLevel(3);
		event.setMetamagicFollowup(followup);
		event.setMetamagicTargetUnitId(target->unitId());
		event.cast(gameHandler->spellcastEnvironment(), {spells::Destination(target)});
	}

	void expectAppliedSpellLockDuration(const CStack * target, int32_t expectedDuration)
	{
		const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLockSpell()));
		const auto bonuses = target->getBonuses(source);
		const auto resistance = bonuses->getFirst(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->type == BonusType::MAGIC_RESISTANCE;
		}));
		ASSERT_NE(resistance, nullptr);
		EXPECT_EQ(resistance->duration, BonusDuration::N_TURNS);
		EXPECT_EQ(resistance->turnsRemain, expectedDuration);
	}

	int32_t effectTurns(const CStack * target, SpellID sourceSpell) const
	{
		const auto bonus = target->getBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(sourceSpell)));
		return bonus && Bonus::NTurns(bonus.get()) ? bonus->turnsRemain : 0;
	}

	int32_t spellLockTurns(const CStack * target) const
	{
		const auto bonuses = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spellLockSpell())).And(Selector::type()(BonusType::MAGIC_RESISTANCE)));
		if(!bonuses)
			return 0;
		const auto lock = bonuses->getFirst(CSelector([](const Bonus * bonus)
		{
			return bonus && Bonus::NTurns(bonus);
		}));
		return lock ? lock->turnsRemain : 0;
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

TEST_F(NewHorizonsMetamagicTest, RetiredDeclineCannotClearAnUnusedOffer)
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
	EXPECT_FALSE(decline());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);
	endRound();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
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

TEST_F(NewHorizonsMetamagicTest, RetiredDeclineRequestIsRejectedAtomically)
{
	prepare(1);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto pendingBefore = battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount;
	const auto sequenceBefore = battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells;

	BattleAction malformed = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
	malformed.metamagicDecline = true;
	malformed.stackNumber = attacker->unitId();
	bool accepted = false;
	EXPECT_NO_THROW(accepted = gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), malformed));
	EXPECT_FALSE(accepted);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, pendingBefore);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells, sequenceBefore);
	endRound();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
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

TEST_F(NewHorizonsMetamagicTest, GrandMetamagicAutomaticallyContinuesTheThirdUsedSequence)
{
	prepare(3, {grandMetamagic});
	for(uint8_t used = 0; used < 3; ++used)
	{
		if(used != 0)
		{
			advanceRound();
			activate(attacker);
		}
		ASSERT_TRUE(cast(SpellID::HASTE, attacker));
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, used);
		if(used == 2)
		{
			// A rejected spell does not consume the third use or activate Grand.
			EXPECT_FALSE(cast(SpellID::RESURRECTION, defender, true));
			EXPECT_FALSE(cast(SpellID::SLOW, defender, true, true));
			EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 2);
			EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicGrandUsed);
			EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
		}
		ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, used + 1);
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, used == 2 ? 1 : 0);
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicGrandUsed, used == 2);
	}

	// The accepted continuation is a saved, pending Grand grant and does not
	// charge another Metamagic use.
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 3);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, 1);
}

TEST_F(NewHorizonsMetamagicTest, PendingGrandContinuationRoundTripsAndPaysFormulaReserveOnce)
{
	prepare(3, {grandMetamagic, formulaReserve});
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	ASSERT_EQ(side.metamagicUsesConsumed, 3);
	ASSERT_TRUE(side.metamagicGrandUsed);
	ASSERT_EQ(side.metamagicPendingCount, 1);
	ASSERT_EQ(side.metamagicSequenceSpells,
		(std::vector<SpellID>{SpellID::HASTE, SpellID::SLOW}));
	ASSERT_EQ(side.heroActionAllowances.remainingCounts(battle()->getRound()).spellActions, 1u);
	ASSERT_EQ(side.heroActionAllowances.grants.size(), 1u);
	ASSERT_EQ(side.heroActionAllowances.grants.front().source,
		HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND);

	// Game snapshots intentionally omit active battles. Restore the independent
	// world first, then send the live battle through its real BattleStart wire path.
	const auto savedWorld = gameState()->saveToMemory();
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(savedWorld);
	ASSERT_TRUE(replica->currentBattles.empty());
	const auto restoredBattleId = replica->nextBattleID;

	BattleStart outgoing;
	outgoing.battleID = restoredBattleId;
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);

	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
	restoredHandler->sendAndApply(incoming);
	auto * restoredBattle = replica->getBattle(restoredBattleId);
	ASSERT_NE(restoredBattle, nullptr);
	auto & restoredSide = restoredBattle->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(restoredSide.metamagicUsesConsumed, 3);
	EXPECT_TRUE(restoredSide.metamagicGrandUsed);
	EXPECT_EQ(restoredSide.metamagicPendingCount, 1);
	EXPECT_EQ(restoredSide.metamagicSequenceSpells,
		(std::vector<SpellID>{SpellID::HASTE, SpellID::SLOW}));
	ASSERT_EQ(restoredSide.heroActionAllowances.remainingCounts(restoredBattle->getRound()).spellActions, 1u);
	ASSERT_EQ(restoredSide.heroActionAllowances.grants.size(), 1u);
	EXPECT_EQ(restoredSide.heroActionAllowances.grants.front().source,
		HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND);

	auto * restoredHero = replica->getHero(attackerSideHero->id);
	const auto * restoredTarget = restoredBattle->getStack(defender->unitId());
	ASSERT_NE(restoredHero, nullptr);
	ASSERT_NE(restoredTarget, nullptr);
	const auto * continuationSpell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(continuationSpell, nullptr);
	const int32_t continuationCost = restoredHero->getSpellCost(continuationSpell);
	ASSERT_GT(continuationCost, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	const int32_t normalBeforeContinuation = restoredHero->getNormalSpellPoints();
	const int32_t bufferBeforeContinuation = restoredHero->getBufferSpellPoints();

	BattleAction continuation;
	continuation.actionType = EActionType::HERO_SPELL;
	continuation.side = BattleSide::ATTACKER;
	continuation.spell = SpellID::MAGIC_ARROW;
	continuation.metamagicFollowup = true;
	continuation.aimToUnit(restoredTarget);
	ASSERT_TRUE(restoredHandler->battles->makePlayerBattleAction(
		restoredBattleId, PlayerColor(0), continuation));

	EXPECT_EQ(restoredSide.metamagicUsesConsumed, 3);
	EXPECT_TRUE(restoredSide.metamagicGrandUsed);
	EXPECT_EQ(restoredSide.metamagicPendingCount, 0);
	EXPECT_TRUE(restoredSide.metamagicSequenceSpells.empty());
	EXPECT_TRUE(restoredSide.metamagicFormulaReserveUsed);
	EXPECT_EQ(restoredSide.heroActionAllowances.remainingCounts(restoredBattle->getRound()).spellActions, 0u);
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), normalBeforeContinuation - continuationCost
		+ newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), bufferBeforeContinuation);
	const auto casts = restoredServer.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().announcement.metamagicManaRefund,
		newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	EXPECT_EQ(std::ranges::count_if(restoredServer.battleLogLines, [](const std::string & line)
	{
		return line.find("Formula Reserve restores 3 Normal Spell Points") != std::string::npos;
	}), 1);

	const int32_t normalAfterContinuation = restoredHero->getNormalSpellPoints();
	BattleAction retired = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::NONE);
	retired.metamagicDecline = true;
	EXPECT_FALSE(restoredHandler->battles->makePlayerBattleAction(
		restoredBattleId, PlayerColor(0), retired));
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), normalAfterContinuation);
}

TEST_F(NewHorizonsMetamagicTest, FormulaReserveRefundsOnlyAfterGrandSequenceResolves)
{
	prepare(3, {grandMetamagic, formulaReserve});
	// Precondition this focused branch test at the third use so Grand defers
	// Formula Reserve until the pending continuation is resolved.
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const auto normalBeforeGrand = attackerSideHero->getNormalSpellPoints();
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	const auto normalAfterGrand = attackerSideHero->getNormalSpellPoints();
	const auto bufferAfterGrand = attackerSideHero->getBufferSpellPoints();
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed, false);

	// The unused Grand continuation closes automatically at the round boundary.
	// Formula Reserve restores Normal only, not Buffer.
	endRound();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalAfterGrand + newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	EXPECT_GT(normalBeforeGrand, normalAfterGrand);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferAfterGrand);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Formula Reserve restores 3 Normal Spell Points") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, FormulaReservePaysForEveryQualifyingSequence)
{
	prepare(2, {formulaReserve});
	const int listedCost = attackerSideHero->getSpellCost(SpellID(SpellID::SLOW).toSpell());
	for(int sequence = 0; sequence < 2; ++sequence)
	{
		if(sequence > 0)
		{
			endRound();
			activate(attacker);
		}
		ASSERT_TRUE(cast(SpellID::HASTE, attacker));
		const int32_t normalBeforeFollowup = attackerSideHero->getNormalSpellPoints();
		ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalBeforeFollowup - listedCost
			+ newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
		const auto slows = server.castsOf(SpellID::SLOW);
		ASSERT_EQ(slows.size(), static_cast<size_t>(sequence + 1));
		EXPECT_EQ(slows.back().announcement.metamagicManaRefund,
			newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	}
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);
	EXPECT_EQ(std::ranges::count_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Formula Reserve restores 3 Normal Spell Points") != std::string::npos;
	}), 2);
}

TEST_F(NewHorizonsMetamagicTest, FormulaReserveRestoresOnlyAvailableNormalCapacity)
{
	prepare(3, {grandMetamagic, formulaReserve});
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	const int32_t normalCapacity = attackerSideHero->manaLimit();
	ASSERT_GT(normalCapacity, 1);
	attackerSideHero->setNormalSpellPoints(normalCapacity - 1);
	const int32_t bufferBeforeExpiry = attackerSideHero->getBufferSpellPoints();

	endRound();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalCapacity);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeExpiry);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Formula Reserve restores 1 Normal Spell Points") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
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
	for(uint8_t used = 0; used < 2; ++used)
	{
		if(used != 0)
		{
			advanceRound();
			activate(attacker);
		}
		ASSERT_TRUE(cast(SpellID::HASTE, attacker));
		ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	}
	advanceRound();
	activate(attacker);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, defender, true));
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

TEST_F(NewHorizonsMetamagicTest, EchoedPhantomArmyFollowupLogReportsThreeRounds)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(phantomArmySpell(), attacker, true));

	const auto casts = server.castsOf(phantomArmySpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(std::ranges::count_if(casts.front().logLines, [](const std::string & line)
	{
		return line.find("through Metamagic, creating a phantom stack of") != std::string::npos
			&& line.find("integrity for 3 rounds.") != std::string::npos;
	}), 1) << ::testing::PrintToString(casts.front().logLines);
	EXPECT_FALSE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("integrity for 2 rounds.") != std::string::npos;
	}));
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
	EXPECT_EQ(ordinaryMechanics->adjustEffectDuration(3), 3);
	EXPECT_EQ(followupMechanics->adjustEffectDuration(3), 4);
	EXPECT_EQ(followupMechanics->adjustEffectDuration(std::numeric_limits<int32_t>::max()),
		std::numeric_limits<int32_t>::max());

	followup.setEffectDuration(7);
	const auto explicitMechanics = slow.toSpell()->battleMechanics(&followup);
	EXPECT_EQ(explicitMechanics->getEffectDuration(), 7);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockScriptRetainsItsOrdinaryThreeRoundCapWithoutPerks)
{
	prepare(1);
	setSpellLockDurationTestPower();

	applySpellLockScript(defender);
	expectAppliedSpellLockDuration(defender, newHorizonsSorcery::SPELL_LOCK_BASE_DURATION_CAP);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockScalesItsSpellPowerTermBySavedSchoolRankBeforeFloor)
{
	prepare(1);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 70, ChangeValueMode::ABSOLUTE);
	applySpellLockScript(attacker);
	expectAppliedSpellLockDuration(attacker, 1);

	const auto sorcery = SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL);
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	applySpellLockScript(defender);
	expectAppliedSpellLockDuration(defender, 2);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockLegacyV2RulesKeepTheUnscaledPowerTerm)
{
	legacySchoolRankRules = true;
	prepare(1);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 70, ChangeValueMode::ABSOLUTE);
	const auto sorcery = SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL);
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);

	applySpellLockScript(defender);
	expectAppliedSpellLockDuration(defender, 1);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockDynamicHelpShowsRankAndCurrentOrdinaryDuration)
{
	prepare(1);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 70, ChangeValueMode::ABSOLUTE);
	const auto sorcery = SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL);
	ASSERT_GE(sorcery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, spellLockSpell().toSpell(), 0);

	EXPECT_NE(description.find("Current Spell Power term: 115%"), std::string::npos);
	EXPECT_NE(description.find("ordinary duration at current Spell Power: 2 rounds."), std::string::npos);
}

TEST_F(NewHorizonsMetamagicTest, SpellbinderRaisesSpellLockOrdinaryDurationToFourRounds)
{
	prepare(1);
	setSpellLockDurationTestPower();
	grantSpellbinder();

	applySpellLockScript(defender);
	expectAppliedSpellLockDuration(defender, newHorizonsSorcery::SPELL_LOCK_SPELLBINDER_DURATION_CAP);
}

TEST_F(NewHorizonsMetamagicTest, EchoedDurationDoesNotChangeAnOrdinarySpellLockCast)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	setSpellLockDurationTestPower();

	applySpellLockScript(defender);
	expectAppliedSpellLockDuration(defender, newHorizonsSorcery::SPELL_LOCK_BASE_DURATION_CAP);
}

TEST_F(NewHorizonsMetamagicTest, EchoedDurationAddsOneRoundToAppliedSpellLockFollowup)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	setSpellLockDurationTestPower();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	applySpellLockScript(defender, true);
	expectAppliedSpellLockDuration(defender, newHorizonsSorcery::SPELL_LOCK_BASE_DURATION_CAP + 1);
}

TEST_F(NewHorizonsMetamagicTest, EchoedDurationAddsOneRoundToFireWallFollowup)
{
	prepare(2, {echoedDuration});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(castFireWallFollowup(BattleHex(70), BattleHex::RIGHT));
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get());
	ASSERT_NE(wall, nullptr);
	EXPECT_EQ(wall->turnsRemaining, 4);
}

TEST_F(NewHorizonsMetamagicTest, EchoedDurationFireWallRoundTripsAndExpiresAfterFourBoundaries)
{
	prepare(2, {echoedDuration});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(castFireWallFollowup(BattleHex(70), BattleHex::RIGHT));
	ASSERT_EQ(battle()->obstacles.size(), 1u);

	// World saves intentionally omit active battles. Restore the world and then
	// transport the live battle through the same detached BattleStart snapshot
	// used by a reconnecting client.
	const auto savedWorld = gameState()->saveToMemory();
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(savedWorld);
	ASSERT_TRUE(replica->currentBattles.empty());
	const auto restoredBattleId = replica->nextBattleID;
	BattleStart outgoing;
	outgoing.battleID = restoredBattleId;
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
	restoredHandler->sendAndApply(incoming);
	const auto * restoredBattle = replica->getBattle(restoredBattleId);
	ASSERT_NE(restoredBattle, nullptr);
	ASSERT_EQ(restoredBattle->obstacles.size(), 1u);
	const auto * restoredWall = dynamic_cast<const SpellCreatedObstacle *>(restoredBattle->obstacles.front().get());
	ASSERT_NE(restoredWall, nullptr);
	EXPECT_EQ(restoredWall->ID, SpellID::FIRE_WALL);
	EXPECT_EQ(restoredWall->turnsRemaining, 4);

	const auto advanceRestoredRound = [&]()
	{
		const int32_t startingRound = restoredBattle->getRound();
		while(restoredBattle->getRound() == startingRound)
		{
			const auto * active = restoredBattle->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(restoredHandler->battles->makePlayerBattleAction(restoredBattleId,
				restoredBattle->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
	};
	for(int expected = 3; expected >= 1; --expected)
	{
		advanceRestoredRound();
		ASSERT_EQ(restoredBattle->obstacles.size(), 1u);
		const auto * current = dynamic_cast<const SpellCreatedObstacle *>(restoredBattle->obstacles.front().get());
		ASSERT_NE(current, nullptr);
		EXPECT_EQ(current->turnsRemaining, expected);
	}
	advanceRestoredRound();
	EXPECT_TRUE(restoredBattle->obstacles.empty());
}

TEST_F(NewHorizonsMetamagicTest, SpellbinderThenEchoedDurationAllowsFiveRoundsOnSpellLockFollowup)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_ECHOED_DURATION.data()});
	setSpellLockDurationTestPower();
	grantSpellbinder();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));

	applySpellLockScript(defender, true);
	// The ordinary three-round cap is raised to four first; Echoed Duration is
	// then applied once for this additional Metamagic Spell Action.
	expectAppliedSpellLockDuration(defender,
		newHorizonsSorcery::SPELL_LOCK_SPELLBINDER_DURATION_CAP + 1);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockRemovesOnlyTheOpposingMagicalPolarity)
{
	prepare(1);
	const auto addEffect = [](CStack * target, SpellID source, BonusType type, int32_t value)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
			value, BonusSourceID(source));
		bonus->turnsRemain = 3;
		target->addNewBonus(bonus);
	};
	addEffect(attacker, SpellID(SpellID::HASTE), BonusType::STACKS_SPEED, 3);
	addEffect(attacker, SpellID(SpellID::SLOW), BonusType::STACKS_SPEED, -3);
	const SpellID deathCloud(SpellID::decode("core:deathCloud"));
	ASSERT_NE(deathCloud.toSpell(), nullptr);
	ASSERT_FALSE(deathCloud.toSpell()->isMagical());
	addEffect(attacker, deathCloud, BonusType::STACKS_SPEED, -4);
	addEffect(defender, SpellID(SpellID::HASTE), BonusType::STACKS_SPEED, 3);
	addEffect(defender, SpellID(SpellID::SLOW), BonusType::STACKS_SPEED, -3);

	setSpellLockDurationTestPower();
	applySpellLockScript(attacker);
	applySpellLockScript(defender);

	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), 3);
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::SLOW)), 0);
	EXPECT_EQ(effectTurns(attacker, deathCloud), 3);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::HASTE)), 0);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::SLOW)), 3);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockBlocksRealCastsAcrossTargetCategories)
{
	prepare(1);
	attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT);
	setSpellLockDurationTestPower();
	const auto addEffect = [](CStack * target, SpellID source, BonusType type, int32_t value)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
			value, BonusSourceID(source));
		bonus->turnsRemain = 3;
		target->addNewBonus(bonus);
	};
	addEffect(attacker, SpellID(SpellID::HASTE), BonusType::STACKS_SPEED, 3);
	addEffect(defender, SpellID(SpellID::SLOW), BonusType::STACKS_SPEED, -3);
	applySpellLockScript(attacker);
	applySpellLockScript(defender);

	const auto attackerHealth = attacker->getAvailableHealth();
	const auto defenderHealth = defender->getAvailableHealth();
	const auto attackerPosition = attacker->getPosition();
	const auto hasteTurns = effectTurns(attacker, SpellID(SpellID::HASTE));
	const auto slowTurns = effectTurns(defender, SpellID(SpellID::SLOW));
	const auto assertLockedCastIsRejected = [&](SpellID spell, const CStack * target,
		spells::Target aimedAt)
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		event.setSpellLevel(3);
		const auto mechanics = spell.toSpell()->battleMechanics(&event);
		EXPECT_FALSE(mechanics->isReceptive(target)) << spell.getNum();
		EXPECT_FALSE(mechanics->canBeCastAt(aimedAt)) << spell.getNum();
		event.cast(gameHandler->spellcastEnvironment(), std::move(aimedAt));
	};

	// Beneficial, hostile, direct damage, dispel, and unit-plus-location
	// teleportation all share the same locked-target barrier.
	assertLockedCastIsRejected(SpellID(SpellID::HASTE), attacker, {spells::Destination(attacker)});
	assertLockedCastIsRejected(SpellID(SpellID::SLOW), defender, {spells::Destination(defender)});
	assertLockedCastIsRejected(SpellID(SpellID::MAGIC_ARROW), defender, {spells::Destination(defender)});
	assertLockedCastIsRejected(SpellID(SpellID::DISPEL), attacker, {spells::Destination(attacker)});

	const auto teleportHex = attacker->getPosition().copyToEast();
	ASSERT_TRUE(teleportHex.isAvailable());
	assertLockedCastIsRejected(SpellID(SpellID::TELEPORT), attacker,
		{spells::Destination(attacker), spells::Destination(teleportHex)});
	assertLockedCastIsRejected(SpellID(SpellID::FIREBALL), defender,
		{spells::Destination(defender->getPosition())});

	EXPECT_EQ(attacker->getAvailableHealth(), attackerHealth);
	EXPECT_EQ(defender->getAvailableHealth(), defenderHealth);
	EXPECT_EQ(attacker->getPosition(), attackerPosition);
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), hasteTurns);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::SLOW)), slowTurns);
}

TEST_F(NewHorizonsMetamagicTest, SpellLockFreezesOnlyPreservedMagicAndOtherRoundStateExpires)
{
	prepare(1);
	setSpellLockDurationTestPower();
	auto haste = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
	haste->turnsRemain = 3;
	attacker->addNewBonus(haste);
	auto defense = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE));
	defense->turnsRemain = 4;
	attacker->addNewBonus(defense);
	const SpellID bindSpell(SpellID::decode("core:bind"));
	auto nonmagicalSpellEffect = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 2, BonusSourceID(bindSpell),
		BonusSubtypeID(PrimarySkill::DEFENSE));
	nonmagicalSpellEffect->turnsRemain = 4;
	attacker->addNewBonus(nonmagicalSpellEffect);
	auto slow = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, -3, BonusSourceID(SpellID(SpellID::SLOW)));
	slow->turnsRemain = 3;
	defender->addNewBonus(slow);
	applySpellLockScript(attacker);
	applySpellLockScript(defender);

	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->getHeroOrderState(BattleSide::ATTACKER));
	const auto nonmagicalTurns = [&]()
	{
		const auto bonuses = attacker->getBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
		if(!bonuses)
			return 0;
		const auto timed = bonuses->getFirst(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->source == BonusSource::OTHER && Bonus::NTurns(bonus);
		}));
		return timed ? timed->turnsRemain : 0;
	};
	const auto nonmagicalSpellTurns = [&]() { return effectTurns(attacker, bindSpell); };
	const auto nextRound = [&]()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	};
	// The transition out of setup round 0 intentionally skips duration aging.
	while(battle()->getRound() == 0)
		nextRound();
	EXPECT_FALSE(battle()->getHeroOrderState(BattleSide::ATTACKER));
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), 3);
	EXPECT_EQ(nonmagicalTurns(), 4);
	EXPECT_EQ(nonmagicalSpellTurns(), 4);

	nextRound();
	EXPECT_EQ(spellLockTurns(attacker), 2);
	EXPECT_EQ(spellLockTurns(defender), 2);
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), 3);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::SLOW)), 3);
	EXPECT_EQ(nonmagicalTurns(), 3);
	EXPECT_EQ(nonmagicalSpellTurns(), 3);

	nextRound();
	EXPECT_EQ(spellLockTurns(attacker), 1);
	EXPECT_EQ(spellLockTurns(defender), 1);
	EXPECT_EQ(nonmagicalTurns(), 2);
	EXPECT_EQ(nonmagicalSpellTurns(), 2);

	nextRound();
	EXPECT_EQ(spellLockTurns(attacker), 0);
	EXPECT_EQ(spellLockTurns(defender), 0);
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), 3);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::SLOW)), 3);
	EXPECT_EQ(nonmagicalTurns(), 1);
	EXPECT_EQ(nonmagicalSpellTurns(), 1);

	nextRound();
	EXPECT_EQ(effectTurns(attacker, SpellID(SpellID::HASTE)), 2);
	EXPECT_EQ(effectTurns(defender, SpellID(SpellID::SLOW)), 2);
	EXPECT_EQ(nonmagicalTurns(), 0);
	EXPECT_EQ(nonmagicalSpellTurns(), 0);
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

TEST_F(NewHorizonsMetamagicTest, SpellBufferDataIsAnAdvancedOnceCombatExpiryReward)
{
	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & entries = perks["skills"][metamagicSkill]["perks"].Vector();
	const auto found = std::find_if(entries.begin(), entries.end(), [](const JsonNode & entry)
	{
		return entry["id"].String() == spellBuffer;
	});
	ASSERT_NE(found, entries.end());
	EXPECT_EQ((*found)["name"].String(), "Spell Buffer");
	EXPECT_EQ((*found)["requires"].String(), "advanced");
	EXPECT_EQ((*found)["effect"]["status"].String(), "active");
	EXPECT_NE((*found)["description"].String().find("unused Metamagic Spell Action expires"), std::string::npos);
	EXPECT_NE((*found)["description"].String().find("6 Buffer Spell Points"), std::string::npos);
	EXPECT_EQ(std::find_if(entries.begin(), entries.end(), [](const JsonNode & entry)
	{
		return entry["id"].String() == "new-horizons:metamagic.spellEcho";
	}), entries.end());
}

TEST_F(NewHorizonsMetamagicTest, SpellBufferRewardsOnlyTheFirstUnusedOfferAtRoundExpiry)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPELL_BUFFER.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const int32_t normalBeforeExpiry = attackerSideHero->getNormalSpellPoints();
	const int32_t bufferBeforeExpiry = attackerSideHero->getBufferSpellPoints();
	const int32_t temporaryBufferBeforeExpiry = battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining;
	endRound();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalBeforeExpiry);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeExpiry
		+ newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, temporaryBufferBeforeExpiry);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Spell Buffer grants 6 Buffer Spell Points") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);

	activate(attacker);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const int32_t bufferBeforeSecondExpiry = attackerSideHero->getBufferSpellPoints();
	endRound();
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeSecondExpiry);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
}

TEST_F(NewHorizonsMetamagicTest, SpellBufferIsConsumedEvenWhenThePoolIsCapped)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPELL_BUFFER.data()});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	// Fill after paying for the triggering spell: expiry must consume the perk
	// even when the grant itself has no available Buffer capacity.
	attackerSideHero->initializeSpellPoints(attackerSideHero->getNormalSpellPoints(),
		std::numeric_limits<int32_t>::max());
	endRound();
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), std::numeric_limits<int32_t>::max());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
}

TEST_F(NewHorizonsMetamagicTest, RetiredDeclineCannotSuppressSpellBufferAtExpiry)
{
	prepare(2, {newHorizonsMagic::METAMAGIC_SPELL_BUFFER.data()});
	const int32_t bufferBeforeDecline = attackerSideHero->getBufferSpellPoints();
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	EXPECT_FALSE(decline());
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeDecline);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	endRound();
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeDecline
		+ newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
}

TEST_F(NewHorizonsMetamagicTest, GrandContinuationExpiryPaysFormulaReserveOnce)
{
	prepare(3, {grandMetamagic, formulaReserve});
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.size(), 2u);
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	const int32_t normalBeforeExpiry = attackerSideHero->getNormalSpellPoints();
	const int32_t bufferBeforeExpiry = attackerSideHero->getBufferSpellPoints();

	endRound();

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalBeforeExpiry
		+ newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeExpiry);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicFormulaReserveUsed);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Formula Reserve restores 3 Normal Spell Points") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
	EXPECT_TRUE(std::ranges::none_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Spell Buffer grants") != std::string::npos;
	}));
}

TEST_F(NewHorizonsMetamagicTest, GrandContinuationExpiryGrantsSpellBuffer)
{
	prepare(3, {grandMetamagic, spellBuffer});
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.size(), 2u);
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 1);
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 3);
	const int32_t bufferBeforeExpiry = attackerSideHero->getBufferSpellPoints();

	endRound();
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeExpiry
		+ newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 3);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Spell Buffer grants 6 Buffer Spell Points") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsMetamagicTest, BattleEndClosesGrandSequenceBeforeReleasingHeroes)
{
	prepare(3, {grandMetamagic, formulaReserve});
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	const int32_t normalBeforeResults = attackerSideHero->getNormalSpellPoints();
	const int32_t bufferBeforeResults = attackerSideHero->getBufferSpellPoints();

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);
	const int32_t normalAfterResults = attackerSideHero->getNormalSpellPoints();
	EXPECT_EQ(normalAfterResults, normalBeforeResults + newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeResults);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).heroID, ObjectInstanceID::NONE);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);

	// Reapplying the same closure event cannot restore the same sequence twice.
	gameState()->apply(applied);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalAfterResults);
}

TEST_F(NewHorizonsMetamagicTest, BattleEndDoesNotGrantSpellBufferForGrandContinuation)
{
	prepare(3, {grandMetamagic, spellBuffer});
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 2;
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_TRUE(cast(SpellID::SLOW, defender, true));
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.size(), 2u);
	const int32_t bufferBeforeResults = attackerSideHero->getBufferSpellPoints();

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeResults);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
}

TEST_F(NewHorizonsMetamagicTest, BattleEndKeepsEarnedSpellBufferAfterTemporaryBufferCleanup)
{
	prepare(2, {spellBuffer}, false, 10);
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.temporaryBufferRemaining, 10);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	ASSERT_GT(side.temporaryBufferRemaining, 0);
	const int32_t temporaryBufferAfterCast = side.temporaryBufferRemaining;
	ASSERT_LT(temporaryBufferAfterCast, 10);

	endRound();
	ASSERT_TRUE(side.metamagicSpellBufferUsed);
	EXPECT_EQ(side.temporaryBufferRemaining, temporaryBufferAfterCast);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), temporaryBufferAfterCast
		+ newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS);

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS);
	EXPECT_EQ(side.temporaryBufferRemaining, temporaryBufferAfterCast);
}

TEST_F(NewHorizonsMetamagicTest, BattleEndDoesNotGrantBufferForAnUnusedOffer)
{
	prepare(2, {spellBuffer});
	ASSERT_TRUE(cast(SpellID::HASTE, attacker));
	const int32_t bufferBeforeResults = attackerSideHero->getBufferSpellPoints();
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.size(), 1u);

	BattleResultsApplied applied;
	applied.battleID = BattleID(0);
	gameState()->apply(applied);

	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferBeforeResults);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).metamagicSpellBufferUsed);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).metamagicSequenceSpells.empty());
}

TEST(NewHorizonsMetamagicStateTest, SpellBufferUseHasVersionedRoundTripAndOldReadDefault)
{
	SideInBattle outgoing(nullptr);
	outgoing.metamagicSpellBufferUsed = true;
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);
	SideInBattle restored(nullptr);
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.metamagicSpellBufferUsed);

	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_POINTS;
	oldWire.iser.version = ESerializationVersion::NEW_HORIZONS_SPELL_POINTS;
	SideInBattle oldDefault(nullptr);
	ASSERT_NO_THROW(oldWire.oser & oldDefault);
	SideInBattle oldDecoded(nullptr);
	oldDecoded.metamagicSpellBufferUsed = true;
	ASSERT_NO_THROW(oldWire.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.metamagicSpellBufferUsed);

	CMemorySerializer lossy;
	lossy.oser.version = ESerializationVersion::NEW_HORIZONS_SPELL_POINTS;
	EXPECT_THROW(lossy.oser & outgoing, std::runtime_error);
}
