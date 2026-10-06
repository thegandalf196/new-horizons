/*
 * NewHorizonsBloodragePerksTest.cpp, part of VCMI engine
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
#include "../../../server/CGameHandler.h"

namespace
{
constexpr std::string_view BLOODRAGE_SKILL = "new-horizons:bloodrage";
constexpr std::string_view WAR_DRUMS = "new-horizons:bloodrage.warDrums";
constexpr std::string_view FURY_UNBOUND = "new-horizons:bloodrage.furyUnbound";
constexpr std::string_view ENDLESS_BLOODSHED = "new-horizons:bloodrage.endlessBloodshed";

class BloodragePerksEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BloodragePerksEnvironment(std::shared_ptr<CGameState> state_) : state(std::move(state_)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsBloodragePerksTest : public BattleTestFixture
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
			if(id == FURY_UNBOUND || id == ENDLESS_BLOODSHED)
			{
				const auto status = perk["effect"]["status"].String();
				if(status != "planned" && status != "active")
					throw std::runtime_error("Bloodrage test perk must be planned or active in canonical content");
				perk["effect"]["status"].String() = "active";
				++activated;
			}
		}
		if(activated != 2)
			throw std::runtime_error("Missing Fury Unbound or Endless Bloodshed registry entry");
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

	void selectAdvancedFury(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, FURY_UNBOUND);
	}

	void selectExpertEndless(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, ENDLESS_BLOODSHED);
	}

	void selectFullBloodrage(CGHeroInstance * hero)
	{
		selectBasicWarDrums(hero);
		selectAdvancedFury(hero);
		selectExpertEndless(hero);
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
		// Fixture setup removes the army stacks, and authoritative removals count
		// as Bloodrage casualties. Restore only these setup-generated increments.
		battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = attackerOpeningBloodrage;
		battle()->getSide(BattleSide::DEFENDER).bloodrageDamagePercent = defenderOpeningBloodrage;
	}

	std::shared_ptr<Bonus> morale(CStack * stack, int value)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::OTHER, value, BonusSourceID());
		stack->addNewBonus(result);
		return result;
	}

	int minimumMorale() const
	{
		return -static_cast<int>(LIBRARY->engineSettings()->getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
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

class NewHorizonsBloodragePerksLegacySerializationTest : public NewHorizonsBloodragePerksTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsBloodragePerksTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		auto magicRules = LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS);
		magicRules["warcasting"] = JsonNode(false);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}
};
}

TEST_F(NewHorizonsBloodragePerksLegacySerializationTest, LegalOffersUnlockFuryAndExtendedCapWithCurrentSerialization)
{
	startGame();
	EXPECT_FALSE(newHorizonsBloodrage::hasWarDrums(attackerSideHero));
	EXPECT_FALSE(newHorizonsBloodrage::hasFuryUnbound(attackerSideHero));
	EXPECT_FALSE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));

	selectBasicWarDrums(attackerSideHero);
	EXPECT_TRUE(newHorizonsBloodrage::hasWarDrums(attackerSideHero));
	EXPECT_FALSE(newHorizonsBloodrage::hasFuryUnbound(attackerSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 20);

	selectAdvancedFury(attackerSideHero);
	EXPECT_TRUE(newHorizonsBloodrage::hasFuryUnbound(attackerSideHero));
	EXPECT_FALSE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 40);

	attackerSideHero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 60)
		<< "Expert rank alone retains the ordinary cap until its perk is selected";
	acceptPerk(attackerSideHero, ENDLESS_BLOODSHED);
	EXPECT_TRUE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));
	EXPECT_EQ(newHorizonsBloodrage::capForRank(3), 60);
	EXPECT_EQ(newHorizonsBloodrage::capForRank(3, true), 80);
	EXPECT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 80);

	startBattle();
	EXPECT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 12)
		<< "Endless Bloodshed changes the cap, not the Expert increment";

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	EXPECT_EQ(restored->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::BONUS_EFFECT_HOSTILITY;
	try
	{
		oldWriter.oser & *battle();
		FAIL() << "An older writer cannot silently drop the selected extended cap";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard non-base Bloodrage cap in an older format");
	}
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}

TEST_F(NewHorizonsBloodragePerksLegacySerializationTest, LegacyRankOnlySnapshotRestoresTheOrdinaryExpertCap)
{
	startGame();
	selectBasicWarDrums(attackerSideHero);
	selectAdvancedFury(attackerSideHero);
	attackerSideHero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_FALSE(newHorizonsBloodrage::hasEndlessBloodshed(attackerSideHero));
	ASSERT_EQ(newHorizonsBloodrage::capForHero(attackerSideHero), 60);
	startBattle();
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 60);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::BONUS_EFFECT_HOSTILITY;
	legacy.iser.version = ESerializationVersion::BONUS_EFFECT_HOSTILITY;
	legacy.oser & *battle();
	BattleInfo restored(gameState().get());
	legacy.iser & restored;
	EXPECT_EQ(restored.getBloodrageRank(BattleSide::ATTACKER), 3);
	EXPECT_EQ(restored.getBloodrageCapPercent(BattleSide::ATTACKER), 60)
		<< "The older format has rank and counter, so the missing cap falls back to the rank cap";
	EXPECT_EQ(restored.getBloodrageDamagePercent(BattleSide::ATTACKER), 12);
}

TEST_F(NewHorizonsBloodragePerksTest, EndlessBloodshedAllowsQualifyingDeathsPastSixtyAndSaturatesAtEighty)
{
	startGame();
	selectFullBloodrage(attackerSideHero);
	startBattle();
	removeStartingUnits();
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);

	std::vector<CStack *> victims;
	for(int index = 0; index < 8; ++index)
		victims.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(1 + index * 2, 8), 1));

	kill({victims[0], victims[1], victims[2], victims[3]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 60);
	kill({victims[4]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 72)
		<< "The first increment above the ordinary 60 cap remains available";

	BloodragePerksEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_EQ(parent->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(branch->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(sibling->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	auto projectedVictim = branch->getForUpdate(victims[5]->unitId());
	ASSERT_TRUE(projectedVictim->alive());
	int64_t lethal = projectedVictim->getAvailableHealth();
	projectedVictim->damage(lethal);
	branch->recordBloodrageTransition(projectedVictim, true);
	EXPECT_EQ(branch->getBloodrageDamagePercent(BattleSide::ATTACKER), 80);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::ATTACKER), 72);
	EXPECT_EQ(sibling->getBloodrageDamagePercent(BattleSide::ATTACKER), 72);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 72);

	kill({victims[5]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 80);
	EXPECT_EQ(branch->getBloodrageDamagePercent(BattleSide::ATTACKER), 80);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::ATTACKER), 72);
	EXPECT_EQ(sibling->getBloodrageDamagePercent(BattleSide::ATTACKER), 72);
	kill({victims[6], victims[7]});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 80)
		<< "Further qualifying deaths saturate at the snapshotted extended cap";
}

TEST_F(NewHorizonsBloodragePerksTest, FuryFloorsNegativeMoraleOnlyForCurrentControllerWithActiveBloodrage)
{
	startGame();
	selectFullBloodrage(attackerSideHero);
	startBattle();
	removeStartingUnits();
	const int lowerCap = minimumMorale();
	ASSERT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);

	auto * negative = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	auto * positive = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * immune = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	auto * hypnotized = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 5), 10);

	morale(negative, -100);
	morale(positive, 100);
	morale(immune, -100);
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NO_MORALE,
		BonusSource::OTHER, 1, BonusSourceID()));
	morale(hypnotized, -100);

	// Isolate the zero-increment boundary despite the legally selected War Drums opening increment.
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 0;
	const auto beforeBloodshed = battle()->battleGetMoraleInfo(negative);
	ASSERT_EQ(beforeBloodshed.real, lowerCap);
	ASSERT_EQ(beforeBloodshed.effective, lowerCap);
	ASSERT_FALSE(beforeBloodshed.furyUnboundFloorApplied);
	const int positiveMorale = battle()->battleGetMorale(positive);
	ASSERT_GT(positiveMorale, 0);
	ASSERT_EQ(battle()->battleGetMorale(immune), 0);
	ASSERT_EQ(battle()->battleGetMorale(hypnotized), lowerCap);
	ASSERT_FALSE(newHorizonsBloodrage::hasFuryUnbound(defenderSideHero));

	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
	EXPECT_EQ(battle()->battleGetMorale(negative), lowerCap);
	EXPECT_EQ(battle()->battleGetMorale(positive), positiveMorale);
	EXPECT_EQ(battle()->battleGetMorale(immune), 0);

	BloodragePerksEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentNegative = parent->getForUpdate(negative->unitId());
	const auto branchNegative = branch->getForUpdate(negative->unitId());
	const auto siblingNegative = sibling->getForUpdate(negative->unitId());
	ASSERT_EQ(parent->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(branch->getBloodrageCapPercent(BattleSide::ATTACKER), 80);
	ASSERT_EQ(sibling->getBloodrageCapPercent(BattleSide::ATTACKER), 80);

	auto projectedVictim = branch->getForUpdate(victim->unitId());
	const bool wasAlive = projectedVictim->alive();
	ASSERT_TRUE(wasAlive);
	int64_t lethal = projectedVictim->getAvailableHealth();
	projectedVictim->damage(lethal);
	branch->recordBloodrageTransition(projectedVictim, wasAlive);
	EXPECT_EQ(branch->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);
	const auto projectedNegative = branch->battleGetMoraleInfo(branchNegative.get());
	EXPECT_EQ(projectedNegative.real, lowerCap);
	EXPECT_EQ(projectedNegative.effective, 0);
	EXPECT_TRUE(projectedNegative.furyUnboundFloorApplied);
	EXPECT_EQ(parent->battleGetMorale(parentNegative.get()), lowerCap);
	EXPECT_EQ(sibling->battleGetMorale(siblingNegative.get()), lowerCap);
	EXPECT_EQ(battle()->battleGetMorale(negative), lowerCap);

	kill({victim});
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 12);
	const auto afterBloodshed = battle()->battleGetMoraleInfo(negative);
	EXPECT_EQ(afterBloodshed.real, lowerCap);
	EXPECT_EQ(afterBloodshed.effective, 0);
	EXPECT_TRUE(afterBloodshed.furyUnboundFloorApplied);
	EXPECT_EQ(battle()->battleGetMorale(positive), positiveMorale)
		<< "Fury does not change positive Morale";
	EXPECT_EQ(battle()->battleGetMorale(immune), 0)
		<< "NO_MORALE immunity remains effective while Bloodrage is active";
	EXPECT_FALSE(battle()->battleGetMoraleInfo(positive).furyUnboundFloorApplied);
	EXPECT_FALSE(battle()->battleGetMoraleInfo(immune).furyUnboundFloorApplied);
	EXPECT_EQ(branch->battleGetMorale(branchNegative.get()), 0);
	EXPECT_EQ(parent->battleGetMorale(parentNegative.get()), lowerCap);
	EXPECT_EQ(sibling->battleGetMorale(siblingNegative.get()), lowerCap);

	ASSERT_EQ(hypnotized->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwnerHero(hypnotized), defenderSideHero);
	EXPECT_EQ(battle()->battleGetMorale(hypnotized), lowerCap)
		<< "A defender-controlled stack does not borrow the attacker's Fury or Bloodrage";
	hypnotized->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwnerHero(hypnotized), attackerSideHero);
	EXPECT_EQ(battle()->battleGetMorale(hypnotized), 0)
		<< "Hypnotize switches the Morale floor to the current controller's active Fury and Bloodrage";
}
