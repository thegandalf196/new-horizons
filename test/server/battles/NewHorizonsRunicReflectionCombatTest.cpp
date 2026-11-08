/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionFixture.h"
#include "../../../lib/spells/NewHorizonsShadowMagnitudes.h"

TEST_F(NewHorizonsRunicReflectionCombatTest, AcceptedNativeOutcomeReflectsOnceWithOnePaymentAndTheExistingDrawSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto expectedRng = chooseSample(0, 40, true);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto command = battle()->getCommandResources(BattleSide::ATTACKER);
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 20);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), command);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_FALSE(casts.front().announcement.resistedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, OtherSourceResistanceRemainsResistanceWithoutAnIndependentReflectionRoll)
{
	ASSERT_NO_FATAL_FAILURE(prepare(15));
	const auto expectedRng = chooseSample(40, 55, false);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.resistedCres.contains(dwarf->unitId()));
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, FailedOrdinaryResistanceAppliesTheSpellNormally)
{
	ASSERT_NO_FATAL_FAILURE(prepare(15));
	const auto expectedRng = chooseSample(55, 100, false);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth - 20);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	EXPECT_TRUE(server.castsOf(SpellID::MAGIC_ARROW).front().announcement.reflectedCres.empty());
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, OrdinaryFullResistanceStillRejectsTheTargetWithoutPaymentOrRandomDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare(60));
	const auto before = wire(*gameHandler->randomizer);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(castArrow());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	EXPECT_EQ(wire(*gameHandler->randomizer), before);
	EXPECT_TRUE(server.castsOf(SpellID::MAGIC_ARROW).empty());
}

TEST_F(NewHorizonsRunicReflectionCombatTest, AreaReflectionReturnsOnlyTheResistedRecipientShare)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
	auto * secondOwn = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(69), 1000);
	auto * ordinaryVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000);
	(void)chooseSample(0, 40, false);
	const auto nativeHealth = dwarf->getAvailableHealth();
	const auto firstOwnHealth = casterArmy->getAvailableHealth(), secondOwnHealth = secondOwn->getAvailableHealth();
	const auto ordinaryHealth = ordinaryVictim->getAvailableHealth();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = SpellID::FIREBALL; action.aimToHex(dwarf->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(ordinaryVictim->getAvailableHealth(), ordinaryHealth - 25);
	const auto firstDamage = firstOwnHealth - casterArmy->getAvailableHealth();
	const auto secondDamage = secondOwnHealth - secondOwn->getAvailableHealth();
	EXPECT_EQ(firstDamage + secondDamage, 25);
	EXPECT_TRUE(firstDamage == 0 || secondDamage == 0) << "A reflected share must not duplicate the area";
	const auto casts = server.castsOf(SpellID::FIREBALL);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
}

TEST_F(NewHorizonsRunicReflectionCombatTest, LaterResistedChainHopRetainsItsOwnDamageIndexWithoutReplayingTheChain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(SpellID::CHAIN_LIGHTNING);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000);
	(void)chooseSample(0, 40, false);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	const auto primaryHealth = primary->getAvailableHealth();
	const auto * spell = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&prediction);
	int64_t laterHop = -1;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		if(const auto * damage = dynamic_cast<const spells::effects::native::Damage *>(&effect))
			laterHop = damage->damageForTarget(1, mechanics.get(), casterArmy);
		return true;
	});
	ASSERT_GT(laterHop, 0); ASSERT_LT(laterHop, 130);
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = SpellID::CHAIN_LIGHTNING; action.aimToUnit(primary);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(primary->getAvailableHealth(), primaryHealth - 130);
	// Ordinary resistance skips this dwarf without consuming a hop. The next
	// ordinary recipient and the one redirected share each retain index1.
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 2 * laterHop);
	const auto casts = server.castsOf(SpellID::CHAIN_LIGHTNING);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
}

