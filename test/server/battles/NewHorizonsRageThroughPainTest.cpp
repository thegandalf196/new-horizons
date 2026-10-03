/*
 * NewHorizonsRageThroughPainTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

namespace
{
constexpr std::string_view BLOODRAGE_SKILL = "new-horizons:bloodrage";
constexpr std::string_view RAGE_THROUGH_PAIN = "new-horizons:bloodrage.rageThroughPain";

class RageThroughPainEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RageThroughPainEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsRageThroughPainTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		int found = 0;
		bool changed = false;
		for(auto & perk : rules["skills"][std::string(BLOODRAGE_SKILL)]["perks"].Vector())
		{
			if(perk["id"].String() != RAGE_THROUGH_PAIN)
				continue;
			++found;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				continue;
			if(status != "planned")
				throw std::runtime_error("Rage Through Pain must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			changed = true;
		}
		if(found != 1)
			throw std::runtime_error("Rage Through Pain is missing or duplicated in the New Horizons registry");
		if(changed)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
	}

	HeroTypeID heroTypeForFaction(FactionID faction) const
	{
		for(const auto & id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == faction)
				return id;
		}
		throw std::runtime_error("No default-allowed hero found for the requested fixture faction");
	}

	void neutralizeHero(CGHeroInstance * hero)
	{
		for(const auto & bonus : hero->getHeroType()->specialty)
			hero->removeBonus(bonus);
		for(int i = 0; i < LIBRARY->skillh->size(); ++i)
			hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
		for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	void startGameWithStrongholdHero()
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroTypeForFaction(FactionID::STRONGHOLD), PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, heroTypeForFaction(FactionID::CASTLE), PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		neutralizeHero(attackerSideHero);
		neutralizeHero(defenderSideHero);
	}

	SecondarySkill bloodrageSkill() const
	{
		const int decoded = SecondarySkill::decode(std::string(BLOODRAGE_SKILL));
		if(decoded < 0)
			throw std::runtime_error("Missing New Horizons Bloodrage skill");
		return SecondarySkill(decoded);
	}

	void selectRageThroughPain(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == BLOODRAGE_SKILL
					&& offers[choice].selection.perkId == RAGE_THROUGH_PAIN)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(BLOODRAGE_SKILL), std::string(RAGE_THROUGH_PAIN)));
					return;
				}
			}
		}
		FAIL() << "No legal Basic Rage Through Pain offer";
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void setAvailableHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		ASSERT_GE(desiredHealth, 0);
		ASSERT_LE(desiredHealth, state->getAvailableHealth());
		int64_t damage = state->getAvailableHealth() - desiredHealth;
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void healAvailableHealth(CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		int64_t requested = amount;
		const int64_t restored = state->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = restored;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	CStack * addAdjacentEnemy(CStack * target)
	{
		for(const auto & hex : target->getSurroundingHexes())
		{
			if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
				continue;
			auto * result = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), hex, 1);
			forceMaximumDamage(result);
			return result;
		}
		return nullptr;
	}

	CStack * addDistantFriendlyVictim(const CStack * first)
	{
		for(int rawHex = 0; rawHex < GameConstants::BFIELD_SIZE; ++rawHex)
		{
			const BattleHex position(static_cast<si16>(rawHex));
			if(!position.isAvailable() || battle()->battleGetStackByPos(position)
				|| BattleHex::getDistance(position, first->getPosition()) < 6)
				continue;
			bool hasOpenNeighbor = false;
			for(const auto direction : {BattleHex::TOP_LEFT, BattleHex::TOP_RIGHT, BattleHex::RIGHT,
				BattleHex::BOTTOM_RIGHT, BattleHex::BOTTOM_LEFT, BattleHex::LEFT})
			{
				const auto hex = position.cloneInDirection(direction, false);
				if(hex.isAvailable() && !battle()->battleGetStackByPos(hex))
				{
					hasOpenNeighbor = true;
					break;
				}
			}
			if(hasOpenNeighbor)
				return addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), position, 1);
		}
		return nullptr;
	}

	void readyFixtureBattle()
	{
		startGameWithStrongholdHero();
		selectRageThroughPain(attackerSideHero);
		startBattle();
		removeStartingUnits();
	}

	static std::size_t rageThroughPainLogCount(const RecordingGameServer & recording)
	{
		return static_cast<std::size_t>(std::ranges::count_if(recording.battleLogLines, [](const std::string & line)
		{
			return line.find("Rage Through Pain:") != std::string::npos;
		}));
	}
};
}

TEST_F(NewHorizonsRageThroughPainTest, LegalBasicPerkAndAcceptedHitCrossStrictHalfHealth)
{
	readyFixtureBattle();
	ASSERT_EQ(battle()->getBloodragePainIncrement(BattleSide::ATTACKER), 5);
	const int sideBloodrageBefore = battle()->getBloodrageDamagePercent(BattleSide::ATTACKER);
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 1);
	auto * hitter = addAdjacentEnemy(victim);
	ASSERT_NE(hitter, nullptr);
	blockRetaliation(hitter);
	beginCombat();
	ASSERT_EQ(victim->getTotalHealth(), 10);
	setAvailableHealth(victim, 5);
	EXPECT_EQ(victim->getPersonalBloodrageIncrement(), 0);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(victim), sideBloodrageBefore)
		<< "Exactly half maximum HP does not grant Rage Through Pain";

	const int64_t healthBefore = victim->getAvailableHealth();
	ASSERT_TRUE(attack(hitter, victim->getPosition()));
	ASSERT_TRUE(victim->alive());
	ASSERT_TRUE(hitter->alive()) << "The weak attacker must remain present after the accepted hit";
	EXPECT_LT(victim->getAvailableHealth(), healthBefore)
		<< "The assertion follows a real accepted physical attack, not a helper-only threshold query";
	EXPECT_EQ(victim->getPersonalBloodrageIncrement(), 5);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(victim),
		std::min(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), sideBloodrageBefore + 5));
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), sideBloodrageBefore)
		<< "A stack-local grant does not increase the army-wide Bloodrage counter";
	EXPECT_EQ(rageThroughPainLogCount(server), 1u)
		<< "The accepted first grant is reported once in battle feedback";
}

TEST_F(NewHorizonsRageThroughPainTest, SeparateStacksEarnOnceAndHealingDoesNotRearmTheGrant)
{
	readyFixtureBattle();
	const int sideBloodrageBefore = battle()->getBloodrageDamagePercent(BattleSide::ATTACKER);
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 1);
	auto * firstHitter = addAdjacentEnemy(first);
	auto * second = addDistantFriendlyVictim(first);
	ASSERT_NE(second, nullptr);
	auto * secondHitter = addAdjacentEnemy(second);
	ASSERT_NE(firstHitter, nullptr);
	ASSERT_NE(secondHitter, nullptr);
	blockRetaliation(firstHitter);
	blockRetaliation(secondHitter);
	beginCombat();
	setAvailableHealth(first, 5);
	setAvailableHealth(second, 5);

	ASSERT_TRUE(attack(firstHitter, first->getPosition()));
	ASSERT_TRUE(firstHitter->alive());
	ASSERT_TRUE(attack(secondHitter, second->getPosition()));
	ASSERT_TRUE(secondHitter->alive());
	EXPECT_EQ(first->getPersonalBloodrageIncrement(), 5);
	EXPECT_EQ(second->getPersonalBloodrageIncrement(), 5)
		<< "The persistent increment is tracked independently for each stack";
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), sideBloodrageBefore);

	healAvailableHealth(first, 1);
	ASSERT_EQ(first->getAvailableHealth(), 5);
	auto * repeatHitter = addAdjacentEnemy(first);
	ASSERT_NE(repeatHitter, nullptr);
	blockRetaliation(repeatHitter);
	ASSERT_TRUE(attack(repeatHitter, first->getPosition()));
	ASSERT_TRUE(repeatHitter->alive());
	EXPECT_EQ(first->getPersonalBloodrageIncrement(), 5)
		<< "Healing back to half and crossing again cannot earn a second increment";
	EXPECT_EQ(rageThroughPainLogCount(server), 2u)
		<< "Only the two first per-stack grants produce feedback";
}

TEST_F(NewHorizonsRageThroughPainTest, DetachedAttackProjectionAwardsOnlyItsOwnBelowHalfCrossing)
{
	readyFixtureBattle();
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 1);
	auto * hitter = addAdjacentEnemy(victim);
	ASSERT_NE(hitter, nullptr);
	blockRetaliation(hitter);
	beginCombat();
	setAvailableHealth(victim, 5);
	ASSERT_EQ(victim->getPersonalBloodrageIncrement(), 0);

	RageThroughPainEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentVictim = parent->getForUpdate(victim->unitId());
	const auto siblingVictim = sibling->getForUpdate(victim->unitId());
	auto branchAttacker = branch->getForUpdate(hitter->unitId());
	auto branchVictim = branch->getForUpdate(victim->unitId());
	ASSERT_NE(branchAttacker, nullptr);
	ASSERT_NE(branchVictim, nullptr);
	battle()->activeStack = hitter->unitId();
	DamageCache cache;
	cache.buildDamageCache(branch, BattleSide::DEFENDER);
	const auto projected = AttackPossibility::evaluate(BattleAttackInfo(
		branchAttacker.get(), branchVictim.get(), 0, false), hitter->getPosition(), cache, branch);
	ASSERT_NE(projected.effectPreview, nullptr);
	auto projectedVictim = projected.effectPreview->getForUpdate(victim->unitId());
	ASSERT_NE(projectedVictim, nullptr);
	EXPECT_EQ(projectedVictim->getPersonalBloodrageIncrement(), 5);
	EXPECT_EQ(projected.effectPreview->battleGetBloodrageDamagePercent(projectedVictim.get()),
		std::min(battle()->getBloodrageCapPercent(BattleSide::ATTACKER),
			battle()->getBloodrageDamagePercent(BattleSide::ATTACKER) + 5));
	EXPECT_EQ(parentVictim->getPersonalBloodrageIncrement(), 0);
	EXPECT_EQ(siblingVictim->getPersonalBloodrageIncrement(), 0);
	EXPECT_EQ(victim->getPersonalBloodrageIncrement(), 0)
		<< "The AI's projected hit cannot mutate the live stack or sibling forecast";
}

TEST_F(NewHorizonsRageThroughPainTest, PersonalIncrementPersistsInJsonAndNarrowStackBinaryState)
{
	readyFixtureBattle();
	auto * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(81), 1);
	auto * hitter = addAdjacentEnemy(victim);
	ASSERT_NE(hitter, nullptr);
	blockRetaliation(hitter);
	beginCombat();
	setAvailableHealth(victim, 5);
	ASSERT_TRUE(attack(hitter, victim->getPosition()));
	ASSERT_EQ(victim->getPersonalBloodrageIncrement(), 5);

	auto savedState = victim->acquireState()->save();
	ASSERT_EQ(savedState["state"]["personalBloodrageIncrement"].Integer(), 5);
	auto restoredState = victim->acquireState();
	restoredState->load(savedState);
	EXPECT_EQ(restoredState->getPersonalBloodrageIncrement(), 5);
	auto invalidState = savedState;
	invalidState["state"]["personalBloodrageIncrement"].Integer() = 7;
	EXPECT_THROW(restoredState->load(invalidState), std::runtime_error)
		<< "Only rank-sized personal Bloodrage increments are valid in saved unit state";
	auto hugeState = savedState;
	hugeState["state"]["personalBloodrageIncrement"].Integer() = std::numeric_limits<int64_t>::max();
	EXPECT_THROW(restoredState->load(hugeState), std::runtime_error)
		<< "The JSON field cannot overflow the saved rank-sized increment";
	auto fractionalState = savedState;
	fractionalState["state"]["personalBloodrageIncrement"].Float() = 5.5;
	EXPECT_THROW(restoredState->load(fractionalState), std::runtime_error)
		<< "A fractional JSON value is not a valid saved increment";

	CStackBasicDescriptor base(victim->unitType()->getId(), victim->unitBaseAmount());
	CStack descriptor(&base, victim->unitOwner(), static_cast<int>(victim->unitId()), victim->unitSide());
	descriptor.personalBloodrageIncrement = 5;
	CMemorySerializer current;
	current.oser & descriptor;
	CStack currentRestored;
	current.iser.cb = gameState().get();
	current.iser & currentRestored;
	EXPECT_EQ(currentRestored.getPersonalBloodrageIncrement(), 5);

	CMemorySerializer olderWriter;
	olderWriter.oser.version = ESerializationVersion::BATTLE_INITIAL_ARMY_VALUE;
	EXPECT_THROW(olderWriter.oser & descriptor, std::runtime_error);
	EXPECT_TRUE(olderWriter.extractBuffer().empty())
		<< "A writer without Rage Through Pain state rejects loss before writing a descriptor payload";

	descriptor.personalBloodrageIncrement = 0;
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::BATTLE_INITIAL_ARMY_VALUE;
	legacy.iser.version = ESerializationVersion::BATTLE_INITIAL_ARMY_VALUE;
	legacy.oser & descriptor;
	CStack legacyRestored;
	legacy.iser.cb = gameState().get();
	legacy.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.getPersonalBloodrageIncrement(), 0)
		<< "A pre-feature descriptor defaults the omitted personal increment to zero";
}
