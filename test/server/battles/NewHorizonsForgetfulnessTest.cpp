/*
 * NewHorizonsForgetfulnessTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view CHAOS_MAGIC_SKILL = "new-horizons:chaosMagic";
constexpr std::string_view MINDBREAKER_PERK = "new-horizons:chaosMagic.mindbreaker";

SpellID forgetfulnessSpell()
{
	return SpellID(SpellID::FORGETFULNESS);
}

class NewHorizonsForgetfulnessTest : public HeroCommandFixture
{
protected:
	CStack * casterStack = nullptr;
	CStack * adjacentEnemy = nullptr;
	CStack * afflicted = nullptr;
	int32_t casterSpellPower = 160;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill chaosMagic() const
	{
		const int decoded = SecondarySkill::decode(std::string(CHAOS_MAGIC_SKILL));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void configureHero(CGHeroInstance * hero, const bool caster)
	{
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->removeAllSpells();
		hero->addSpellToSpellbook(SpellID::DISPEL);
		if(caster)
			hero->addSpellToSpellbook(forgetfulnessSpell());
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, caster ? casterSpellPower : 0,
			ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 1000);
	}

	void acceptMindbreakerThroughLegalOffers(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		const auto selectOffered = [&](const std::string_view requiredPerk, const int requiredRank)
		{
			for(uint64_t seed = 0; seed < 4096; ++seed)
			{
				const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
				const auto selected = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
				{
					return offer.selection.skillId == CHAOS_MAGIC_SKILL
						&& (requiredPerk.empty() || offer.selection.perkId == requiredPerk)
						&& offer.requiredRank == requiredRank;
				});
				if(selected == offers.end())
					continue;
				const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				return true;
			}
			return false;
		};

		const auto chaos = chaosMagic();
		hero->setSecSkillLevel(chaos, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(selectOffered({}, MasteryLevel::BASIC))
			<< "A real Basic Chaos Magic offer is required before the Advanced choice";
		hero->setSecSkillLevel(chaos, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(selectOffered(MINDBREAKER_PERK, MasteryLevel::ADVANCED))
			<< "Mindbreaker must be an active, normally offered Advanced perk";
		ASSERT_TRUE(hero->hasActivePerk(std::string(CHAOS_MAGIC_SKILL), std::string(MINDBREAKER_PERK)));
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

	void prepare(const std::string & targetCreature, const bool mindbreaker = false,
		const bool addAdjacentEnemy = true)
	{
		startGame();
		configureHero(attackerSideHero, true);
		configureHero(defenderSideHero, false);
		attackerSideHero->setSecSkillLevel(chaosMagic(),
			mindbreaker ? MasteryLevel::ADVANCED : MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		if(mindbreaker)
			acceptMindbreakerThroughLegalOffers(attackerSideHero);
		startBattle();
		removeDeployedUnits();

		casterStack = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000);
		if(addAdjacentEnemy)
			adjacentEnemy = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(11, 5), 1000);
		afflicted = addStack(BattleSide::DEFENDER, creatureByName(targetCreature), BattleHex(12, 5), 1000);
		ASSERT_NE(casterStack, nullptr);
		ASSERT_NE(afflicted, nullptr);
		if(addAdjacentEnemy)
			ASSERT_NE(adjacentEnemy, nullptr);
		beginCombat();
	}

	bool issue(const battle::Unit * actor, const BattleAction & action)
	{
		if(!actor)
			return false;
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(actor->unitSide()), action);
	}

	bool activate(const battle::Unit * wanted)
	{
		const auto maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t attempt = 0; attempt < maximumActions; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == wanted->unitId())
				return true;
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool castForgetfulness()
	{
		if(!activate(casterStack))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = forgetfulnessSpell();
		action.aimToUnit(afflicted);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::shared_ptr<const Bonus> suppressionMarker(const battle::Unit * unit) const
	{
		const auto markers = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(forgetfulnessSpell())).And(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION)));
		return !markers || markers->empty() ? std::shared_ptr<const Bonus>{} : markers->front();
	}

	bool castDispel()
	{
		if(!activate(afflicted))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID::DISPEL;
		action.aimToUnit(afflicted);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action);
	}

	bool castCounterstrike()
	{
		if(!activate(afflicted))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID(SpellID::COUNTERSTRIKE);
		action.aimToUnit(afflicted);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action);
	}

	void expectRealSuppressionMarker(const int expectedLevel, const int expectedTurns = 3)
	{
		const auto marker = suppressionMarker(afflicted);
		ASSERT_NE(marker, nullptr);
		EXPECT_EQ(marker->type, BonusType::CREATURE_ABILITY_SUPPRESSION);
		EXPECT_EQ(marker->source, BonusSource::SPELL_EFFECT);
		EXPECT_EQ(marker->sid.as<SpellID>(), forgetfulnessSpell());
		EXPECT_EQ(marker->duration, BonusDuration::N_TURNS);
		EXPECT_EQ(marker->turnsRemain, expectedTurns);
		EXPECT_EQ(marker->val, expectedLevel);
		EXPECT_EQ(marker->valType, BonusValueType::INDEPENDENT_MAX);
		EXPECT_EQ(marker->statusTags, (std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF}));
	}
};

class ForgetfulnessPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ForgetfulnessPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

Bonus makeSuppressionMarker(const int32_t level, const int32_t turns)
{
	Bonus result(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, level, BonusSourceID(forgetfulnessSpell()),
		BonusSubtypeID(), BonusValueType::INDEPENDENT_MAX);
	result.turnsRemain = turns;
	result.statusTags = {BonusStatusTag::DEBUFF};
	return result;
}
}

TEST(NewHorizonsCreatureAbilitySuppressionClassificationTest, CoversSpecialAttackGeometryAndTriggeredAbilitiesOnly)
{
	for(const auto type : {BonusType::FEROCITY, BonusType::MULTIHEX_UNIT_ATTACK, BonusType::MULTIHEX_ENEMY_ATTACK})
	{
		const Bonus ability(BonusDuration::PERMANENT, type, BonusSource::CREATURE_ABILITY, 1, BonusSourceID());
		EXPECT_TRUE(newHorizonsCreatureAbilitySuppression::isSuppressed(ability, 1));
	}

	const Bonus revenge(BonusDuration::PERMANENT, BonusType::REVENGE, BonusSource::CREATURE_ABILITY, 1,
		BonusSourceID());
	EXPECT_FALSE(newHorizonsCreatureAbilitySuppression::isSuppressed(revenge, 1));
	EXPECT_TRUE(newHorizonsCreatureAbilitySuppression::isSuppressed(revenge, 2));

	const Bonus footprint(BonusDuration::PERMANENT, BonusType::MULTIHEX_ANIMATION,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID());
	EXPECT_FALSE(newHorizonsCreatureAbilitySuppression::isSuppressed(footprint, 1));
	EXPECT_FALSE(newHorizonsCreatureAbilitySuppression::isSuppressed(footprint, 2));
}

TEST_F(NewHorizonsForgetfulnessTest, RealCastSuppressesShooterAndRejectsShotWithoutSpendingTheTurn)
{
	prepare("core:archer", false, false);
	ASSERT_TRUE(afflicted->canShoot());
	EXPECT_TRUE(battle()->battleCanShoot(afflicted, casterStack->getPosition()));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto castCost = battle()->battleGetSpellCost(forgetfulnessSpell().toSpell(), attackerSideHero);

	ASSERT_TRUE(castForgetfulness());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - castCost);
	expectRealSuppressionMarker(1);
	EXPECT_EQ(server.castsOf(forgetfulnessSpell()).size(), 1u);
	EXPECT_FALSE(afflicted->canShoot());
	EXPECT_FALSE(battle()->battleCanShoot(afflicted, casterStack->getPosition()))
		<< "The shot query is intentionally warmed before the real cast";

	ASSERT_TRUE(activate(afflicted));
	const auto startedActionsBefore = server.startedActions.size();
	const auto manaAfterCast = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(issue(afflicted, BattleAction::makeShotAttack(afflicted, casterStack)))
		<< "The authoritative action path rejects a forged shot, not only its availability preview";
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaAfterCast);
	EXPECT_NE(suppressionMarker(afflicted), nullptr);
}

TEST_F(NewHorizonsForgetfulnessTest, RealCastSuppressesNonShooterSpellcastingButAllowsWaitDefendAndMelee)
{
	prepare("core:ogreMage");
	ASSERT_FALSE(afflicted->canShoot());
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::SPELLCASTER));
	BattleClientInterfaceData clientData{};
	clientData.creatureSpellsToCast.push_back(SpellID(SpellID::BLOODLUST));
	const auto activeActionsBefore = battle()->getClientActionsForStack(afflicted, clientData);
	EXPECT_TRUE(std::ranges::any_of(activeActionsBefore, [](const PossiblePlayerBattleAction & action)
	{
		return action.spellcast() && action.spell() == SpellID(SpellID::BLOODLUST);
	})) << "Ogre Mage’s actual activated Bloodlust is advertised before Forgetfulness";
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(1);
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::SPELLCASTER));
	const auto activeActionsAfter = battle()->getClientActionsForStack(afflicted, clientData);
	EXPECT_FALSE(std::ranges::any_of(activeActionsAfter, [](const PossiblePlayerBattleAction & action)
	{
		return action.spellcast() && action.spell() == SpellID(SpellID::BLOODLUST);
	})) << "The same creature action disappears from the client options as well as server validation";

	ASSERT_TRUE(activate(afflicted));
	spells::Target self{battle::Destination(afflicted)};
	const auto creatureSpell = BattleAction::makeCreatureSpellcast(
		afflicted, self, SpellID(SpellID::BLOODLUST));
	const auto startedActionsBefore = server.startedActions.size();
	EXPECT_FALSE(issue(afflicted, creatureSpell))
		<< "A non-shooter’s activated creature spell is denied by the server";
	EXPECT_EQ(server.startedActions.size(), startedActionsBefore);

	ASSERT_TRUE(issue(afflicted, BattleAction::makeWait(afflicted)));
	EXPECT_NE(suppressionMarker(afflicted), nullptr)
		<< "Waiting defers the normal action and does not remove Forgetfulness";
	ASSERT_TRUE(activate(afflicted));
	ASSERT_TRUE(issue(afflicted, BattleAction::makeMeleeAttack(
		afflicted, adjacentEnemy->getPosition(), afflicted->getPosition(), false)));
	EXPECT_NE(suppressionMarker(afflicted), nullptr);

	ASSERT_TRUE(activate(afflicted));
	ASSERT_NE(suppressionMarker(afflicted), nullptr)
		<< "Defend must be exercised before the timed spell naturally expires";
	ASSERT_TRUE(issue(afflicted, BattleAction::makeDefend(afflicted)));
}

TEST_F(NewHorizonsForgetfulnessTest, OrdinaryMovementRemainsAvailableWhileTheSuppressionIsActive)
{
	prepare("core:peasant");
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(1);
	ASSERT_TRUE(activate(afflicted));
	const auto originalPosition = afflicted->getPosition();
	ASSERT_TRUE(issue(afflicted, BattleAction::makeMove(afflicted, BattleHex(13, 5))));
	EXPECT_NE(afflicted->getPosition(), originalPosition);
	EXPECT_NE(suppressionMarker(afflicted), nullptr);
}

TEST_F(NewHorizonsForgetfulnessTest, BaseForgetfulnessSuppressesSpecialAttackModes)
{
	prepare("core:hydra", false, false);
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(1);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 1);
	const auto nativeSpecial = afflicted->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::ATTACKS_ALL_ADJACENT), {}, true);
	ASSERT_NE(nativeSpecial, nullptr);
	ASSERT_EQ(nativeSpecial->size(), 1u);
	EXPECT_EQ(nativeSpecial->front()->source, BonusSource::CREATURE_ABILITY);
	EXPECT_EQ(nativeSpecial->front()->sid, BonusSourceID(afflicted->creatureId()));
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT))
		<< "Hydra’s special all-adjacent attack is disabled at base suppression level";
}

TEST_F(NewHorizonsForgetfulnessTest, BaseForgetfulnessLeavesASeparateNativePassiveAbilityEnabled)
{
	prepare("core:medusa");
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(1);
	const auto nativePassive = afflicted->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::NO_MELEE_PENALTY), {}, true);
	ASSERT_NE(nativePassive, nullptr);
	ASSERT_EQ(nativePassive->size(), 1u);
	EXPECT_EQ(nativePassive->front()->source, BonusSource::CREATURE_ABILITY);
	EXPECT_EQ(nativePassive->front()->sid, BonusSourceID(afflicted->creatureId()));
	EXPECT_TRUE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY))
		<< "Base suppression disables special/triggered abilities but not Mindbreaker’s passive tier";
}

TEST_F(NewHorizonsForgetfulnessTest, SuppressionFilterPreservesSameBonusTypeFromNonNativeSources)
{
	prepare("core:medusa");
	Bonus native(BonusDuration::PERMANENT, BonusType::NO_MELEE_PENALTY,
		BonusSource::CREATURE_ABILITY, 0, BonusSourceID(afflicted->creatureId()));
	Bonus otherCreature(BonusDuration::PERMANENT, BonusType::NO_MELEE_PENALTY,
		BonusSource::CREATURE_ABILITY, 0, BonusSourceID(creatureByName("core:ogre")));
	Bonus spellEffect(BonusDuration::N_TURNS, BonusType::NO_MELEE_PENALTY,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::HASTE)));
	auto sourceBonuses = std::make_shared<BonusList>();
	sourceBonuses->push_back(std::make_shared<Bonus>(native));
	sourceBonuses->push_back(std::make_shared<Bonus>(otherCreature));
	sourceBonuses->push_back(std::make_shared<Bonus>(spellEffect));

	const auto filtered = newHorizonsCreatureAbilitySuppression::filterBonuses(*afflicted,
		sourceBonuses, 2, false);
	ASSERT_NE(filtered, nullptr);
	ASSERT_EQ(filtered->size(), 2u);
	EXPECT_EQ((*filtered)[0]->source, BonusSource::CREATURE_ABILITY);
	EXPECT_EQ((*filtered)[0]->sid, otherCreature.sid)
		<< "An ability inherited from another creature is not this unit’s intrinsic ability";
	EXPECT_EQ((*filtered)[1]->source, BonusSource::SPELL_EFFECT)
		<< "Spell-sourced bonuses are not suppressed just because their type is normally an innate trait";
}

TEST_F(NewHorizonsForgetfulnessTest, DetachedChildCanRestoreAnAbilityWithoutMutatingTheLiveSuppressedStack)
{
	prepare("core:hydra", false, false);
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	ASSERT_TRUE(castForgetfulness());
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 1);
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	ForgetfulnessPredictionEnvironment environment(gameState());
	auto projected = std::make_shared<HypotheticBattle>(&environment, callback);
	const auto * projectedHydra = projected->battleGetUnitByID(afflicted->unitId());
	ASSERT_NE(projectedHydra, nullptr);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*projectedHydra), 1);
	EXPECT_FALSE(projectedHydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));

	HypotheticBattle child(&environment, projected);
	const auto childHydra = child.getForUpdate(afflicted->unitId());
	ASSERT_NE(childHydra, nullptr);
	childHydra->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(forgetfulnessSpell())));
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*childHydra), 0);
	EXPECT_TRUE(childHydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	EXPECT_FALSE(projectedHydra->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT));
}

TEST_F(NewHorizonsForgetfulnessTest, MindbreakerRequiresAnAdvancedOfferAndSuppressesPassiveOffense)
{
	prepare("core:medusa", true);
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(2);
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*afflicted), 2);
	const auto nativePassive = afflicted->getBonusesBeforeCreatureAbilitySuppression(
		Selector::type()(BonusType::NO_MELEE_PENALTY), {}, true);
	ASSERT_NE(nativePassive, nullptr);
	ASSERT_EQ(nativePassive->size(), 1u);
	EXPECT_EQ(nativePassive->front()->sid, BonusSourceID(afflicted->creatureId()));
	EXPECT_FALSE(afflicted->hasBonusOfType(BonusType::NO_MELEE_PENALTY))
		<< "The actual Mindbreaker offer sets the stronger suppression level";
}

TEST_F(NewHorizonsForgetfulnessTest, MindbreakerSuppressesNativeUnlimitedRetaliationsButKeepsSpellGrantedRetaliation)
{
	prepare("core:royalGriffin", true, false);
	ASSERT_TRUE(afflicted->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	defenderSideHero->addSpellToSpellbook(SpellID(SpellID::COUNTERSTRIKE));

	ASSERT_TRUE(castCounterstrike());
	const auto spellRetaliation = afflicted->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::COUNTERSTRIKE))).And(Selector::type()(BonusType::ADDITIONAL_RETALIATION)));
	ASSERT_NE(spellRetaliation, nullptr);
	ASSERT_EQ(spellRetaliation->size(), 1u);
	EXPECT_EQ(spellRetaliation->front()->val, 1);
	EXPECT_FALSE(afflicted->counterAttacks.isLimited());
	EXPECT_EQ(afflicted->counterAttacks.total(), 2)
		<< "The native unlimited ability and external Counterstrike are both warmed before suppression";

	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(2);
	EXPECT_TRUE(afflicted->counterAttacks.isLimited())
		<< "Mindbreaker suppresses the Griffin’s native unlimited-retaliation ability";
	EXPECT_EQ(afflicted->counterAttacks.total(), 2)
		<< "The ordinary retaliation plus the unrelated spell-granted extra remain available";
	EXPECT_TRUE(afflicted->counterAttacks.canUse());

	endRound();
	ASSERT_TRUE(castDispel());
	EXPECT_EQ(suppressionMarker(afflicted), nullptr);
	EXPECT_TRUE(afflicted->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS))
		<< "Removing the temporary marker restores the intrinsic ability";
	EXPECT_FALSE(afflicted->counterAttacks.isLimited());
}

TEST_F(NewHorizonsForgetfulnessTest, CanonicalDurationHasOneRoundAtZeroSpellPower)
{
	casterSpellPower = 0;
	prepare("core:peasant", false, false);
	ASSERT_TRUE(castForgetfulness());
	expectRealSuppressionMarker(1, 1);
}

TEST_F(NewHorizonsForgetfulnessTest, ExpertSchoolCoefficientCrossesForgetfulnessDurationThreshold)
{
	casterSpellPower = 60;
	prepare("core:peasant", false, false);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(chaosMagic()), MasteryLevel::EXPERT);
	ASSERT_TRUE(castForgetfulness());
	// Expert Chaos Magic applies its saved 145% coefficient: floor(60 * 1.45 / 80)
	// adds one round even though raw Spell Power is below the 80-point threshold.
	expectRealSuppressionMarker(1, 2);
}

TEST_F(NewHorizonsForgetfulnessTest, DispelAndNaturalExpiryRestoreTheWarmShooterCapability)
{
	prepare("core:archer");
	ASSERT_TRUE(afflicted->canShoot());
	ASSERT_TRUE(castForgetfulness());
	EXPECT_FALSE(afflicted->canShoot());
	ASSERT_TRUE(castDispel());
	EXPECT_EQ(suppressionMarker(afflicted), nullptr);
	EXPECT_TRUE(afflicted->canShoot());

	// A second real cast is made after the next round, then allowed to expire through
	// the ordinary BattleNextRound lifecycle rather than manually removing its marker.
	endRound();
	ASSERT_TRUE(castForgetfulness());
	ASSERT_FALSE(afflicted->canShoot());
	const auto startingRound = battle()->battleGetRound();
	for(int round = 0; round < 5 && suppressionMarker(afflicted); ++round)
		endRound();
	EXPECT_EQ(suppressionMarker(afflicted), nullptr);
	EXPECT_TRUE(afflicted->canShoot());
	EXPECT_GT(battle()->battleGetRound(), startingRound);
}

TEST(NewHorizonsForgetfulnessSerializationTest, MarkerRoundTripsOnCurrentWireAndRejectsLossyOlderWriters)
{
	const Bonus marker = makeSuppressionMarker(2, 3);
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & marker);
	Bonus decoded;
	ASSERT_NO_THROW(current.iser & decoded);
	EXPECT_EQ(decoded.type, BonusType::CREATURE_ABILITY_SUPPRESSION);
	EXPECT_EQ(decoded.source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(decoded.sid.as<SpellID>(), forgetfulnessSpell());
	EXPECT_EQ(decoded.val, 2);
	EXPECT_EQ(decoded.turnsRemain, 3);
	EXPECT_EQ(decoded.statusTags, (std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF}));

	CMemorySerializer oldBonusWriter;
	oldBonusWriter.oser.version = ESerializationVersion::BONUS_STATUS_TAGS;
	EXPECT_THROW(oldBonusWriter.oser & marker, std::runtime_error);
	EXPECT_TRUE(oldBonusWriter.extractBuffer().empty());

	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(1u, std::vector<Bonus>{marker});
	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = ESerializationVersion::BONUS_STATUS_TAGS;
	EXPECT_THROW(oldPacketWriter.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty());
}