TEST_F(NewHorizonsRunicReflectionCombatTest, MagicalCreatureCastReflectsWithoutChargingTheHeroOrAddingAnotherCreatureAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	ASSERT_EQ(casterArmy->casts.available(), 1);
	(void)chooseSample(0, 40, true);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto command = battle()->getCommandResources(BattleSide::ATTACKER);
	const battle::Target destination{battle::Destination(dwarf)};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(casterArmy, destination, SpellID::MAGIC_ARROW)));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 20);
	EXPECT_EQ(casterArmy->casts.available(), 0);
	EXPECT_TRUE(casterArmy->movedThisRound);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), command);
	ASSERT_EQ(server.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, ReflectedTargetMayResistButItsOwnNativeShareCannotReflectAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, true));
	ASSERT_EQ(newHorizonsRunicReflection::nativeChance(*casterArmy), 20);
	const auto expectedRng = chooseSample(0, 40, true, 0, 20);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().announcement.reflectedCres.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_TRUE(casts.front().announcement.resistedCres.contains(casterArmy->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, NativeCreatureCasterFactsSurviveActualSourceRemovalWithoutBorrowingItsGraph)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CREATURE_SPELL_POWER, BonusSource::CREATURE_ABILITY, 50, BonusSourceID()));
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const int power = casterArmy->getEffectPower(spell);
	const auto id = casterArmy->unitId();
	const battle::Units recipients{dwarf};
	auto snapshot = spells::captureRunicCreatureCaster(*casterArmy, *spell, *battle(), recipients);
	ASSERT_NE(snapshot, nullptr);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(69), 1000);
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(id, UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	const auto * removed = battle()->battleGetUnitByID(id);
	ASSERT_NE(removed, nullptr);
	ASSERT_TRUE(removed->isGhost());
	EXPECT_EQ(spells::captureRunicCreatureCaster(*removed, *spell, *battle(), recipients), nullptr);
	EXPECT_EQ(snapshot->getCasterUnitId(), id);
	EXPECT_EQ(snapshot->getCasterOwner(), PlayerColor(0));
	EXPECT_EQ(snapshot->getEffectPower(spell), power);
	EXPECT_EQ(snapshot->getSpellBonus(spell, 123, dwarf), 123);
	EXPECT_EQ(snapshot->getSpecificSpellBonus(spell, 456), 456);
	MetaString description;
	EXPECT_NO_THROW(snapshot->getCastDescription(spell, recipients, description));
	const auto reserveBefore = reserve->save();
	EXPECT_THROW(snapshot->spendMana(gameHandler->spellEnv.get(), 1), std::logic_error);
	EXPECT_EQ(reserve->save(), reserveBefore);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, ReflectedShareAndReadOnlyValueViewRetainTheOriginalOffensiveProfileOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, true));
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&prediction);
	const auto before = gameState()->saveToMemory();
	const auto beforeRng = wire(*gameHandler->randomizer);
	auto reflected = mechanics->runicReflectionView();
	ASSERT_NE(reflected, nullptr);
	ASSERT_TRUE(reflected->isMagicMirror());
	EXPECT_TRUE(reflected->ownerMatches(casterArmy, false));
	std::vector<const spells::effects::Effect *> effects;
	mechanics->forEachEffect([&](const auto & effect) { effects.push_back(&effect); return false; });
	const auto pool = newHorizonsRunicReflection::legalRecipients(*reflected, effects);
	ASSERT_EQ(pool.size(), 1u); EXPECT_EQ(pool.front(), casterArmy);
	const auto * damage = dynamic_cast<const spells::effects::native::Damage *>(effects.front());
	ASSERT_NE(damage, nullptr);
	EXPECT_EQ(damage->damageForTarget(0, reflected.get(), casterArmy), 23);
	const spells::Target destination{spells::Destination(casterArmy)};
	EXPECT_THROW(reflected->cast(gameHandler->spellEnv.get(), destination), std::logic_error);
	EXPECT_THROW(reflected->castEval(gameHandler->spellEnv.get(), destination), std::logic_error);
	EXPECT_THROW(reflected->applyEffects(gameHandler->spellEnv.get(), destination, false, false), std::logic_error);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), beforeRng);
	(void)chooseSample(0, 40, true);
	const auto ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 23);
	ASSERT_EQ(server.castsOf(SpellID::MAGIC_ARROW).size(), 1u);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, NativeCasterSnapshotRejectsForeignOccurrenceWrappersAndOtherBattleDomains)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const battle::Units recipients{dwarf};
	CStackBasicDescriptor descriptor(casterArmy->creatureId(), casterArmy->unitBaseAmount());
	CStack foreign(&descriptor, casterArmy->unitOwner(), casterArmy->unitId(), casterArmy->unitSide(), casterArmy->unitSlot());
	foreign.initialPosition = BattleHex(80); foreign.localInit(battle());
	ASSERT_EQ(foreign.getBattle(), battle());
	struct ForgedCallback final : CPlayerBattleCallback
	{
		const battle::Unit & foreign;
		ForgedCallback(const BattleInfo * native, const battle::Unit & unit)
			: CPlayerBattleCallback(native, PlayerColor(0)), foreign(unit) {}
		const battle::Unit * battleGetUnitByID(uint32_t id) const override
		{ return id == foreign.unitId() ? &foreign : nullptr; }
	} forged(battle(), foreign);
	EXPECT_EQ(spells::captureRunicCreatureCaster(foreign, *spell, forged, recipients), nullptr);
	spells::AbilityCaster foreignWrapper(&foreign, 1);
	EXPECT_EQ(spells::captureRunicCreatureCaster(foreignWrapper, *spell, forged, recipients), nullptr);
	BattleInfo other(gameState().get());
	const CPlayerBattleCallback otherDomain(&other, PlayerColor(0));
	EXPECT_EQ(spells::captureRunicCreatureCaster(*casterArmy, *spell, otherDomain, recipients), nullptr);
	spells::AbilityCaster admitted(casterArmy, 1);
	EXPECT_NE(spells::captureRunicCreatureCaster(admitted, *spell, *battle(), recipients), nullptr);
	std::vector<std::unique_ptr<spells::BonusCaster>> chain;
	const spells::Caster * cursor = casterArmy;
	for(int depth = 0; depth < 9; ++depth)
	{
		auto descriptor = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NONE, BonusSource::OTHER, 0, BonusSourceID());
		chain.push_back(std::make_unique<spells::BonusCaster>(cursor, descriptor));
		cursor = chain.back().get();
	}
	EXPECT_EQ(spells::captureRunicCreatureCaster(*cursor, *spell, *battle(), recipients), nullptr);
	// The unknown cyclic proxy is rejected by native type admission before
	// any delegated virtual getter can recurse through its self-reference.
	spells::ExternalCaster cyclic;
	cyclic.setActualCaster(&cyclic);
	EXPECT_EQ(spells::captureRunicCreatureCaster(cyclic, *spell, *battle(), recipients), nullptr);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, OrdinaryResistanceRerollPartitionsItsFinalAcceptedSampleRatherThanTheFirstDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, false, false, true));
	ASSERT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).available());
	std::vector<std::byte> expectedRng;
	bool found = false;
	for(int seed = 0; seed != 4096 && !found; ++seed)
	{
		GameRandomizer expected(*gameState()); expected.setSeed(seed);
		int first = -1;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const int sample = expected.getDefault().nextInt(0, 99);
			if(unit == dwarf) first = sample;
		}
		if(first < 40) continue;
		const int reroll = expected.getDefault().nextInt(0, 99);
		if(reroll >= 40) continue;
		const std::vector<const battle::Unit *> targets{casterArmy};
		(void)RandomGeneratorUtil::nextItem(targets, expected.getDefault());
		(void)expected.getDefault().nextInt(0, 99);
		expectedRng = wire(expected);
		gameHandler->randomizer->setSeed(seed); found = true;
	}
	ASSERT_TRUE(found);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_TRUE(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER).used);
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 20);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, NoLegalReflectedRecipientFallsBackToResistanceWithoutAnotherRandomOutcome)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	casterArmy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LEVEL_SPELL_IMMUNITY, BonusSource::OTHER, 5, BonusSourceID()));
	const auto expectedRng = chooseSample(0, 40, false);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_TRUE(casts.front().announcement.resistedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, LifeDrainRedirectsItsActualDamageAndHealingPairWithoutASecondCastOrCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID drain(SpellID::decode(std::string(newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL)));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(drain);
	auto wounded = dwarf->acquireState(); int64_t injury = 7; wounded->damage(injury);
	BattleUnitsChanged update; update.battleID = BattleID(0);
	update.changedStacks.emplace_back(dwarf->unitId(), UnitChanges::EOperation::UPDATE);
	update.changedStacks.back().data = wounded->save(); update.changedStacks.back().healthDelta = -7;
	gameHandler->sendAndApply(update);
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, drain.toSpell());
	auto mechanics = drain.toSpell()->battleMechanics(&prediction);
	auto reflected = mechanics->runicReflectionView();
	std::vector<const spells::effects::Effect *> effects;
	mechanics->forEachEffect([&](const auto & effect) { effects.push_back(&effect); return false; });
	EXPECT_TRUE(newHorizonsRunicReflection::legalRecipients(*reflected, effects).empty());
	ASSERT_EQ(newHorizonsRunicReflection::legalRecipients(*reflected, effects, dwarf).size(), 1u);
	const auto expectedDamage = reflected->adjustEffectValue(casterArmy);
	ASSERT_GT(expectedDamage * newHorizonsMagic::lifeDrainHealingPercent(attackerSideHero) / 100, 7);
	const auto expectedRng = chooseSingleHostileSample();
	const auto ownHealth = casterArmy->getAvailableHealth(), nativeHealth = dwarf->getAvailableHealth();
	const auto cost = battle()->battleGetSpellCost(drain.toSpell(), attackerSideHero);
	const auto mana = attackerSideHero->getManaAvailable();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = drain; action.aimToUnit(dwarf); action.aimToUnit(casterArmy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - expectedDamage);
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth + 7);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	const auto casts = server.castsOf(drain);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, PandemoniumUsesRedirectedPrecastDebuffsAfterOriginalDamageRemovesFrozen)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode(std::string(newHorizonsPandemonium::SPELL_KEY)));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	Bonus slow(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		-1, BonusSourceID(SpellID(SpellID::SLOW)));
	slow.turnsRemain = 3; slow.statusTags = {BonusStatusTag::DEBUFF}; slow.statusIdentity = "slow";
	SetStackEffect statuses; statuses.battleID = BattleID(0);
	statuses.toAdd.emplace_back(dwarf->unitId(), std::vector<Bonus>{slow});
	statuses.toAdd.emplace_back(casterArmy->unitId(), std::vector<Bonus>{slow,
		newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
	gameHandler->sendAndApply(statuses);
	ASSERT_EQ(newHorizonsDebuffStatuses::snapshot(*dwarf).count(), 1u);
	ASSERT_EQ(newHorizonsDebuffStatuses::snapshot(*casterArmy).count(), 2u);
	ASSERT_TRUE(casterArmy->isFrozen());
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	auto mechanics = spell.toSpell()->battleMechanics(&prediction);
	auto reflected = mechanics->runicReflectionView();
	const auto expectedDamage = reflected->getPandemoniumDamage(casterArmy, 2);
	ASSERT_EQ(expectedDamage, 40);
	const auto expectedRng = chooseSample(0, 40, true);
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_FALSE(casterArmy->isFrozen());
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*casterArmy).count(), 1u);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 2 * expectedDamage);
	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, NaturesWrathReflectsTheOriginalLaterHopAsHostileDamageWithoutReplayingItsRoute)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode(std::string(newHorizonsNaturesWrath::SPELL_KEY)));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	auto mechanics = spell.toSpell()->battleMechanics(&prediction);
	const auto route = mechanics->getNaturesWrathRoute(casterArmy);
	ASSERT_EQ(route.size(), 2u); ASSERT_EQ(route[1].unitValue, dwarf);
	auto reflected = mechanics->runicReflectionView();
	const auto expectedDamage = reflected->getNaturesWrathDamage(casterArmy, 1);
	ASSERT_GT(expectedDamage, 0);
	ASSERT_LT(expectedDamage, reflected->getNaturesWrathDamage(casterArmy, 0));
	const auto expectedRng = chooseSingleHostileSample();
	const auto nativeHealth = dwarf->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell; action.aimToUnit(casterArmy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - expectedDamage);
	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, HandOfFateRedirectsOnlyItsResolvedSpillWithoutASecondPrimaryOrCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode("new-horizons:handOfFate"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000);
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	auto mechanics = spell.toSpell()->battleMechanics(&prediction);
	const auto candidates = mechanics->getHandOfFateCollateralPool(primary);
	ASSERT_EQ(candidates.size(), 2u);
	std::vector<std::byte> expectedRng;
	for(int seed = 0; seed != 4096 && expectedRng.empty(); ++seed)
	{
		GameRandomizer expected(*gameState()); expected.setSeed(seed);
		int resistance = -1;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const int sample = expected.getDefault().nextInt(0, 99);
			if(unit == dwarf) resistance = sample;
		}
		if(resistance >= 40) continue;
		const int chosen = expected.getDefault().nextInt(1, static_cast<int>(candidates.size())) - 1;
		if(candidates[chosen].unitValue != dwarf) continue;
		const std::vector<const battle::Unit *> redirect{casterArmy};
		(void)RandomGeneratorUtil::nextItem(redirect, expected.getDefault());
		(void)expected.getDefault().nextInt(0, 99);
		expectedRng = wire(expected); gameHandler->randomizer->setSeed(seed);
	}
	ASSERT_FALSE(expectedRng.empty());
	const auto primaryDamage = mechanics->adjustEffectValue(primary);
	const auto spillDamage = mechanics->adjustRecipientDamage(casterArmy, primaryDamage / 2);
	ASSERT_EQ(primaryDamage, 70); ASSERT_EQ(spillDamage, 35);
	const auto primaryHealth = primary->getAvailableHealth(), ownHealth = casterArmy->getAvailableHealth();
	const auto nativeHealth = dwarf->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell.toSpell(), attackerSideHero);
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell; action.aimToUnit(primary);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(primary->getAvailableHealth(), primaryHealth - primaryDamage);
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - spillDamage);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	ASSERT_EQ(server.castsOf(spell).size(), 1u);
	const auto feedback = LIBRARY->generaltexth->translate("new-horizons.combat.runicEndurance.reflected");
	EXPECT_EQ(std::ranges::count(server.battleLogLines, feedback), 1);
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}
