/*
 * NewHorizonsSanctuaryKeeperTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view sanctuarySpellKey = "new-horizons:sanctuary";
constexpr std::string_view lightMagicSkillKey = "new-horizons:lightMagic";
constexpr std::string_view sanctuaryKeeperPerkKey = "new-horizons:lightMagic.sanctuaryKeeper";

SpellID sanctuarySpell()
{
	return SpellID(SpellID::decode(std::string(sanctuarySpellKey)));
}

JsonNode savedV2MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	auto & spells = rules["spells"].Struct();
	for(auto it = spells.begin(); it != spells.end();)
	{
		if(it->second.Struct().contains("variant"))
			it = spells.erase(it);
		else
		{
			it->second.Struct().erase("selectedPlacement");
			it->second.Struct().erase("earthquake");
			it->second.Struct().erase("structures");
			++it;
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class SanctuaryKeeperPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SanctuaryKeeperPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

bool activatePerkInFixture(JsonNode & rules, const std::string_view skillId,
	const std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class NewHorizonsSanctuaryKeeperTest : public HeroCommandFixture
{
protected:
	bool useSavedV2Rules = false;
	CStack * protectedStack = nullptr;
	CStack * movingStack = nullptr;
	CStack * meleeStack = nullptr;
	CStack * abilityStack = nullptr;
	CStack * hostileStack = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(sanctuarySpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activatePerkInFixture(perkRules, lightMagicSkillKey, sanctuaryKeeperPerkKey))
			throw std::runtime_error("Missing Sanctuary Keeper from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, useSavedV2Rules
			? savedV2MagicRules()
			: JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	SecondarySkill lightMagic() const
	{
		const int decoded = SecondarySkill::decode(std::string(lightMagicSkillKey));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptSanctuaryKeeperThroughOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [](const auto & offer)
			{
				return offer.selection.skillId == lightMagicSkillKey
					&& offer.selection.perkId == sanctuaryKeeperPerkKey;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(MasteryLevel::BASIC));
			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(lightMagicSkillKey),
				std::string(sanctuaryKeeperPerkKey)));
			return;
		}

		FAIL() << "Sanctuary Keeper was not available as a Basic New Horizons perk offer";
	}

	void prepare(const bool selectSanctuaryKeeper = true, const bool prepareDispel = false,
		const bool savedV2Rules = false)
	{
		useSavedV2Rules = savedV2Rules;
		startGame();
		attackerSideHero->setSecSkillLevel(lightMagic(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(sanctuarySpell());
		if(prepareDispel)
			attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(selectSanctuaryKeeper)
			acceptSanctuaryKeeperThroughOffer(attackerSideHero);

		startBattle();
		removeDeployedUnits();
		protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
		movingStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 7), 20);
		meleeStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 20);
		abilityStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 8), 20);
		hostileStack = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(7, 5), 100);
		beginCombat();
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(movingStack, nullptr);
		ASSERT_NE(meleeStack, nullptr);
		ASSERT_NE(abilityStack, nullptr);
		ASSERT_NE(hostileStack, nullptr);
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), useSavedV2Rules
			? newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
			: newHorizonsMagic::CURRENT_RULESET_VERSION);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), sanctuarySpell()));
		ASSERT_EQ(battle()->battleGetOwner(protectedStack), PlayerColor(0));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(stack), action);
	}

	bool cast(const CStack * caster, const SpellID spell, const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return act(caster, action);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	int morale(const CStack * stack) const
	{
		return stack->valOfBonuses(BonusType::MORALE);
	}

	TConstBonusListPtr sanctuaryMoraleBonuses(const battle::Unit * stack) const
	{
		return stack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(sanctuarySpell())).And(Selector::type()(BonusType::MORALE)));
	}
};
}

TEST_F(NewHorizonsSanctuaryKeeperTest, BasicOfferAddsTwoMoraleToActualSanctuaryCastAndRepeatedCastDoesNotStack)
{
	prepare();
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(lightMagicSkillKey),
		std::string(sanctuaryKeeperPerkKey)));
	const int moraleBefore = morale(protectedStack);

	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore + 2);
	auto sanctuaryMorale = sanctuaryMoraleBonuses(protectedStack);
	ASSERT_EQ(sanctuaryMorale->size(), 1u);
	EXPECT_EQ(sanctuaryMorale->front()->val, 2);

	advanceRound();
	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore + 2);
	sanctuaryMorale = sanctuaryMoraleBonuses(protectedStack);
	EXPECT_EQ(sanctuaryMorale->size(), 1u);
}

TEST_F(NewHorizonsSanctuaryKeeperTest, WaitAndDefendPreserveSanctuaryAndItsMoraleBonus)
{
	prepare();
	const int protectedMorale = morale(protectedStack);
	const int defendingMorale = morale(movingStack);
	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	ASSERT_EQ(morale(protectedStack), protectedMorale + 2);
	ASSERT_TRUE(act(protectedStack, BattleAction::makeWait(protectedStack)));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), protectedMorale + 2);

	advanceRound();
	ASSERT_TRUE(cast(movingStack, sanctuarySpell(), movingStack));
	ASSERT_EQ(morale(movingStack), defendingMorale + 2);
	ASSERT_TRUE(act(movingStack, BattleAction::makeDefend(movingStack)));
	EXPECT_TRUE(movingStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(movingStack), defendingMorale + 2);
}

TEST_F(NewHorizonsSanctuaryKeeperTest, MovementAttackAndOffensiveCreatureSpellRemoveTheBonus)
{
	prepare();
	const int movingMorale = morale(movingStack);
	const int meleeMorale = morale(meleeStack);
	const int abilityMorale = morale(abilityStack);
	ASSERT_TRUE(cast(movingStack, sanctuarySpell(), movingStack));
	ASSERT_EQ(morale(movingStack), movingMorale + 2);
	ASSERT_TRUE(act(movingStack, BattleAction::makeMove(movingStack, BattleHex(5, 7))));
	EXPECT_FALSE(movingStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(movingStack), movingMorale);

	advanceRound();
	ASSERT_TRUE(cast(meleeStack, sanctuarySpell(), meleeStack));
	ASSERT_EQ(morale(meleeStack), meleeMorale + 2);
	BattleAction melee = BattleAction::makeMeleeAttack(meleeStack, hostileStack->getPosition(),
		meleeStack->getPosition());
	ASSERT_TRUE(act(meleeStack, melee));
	EXPECT_FALSE(meleeStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(meleeStack), meleeMorale);

	advanceRound();
	ASSERT_TRUE(cast(abilityStack, sanctuarySpell(), abilityStack));
	ASSERT_EQ(morale(abilityStack), abilityMorale + 2);
	abilityStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	abilityStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	ASSERT_EQ(abilityStack->casts.total(), 1)
		<< "The fixture's explicit CASTS bonus refreshes the unit's cached cast allowance";
	ASSERT_EQ(morale(abilityStack), abilityMorale + 2);
	spells::Target enemyTarget{spells::Destination(hostileStack)};
	ASSERT_TRUE(act(abilityStack,
		BattleAction::makeCreatureSpellcast(abilityStack, enemyTarget, SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(abilityStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(abilityStack), abilityMorale);
}

TEST_F(NewHorizonsSanctuaryKeeperTest, DispelRemovesTheSanctifiedMarkerAndItsMoraleBonus)
{
	prepare(true, true);
	const int moraleBefore = morale(protectedStack);
	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	ASSERT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	ASSERT_EQ(morale(protectedStack), moraleBefore + 2);

	advanceRound();
	ASSERT_TRUE(cast(protectedStack, SpellID::DISPEL, protectedStack));
	EXPECT_FALSE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore);
	EXPECT_TRUE(sanctuaryMoraleBonuses(protectedStack)->empty());
}

TEST_F(NewHorizonsSanctuaryKeeperTest, DroppingAndRestoringLightMagicDoesNotReactivateStaleSanctuaryMorale)
{
	prepare();
	const int moraleBefore = morale(protectedStack);
	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	ASSERT_EQ(morale(protectedStack), moraleBefore + 2);

	attackerSideHero->setSecSkillLevel(lightMagic(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(lightMagicSkillKey),
		std::string(sanctuaryKeeperPerkKey)));
	EXPECT_EQ(morale(protectedStack), moraleBefore + 2)
		<< "A previously cast snapshot persists while its Sanctuary marker remains active";
	advanceRound();
	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore)
		<< "A non-perk recast cleans the previous Sanctuary Keeper bonus family";

	attackerSideHero->setSecSkillLevel(lightMagic(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(std::string(lightMagicSkillKey),
		std::string(sanctuaryKeeperPerkKey)));
	EXPECT_EQ(morale(protectedStack), moraleBefore);
	EXPECT_TRUE(sanctuaryMoraleBonuses(protectedStack)->empty());
}

TEST_F(NewHorizonsSanctuaryKeeperTest, UnselectedPerkDoesNotAddMoraleToSanctuary)
{
	prepare(false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(std::string(lightMagicSkillKey),
		std::string(sanctuaryKeeperPerkKey)));
	const int moraleBefore = morale(protectedStack);

	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore);
	EXPECT_TRUE(sanctuaryMoraleBonuses(protectedStack)->empty());
}

TEST_F(NewHorizonsSanctuaryKeeperTest, SavedV2SanctuaryRowDoesNotEnableTheKeeperBonus)
{
	prepare(true, false, true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(lightMagicSkillKey),
		std::string(sanctuaryKeeperPerkKey)));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), sanctuarySpell()));
	const int moraleBefore = morale(protectedStack);

	ASSERT_TRUE(cast(protectedStack, sanctuarySpell(), protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), moraleBefore);
	EXPECT_TRUE(sanctuaryMoraleBonuses(protectedStack)->empty());
}

TEST_F(NewHorizonsSanctuaryKeeperTest, DetachedCastEvalRefreshesMaterializedForecastWithoutStackingOrChangingLiveState)
{
	prepare();
	const int liveMoraleBefore = morale(protectedStack);
	const auto liveManaBefore = attackerSideHero->getManaAvailable();
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	SanctuaryKeeperPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const Bonus materializationBonus(BonusDuration::ONE_BATTLE, BonusType::MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	projected.addUnitBonus(protectedStack->unitId(), {materializationBonus});
	const auto * projectedTarget = projected.battleGetUnitByID(protectedStack->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	ASSERT_FALSE(projectedTarget->hasBonusOfType(BonusType::SANCTIFIED));
	const int projectedMoraleBefore = projectedTarget->valOfBonuses(BonusType::MORALE);

	const auto * spell = sanctuarySpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast castEvent(&projected, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&castEvent);
	spells::Target aim{spells::Destination(projectedTarget)};
	mechanics->castEval(projected.getServerCallback(), aim);

	const auto * afterFirstForecast = projected.battleGetUnitByID(protectedStack->unitId());
	ASSERT_NE(afterFirstForecast, nullptr);
	EXPECT_TRUE(afterFirstForecast->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(afterFirstForecast->valOfBonuses(BonusType::MORALE), projectedMoraleBefore + 2);
	ASSERT_EQ(sanctuaryMoraleBonuses(afterFirstForecast)->size(), 1u);

	const auto * targetForRepeatedForecast = projected.battleGetUnitByID(protectedStack->unitId());
	ASSERT_NE(targetForRepeatedForecast, nullptr);
	spells::Target repeatedAim{spells::Destination(targetForRepeatedForecast)};
	mechanics->castEval(projected.getServerCallback(), repeatedAim);
	const auto * afterRepeatedForecast = projected.battleGetUnitByID(protectedStack->unitId());
	ASSERT_NE(afterRepeatedForecast, nullptr);
	EXPECT_EQ(afterRepeatedForecast->valOfBonuses(BonusType::MORALE), projectedMoraleBefore + 2);
	EXPECT_EQ(sanctuaryMoraleBonuses(afterRepeatedForecast)->size(), 1u);
	EXPECT_FALSE(protectedStack->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_EQ(morale(protectedStack), liveMoraleBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveManaBefore);
}
