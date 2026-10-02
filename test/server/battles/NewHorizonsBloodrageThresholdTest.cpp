/*
 * NewHorizonsBloodrageThresholdTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

namespace
{
constexpr std::string_view BLOODRAGE_SKILL = "new-horizons:bloodrage";
constexpr std::string_view WAR_DRUMS = "new-horizons:bloodrage.warDrums";
constexpr std::string_view UNRELENTING = "new-horizons:bloodrage.unrelenting";
constexpr std::string_view BERSERKER = "new-horizons:bloodrage.berserker";
constexpr std::string_view ENDLESS_BLOODSHED = "new-horizons:bloodrage.endlessBloodshed";

class BloodrageThresholdEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BloodrageThresholdEnvironment(std::shared_ptr<CGameState> state_) : state(std::move(state_)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsBloodrageThresholdTest : public BattleTestFixture
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
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		int activated = 0;
		for(auto & perk : rules["skills"][std::string(BLOODRAGE_SKILL)]["perks"].Vector())
		{
			const auto id = perk["id"].String();
			if(id == UNRELENTING || id == BERSERKER)
			{
				const auto status = perk["effect"]["status"].String();
				if(status != "planned" && status != "active")
					throw std::runtime_error("Bloodrage threshold test perk must be planned or active");
				perk["effect"]["status"].String() = "active";
				++activated;
			}
		}
		if(activated != 2)
			throw std::runtime_error("Missing Unrelenting or Berserker registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
	}

	SecondarySkill bloodrageSkill() const
	{
		const int decoded = SecondarySkill::decode(std::string(BLOODRAGE_SKILL));
		if(decoded < 0)
			throw std::runtime_error("Missing New Horizons Bloodrage skill");
		return SecondarySkill(decoded);
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
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
					&& offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(BLOODRAGE_SKILL), std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal Bloodrage perk offer for " << perkId;
	}

	void selectBasicWarDrums(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, WAR_DRUMS);
	}

	void selectAdvancedPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, perkId);
	}

	void selectUnrelenting(CGHeroInstance * hero, bool endlessBloodshed = false)
	{
		selectBasicWarDrums(hero);
		selectAdvancedPerk(hero, UNRELENTING);
		if(endlessBloodshed)
		{
			hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			acceptPerk(hero, ENDLESS_BLOODSHED);
		}
	}

	void selectBerserker(CGHeroInstance * hero)
	{
		selectBasicWarDrums(hero);
		selectAdvancedPerk(hero, BERSERKER);
	}

	void removeStartingUnits()
	{
		const int attackerOpeningBloodrage = battle()->getBloodrageDamagePercent(BattleSide::ATTACKER);
		const int defenderOpeningBloodrage = battle()->getBloodrageDamagePercent(BattleSide::DEFENDER);
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!changes.changedStacks.empty())
			gameHandler->sendAndApply(changes);
		// Setup removals are authoritative casualties; retain only the real opening increment.
		battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = attackerOpeningBloodrage;
		battle()->getSide(BattleSide::DEFENDER).bloodrageDamagePercent = defenderOpeningBloodrage;
	}

	std::shared_ptr<Bonus> addBonus(CStack * stack, BonusType type, int value)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::OTHER, value, BonusSourceID());
		stack->addNewBonus(result);
		return result;
	}

	void setBloodrage(BattleSide side, int value)
	{
		battle()->getSide(side).bloodrageDamagePercent = value;
	}

	void kill(const std::vector<CStack *> & targets)
	{
		StacksInjured injury;
		injury.battleID = BattleID(0);
		for(const auto * target : targets)
		{
			auto & attacked = injury.stacks.emplace_back();
			attacked.stackAttacked = target->unitId();
			attacked.damageAmount = target->getAvailableHealth();
			target->prepareAttacked(attacked, gameHandler->getRandomGenerator());
			ASSERT_TRUE(attacked.killed());
			ASSERT_FALSE(attacked.willRebirth());
		}
		gameHandler->sendAndApply(injury);
	}
};
}

TEST_F(NewHorizonsBloodrageThresholdTest, LegalOffersUseEachSideSnapshotForExactHalfCapThresholds)
{
	startGame();
	selectUnrelenting(attackerSideHero, true);
	selectUnrelenting(defenderSideHero);
	EXPECT_TRUE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 80);
	EXPECT_FALSE(newHorizonsBloodrage::hasEndlessBloodshed(defenderSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForHero(defenderSideHero), 40);

	startBattle();
	removeStartingUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 10);
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::DEFENDER), 40);

	setBloodrage(BattleSide::ATTACKER, 39);
	setBloodrage(BattleSide::DEFENDER, 19);
	EXPECT_EQ(battle()->battleBloodrageSpeed(attacker), 0);
	EXPECT_EQ(battle()->battleBloodrageSpeed(defender), 0);
	setBloodrage(BattleSide::ATTACKER, 40);
	setBloodrage(BattleSide::DEFENDER, 20);
	EXPECT_EQ(battle()->battleBloodrageSpeed(attacker), 1)
		<< "The Expert cap requires 40 Bloodrage for Unrelenting";
	EXPECT_EQ(battle()->battleBloodrageSpeed(defender), 1)
		<< "The ordinary cap requires 20 Bloodrage for Unrelenting";

	// A snapshot without an active cap must never enable the threshold.
	battle()->getSide(BattleSide::DEFENDER).bloodrageCapPercent = 0;
	setBloodrage(BattleSide::DEFENDER, 80);
	EXPECT_EQ(battle()->battleBloodrageSpeed(defender), 0);
}

TEST_F(NewHorizonsBloodrageThresholdTest, RealCasualtiesAddSpeedAndOnlyFallbackInitiative)
{
	startGame();
	selectUnrelenting(attackerSideHero, true);
	startBattle();
	removeStartingUnits();
	ASSERT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);

	auto * fallback = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * explicitInitiative = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	addBonus(explicitInitiative, BonusType::STACKS_INITIATIVE_BASE, 31);
	std::vector<CStack *> victims;
	for(int index = 0; index < 3; ++index)
		victims.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11 + 2 * index, 7), 1));

	ASSERT_EQ(battle()->battleBloodrageSpeed(fallback), 0);
	ASSERT_EQ(battle()->battleBloodrageSpeed(explicitInitiative), 0);
	const auto fallbackMovement = fallback->getMovementRange(0);
	const auto explicitMovement = explicitInitiative->getMovementRange(0);
	const auto fallbackInitiative = fallback->getInitiative(0);
	const auto explicitCurrentInitiative = explicitInitiative->getInitiative(0);
	const auto fallbackNextInitiative = fallback->getInitiative(1);
	const auto explicitNextInitiative = explicitInitiative->getInitiative(1);

	kill({victims[0]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	EXPECT_EQ(battle()->battleBloodrageSpeed(fallback), 0);
	kill({victims[1]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 36);
	EXPECT_EQ(battle()->battleBloodrageSpeed(fallback), 0);
	kill({victims[2]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 48)
		<< "The third actual qualifying death crosses the modified cap's half threshold";
	EXPECT_EQ(battle()->battleBloodrageSpeed(fallback), 1);
	EXPECT_EQ(battle()->battleBloodrageSpeed(explicitInitiative), 1);
	EXPECT_EQ(fallback->getMovementRange(0), fallbackMovement + 1);
	EXPECT_EQ(explicitInitiative->getMovementRange(0), explicitMovement + 1);
	EXPECT_EQ(fallback->getInitiative(0), fallbackInitiative + 1)
		<< "Fallback Initiative follows the Speed increase";
	EXPECT_EQ(fallback->getInitiative(1), fallbackNextInitiative + 1)
		<< "The battle-long Speed benefit also affects a future turn's fallback Initiative";
	EXPECT_EQ(explicitInitiative->getInitiative(0), explicitCurrentInitiative)
		<< "An explicit initiative base is not increased by Unrelenting";
	EXPECT_EQ(explicitInitiative->getInitiative(1), explicitNextInitiative)
		<< "An explicit future-turn initiative base remains unchanged";
}

TEST_F(NewHorizonsBloodrageThresholdTest, BerserkerAddsAUseAfterThresholdAndResetsNormally)
{
	startGame();
	selectBerserker(attackerSideHero);
	startBattle();
	removeStartingUnits();
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 40);
	ASSERT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 8);

	auto * berserker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * firstVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	auto * secondVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 5), 1);
	auto * survivor = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 7), 10);
	ASSERT_EQ(berserker->counterAttacks.total(), 1);
	berserker->counterAttacks.use();
	ASSERT_EQ(berserker->counterAttacks.available(), 0);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(berserker), 0);

	kill({firstVictim});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 16);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(berserker), 0);
	EXPECT_EQ(berserker->counterAttacks.total(), 1);
	EXPECT_FALSE(berserker->counterAttacks.canUse());

	kill({secondVictim});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(berserker), 1);
	EXPECT_EQ(berserker->counterAttacks.total(), 2);
	EXPECT_EQ(berserker->counterAttacks.available(), 1)
		<< "The newly earned allowance is available even though the original counter was already spent";
	berserker->counterAttacks.use();
	EXPECT_EQ(berserker->counterAttacks.available(), 0);

	// Losing threshold eligibility cannot refund the spent counters or leave a cached extra use.
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 16;
	EXPECT_EQ(battle()->battleBloodrageRetaliations(berserker), 0);
	EXPECT_EQ(berserker->counterAttacks.total(), 1);
	EXPECT_FALSE(berserker->counterAttacks.canUse());

	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 24;
	ASSERT_NE(survivor, nullptr);
	beginCombat();
	endRound();
	EXPECT_EQ(berserker->counterAttacks.total(), 2);
	EXPECT_EQ(berserker->counterAttacks.available(), 2)
		<< "The normal round reset re-arms the ordinary and currently eligible dynamic retaliation";
}

TEST_F(NewHorizonsBloodrageThresholdTest, NoRetaliationAndUnlimitedCountersKeepTheirExistingPrecedence)
{
	startGame();
	selectBerserker(attackerSideHero);
	startBattle();
	removeStartingUnits();
	setBloodrage(BattleSide::ATTACKER, 20);

	auto * blocked = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	blocked->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
	auto * unlimited = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	unlimited->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleBloodrageRetaliations(blocked), 1);
	ASSERT_EQ(battle()->battleBloodrageRetaliations(unlimited), 1);
	EXPECT_EQ(blocked->counterAttacks.total(), 0);
	EXPECT_FALSE(blocked->counterAttacks.canUse())
		<< "NO_RETALIATION still suppresses the base and Berserker counters";
	EXPECT_FALSE(unlimited->counterAttacks.isLimited());
	EXPECT_TRUE(unlimited->counterAttacks.canUse());
	unlimited->counterAttacks.use(3);
	EXPECT_TRUE(unlimited->counterAttacks.canUse())
		<< "Berserker does not convert unlimited retaliation into a limited allowance";

	setBloodrage(BattleSide::ATTACKER, 0);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(blocked), 0);
	EXPECT_EQ(blocked->counterAttacks.total(), 0);
	EXPECT_FALSE(blocked->counterAttacks.canUse());
	EXPECT_FALSE(unlimited->counterAttacks.isLimited());
	EXPECT_TRUE(unlimited->counterAttacks.canUse());
}

TEST_F(NewHorizonsBloodrageThresholdTest, HypnotizeAndDetachedBranchesUseCurrentControllerForBerserker)
{
	startGame();
	selectBerserker(attackerSideHero);
	startBattle();
	removeStartingUnits();
	setBloodrage(BattleSide::ATTACKER, 24);

	auto * controlled = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 10);
	ASSERT_EQ(controlled->counterAttacks.total(), 1);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(controlled), 0)
		<< "A defender-controlled unit cannot use the attacker's perk";
	auto hypnosis = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	controlled->addNewBonus(hypnosis);
	ASSERT_EQ(controlled->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwnerHero(controlled), attackerSideHero);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(controlled), 1);
	EXPECT_EQ(controlled->counterAttacks.total(), 2);
	controlled->removeBonus(hypnosis);
	ASSERT_EQ(battle()->battleGetOwnerHero(controlled), defenderSideHero);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(controlled), 0);
	EXPECT_EQ(controlled->counterAttacks.total(), 1)
		<< "Losing current control removes the dynamic extra without making it sticky";

	BloodrageThresholdEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentTarget = parent->getForUpdate(controlled->unitId());
	const auto branchTarget = branch->getForUpdate(controlled->unitId());
	const auto siblingTarget = sibling->getForUpdate(controlled->unitId());
	ASSERT_EQ(parent->battleBloodrageRetaliations(parentTarget.get()), 0);
	ASSERT_EQ(sibling->battleBloodrageRetaliations(siblingTarget.get()), 0);

	branch->addUnitBonus(controlled->unitId(), {*hypnosis});
	EXPECT_EQ(branch->battleBloodrageRetaliations(branchTarget.get()), 1);
	EXPECT_EQ(branchTarget->counterAttacks.total(), 2);
	EXPECT_EQ(parent->battleBloodrageRetaliations(parentTarget.get()), 0);
	EXPECT_EQ(parentTarget->counterAttacks.total(), 1);
	EXPECT_EQ(sibling->battleBloodrageRetaliations(siblingTarget.get()), 0);
	EXPECT_EQ(siblingTarget->counterAttacks.total(), 1);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(controlled), 0);

	branch->removeUnitBonus(controlled->unitId(), {*hypnosis});
	EXPECT_EQ(branch->battleBloodrageRetaliations(branchTarget.get()), 0);
	EXPECT_EQ(branchTarget->counterAttacks.total(), 1)
		<< "Only the branch that changes control gains and then loses the extra allowance";
	EXPECT_EQ(parentTarget->counterAttacks.total(), 1);
	EXPECT_EQ(siblingTarget->counterAttacks.total(), 1);
	EXPECT_EQ(controlled->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsBloodrageThresholdTest, DetachedQualifyingDeathCrossesBothCurrentControllerThresholds)
{
	startGame();
	selectUnrelenting(attackerSideHero);
	selectBerserker(defenderSideHero);
	startBattle();
	removeStartingUnits();
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 40);
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::DEFENDER), 40);

	auto * fastStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * berserkerStack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(15, 5), 1);
	setBloodrage(BattleSide::ATTACKER, 16);
	setBloodrage(BattleSide::DEFENDER, 16);
	ASSERT_EQ(battle()->battleBloodrageSpeed(fastStack), 0);
	ASSERT_EQ(battle()->battleBloodrageRetaliations(berserkerStack), 0);
	ASSERT_EQ(berserkerStack->counterAttacks.total(), 1);

	BloodrageThresholdEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentFast = parent->getForUpdate(fastStack->unitId());
	const auto parentBerserker = parent->getForUpdate(berserkerStack->unitId());
	const auto branchFast = branch->getForUpdate(fastStack->unitId());
	const auto branchBerserker = branch->getForUpdate(berserkerStack->unitId());
	const auto siblingFast = sibling->getForUpdate(fastStack->unitId());
	const auto siblingBerserker = sibling->getForUpdate(berserkerStack->unitId());
	const auto projectedVictim = branch->getForUpdate(victim->unitId());
	ASSERT_TRUE(projectedVictim->alive());
	int64_t lethal = projectedVictim->getAvailableHealth();
	projectedVictim->damage(lethal);
	branch->recordBloodrageTransition(projectedVictim, true);

	EXPECT_EQ(branch->getBloodrageDamagePercent(BattleSide::ATTACKER), 24);
	EXPECT_EQ(branch->getBloodrageDamagePercent(BattleSide::DEFENDER), 24);
	EXPECT_EQ(branch->battleBloodrageSpeed(branchFast.get()), 1);
	EXPECT_EQ(branch->battleBloodrageRetaliations(branchBerserker.get()), 1);
	EXPECT_EQ(branchBerserker->counterAttacks.total(), 2);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::ATTACKER), 16);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::DEFENDER), 16);
	EXPECT_EQ(parent->battleBloodrageSpeed(parentFast.get()), 0);
	EXPECT_EQ(parent->battleBloodrageRetaliations(parentBerserker.get()), 0);
	EXPECT_EQ(sibling->getBloodrageDamagePercent(BattleSide::ATTACKER), 16);
	EXPECT_EQ(sibling->getBloodrageDamagePercent(BattleSide::DEFENDER), 16);
	EXPECT_EQ(sibling->battleBloodrageSpeed(siblingFast.get()), 0);
	EXPECT_EQ(sibling->battleBloodrageRetaliations(siblingBerserker.get()), 0);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 16);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::DEFENDER), 16);
	EXPECT_EQ(battle()->battleBloodrageSpeed(fastStack), 0);
	EXPECT_EQ(battle()->battleBloodrageRetaliations(berserkerStack), 0);
}

TEST_F(NewHorizonsBloodrageThresholdTest, ThresholdBonusesRoundTripAndLegacyFormatsDefaultThemToZero)
{
	startGame();
	selectUnrelenting(attackerSideHero);
	selectBerserker(defenderSideHero);
	startBattle();
	ASSERT_EQ(battle()->getBloodrageSpeedBonus(BattleSide::ATTACKER), 1);
	ASSERT_EQ(battle()->getBloodrageAdditionalRetaliations(BattleSide::ATTACKER), 0);
	ASSERT_EQ(battle()->getBloodrageSpeedBonus(BattleSide::DEFENDER), 0);
	ASSERT_EQ(battle()->getBloodrageAdditionalRetaliations(BattleSide::DEFENDER), 1);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getBloodrageSpeedBonus(BattleSide::ATTACKER), 1);
	EXPECT_EQ(restored->getBloodrageAdditionalRetaliations(BattleSide::ATTACKER), 0);
	EXPECT_EQ(restored->getBloodrageSpeedBonus(BattleSide::DEFENDER), 0);
	EXPECT_EQ(restored->getBloodrageAdditionalRetaliations(BattleSide::DEFENDER), 1);

	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_CAP;
	try
	{
		rejected.oser & *battle();
		FAIL() << "A writer before threshold-bonus support must not drop enabled snapshot flags";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard Bloodrage threshold bonuses in an older format");
	}
	EXPECT_TRUE(rejected.extractBuffer().empty());

	// Simulate a pre-feature battle record: flags were absent, even if the attached
	// heroes now have the perks. The old writer omits them and the reader defaults 0.
	auto legacySource = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(legacySource, nullptr);
	legacySource->getSide(BattleSide::ATTACKER).bloodrageSpeedBonus = 0;
	legacySource->getSide(BattleSide::ATTACKER).bloodrageAdditionalRetaliations = 0;
	legacySource->getSide(BattleSide::DEFENDER).bloodrageSpeedBonus = 0;
	legacySource->getSide(BattleSide::DEFENDER).bloodrageAdditionalRetaliations = 0;
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_CAP;
	legacy.oser & *legacySource;
	auto legacyBytes = legacy.extractBuffer();
	ASSERT_FALSE(legacyBytes.empty());
	CMemorySerializer legacyReader(std::move(legacyBytes));
	legacyReader.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_CAP;
	legacyReader.iser.cb = gameState().get();
	BattleInfo legacyRestored(gameState().get());
	legacyReader.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.getBloodrageCapPercent(BattleSide::ATTACKER), 40);
	EXPECT_EQ(legacyRestored.getBloodrageCapPercent(BattleSide::DEFENDER), 40);
	EXPECT_EQ(legacyRestored.getBloodrageSpeedBonus(BattleSide::ATTACKER), 0);
	EXPECT_EQ(legacyRestored.getBloodrageAdditionalRetaliations(BattleSide::ATTACKER), 0);
	EXPECT_EQ(legacyRestored.getBloodrageSpeedBonus(BattleSide::DEFENDER), 0);
	EXPECT_EQ(legacyRestored.getBloodrageAdditionalRetaliations(BattleSide::DEFENDER), 0);
}
