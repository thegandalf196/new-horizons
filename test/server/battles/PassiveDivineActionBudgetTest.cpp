/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../../lib/spells/NewHorizonsWisdom.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/PassiveDivineMandate.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/battle/PassiveDivineMandateSpellPool.h"
#include "../../../lib/spells/NewHorizonsTimedUtility.h"
#include "../../../lib/spells/NewHorizonsMetamagicRefresh.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsPurify.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/spells/RecipientSpellEffectPreview.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class PassiveDivineActionBudgetTest : public HeroCommandFixture
{
protected:
	bool enableRetiredCrusade = false;
	bool historicalMetamagic = false;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(historicalMetamagic)
		{
			perks["metamagicPolicy"] = JsonNode();
			perks["metamagicPolicy"]["version"].Integer() = 2;
			perks.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(enableRetiredCrusade)
			magic["spells"]["new-horizons:crusade"]["active"].Bool() = true;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void prepareFixedProtection(const char * key, int32_t power)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(true));
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode(key)));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	}
	std::vector<std::byte> utilityRngState()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		wire.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		return wire.extractBuffer();
	}
	void checkFreshFixedProtection(const char * key)
	{
		ASSERT_NO_FATAL_FAILURE(prepareFixedProtection(key, 100));
		const auto spell = SpellID(SpellID::decode(key));
		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		ASSERT_EQ(server.casts.size(), 1);
		ASSERT_EQ(server.casts.front().announcement.spellID, spell);
		const auto * target = battle()->battleActiveUnit();
		ASSERT_NE(target, nullptr);
		const auto current = battle()->getLocalSpellEffects(target->unitId());
		ASSERT_FALSE(current.empty());
		for(const auto & effect : current)
			if(effect.sid == BonusSourceID(spell))
				EXPECT_EQ(effect.turnsRemain, 2);
		// This native fixture has exactly one friendly stack: the first free
		// spell therefore already protects every receptive friendly recipient.
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::ATTACKER)
			{
				ASSERT_EQ(unit->unitId(), target->unitId());
				ASSERT_FALSE(unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)))->empty());
			}
		auto receipt = server.casts.front().announcement.automaticHeroCast;
		++receipt.mandateUseOrdinal;
		const auto rng = utilityRngState();
		EXPECT_TRUE(newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
			receipt, {target->unitId()}).empty());
		EXPECT_EQ(utilityRngState(), rng);
		ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
		ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
		EXPECT_EQ(server.casts.size(), 1);
		EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
		EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
		EXPECT_EQ(utilityRngState(), rng);
		const auto after = battle()->getLocalSpellEffects(target->unitId());
		for(const auto & effect : current)
			if(effect.sid == BonusSourceID(spell))
			{
				const auto found = std::ranges::find_if(after, [&](const Bonus & other)
					{ return other.sid == effect.sid && other.type == effect.type; });
				ASSERT_NE(found, after.end());
				EXPECT_EQ(found->val, effect.val);
				EXPECT_EQ(found->turnsRemain, effect.turnsRemain);
			}
	}
	void prepare(bool doubleCommand = false)
	{
		useRevisedOrders = true;
		startGame();
		ASSERT_TRUE(newHorizonsPassiveDivineMandate::enabled(attackerSideHero->getPerkState().rules));
		const auto mandate = SecondarySkill::decode("new-horizons:divineMandate");
		const auto light = SecondarySkill::decode("new-horizons:lightMagic");
		ASSERT_GE(mandate, 0);
		ASSERT_GE(light, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(mandate), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(light), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(doubleCommand)
		{
			const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.aggressiveCommander"});
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.veteranCommander"});
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.doubleCommand"});
		}
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->initializeSpellPoints(0, 0);
		startBattle();
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}
};

