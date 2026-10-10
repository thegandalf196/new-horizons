/* NewHorizonsTemporaryMagicDispelTest.cpp, part of VCMI; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../luascript/StdInc.h"
#include "../../../luascript/api/library/Bonus.h"

namespace
{
class NewHorizonsTemporaryMagicDispelTest : public NewHorizonsElementalTerrainFixture
{
protected:
	bool capturedPolicy = true;
	CStack * binder = nullptr;
	CStack * target = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsElementalTerrainFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(capturedPolicy)
			rules["spells"]["core:dispel"]["temporaryMagicalEffectsOnly"].Bool() = true;
		else
			rules["spells"]["core:dispel"].Struct().erase("temporaryMagicalEffectsOnly");
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void activateAttacker()
	{
		for(int attempts = 0; attempts < 32; ++attempts)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(active->unitSide() == BattleSide::ATTACKER)
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		FAIL() << "No attacker activation after legal queue progression";
	}

	void prepare()
	{
		startGame();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			for(const auto * school : {"new-horizons:lightMagic", "new-horizons:sorceryMagic"})
				hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(school)),
					MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->addSpellToSpellbook(SpellID::BLESS);
			hero->addSpellToSpellbook(SpellID::DISPEL);
			setTestSpellPointTotal(hero, 1000);
		}
		startTerrainBattle(TerrainId::GRASS);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		binder = addStack(BattleSide::ATTACKER, creatureByName("core:dendroidGuard"), BattleHex(5, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 10000);
		ASSERT_NE(binder, nullptr);
		ASSERT_NE(target, nullptr);
		blockRetaliation(binder);
		beginCombat();
		ASSERT_NO_FATAL_FAILURE(activateAttacker());
	}

	bool has(SpellID source, BonusType type) const
	{
		return !target->getBonuses(CSelector([=](const Bonus * bonus)
		{
			return bonus->source == BonusSource::SPELL_EFFECT && bonus->sid == BonusSourceID(source)
				&& bonus->type == type;
		}))->empty();
	}

	void add(const std::vector<Bonus> & bonuses)
	{
		SetStackEffect change;
		change.battleID = BattleID(0);
		change.toAdd.emplace_back(target->unitId(), bonuses);
		gameHandler->sendAndApply(change);
	}

	void bindAndBless()
	{
		// Bless uses the real spell mechanics and authoritative effect pipeline.
		// The enemy hero owns the recipient; the Dispel below is a public paid action.
		ASSERT_TRUE(castOn(defenderSideHero, SpellID::BLESS, target));
		ASSERT_TRUE(has(SpellID::BLESS, BonusType::ALWAYS_MAXIMUM_DAMAGE));
		ASSERT_TRUE(attack(binder, target->getPosition()));
		ASSERT_TRUE(binder->alive());
		ASSERT_TRUE(target->alive());
		ASSERT_TRUE(has(SpellID::BIND, BonusType::BIND_EFFECT));
		ASSERT_NO_FATAL_FAILURE(activateAttacker());
		ASSERT_TRUE(has(SpellID::BLESS, BonusType::ALWAYS_MAXIMUM_DAMAGE));
	}

	void paidDispel()
	{
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::ATTACKER);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.aimToUnit(target);
		const auto before = attackerSideHero->getManaAvailable();
		const auto cost = battle()->battleGetSpellCost(SpellID(SpellID::DISPEL).toSpell(), attackerSideHero);
		ASSERT_GT(cost, 0);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(BattleSide::ATTACKER), action));
		EXPECT_EQ(before - attackerSideHero->getManaAvailable(), cost);
	}
};
}

TEST_F(NewHorizonsTemporaryMagicDispelTest, ActualDendroidBindSurvivesPaidDispelWhileBlessIsRemoved)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(bindAndBless());
	ASSERT_NO_FATAL_FAILURE(paidDispel());
	EXPECT_TRUE(has(SpellID::BIND, BonusType::BIND_EFFECT));
	EXPECT_FALSE(has(SpellID::BLESS, BonusType::ALWAYS_MAXIMUM_DAMAGE));
}

TEST_F(NewHorizonsTemporaryMagicDispelTest, AbsentCapturedPolicyRetainsPriorV3BindRemoval)
{
	capturedPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(bindAndBless());
	ASSERT_NO_FATAL_FAILURE(paidDispel());
	EXPECT_FALSE(has(SpellID::BIND, BonusType::BIND_EFFECT));
	EXPECT_FALSE(has(SpellID::BLESS, BonusType::ALWAYS_MAXIMUM_DAMAGE));
}

TEST_F(NewHorizonsTemporaryMagicDispelTest, MagicalPoisonIsRemovedWithoutRemovingTypedPhysicalPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus poison(BonusDuration::N_TURNS, BonusType::POISON, BonusSource::SPELL_EFFECT,
		30, BonusSourceID(SpellID(SpellID::POISON)));
	poison.turnsRemain = 3;
	add({poison});
	auto state = target->acquireState();
	state->physicalPoisonBaseDamage = 5;
	state->physicalPoisonActivationsRemaining = 3;
	state->physicalPoisonSourceStackId = static_cast<int32_t>(binder->unitId());
	BattleUnitsChanged change;
	change.battleID = BattleID(0);
	change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.changedStacks.back().data = state->save();
	gameHandler->sendAndApply(change);
	ASSERT_TRUE(SpellID(SpellID::POISON).toSpell()->isMagical());
	ASSERT_TRUE(has(SpellID::POISON, BonusType::POISON));
	ASSERT_NO_FATAL_FAILURE(paidDispel());
	EXPECT_FALSE(has(SpellID::POISON, BonusType::POISON));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 5);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsTemporaryMagicDispelTest, AllTemporaryLifetimesAreRemovedButPermanentZeroAndMixedRemain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	std::vector<Bonus> effects;
	for(unsigned bit = 1; bit < BonusDuration::Size; ++bit)
	{
		Bonus effect(static_cast<BonusDuration::Type>(1u << bit), BonusType::PRIMARY_SKILL,
			BonusSource::SPELL_EFFECT, 100 + bit, BonusSourceID(SpellID(SpellID::HASTE)),
			BonusSubtypeID(PrimarySkill::ATTACK));
		effect.turnsRemain = 5;
		effects.push_back(effect);
	}
	for(const auto duration : {BonusDuration::Type(0), BonusDuration::Type(BonusDuration::PERMANENT),
		BonusDuration::Type(BonusDuration::PERMANENT | BonusDuration::N_TURNS)})
	{
		Bonus effect(duration, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
			200 + effects.size(), BonusSourceID(SpellID(SpellID::HASTE)), BonusSubtypeID(PrimarySkill::ATTACK));
		effect.turnsRemain = 5;
		effects.push_back(effect);
	}
	// A temporary nonmagical source and an ordinary non-spell modifier remain
	// independently of lifetime. Bind's real permanent source is covered above.
	effects.emplace_back(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, 300, BonusSourceID(SpellID(SpellID::BIND)), BonusSubtypeID(PrimarySkill::ATTACK));
	effects.emplace_back(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 301, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	effects[effects.size() - 2].turnsRemain = 5;
	effects.back().turnsRemain = 5;
	add(effects);
	for(const auto & expected : effects)
		ASSERT_TRUE(target->hasBonus(CSelector([&](const Bonus * bonus)
		{
			return bonus->source == expected.source && bonus->sid == expected.sid
				&& bonus->type == expected.type && bonus->val == expected.val;
		})));
	ASSERT_NO_FATAL_FAILURE(paidDispel());
	for(const auto & expected : effects)
	{
		SCOPED_TRACE(expected.duration);
		EXPECT_EQ(target->hasBonus(CSelector([&](const Bonus * bonus)
		{
			return bonus->source == expected.source && bonus->sid == expected.sid
				&& bonus->type == expected.type && bonus->val == expected.val;
		})), expected.duration == 0 || (expected.duration & BonusDuration::PERMANENT) != 0
			|| expected.source != BonusSource::SPELL_EFFECT || expected.sid == BonusSourceID(SpellID(SpellID::BIND)));
	}
}

TEST(NewHorizonsTemporaryMagicDispelRulesTest, TemporaryLifetimeProxyIncludesActivationFlagsAndRejectsPermanentMixtures)
{
	Bonus effect;
	for(unsigned bit = 1; bit < BonusDuration::Size; ++bit)
	{
		effect.duration = static_cast<BonusDuration::Type>(1u << bit);
		EXPECT_TRUE(scripting::api::BonusProxy::isTemporary(effect));
	}
	for(const auto duration : {BonusDuration::Type(0), BonusDuration::Type(BonusDuration::PERMANENT),
		BonusDuration::Type(BonusDuration::PERMANENT | BonusDuration::N_TURNS)})
	{
		effect.duration = duration;
		EXPECT_FALSE(scripting::api::BonusProxy::isTemporary(effect));
	}
}

TEST(NewHorizonsTemporaryMagicDispelRulesTest, PolicyIsExplicitAndStrictAndCannotCrossTheOldFormatBoundary)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	auto & row = rules["spells"]["core:dispel"];
	row.Struct().erase("temporaryMagicalEffectsOnly");
	EXPECT_FALSE(newHorizonsMagic::dispelRemovesTemporaryMagicalEffectsOnly(rules));
	EXPECT_NO_THROW(newHorizonsMagic::validateTemporaryMagicDispelSerialization(rules, false));
	row["temporaryMagicalEffectsOnly"].Bool() = true;
	EXPECT_TRUE(newHorizonsMagic::dispelRemovesTemporaryMagicalEffectsOnly(rules));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_THROW(newHorizonsMagic::validateTemporaryMagicDispelSerialization(rules, false), std::runtime_error);
	row["temporaryMagicalEffectsOnly"].Bool() = false;
	EXPECT_FALSE(newHorizonsMagic::dispelRemovesTemporaryMagicalEffectsOnly(rules));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_THROW(newHorizonsMagic::validateTemporaryMagicDispelSerialization(rules, false), std::runtime_error);
	row["temporaryMagicalEffectsOnly"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::validateTemporaryMagicDispelSerialization(rules, false), std::runtime_error);
}
