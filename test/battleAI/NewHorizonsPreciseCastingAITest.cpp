/*
 * NewHorizonsPreciseCastingAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto spellcraftSkill = "new-horizons:spellcraft";
constexpr auto spellPenetrationPerk = "new-horizons:spellcraft.spellPenetration";
constexpr auto preciseCastingPerk = "new-horizons:spellcraft.preciseCasting";
}

class NewHorizonsPreciseCastingAITest : public HeroCommandFixture
{
protected:
	std::string preciseCastingStatus = "active";
	CStack * center = nullptr;
	CStack * firstCollateral = nullptr;
	CStack * secondCollateral = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);

		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		bool foundPreciseCasting = false;
		for(auto & perk : perkRules["skills"][spellcraftSkill]["perks"].Vector())
			if(perk["id"].String() == preciseCastingPerk)
			{
				perk["effect"]["status"].String() = preciseCastingStatus;
				foundPreciseCasting = true;
			}
		ASSERT_TRUE(foundPreciseCasting);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);

		// Keep this test about spell target utility, not competing Order bonuses.
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		for(auto & commandEntry : commandRules["commands"].Struct())
			for(auto & effectEntry : commandEntry.second["effects"].Struct())
			{
				auto & formula = effectEntry.second;
				formula["base"].Float() = 0;
				formula["attack"].Float() = 0;
				formula["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
	}

	void prepare(bool selectPreciseCasting)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());

		const SpellID fireball(SpellID::FIREBALL);
		ASSERT_NE(fireball, SpellID::NONE);
		const auto spellcraft = SecondarySkill(SecondarySkill::decode(spellcraftSkill));
		const auto havocMagic = SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic"));
		ASSERT_TRUE(spellcraft.hasValue());
		ASSERT_TRUE(havocMagic.hasValue());
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(havocMagic, MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		if(selectPreciseCasting)
		{
			ASSERT_EQ(preciseCastingStatus, "active");
			attackerSideHero->applyPerkSelection({spellcraftSkill, spellPenetrationPerk});
			attackerSideHero->applyPerkSelection({spellcraftSkill, preciseCastingPerk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(spellcraftSkill, preciseCastingPerk));
		}

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(fireball);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		center = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(75), 5000);
		firstCollateral = addStack(BattleSide::DEFENDER,
			creatureByName("core:pikeman"), BattleHex(74), 5000);
		secondCollateral = addStack(BattleSide::DEFENDER,
			creatureByName("core:pikeman"), BattleHex(76), 5000);
		ASSERT_NE(center, nullptr);
		ASSERT_NE(firstCollateral, nullptr);
		ASSERT_NE(secondCollateral, nullptr);
	}

	void expectFriendlyCenterIsAffected()
	{
		const SpellID fireball(SpellID::FIREBALL);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fireball.toSpell());
		auto mechanics = fireball.toSpell()->battleMechanics(&cast);
		ASSERT_NE(mechanics, nullptr);
		const spells::Target target{spells::Destination(center->getPosition())};
		ASSERT_TRUE(mechanics->canBeCastAt(target));
		const auto affected = mechanics->getAffectedStacks(target);
		EXPECT_TRUE(std::any_of(affected.begin(), affected.end(), [this](const CStack * stack)
		{
			return stack->unitId() == center->unitId();
		}));
	}
};

TEST_F(NewHorizonsPreciseCastingAITest, PlannedPerkDoesNotProtectFriendlyFireballCenter)
{
	preciseCastingStatus = "planned";
	prepare(false);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(spellcraftSkill, preciseCastingPerk));
	expectFriendlyCenterIsAffected();
}

TEST_F(NewHorizonsPreciseCastingAITest, ActiveButUnselectedPerkDoesNotProtectFriendlyFireballCenter)
{
	preciseCastingStatus = "active";
	prepare(false);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(spellcraftSkill, preciseCastingPerk));
	expectFriendlyCenterIsAffected();
}

TEST_F(NewHorizonsPreciseCastingAITest, SelectedPerkRemovesFriendlyCenterFromAIAreaCandidates)
{
	preciseCastingStatus = "active";
	prepare(true);
	const SpellID fireball(SpellID::FIREBALL);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fireball.toSpell());
	auto mechanics = fireball.toSpell()->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	const spells::Target friendlyCenter{spells::Destination(center->getPosition())};
	ASSERT_TRUE(mechanics->canBeCastAt(friendlyCenter));
	const auto affectedByFriendlyCenter = mechanics->getAffectedStacks(friendlyCenter);
	EXPECT_FALSE(std::any_of(affectedByFriendlyCenter.begin(), affectedByFriendlyCenter.end(),
		[this](const CStack * stack) { return stack->unitId() == center->unitId(); }));
	EXPECT_TRUE(std::any_of(affectedByFriendlyCenter.begin(), affectedByFriendlyCenter.end(),
		[this](const CStack * stack) { return stack->unitId() == firstCollateral->unitId(); }));
	EXPECT_TRUE(std::any_of(affectedByFriendlyCenter.begin(), affectedByFriendlyCenter.end(),
		[this](const CStack * stack) { return stack->unitId() == secondCollateral->unitId(); }));

	const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	EXPECT_TRUE(std::any_of(viableTargets.begin(), viableTargets.end(), [this](const spells::Target & target)
	{
		return target.size() == 1 && target.front().hexValue == center->getPosition();
	})) << "The AI target evaluator should retain the friendly center when its collateral targets are hostile";
}