TEST_F(PassiveDivineActionBudgetTest, OrderAutomaticLightAndTypedSecondOrderDoNotInventThirdAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.mysticism"});
	attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.manaFasting"});
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, false), 10);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	if(server.casts.empty())
	{
		// Keep a native admission reason when the trigger unexpectedly produces
		// no cast; the expected accepted-cast count below remains unchanged.
		const auto order = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
		ASSERT_TRUE(order);
		const auto policy = newHorizonsPassiveDivineMandate::capturePolicy(attackerSideHero);
		EXPECT_EQ(policy.rank, 1);
		EXPECT_EQ(order->passiveDivineMandate, std::optional{policy});
		spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, 1};
		receipt.issuingOrder = order->command;
		receipt.issuingRound = order->issuedRound;
		const auto * spell = SpellID(SpellID::BLESS).toSpell();
		EXPECT_TRUE(spells::automaticHeroCastAllowed(*battle(), attackerSideHero, spell, receipt));
		spells::BattleCast candidate(battle(), attackerSideHero, spells::Mode::HERO, spell);
		candidate.setAutomaticHeroCast(receipt);
		const battle::Target aim{battle::Destination(battle()->battleActiveUnit())};
		spells::detail::ProblemImpl problem;
		const bool legal = candidate.mechanicsForTarget(aim)->canBeCastAt(aim, problem);
		std::vector<std::string> reasons;
		problem.getAll(reasons);
		EXPECT_TRUE(legal) << (reasons.empty() ? "No native admission reason" : reasons.front());
	}
	ASSERT_EQ(server.casts.size(), 1);
	const auto & cast = server.casts.front().announcement;
	EXPECT_EQ(cast.automaticHeroCast.kind, spells::AutomaticHeroCastKind::DIVINE_MANDATE);
	EXPECT_EQ(cast.automaticHeroCast.mandateUseOrdinal, 1);
	EXPECT_TRUE(cast.castByHero);
	EXPECT_TRUE(cast.activeCast);
	EXPECT_EQ(cast.paidHeroManaCost, 0);
	const auto & wisdomHistory = battle()->getWisdomCombatState(BattleSide::ATTACKER);
	EXPECT_EQ(wisdomHistory.priorCasts(cast.spellID), 1);
	EXPECT_EQ(newHorizonsWisdom::fastingRecovery(*attackerSideHero, true, !wisdomHistory.completedCasts.empty()), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).active);

	// A legitimate typed exception must survive descriptive spell history.
	battle()->getSide(BattleSide::ATTACKER).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, battle()->getRound());
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(server.casts.size(), 1); // Basic's one successful activation is exhausted.
	EXPECT_FALSE(issue(HeroCommand::BRACE));
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
}

TEST_F(PassiveDivineActionBudgetTest, AutomaticLightPreservesRealDoubleCommandContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	ASSERT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto continuation = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(continuation);
	EXPECT_EQ(continuation->source, HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(server.casts.size(), 1);
	EXPECT_FALSE(issue(HeroCommand::BRACE));
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
}

TEST_F(PassiveDivineActionBudgetTest, ExplicitlyEnabledCrusadeIsNotAUsefulGenericDurationRefresh)
{
	enableRetiredCrusade = true; // Only this captured fixture retains the retired spell.
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode("new-horizons:crusade"));
	ASSERT_NE(spell, SpellID::NONE);
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 50, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	ASSERT_EQ(server.casts.back().announcement.spellID, spell);
	auto receipt = server.casts.back().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	const auto * recipient = battle()->battleActiveUnit();
	ASSERT_NE(recipient, nullptr);
	const auto applied = recipient->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_FALSE(applied->empty());
	for(const auto & bonus : *applied)
		EXPECT_EQ(bonus->turnsRemain, 3);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget({battle::Destination(recipient)});
	EXPECT_GT(mechanics->getEffectDuration(), 3);
	EXPECT_EQ(newHorizonsMagic::timedLightEffectDuration(*mechanics), 3);
	EXPECT_TRUE(newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {recipient->unitId()}).empty());
}

TEST_F(PassiveDivineActionBudgetTest, ChosenRefreshUsesItsNativeCappedDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode("new-horizons:theChosen"));
	ASSERT_NE(spell, SpellID::NONE);
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 900, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	ASSERT_EQ(server.casts.back().announcement.spellID, spell);
	auto receipt = server.casts.back().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	const battle::Unit * recipient = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(!unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)))->empty())
			recipient = unit;
	ASSERT_NE(recipient, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget({battle::Destination(recipient)});
	const auto actual = newHorizonsMagic::timedLightEffectDuration(*mechanics);
	EXPECT_EQ(actual, mechanics->getTheChosenDuration());
	EXPECT_GT(mechanics->getEffectDuration(), actual);
	const auto applied = recipient->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_FALSE(applied->empty());
	for(const auto & bonus : *applied)
		EXPECT_EQ(bonus->turnsRemain, actual);
	const auto pool = newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {recipient->unitId()});
	for(const auto & choice : pool)
		for(const auto & target : choice.targets)
			EXPECT_NE(target.aimUnitId, recipient->unitId());
}

