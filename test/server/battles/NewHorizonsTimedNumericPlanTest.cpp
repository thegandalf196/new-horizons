/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionFixture.h"
#include "../../../lib/spells/effects/native/Timed.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/SetMetamagicPointState.h"
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include <limits>

class NewHorizonsTimedNumericPlanTest : public NewHorizonsRunicReflectionCombatTest
{
protected:
	enum class Scaling { Ordinary, Wisdom, Points, Conduit };
	static std::vector<std::byte> numericWire(Bonus bonus)
	{
		// Compare serialized numeric and gameplay ownership fields. Display
		// resources and the unrelated dynamic aura owner are outside this plan.
		bonus.bonusOwner = PlayerColor::CANNOT_DETERMINE;
		bonus.description = MetaString(); bonus.customIconPath = ImagePath();
		return wire(bonus);
	}
	void configureScaling(Scaling scaling)
	{
		if(scaling == Scaling::Wisdom)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:aine")));
			ASSERT_TRUE(attackerSideHero->getHeroClass()->isMagicHero());
			const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
			attackerSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.intelligence"});
			attackerSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.arcaneInsight"});
		}
		if(scaling == Scaling::Points)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:aine")));
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), 3, ChangeValueMode::ABSOLUTE);
			SetMetamagicPointState points; points.battleID = BattleID(0); points.side = BattleSide::ATTACKER;
			points.round = battle()->getRound(); points.state.points = 5;
			gameHandler->sendAndApply(points);
		}
	}
	void assertScaling(const spells::Mechanics & reflected, const std::vector<Bonus> & expected, SpellID spell, Scaling scaling)
	{
		const auto * captured = reflected.getMetamagicCastProfile();
		ASSERT_NE(captured, nullptr);
		if(scaling == Scaling::Wisdom) EXPECT_EQ(captured->perks.wisdomEffectivenessBonus, 25);
		if(scaling == Scaling::Points)
		{
			EXPECT_TRUE(captured->perks.pointPolicy); EXPECT_EQ(captured->points, 5);
			EXPECT_EQ(newHorizonsMetamagic::targetFreeEffectivenessBonusPercent(captured), 15);
		}
		if(scaling == Scaling::Conduit)
		{
			EXPECT_EQ(captured->factionSpellDamageBonus, 15);
			EXPECT_EQ(newHorizonsMetamagic::targetFreeEffectivenessBonusPercent(captured), 0);
		}
		if(scaling == Scaling::Wisdom || scaling == Scaling::Points)
		{
			SetStackEffect twice;
			twice.toAdd.emplace_back(casterArmy->unitId(), expected);
			newHorizonsMetamagic::adjustAppliedBonuses(captured, *battle(), spell, spell, {casterArmy->unitId()}, twice);
			std::vector<std::vector<std::byte>> onceWire, twiceWire;
			for(const auto & bonus : expected) onceWire.push_back(numericWire(bonus));
			for(const auto & bonus : twice.toAdd.front().second) twiceWire.push_back(numericWire(bonus));
			EXPECT_NE(onceWire, twiceWire) << "The enabled profile must distinguish one transform from two";
		}
	}
	void reflectedParity(SpellID spell, const char * school, Scaling scaling = Scaling::Ordinary)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(0, false, scaling == Scaling::Conduit));
		ASSERT_NO_FATAL_FAILURE(configureScaling(scaling));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(school)), 3, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addSpellToSpellbook(spell);
		spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		auto mechanics = spell.toSpell()->battleMechanics(&prediction);
		auto reflected = mechanics->runicReflectionView();
		const auto ownBefore = casterArmy->save(), enemyBefore = dwarf->save();
		const auto rng = wire(*gameHandler->randomizer);
		const auto mana = attackerSideHero->getManaAvailable();
		const auto command = battle()->getCommandResources(BattleSide::ATTACKER);
		const auto failedIdentifiers = LIBRARY->identifiers()->getModsWithFailedRequests();
		std::optional<std::vector<Bonus>> expected;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			if(const auto * timed = dynamic_cast<const spells::effects::native::Timed *>(&effect))
				expected = timed->preparedRecipientBonuses(reflected.get(), casterArmy);
			return false;
		});
		ASSERT_TRUE(expected); ASSERT_FALSE(expected->empty());
		ASSERT_NO_FATAL_FAILURE(assertScaling(*reflected, *expected, spell, scaling));
		EXPECT_EQ(casterArmy->save(), ownBefore); EXPECT_EQ(dwarf->save(), enemyBefore);
		EXPECT_EQ(wire(*gameHandler->randomizer), rng);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), command);
		EXPECT_EQ(LIBRARY->identifiers()->getModsWithFailedRequests(), failedIdentifiers);
		(void)chooseSample(0, 40, true);
		BattleAction action; action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER; action.spell = spell; action.aimToUnit(dwarf);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		std::vector<std::vector<std::byte>> predicted, actual;
		for(const auto & bonus : *expected) predicted.push_back(numericWire(bonus));
		for(const auto & bonus : battle()->getLocalSpellEffects(casterArmy->unitId()))
			if(bonus.source == BonusSource::SPELL_EFFECT && bonus.sid == BonusSourceID(spell)) actual.push_back(numericWire(bonus));
		std::ranges::sort(predicted); std::ranges::sort(actual);
		EXPECT_EQ(actual, predicted);
		EXPECT_TRUE(battle()->getLocalSpellEffects(dwarf->unitId()).empty());
	}
};

TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedSlowNumbersAndLifetimesMatchNativeAppliedBonusWires)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::SLOW, "new-horizons:sorceryMagic"));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedCurseNumbersAndStatusMetadataMatchNativeAppliedBonusWires)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::CURSE, "new-horizons:shadowMagic"));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedSorrowNumbersAndEnemyMoraleProvenanceMatchNativeAppliedBonusWires)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::SORROW, "new-horizons:shadowMagic"));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedMisfortunePreparedComponentsMatchNativeAppliedBonusWires)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::MISFORTUNE, "new-horizons:chaosMagic"));
}

TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedSlowWisdomEffectivenessMatchesOneNativeMagnitudeAndDurationAdjustment)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::SLOW, "new-horizons:sorceryMagic", Scaling::Wisdom));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedSorrowHeroPointsMatchOneNativeMagnitudeAndDurationAdjustment)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::SORROW, "new-horizons:shadowMagic", Scaling::Points));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ReflectedMisfortuneWisdomEffectivenessMatchesItsNativeChanceFactorAndDuration)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::MISFORTUNE, "new-horizons:chaosMagic", Scaling::Wisdom));
}
TEST_F(NewHorizonsTimedNumericPlanTest, ConduitDamageBonusDoesNotScaleReflectedSorrowNumbersOrDuration)
{
	ASSERT_NO_FATAL_FAILURE(reflectedParity(SpellID::SORROW, "new-horizons:shadowMagic", Scaling::Conduit));
}

TEST_F(NewHorizonsTimedNumericPlanTest, UnsupportedDynamicScriptAndUnknownIdentifierPlansRemainExplicitAndReadOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	auto mechanics = SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&prediction);
	const auto own = casterArmy->save(), enemy = dwarf->save();
	const auto rng = wire(*gameHandler->randomizer);
	const auto failed = LIBRARY->identifiers()->getModsWithFailedRequests();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto command = battle()->getCommandResources(BattleSide::ATTACKER);
	const std::string unknown = "new-horizons:unregisteredTimedNumericPlanTest";
	ASSERT_FALSE(LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "bonus", unknown, true));
	for(const auto * capability : {"updater", "limiters", "propagator", "propagationUpdater", "script", "unknownType", "unknownSubtype"})
	{
		JsonNode data; auto & bonus = data["bonus"]["probe"];
		bonus["type"].String() = "MORALE"; bonus["val"].Integer() = -1;
		bonus["duration"].String() = "N_TURNS"; bonus["turns"].Integer() = 3;
		if(std::string_view(capability) == "unknownType") bonus["type"].String() = unknown;
		else if(std::string_view(capability) == "unknownSubtype")
		{ bonus["type"].String() = "PRIMARY_SKILL"; bonus["subtype"].String() = unknown; }
		else if(std::string_view(capability) == "script") bonus["type"].String() = "COMBAT_EVENT_TRIGGER";
		else bonus[capability].String() = "unsupported";
		spells::effects::native::Timed effect; effect.init(data);
		EXPECT_FALSE(effect.preparedRecipientBonuses(mechanics.get(), dwarf)) << capability;
	}
	EXPECT_FALSE(LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "bonus", unknown, true));
	EXPECT_EQ(LIBRARY->identifiers()->getModsWithFailedRequests(), failed);
	EXPECT_EQ(casterArmy->save(), own); EXPECT_EQ(dwarf->save(), enemy);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), command);
}

