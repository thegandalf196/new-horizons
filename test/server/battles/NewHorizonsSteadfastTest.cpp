/*
 * NewHorizonsSteadfastTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/CBonusSystemNode.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonBonus.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto disciplineSkillId = "new-horizons:discipline";
constexpr auto steadfastPerkId = "new-horizons:discipline.steadfast";
constexpr auto standardBearerPerkId = "new-horizons:discipline.standardBearer";
constexpr auto shieldOfChaosId = "new-horizons:shieldOfChaos";

bool setPerkActive(JsonNode & rules, std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(disciplineSkillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class SteadfastEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit SteadfastEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSteadfastTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate native New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perks, steadfastPerkId) || !setPerkActive(perks, standardBearerPerkId))
			throw std::runtime_error("Missing Steadfast or Standard Bearer from the perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		for(const auto setting : {EGameSettings::COMBAT_GOOD_MORALE_CHANCE, EGameSettings::COMBAT_BAD_MORALE_CHANCE})
		{
			JsonNode chances;
			for(int index = 0; index < 10; ++index)
				chances.Vector().emplace_back(0);
			loaded->overrideGameSetting(setting, std::move(chances));
		}
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [perkId](const auto & offer)
			{
				return offer.selection.skillId == disciplineSkillId && offer.selection.perkId == perkId;
			});
			if(selected == offers.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(disciplineSkillId, std::string(perkId)));
			return;
		}
		FAIL() << "Perk never appeared in a legal offer: " << perkId;
	}

	void selectSteadfast(CGHeroInstance * hero, bool alsoSelectStandardBearer = false)
	{
		const int decoded = SecondarySkill::decode(disciplineSkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill discipline(decoded);
		hero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, steadfastPerkId);
		if(alsoSelectStandardBearer)
		{
			hero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			acceptPerkThroughOffer(hero, standardBearerPerkId);
		}
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!changes.changedStacks.empty())
			gameHandler->sendAndApply(changes);
	}

	void prepareMoraleBattle(BattleSide ownerSide, bool standardBearer = false)
	{
		startGame();
		selectSteadfast(ownerSide == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero, standardBearer);
		startBattle();
		clearStartingUnits();
		target = addStack(ownerSide, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
		const BattleSide enemySide = ownerSide == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		addStack(enemySide, creatureByName("core:peasant"), BattleHex(12, 5), 10);
		if(standardBearer)
			addStack(ownerSide, creatureByName("core:sprite"), BattleHex(8, 5), 10);
		ASSERT_NE(target, nullptr);
		beginCombat();
	}

	std::shared_ptr<Bonus> addMorale(CStack * stack, int value, BonusSource source,
		bool appliedByEnemy = false, std::string stacking = {}, PlayerColor bonusOwner = PlayerColor::CANNOT_DETERMINE)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			source, value, BonusSourceID());
		result->appliedByEnemy = appliedByEnemy;
		result->stacking = std::move(stacking);
		result->bonusOwner = bonusOwner;
		stack->addNewBonus(result);
		return result;
	}

	std::shared_ptr<Bonus> addMarker(CStack * stack, BonusType type, int value = 1)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::OTHER, value, BonusSourceID());
		stack->addNewBonus(result);
		return result;
	}

	void setRawMorale(CStack * stack, int value)
	{
		const auto current = stack->getBonusesOfType(BonusType::MORALE);
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::OTHER, value - (current ? current->totalValue() : 0), BonusSourceID()));
	}

	int minimumMorale() const
	{
		return -static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
	}

	int maximumMorale() const
	{
		return static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size());
	}

	bool cast(BattleSide side, SpellID spell, const CStack * destination)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToUnit(destination);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side), action);
	}

	const Bonus * spellMorale(const CStack * stack, SpellID spell) const
	{
		const auto bonuses = stack->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		if(!bonuses)
			return nullptr;
		for(const auto & bonus : *bonuses)
			if(bonus && bonus->type == BonusType::MORALE)
				return bonus.get();
		return nullptr;
	}
};
}

TEST_F(NewHorizonsSteadfastTest, EnemyMoralePenaltiesAreReducedIndividuallyBeforeTheyStack)
{
	prepareMoraleBattle(BattleSide::ATTACKER);
	setRawMorale(target, 0);

	auto minusThree = addMorale(target, -3, BonusSource::SPELL_EFFECT, true);
	EXPECT_EQ(battle()->battleGetMorale(target), -2);
	target->removeBonus(minusThree);

	auto minusOne = addMorale(target, -1, BonusSource::SPELL_EFFECT, true);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	target->removeBonus(minusOne);

	addMorale(target, -1, BonusSource::SPELL_EFFECT, true);
	addMorale(target, -2, BonusSource::SPELL_EFFECT, true);
	EXPECT_EQ(battle()->battleGetMorale(target), -1)
		<< "Each independent penalty loses one point before the effects are summed";
}

TEST_F(NewHorizonsSteadfastTest, SharedDAGBonusIsClonedOnceUnlessAlwaysStackingKeepsBothPaths)
{
	prepareMoraleBattle(BattleSide::ATTACKER);
	setRawMorale(target, 0);

	CBonusSystemNode firstParent(BonusNodeType::HERO);
	CBonusSystemNode secondParent(BonusNodeType::HERO);
	target->attachTo(firstParent);
	target->attachTo(secondParent);
	auto shared = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::SPELL_EFFECT, -3, BonusSourceID());
	shared->appliedByEnemy = true;
	firstParent.addNewBonus(shared);
	secondParent.addNewBonus(shared);

	const auto sharedInstances = target->getUnstackedBonuses(Selector::type()(BonusType::MORALE));
	ASSERT_NE(sharedInstances, nullptr);
	ASSERT_EQ(std::count(sharedInstances->begin(), sharedInstances->end(), shared), 2);
	EXPECT_EQ(battle()->battleGetMorale(target), -2)
		<< "The same inherited bonus pointer is one non-stacking effect and must be attenuated once";

	firstParent.removeBonus(shared);
	secondParent.removeBonus(shared);
	auto always = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::SPELL_EFFECT, -2, BonusSourceID());
	always->appliedByEnemy = true;
	always->stacking = "ALWAYS";
	firstParent.addNewBonus(always);
	secondParent.addNewBonus(always);
	EXPECT_EQ(battle()->battleGetMorale(target), -2)
		<< "ALWAYS preserves both explicit DAG contributions after each is reduced to -1";
}

TEST_F(NewHorizonsSteadfastTest, FriendlyArmyAndArtifactPenaltiesAreUntouchedAndStackingFollowsAttenuation)
{
	prepareMoraleBattle(BattleSide::ATTACKER);
	setRawMorale(target, 0);

	auto armyPenalty = addMorale(target, -1, BonusSource::ARMY);
	auto artifactPenalty = addMorale(target, -2, BonusSource::ARTIFACT_INSTANCE);
	EXPECT_EQ(battle()->battleGetMorale(target), -3);
	target->removeBonus(armyPenalty);
	target->removeBonus(artifactPenalty);

	addMorale(target, -2, BonusSource::SPELL_EFFECT, true, "same morale effect");
	addMorale(target, -2, BonusSource::ARTIFACT, false, "same morale effect");
	EXPECT_EQ(battle()->battleGetMorale(target), -2)
		<< "The unchanged friendly -2 wins over the hostile -2 after that hostile effect becomes -1";
}

TEST_F(NewHorizonsSteadfastTest, BoneDragonAuraUsesItsPropagatedOwnerAsHostility)
{
	startGame();
	selectSteadfast(attackerSideHero);
	startBattle();
	clearStartingUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	auto * boneDragon = addStack(BattleSide::DEFENDER, creatureByName("core:boneDragon"), BattleHex(12, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(boneDragon, nullptr);
	beginCombat();
	setRawMorale(target, -1);

	const auto moraleBonuses = target->getUnstackedBonuses(Selector::type()(BonusType::MORALE));
	ASSERT_NE(moraleBonuses, nullptr);
	const auto aura = std::find_if(moraleBonuses->begin(), moraleBonuses->end(), [boneDragon](const auto & bonus)
	{
		return bonus->source == BonusSource::CREATURE_ABILITY && bonus->val == -1
			&& bonus->stacking == "Undead Dragons" && bonus->bonusOwner == boneDragon->unitOwner();
	});
	ASSERT_NE(aura, moraleBonuses->end());
	EXPECT_FALSE((*aura)->appliedByEnemy)
		<< "The native aura is dynamic; hostility comes from the owned stack's propagated owner";
	EXPECT_EQ((*aura)->bonusOwner, battle()->sideToPlayer(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->battleGetMorale(target), 0);

	auto unknownOwnerPenalty = addMorale(target, -1, BonusSource::CREATURE_ABILITY, false, "unknown owner control");
	EXPECT_EQ(unknownOwnerPenalty->bonusOwner, PlayerColor::CANNOT_DETERMINE);
	EXPECT_EQ(battle()->battleGetMorale(target), -1)
		<< "The known hostile aura is reduced, while an otherwise similar unknown-owner ability is unchanged";
	target->removeBonus(unknownOwnerPenalty);

	auto friendlyOwnerPenalty = addMorale(target, -1, BonusSource::CREATURE_ABILITY, false,
		"friendly owner control", target->unitOwner());
	EXPECT_EQ(battle()->battleGetMorale(target), -1)
		<< "A known friendly creature ability is unchanged by Steadfast";
}

TEST_F(NewHorizonsSteadfastTest, FriendlyBoneDragonAuraDoesNotReduceFriendlyMorale)
{
	startGame();
	startBattle();
	clearStartingUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	auto * boneDragon = addStack(BattleSide::ATTACKER, creatureByName("core:boneDragon"), BattleHex(10, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(boneDragon, nullptr);
	beginCombat();
	setRawMorale(target, 0);

	const auto moraleBonuses = target->getUnstackedBonuses(Selector::type()(BonusType::MORALE));
	ASSERT_NE(moraleBonuses, nullptr);
	EXPECT_FALSE(std::any_of(moraleBonuses->begin(), moraleBonuses->end(), [](const auto & bonus)
	{
		return bonus->source == BonusSource::CREATURE_ABILITY && bonus->stacking == "Undead Dragons";
	})) << "The Bone Dragon's native aura is limited to the opposite side";
	EXPECT_EQ(battle()->battleGetMorale(target), 0)
		<< "An allied Bone Dragon does not reduce friendly morale";
}

TEST_F(NewHorizonsSteadfastTest, StandardBearerMoraleAndExistingCapsAndImmunityRemainShared)
{
	prepareMoraleBattle(BattleSide::ATTACKER, true);
	setRawMorale(target, -1);
	addMorale(target, -1, BonusSource::SPELL_EFFECT, true);
	EXPECT_EQ(battle()->battleGetMorale(target), 0)
		<< "Steadfast first removes the enemy point, then adjacent Standard Bearer adds one";

	const int minimumMoraleCap = minimumMorale();
	const int maximumMoraleCap = maximumMorale();
	setRawMorale(target, -30);
	EXPECT_EQ(battle()->battleGetMorale(target), minimumMoraleCap) << "The negative Morale cap is preserved";
	const auto minimum = addMarker(target, BonusType::MINIMUM_MORALE, 0);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	const auto immunity = addMarker(target, BonusType::NO_MORALE);
	EXPECT_EQ(battle()->battleGetMorale(target), 0);
	const auto maximum = addMarker(target, BonusType::MAX_MORALE);
	EXPECT_EQ(battle()->battleGetMorale(target), maximumMoraleCap)
		<< "MAX_MORALE retains priority over immunity, as in the common Morale calculation";

	target->removeBonus(maximum);
	target->removeBonus(immunity);
	target->removeBonus(minimum);
	setRawMorale(target, 30);
	EXPECT_EQ(battle()->battleGetMorale(target), maximumMoraleCap) << "The positive Morale cap is preserved";
}

TEST_F(NewHorizonsSteadfastTest, HostileSorrowCastIsStampedAndReducesTheActualEffect)
{
	startGame();
	selectSteadfast(defenderSideHero);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::SORROW);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 140, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	startBattle();
	clearStartingUnits();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(target, nullptr);
	beginCombat();
	setRawMorale(target, 0);

	ASSERT_TRUE(cast(BattleSide::ATTACKER, SpellID::SORROW, target));
	const auto * applied = spellMorale(target, SpellID::SORROW);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -3);
	EXPECT_TRUE(applied->appliedByEnemy);
	EXPECT_EQ(battle()->battleGetMorale(target), -2);
}

TEST_F(NewHorizonsSteadfastTest, FriendlyShieldOfChaosPenaltyIsNotAttenuated)
{
	startGame();
	selectSteadfast(attackerSideHero);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const auto spell = SpellID(SpellID::decode(shieldOfChaosId));
	ASSERT_NE(spell, SpellID::NONE);
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 50, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	startBattle();
	clearStartingUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
	ASSERT_NE(target, nullptr);
	beginCombat();
	setRawMorale(target, 0);

	ASSERT_TRUE(cast(BattleSide::ATTACKER, spell, target));
	const auto * applied = spellMorale(target, spell);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -10);
	EXPECT_FALSE(applied->appliedByEnemy);
	EXPECT_EQ(battle()->battleGetMorale(target), minimumMorale());
}

TEST_F(NewHorizonsSteadfastTest, DetachedRemovalChangesOnlyItsOwnMoraleProjection)
{
	prepareMoraleBattle(BattleSide::ATTACKER);
	setRawMorale(target, 0);
	auto hostilePenalty = addMorale(target, -3, BonusSource::SPELL_EFFECT, true);
	ASSERT_EQ(battle()->battleGetMorale(target), -2);

	SteadfastEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = branch->getForUpdate(target->unitId());
	ASSERT_EQ(branch->battleGetMorale(projected.get()), -2);

	branch->removeUnitBonus(target->unitId(), {*hostilePenalty});
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 0);
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(target->unitId()).get()), -2);
	EXPECT_EQ(sibling->battleGetMorale(sibling->getForUpdate(target->unitId()).get()), -2);
	EXPECT_EQ(battle()->battleGetMorale(target), -2);
}

TEST_F(NewHorizonsSteadfastTest, ProvenanceRoundTripsThroughBinaryAndJsonAndCannotBeDownsaved)
{
	startGame();
	Bonus source(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::OTHER, -3, BonusSourceID());
	source.turnsRemain = 2;
	source.appliedByEnemy = true;

	CMemorySerializer wire;
	wire.oser & source;
	Bonus restored;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.iser & restored;
	EXPECT_TRUE(restored.appliedByEnemy);
	EXPECT_EQ(restored.val, -3);
	EXPECT_EQ(restored.turnsRemain, 2);

	const JsonNode json = source.toJsonNode();
	ASSERT_TRUE(json["appliedByEnemy"].Bool());
	const auto parsed = JsonUtils::parseBonus(json);
	ASSERT_NE(parsed, nullptr);
	EXPECT_TRUE(parsed->appliedByEnemy);
	EXPECT_EQ(parsed->val, -3);

	Bonus defaultFriendly(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::OTHER, -1, BonusSourceID());
	EXPECT_TRUE(defaultFriendly.toJsonNode()["appliedByEnemy"].isNull());
	EXPECT_FALSE(JsonUtils::parseBonus(defaultFriendly.toJsonNode())->appliedByEnemy);

	auto invalidString = json;
	invalidString["appliedByEnemy"] = JsonNode("true");
	EXPECT_THROW(JsonUtils::parseBonus(invalidString), std::runtime_error);
	auto invalidInteger = json;
	invalidInteger["appliedByEnemy"] = JsonNode(1);
	EXPECT_THROW(JsonUtils::parseBonus(invalidInteger), std::runtime_error);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_UNBREAKABLE;
	EXPECT_THROW(oldWriter.oser & source, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty()) << "Refuse a lossy write before emitting any Bonus fields";
}
