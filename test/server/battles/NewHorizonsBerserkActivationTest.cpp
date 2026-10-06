/*
 * NewHorizonsBerserkActivationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/battle/NewHorizonsBerserk.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <vstd/RNG.h>

namespace
{
JsonNode magicRulesForVersion(const int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
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
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("warcasting");
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [spellId, spell] : rules["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = rules["spells"].Struct().erase(it);
		else
			++it;
	}
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	newHorizonsMagic::validateRules(rules);
	return rules;
}
}

class NewHorizonsBerserkActivationTest : public HeroCommandFixture
{
protected:
	static constexpr auto CHAOS_MAGIC_SKILL = "new-horizons:chaosMagic";
	static constexpr auto FRENZIED_CURSE_PERK = "new-horizons:chaosMagic.frenziedCurse";

	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * berserker = nullptr;
	CStack * casterStack = nullptr;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesForVersion(magicVersion));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void prepare(const BattleHex & position)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		berserker = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), position, 10);
		ASSERT_NE(berserker, nullptr);
		berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BERSERK))));
	}

	void selectFrenziedCurse(CGHeroInstance * hero)
	{
		const int chaosMagicIndex = SecondarySkill::decode(CHAOS_MAGIC_SKILL);
		ASSERT_GE(chaosMagicIndex, 0);
		const SecondarySkill chaosMagic(chaosMagicIndex);
		hero->setSecSkillLevel(chaosMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == CHAOS_MAGIC_SKILL
					&& candidate.selection.perkId == FRENZIED_CURSE_PERK
					&& candidate.requiredRank == MasteryLevel::BASIC;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
			return;
		}

		FAIL() << "Frenzied Curse never appeared in a legal Basic Chaos Magic perk offer";
	}

	void markSavedFrenziedCurseInactive(CGHeroInstance * hero)
	{
		auto savedState = hero->getPerkState();
		ASSERT_TRUE(savedState.hasSelection(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
		auto & perks = savedState.rules["skills"][CHAOS_MAGIC_SKILL]["perks"].Vector();
		const auto selected = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
		{
			return perk["id"].String() == FRENZIED_CURSE_PERK;
		});
		ASSERT_NE(selected, perks.end());
		(*selected)["effect"]["status"].String() = "planned";
		savedState.validate();
		const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()) = std::move(savedState);
		EXPECT_TRUE(hero->getPerkState().hasSelection(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
		EXPECT_FALSE(hero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	}

	void prepareRealCast(bool perkOnOriginalCaster, bool perkOnTargetOwner, bool inactiveOriginalCaster,
		const BattleHex & casterPosition, const BattleHex & berserkerPosition,
		const std::string & berserkerCreature = "core:archer")
	{
		startGame();
		if(perkOnOriginalCaster || inactiveOriginalCaster)
			selectFrenziedCurse(attackerSideHero);
		if(perkOnTargetOwner)
			selectFrenziedCurse(defenderSideHero);
		if(inactiveOriginalCaster)
			markSavedFrenziedCurseInactive(attackerSideHero);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::BERSERK);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);

		casterStack = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), casterPosition, 1000);
		berserker = addStack(BattleSide::DEFENDER, creatureByName(berserkerCreature), berserkerPosition, 1000);
		ASSERT_NE(casterStack, nullptr);
		ASSERT_NE(berserker, nullptr);
		casterStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000, BonusSourceID()));

		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), casterStack);
		ASSERT_EQ(battle()->battleGetOwner(casterStack), PlayerColor(0));

		BattleAction cast;
		cast.actionType = EActionType::HERO_SPELL;
		cast.side = BattleSide::ATTACKER;
		cast.spell = SpellID::BERSERK;
		if(newHorizonsMagic::berserkUsesSingleCreatureTarget(battle()->getMagicRules()))
			cast.aimToUnit(berserker);
		else
			cast.aimToHex(berserker->getPosition());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));
		ASSERT_TRUE(berserker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
		ASSERT_EQ(server.castsOf(SpellID(SpellID::BERSERK)).size(), 1u);
	}

	void finishCasterActivation()
	{
		ASSERT_EQ(battle()->battleActiveUnit(), casterStack);
		const auto defend = BattleAction::makeDefend(casterStack);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), defend));
	}

	const StartAction * forcedActionForBerserker() const
	{
		const auto action = std::ranges::find_if(server.startedActions, [this](const StartAction & started)
		{
			return started.ba.stackNumber == berserker->unitId();
		});
		return action == server.startedActions.end() ? nullptr : &*action;
	}

	bool hasFrenziedCurseSpeedBonus() const
	{
		const auto bonuses = berserker->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
		return bonuses && std::ranges::any_of(*bonuses, [](const auto & bonus)
		{
			return bonus && newHorizonsBerserk::isFrenziedCurseSpeedBonus(bonus.get());
		});
	}

	bool hasBerserkSpellMarker() const
	{
		return berserker && berserker->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::BERSERK))).And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE)));
	}

	bool hasForcedAttackMarker(const BonusSource source, const BonusSourceID & sourceId) const
	{
		return berserker && berserker->hasBonus(Selector::source(source, sourceId)
			.And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE)));
	}

	void addOtherForcedAttackMarkers()
	{
		berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::CREATURE_ABILITY, 0, BonusSourceID()));
		berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0,
			BonusSourceID(SpellID(SpellID::BLIND))));
	}

	void expectLegacyForcedWalkKeepsUntilOwnAttack(const int version)
	{
		magicVersion = version;
		ASSERT_NO_FATAL_FAILURE(prepareRealCast(false, false, false,
			BattleHex(4, 5), BattleHex(12, 5), "core:pikeman"));
		ASSERT_TRUE(hasBerserkSpellMarker());
		EXPECT_TRUE(newHorizonsBerserk::completedForcedActivationBonuses(*battle(), berserker).empty());
		const auto candidates = battle()->getBerserkForcedActions(berserker);
		ASSERT_EQ(candidates.size(), 1u);
		EXPECT_EQ(candidates.front().type, EActionType::WALK);

		ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
		const auto * forced = forcedActionForBerserker();
		ASSERT_NE(forced, nullptr);
		EXPECT_EQ(forced->ba.actionType, EActionType::WALK);
		EXPECT_TRUE(hasBerserkSpellMarker())
			<< "Legacy UNTIL_OWN_ATTACK behavior remains unchanged by the saved-v3 expiry helper";
	}
};

TEST_F(NewHorizonsBerserkActivationTest, AuthoritativeShooterUsesSeededMeleeTieRatherThanShooting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(8, 5)));
	addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(7, 5), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(9, 5), 100);
	const auto candidates = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(candidates.size(), 2u);
	for(const auto & candidate : candidates)
		ASSERT_EQ(candidate.type, EActionType::WALK_AND_ATTACK);

	CRandomGenerator expectedRandom(BattleTestFixture::seed);
	const auto expectedTarget = RandomGeneratorUtil::nextItem(candidates, expectedRandom)->target->unitId();
	gameHandler->randomizer->setSeed(BattleTestFixture::seed);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto started = std::ranges::find_if(server.startedActions, [&](const StartAction & pack)
	{
		return pack.ba.stackNumber == berserker->unitId();
	});
	ASSERT_NE(started, server.startedActions.end());
	EXPECT_EQ(started->ba.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(started->ba.side, BattleSide::DEFENDER);
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_FALSE(server.attacks.front().bsa.empty());
	EXPECT_EQ(server.attacks.front().bsa.front().stackAttacked, expectedTarget);
}

TEST_F(NewHorizonsBerserkActivationTest, AuthoritativeDefenderWalkUsesItsOwnSideAndSharedDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(12, 5)));
	addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(2, 5), 100);
	const auto candidates = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(candidates.size(), 1u);
	ASSERT_EQ(candidates.front().type, EActionType::WALK);
	const auto expectedPosition = candidates.front().position;
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto started = std::ranges::find_if(server.startedActions, [&](const StartAction & pack)
	{
		return pack.ba.stackNumber == berserker->unitId();
	});
	ASSERT_NE(started, server.startedActions.end());
	EXPECT_EQ(started->ba.actionType, EActionType::WALK);
	EXPECT_EQ(started->ba.side, BattleSide::DEFENDER);
	EXPECT_EQ(berserker->getPosition(), expectedPosition);
}

TEST_F(NewHorizonsBerserkActivationTest, OriginalCastersSelectedCurseExpandsARealCastIntoWalkAndAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(true, false, false, BattleHex(4, 5), BattleHex(12, 5)));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	EXPECT_FALSE(defenderSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));

	const auto baseMovement = berserker->getMovementRange();
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
	EXPECT_EQ(berserker->getMovementRange(), baseMovement)
		<< "The captured perk must not change Speed before the forced activation";
	const auto generatedBonus = newHorizonsBerserk::forcedActivationSpeedBonus(*battle(), berserker);
	ASSERT_TRUE(generatedBonus);
	EXPECT_EQ(generatedBonus->val, 2);
	EXPECT_TRUE(newHorizonsBerserk::isFrenziedCurseSpeedBonus(&*generatedBonus));

	const auto baseline = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(baseline.size(), 1u);
	ASSERT_EQ(baseline.front().type, EActionType::WALK)
		<< "Without the temporary +2, this geometry is outside the archer's melee reach";

	const auto simulatedBonus = std::make_shared<Bonus>(*generatedBonus);
	berserker->addNewBonus(simulatedBonus);
	const auto boostedCandidates = battle()->getBerserkForcedActions(berserker);
	const auto boostedMovement = berserker->getMovementRange();
	berserker->removeBonus(simulatedBonus);
	EXPECT_EQ(boostedMovement, baseMovement + generatedBonus->val);
	ASSERT_EQ(boostedCandidates.size(), 1u);
	EXPECT_EQ(boostedCandidates.front().type, EActionType::WALK_AND_ATTACK)
		<< "The shared forced-action selector recognizes the exact +2 boundary before activation";
	EXPECT_EQ(berserker->getMovementRange(), baseMovement);
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());

	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	EXPECT_EQ(forced->ba.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(forced->ba.side, BattleSide::DEFENDER);
	EXPECT_FALSE(hasBerserkSpellMarker())
		<< "A real forced melee continues to consume the ordinary UNTIL_OWN_ATTACK spell marker";
	const auto forcedAttack = std::ranges::find_if(server.attacks, [this](const auto & attack)
	{
		return attack.stackAttacking == berserker->unitId();
	});
	ASSERT_NE(forcedAttack, server.attacks.end());
	ASSERT_FALSE(forcedAttack->bsa.empty());
	EXPECT_EQ(forcedAttack->bsa.front().stackAttacked, casterStack->unitId());
	EXPECT_EQ(berserker->getMovementRange(), baseMovement)
		<< "The scoped bonus is removed after the accepted melee action";
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
}

TEST_F(NewHorizonsBerserkActivationTest, CurseBelongsToTheOriginalCasterNotTheTargetOwner)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(false, true, false, BattleHex(4, 5), BattleHex(12, 5)));
	EXPECT_TRUE(defenderSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	EXPECT_FALSE(newHorizonsBerserk::forcedActivationSpeedBonus(*battle(), berserker));

	const auto baseMovement = berserker->getMovementRange();
	const auto baseline = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(baseline.size(), 1u);
	EXPECT_EQ(baseline.front().type, EActionType::WALK);
	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	EXPECT_EQ(forced->ba.actionType, EActionType::WALK);
	EXPECT_EQ(berserker->getMovementRange(), baseMovement);
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
}

TEST_F(NewHorizonsBerserkActivationTest, SelectedButInactiveSavedCurseDoesNotAffectMovementOnlyForcedAction)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(true, false, true, BattleHex(2, 5), BattleHex(12, 5)));
	ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));
	EXPECT_FALSE(newHorizonsBerserk::forcedActivationSpeedBonus(*battle(), berserker));

	const BattleHex originalPosition = berserker->getPosition();
	const auto baseMovement = berserker->getMovementRange();
	const auto baseline = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(baseline.size(), 1u);
	EXPECT_EQ(baseline.front().type, EActionType::WALK);
	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	EXPECT_EQ(forced->ba.actionType, EActionType::WALK);
	EXPECT_NE(berserker->getPosition(), originalPosition)
		<< "The movement-only forced Berserk action is accepted";
	EXPECT_EQ(berserker->getMovementRange(), baseMovement)
		<< "Inactive saved rules must never leave a Speed bonus after WALK";
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
}

TEST_F(NewHorizonsBerserkActivationTest, ActiveCurseMovementOnlyWalkUsesAndThenRemovesTemporarySpeed)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(true, false, false, BattleHex(2, 5), BattleHex(12, 5)));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS_MAGIC_SKILL, FRENZIED_CURSE_PERK));

	const BattleHex originalPosition = berserker->getPosition();
	const auto baseMovement = berserker->getMovementRange();
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
	const auto temporaryBonus = newHorizonsBerserk::forcedActivationSpeedBonus(*battle(), berserker);
	ASSERT_TRUE(temporaryBonus);
	ASSERT_EQ(temporaryBonus->val, 2);

	const auto baseline = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(baseline.size(), 1u);
	ASSERT_EQ(baseline.front().type, EActionType::WALK);
	EXPECT_LE(BattleHex::getDistance(originalPosition, baseline.front().position), baseMovement);

	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	ASSERT_EQ(forced->ba.actionType, EActionType::WALK);
	ASSERT_EQ(forced->ba.target.size(), 1u);
	const BattleHex chosenDestination = forced->ba.target.front().hexValue;
	EXPECT_EQ(berserker->getPosition(), chosenDestination);
	const auto actualTravel = BattleHex::getDistance(originalPosition, chosenDestination);
	EXPECT_EQ(actualTravel, baseMovement + temporaryBonus->val)
		<< "The clear straight-line WALK destination uses exactly the temporary +2 Speed";

	EXPECT_EQ(berserker->getMovementRange(), baseMovement)
		<< "The scoped bonus is removed even after a movement-only forced action";
	EXPECT_FALSE(hasFrenziedCurseSpeedBonus());
	EXPECT_TRUE(server.attacks.empty());
}

TEST_F(NewHorizonsBerserkActivationTest, V3AcceptedForcedWalkExpiresOnlyItsBerserkSpellMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(false, false, false, BattleHex(4, 5), BattleHex(12, 5)));
	ASSERT_TRUE(hasBerserkSpellMarker());
	addOtherForcedAttackMarkers();
	ASSERT_TRUE(hasForcedAttackMarker(BonusSource::CREATURE_ABILITY, BonusSourceID()));
	ASSERT_TRUE(hasForcedAttackMarker(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND))));

	const auto completionBonuses = newHorizonsBerserk::completedForcedActivationBonuses(*battle(), berserker);
	ASSERT_EQ(completionBonuses.size(), 1u);
	EXPECT_EQ(completionBonuses.front().source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(completionBonuses.front().sid, BonusSourceID(SpellID(SpellID::BERSERK)));
	EXPECT_EQ(completionBonuses.front().type, BonusType::ATTACKS_NEAREST_CREATURE);
	EXPECT_EQ(completionBonuses.front().duration, BonusDuration::UNTIL_OWN_ATTACK);
	EXPECT_TRUE(hasBerserkSpellMarker()) << "Collecting completion markers must not mutate the live stack";

	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	ASSERT_EQ(forced->ba.actionType, EActionType::WALK);
	EXPECT_FALSE(hasBerserkSpellMarker()) << "The accepted v3 movement-only forced activation completes Berserk";
	EXPECT_TRUE(hasForcedAttackMarker(BonusSource::CREATURE_ABILITY, BonusSourceID()));
	EXPECT_TRUE(hasForcedAttackMarker(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND))))
		<< "A different spell's forced-attack trait is outside the exact Berserk source filter";
}

TEST_F(NewHorizonsBerserkActivationTest, V3AcceptedForcedNoActionExpiresOnlyItsBerserkSpellMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(false, false, false, BattleHex(4, 5), BattleHex(12, 5)));
	addOtherForcedAttackMarkers();
	berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -20, BonusSourceID()));
	ASSERT_TRUE(battle()->getBerserkForcedActions(berserker).empty());

	ASSERT_NO_FATAL_FAILURE(finishCasterActivation());
	const auto * forced = forcedActionForBerserker();
	ASSERT_NE(forced, nullptr);
	EXPECT_EQ(forced->ba.actionType, EActionType::NO_ACTION);
	EXPECT_FALSE(hasBerserkSpellMarker()) << "The server accepted the automatic forced NO_ACTION";
	EXPECT_TRUE(hasForcedAttackMarker(BonusSource::CREATURE_ABILITY, BonusSourceID()));
	EXPECT_TRUE(hasForcedAttackMarker(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLIND))));
}

TEST_F(NewHorizonsBerserkActivationTest, RejectedNonActiveRequestDoesNotConsumeBerserkBeforeForcedActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepareRealCast(false, false, false, BattleHex(4, 5), BattleHex(12, 5)));
	ASSERT_TRUE(hasBerserkSpellMarker());
	ASSERT_EQ(battle()->battleActiveUnit(), casterStack);

	const BattleAction rejected = BattleAction::makeDefend(berserker);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), rejected));
	EXPECT_EQ(forcedActionForBerserker(), nullptr);
	EXPECT_TRUE(hasBerserkSpellMarker()) << "No accepted forced activation occurred";
}

TEST_F(NewHorizonsBerserkActivationTest, V1AcceptedForcedWalkKeepsUntilOwnAttackBehavior)
{
	ASSERT_NO_FATAL_FAILURE(expectLegacyForcedWalkKeepsUntilOwnAttack(newHorizonsMagic::RULESET_VERSION));
}

TEST_F(NewHorizonsBerserkActivationTest, V2AcceptedForcedWalkKeepsUntilOwnAttackBehavior)
{
	ASSERT_NO_FATAL_FAILURE(expectLegacyForcedWalkKeepsUntilOwnAttack(newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION));
}