TEST_F(PassiveDivineActionBudgetTest, AutomaticPurifyDoesNotSpendActivationToReturnControlledEnemy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto purify = newHorizonsPurify::spellID();
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(purify);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const battle::Unit * controlled = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER)
			controlled = unit;
	ASSERT_NE(controlled, nullptr);
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 8), 100), nullptr);
	Bonus hypnosis(BonusDuration::N_TURNS, BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT,
		0, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	hypnosis.turnsRemain = 3;
	SetStackEffect effect; effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(controlled->unitId(), std::vector<Bonus>{hypnosis});
	gameHandler->sendAndApply(effect);
	ASSERT_EQ(battle()->battleGetOwner(controlled), PlayerColor(0));
	ASSERT_TRUE(newHorizonsPurify::removalReturnsHostileStack(*controlled,
		BattleSide::ATTACKER, SpellID::HYPNOTIZE));
	const auto automatic = newHorizonsPassiveDivineMandate::automaticSpellAction(*battle(),
		BattleSide::ATTACKER, purify, controlled->unitId());
	ASSERT_EQ(automatic.spellPurifyChoices,
		(std::vector<std::pair<int32_t, SpellID>>{{static_cast<int32_t>(controlled->unitId()), SpellID::HYPNOTIZE}}));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(server.casts.empty());
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->battleGetOwner(controlled), PlayerColor(0));
	EXPECT_TRUE(controlled->hasBonusOfType(BonusType::HYPNOTIZED));
}

TEST_F(PassiveDivineActionBudgetTest, RetiredCrusadeCannotEnterTheCurrentAutomaticPool)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode("new-horizons:crusade"));
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(server.casts.empty());
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 0);
}

TEST_F(PassiveDivineActionBudgetTest, FreshFullGuardianSpiritDoesNotSpendSecondAutomaticActivation)
{
	ASSERT_NO_FATAL_FAILURE(checkFreshFixedProtection("new-horizons:guardianSpirit"));
}
TEST_F(PassiveDivineActionBudgetTest, FreshFullHolyArmorDoesNotSpendSecondAutomaticActivation)
{
	ASSERT_NO_FATAL_FAILURE(checkFreshFixedProtection("new-horizons:holyArmor"));
}
TEST_F(PassiveDivineActionBudgetTest, FreshFullHeavenlyGaleDoesNotSpendSecondAutomaticActivation)
{
	ASSERT_NO_FATAL_FAILURE(checkFreshFixedProtection("new-horizons:heavenlyGale"));
}
TEST_F(PassiveDivineActionBudgetTest, FreshFullDivineRetributionDoesNotSpendSecondAutomaticActivation)
{
	ASSERT_NO_FATAL_FAILURE(checkFreshFixedProtection("new-horizons:divineRetribution"));
}
TEST_F(PassiveDivineActionBudgetTest, PartiallyDepletedFullDurationGuardianRefillsAtZeroSpellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:guardianSpirit", 0));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto * target = battle()->battleActiveUnit();
	ASSERT_NE(target, nullptr);
	const auto pool = target->getGuardianSpiritHitPoints();
	ASSERT_EQ(pool, 50);
	ASSERT_EQ(target->getGuardianSpiritRoundsRemaining(), 2);
	const auto health = target->getAvailableHealth();
	auto state = target->acquireState();
	int64_t damage = 10;
	state->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_EQ(damage, 0);
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	auto & changed = injury.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = state->save();
	gameHandler->sendAndApply(injury);
	ASSERT_EQ(target->getGuardianSpiritHitPoints(), pool - 10);
	ASSERT_EQ(target->getGuardianSpiritRoundsRemaining(), 2);
	ASSERT_EQ(target->getAvailableHealth(), health);
	auto receipt = server.casts.front().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	const auto rng = utilityRngState();
	const auto choices = newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {target->unitId()});
	EXPECT_EQ(choices.size(), 1);
	EXPECT_EQ(utilityRngState(), rng);
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(server.casts.size(), 2);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 2);
	EXPECT_EQ(target->getGuardianSpiritHitPoints(), pool);
	EXPECT_EQ(target->getGuardianSpiritRoundsRemaining(), 2);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
}