TEST_F(NewHorizonsTimedNumericPlanTest, MalformedMetadataAndMismatchedTypedIdentifiersFailBeforeScalarOrRegistryAccess)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast prediction(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	auto mechanics = SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&prediction);
	const auto own = casterArmy->save(), enemy = dwarf->save();
	const auto rng = wire(*gameHandler->randomizer);
	const auto failed = LIBRARY->identifiers()->getModsWithFailedRequests();
	for(const auto * shape : {"stacking", "statusIdentity", "hidden", "val", "turns", "type", "subtype", "nestedType", "nestedSubtype", "affliction"})
	{
		JsonNode data; auto & bonus = data["bonus"]["probe"];
		bonus["type"].String() = "MORALE"; bonus["val"].Integer() = -1;
		bonus["duration"].String() = "N_TURNS"; bonus["turns"].Integer() = 3;
		if(std::string_view(shape) == "hidden") bonus["hidden"].String() = "yes";
		else if(std::string_view(shape) == "val" || std::string_view(shape) == "turns") bonus[shape].String() = "not a number";
		else if(std::string_view(shape) == "type") bonus["type"].String() = "spell.MORALE";
		else if(std::string_view(shape) == "subtype")
		{ bonus["type"].String() = "PRIMARY_SKILL"; bonus["subtype"].String() = "creature.attack"; }
		else if(std::string_view(shape) == "nestedType") bonus["type"].String() = "bonus.spell.MORALE";
		else if(std::string_view(shape) == "nestedSubtype")
		{ bonus["type"].String() = "PRIMARY_SKILL"; bonus["subtype"].String() = "primarySkill.creature.attack"; }
		else if(std::string_view(shape) == "affliction") bonus["type"].String() = "PHYSICAL_AFFLICTION";
		else bonus[shape].Integer() = 7;
		spells::effects::native::Timed effect; effect.init(data);
		EXPECT_FALSE(effect.preparedRecipientBonuses(mechanics.get(), dwarf)) << shape;
	}
	for(const auto * field : {"val", "turns"})
		for(const double number : {1e100, -1e100, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
		{
			JsonNode data; auto & bonus = data["bonus"]["probe"];
			bonus["type"].String() = "MORALE"; bonus["val"].Integer() = -1;
			bonus["duration"].String() = "N_TURNS"; bonus["turns"].Integer() = 3;
			bonus[field].Float() = number;
			spells::effects::native::Timed effect; effect.init(data);
			EXPECT_FALSE(effect.preparedRecipientBonuses(mechanics.get(), dwarf)) << field;
		}
	JsonNode valid; valid["bonus"]["morale"]["type"].String() = "bonus.MORALE";
	valid["bonus"]["morale"]["val"].Integer() = -1;
	valid["bonus"]["morale"]["duration"].String() = "N_TURNS";
	valid["bonus"]["morale"]["turns"].Integer() = 3;
	spells::effects::native::Timed ordinary; ordinary.init(valid);
	const auto supported = ordinary.preparedRecipientBonuses(mechanics.get(), dwarf);
	ASSERT_TRUE(supported); ASSERT_EQ(supported->size(), 1u);
	EXPECT_EQ(supported->front().type, BonusType::MORALE);
	EXPECT_EQ(supported->front().val, -1);
	EXPECT_EQ(LIBRARY->identifiers()->getModsWithFailedRequests(), failed);
	EXPECT_EQ(casterArmy->save(), own); EXPECT_EQ(dwarf->save(), enemy);
	EXPECT_EQ(wire(*gameHandler->randomizer), rng);
}