TEST_F(PassiveDivineActionBudgetTest, FixedProtectionAppliesExtendOnceAndDoesNotRefreshItAway)
{
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:holyArmor", 100));
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"});
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", "new-horizons:spellcraft.extendSpell"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:spellcraft", "new-horizons:spellcraft.extendSpell"));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto spell = server.casts.front().announcement.spellID;
	const auto * target = battle()->battleActiveUnit();
	const auto bonuses = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(bonuses->size(), 1);
	EXPECT_EQ(bonuses->front()->turnsRemain, 3); // Configured two, Extend once.
	auto receipt = server.casts.front().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget({battle::Destination(target)});
	EXPECT_EQ(mechanics->getExtendSpellBonusRounds(), 0);
	EXPECT_TRUE(newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {target->unitId()}).empty());
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(server.casts.size(), 1);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 1);
	EXPECT_EQ(bonuses->front()->turnsRemain, 3);
}

TEST_F(PassiveDivineActionBudgetTest, ShortFixedTimerRefreshRetainsNativeOrdinaryMagnitude)
{
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:holyArmor", 100));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto spell = server.casts.front().announcement.spellID;
	const auto * target = battle()->battleActiveUnit();
	const auto initial = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(initial->size(), 1);
	const auto value = initial->front()->val;
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	ASSERT_EQ(server.casts.size(), 1); // Lower proposed potency is not itself a useful recast.
	advanceRound();
	const auto before = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(before->size(), 1);
	ASSERT_EQ(before->front()->turnsRemain, 1);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(server.casts.size(), 2);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 2);
	const auto after = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(after->size(), 1);
	EXPECT_EQ(after->front()->turnsRemain, 2);
	EXPECT_EQ(after->front()->val, value); // Native ordinary refresh keeps the old stronger amount.
}

TEST_F(PassiveDivineActionBudgetTest, HistoricalMarkedRecipientUsesConfiguredDurationAndPrecastPotency)
{
	historicalMetamagic = true;
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:holyArmor", 0));
	const auto & captured = attackerSideHero->getPerkState().rules["metamagicPolicy"];
	ASSERT_EQ(captured["version"].Integer(), 2);
	ASSERT_TRUE(captured["adeptAdditionalPoints"].isNull());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto spell = server.casts.front().announcement.spellID;
	const auto * target = battle()->battleActiveUnit();
	const auto initial = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(initial->size(), 1);
	ASSERT_EQ(initial->front()->val, 30);
	ASSERT_EQ(initial->front()->turnsRemain, 2);
	const auto meta = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
	attackerSideHero->setSecSkillLevel(meta, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.lingeringMagic"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.lingeringMagic"));
	auto state = target->acquireState();
	state->arcaneMarks.set(BattleSide::ATTACKER, 3);
	BattleUnitsChanged marks;
	marks.battleID = BattleID(0);
	auto & changed = marks.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = state->save();
	gameHandler->sendAndApply(marks);
	auto receipt = server.casts.front().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget({battle::Destination(target)});
	const auto preview = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, target);
	ASSERT_TRUE(preview);
	ASSERT_TRUE(preview->effect.primary);
	ASSERT_TRUE(preview->durationRounds);
	EXPECT_EQ(preview->effect.primary->value, 43);
	EXPECT_EQ(*preview->durationRounds, 4);
	const auto choices = newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {target->unitId()});
	EXPECT_EQ(choices.size(), 1);
	EXPECT_EQ(target->metamagicMarks(BattleSide::ATTACKER), 3);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(server.casts.size(), 2);
	EXPECT_EQ(battle()->getPassiveDivineMandateUses(BattleSide::ATTACKER), 2);
	const auto after = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(after->size(), 1);
	EXPECT_EQ(after->front()->val, preview->effect.primary->value);
	EXPECT_EQ(after->front()->turnsRemain, *preview->durationRounds);
	EXPECT_EQ(target->metamagicMarks(BattleSide::ATTACKER), 4); // This fresh mark did not strengthen its own cast.
}


TEST_F(PassiveDivineActionBudgetTest, RecalculatedRefreshRequiresMetamagicOrWisdomEffectiveness)
{
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:holyArmor", 100));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto spell = server.casts.front().announcement.spellID;
	const auto * target = battle()->battleActiveUnit();
	const auto initial = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	ASSERT_EQ(initial->size(), 1);
	ASSERT_GT(initial->front()->val, 30);
	for(int policy = 0; policy < 5; ++policy)
	{
		SCOPED_TRACE(policy);
		newHorizonsMetamagic::CastProfile profile;
		profile.hero = attackerSideHero;
		profile.heroSide = BattleSide::ATTACKER;
		profile.perks.pointPolicy = policy == 1 || policy == 3;
		profile.perks.rank = policy == 2 || policy == 3 ? 3 : 0;
		profile.perks.wisdomEffectivenessBonus = policy == 4 ? 5 : 0;
		Bonus incoming = *initial->front();
		incoming.val = 30;
		SetStackEffect packet;
		packet.toUpdate.emplace_back(target->unitId(), std::vector<Bonus>{incoming});
		newHorizonsMetamagic::replaceRecalculatedSpellBonuses(&profile, *battle(),
			spell, spell, {target->unitId()}, packet);
		if(policy < 2)
		{
			EXPECT_TRUE(packet.toAdd.empty());
			EXPECT_TRUE(packet.toRemove.empty());
			ASSERT_EQ(packet.toUpdate.front().second.size(), 1);
		}
		else
		{
			ASSERT_EQ(packet.toAdd.size(), 1);
			ASSERT_EQ(packet.toRemove.size(), 1);
			EXPECT_EQ(packet.toRemove.front().second.front().val, initial->front()->val);
			EXPECT_EQ(packet.toAdd.front().second.front().val, incoming.val);
			EXPECT_EQ(packet.toAdd.front().second.front().turnsRemain, initial->front()->turnsRemain);
			EXPECT_TRUE(packet.toUpdate.front().second.empty());
		}
	}
	EXPECT_EQ(target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)))->front()->val,
		initial->front()->val);
}

TEST_F(PassiveDivineActionBudgetTest, CurrentNonTowerMarksDoNotScaleAutomaticLight)
{
	ASSERT_NO_FATAL_FAILURE(prepareFixedProtection("new-horizons:holyArmor", 0));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(server.casts.size(), 1);
	const auto spell = server.casts.front().announcement.spellID;
	const auto * target = battle()->battleActiveUnit();
	const auto meta = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
	attackerSideHero->setSecSkillLevel(meta, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.lingeringMagic"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.lingeringMagic"));
	auto state = target->acquireState();
	state->arcaneMarks.set(BattleSide::ATTACKER, 3);
	BattleUnitsChanged marks;
	marks.battleID = BattleID(0);
	auto & changed = marks.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = state->save();
	gameHandler->sendAndApply(marks);
	auto receipt = server.casts.front().announcement.automaticHeroCast;
	++receipt.mandateUseOrdinal;
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	cast.setAutomaticHeroCast(receipt);
	const auto mechanics = cast.mechanicsForTarget({battle::Destination(target)});
	ASSERT_NE(mechanics->getMetamagicCastProfile(), nullptr);
	EXPECT_EQ(mechanics->getMetamagicCastProfile()->perks.rank, 0);
	const auto preview = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, target);
	ASSERT_TRUE(preview);
	ASSERT_TRUE(preview->effect.primary);
	EXPECT_EQ(preview->effect.primary->value, 30);
	ASSERT_TRUE(preview->durationRounds);
	EXPECT_EQ(*preview->durationRounds, 2);
	const auto rng = utilityRngState();
	EXPECT_TRUE(newHorizonsPassiveDivineMandate::buildSpellPool(*battle(), *attackerSideHero,
		receipt, {target->unitId()}).empty());
	EXPECT_EQ(utilityRngState(), rng);
	EXPECT_EQ(target->metamagicMarks(BattleSide::ATTACKER), 3);
}
