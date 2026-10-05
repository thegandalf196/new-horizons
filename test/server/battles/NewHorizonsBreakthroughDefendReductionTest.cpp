/*
 * NewHorizonsBreakthroughDefendReductionTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr auto OFFENSE = "new-horizons:offense";
constexpr auto BREAKTHROUGH = "new-horizons:offense.breakthrough";
constexpr auto BATTLECRAFT = "new-horizons:battlecraft";
constexpr auto BULWARK = "new-horizons:bulwarkOfTheMire";

class NewHorizonsBreakthroughDefendReductionTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
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

	void startGameWithFortressDefender()
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroTypeForFaction(FactionID::CASTLE), PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, heroTypeForFaction(FactionID::FORTRESS), PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(defenderSideHero->getHeroClass()->faction, FactionID::FORTRESS);
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			for(const auto & bonus : hero->getHeroType()->specialty)
				hero->removeBonus(bonus);
			for(int i = 0; i < LIBRARY->skillh->size(); ++i)
				hero->setSecSkillLevel(SecondarySkill(i), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
				PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
		}
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view skillId, std::string_view perkId)
	{
		const std::string skill(skillId);
		const std::string perk(perkId);
		const auto rankLookup = [hero](const std::string & id)
		{
			return hero->getPerkSkillRank(id);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId != skill || offers[choice].selection.perkId != perk)
					continue;
				gameHandler->levelUpHero(hero, offers, choice, seed, false);
				ASSERT_TRUE(hero->hasActivePerk(skill, perk));
				return;
			}
		}
		FAIL() << "The active perk was not legally offered: " << perk;
	}

	void selectBreakthrough()
	{
		const auto decodedOffense = SecondarySkill::decode(OFFENSE);
		ASSERT_GE(decodedOffense, 0);
		const auto offense = SecondarySkill(decodedOffense);
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(attackerSideHero, OFFENSE, "new-horizons:offense.shockAssault");
		attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(attackerSideHero, OFFENSE, BREAKTHROUGH);
	}

	template<typename F>
	auto withoutBreakthrough(F && calculate)
	{
		const auto saved = attackerSideHero->getPerkState().selected;
		auto & mutableState = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		std::erase_if(mutableState.selected, [](const auto & selection)
		{
			return selection.skillId == OFFENSE && selection.perkId == BREAKTHROUGH;
		});
		const auto result = calculate();
		mutableState.selected = saved;
		return result;
	}

	bool defend(CStack * stack)
	{
		battle()->activeStack = stack->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), stack->unitOwner(),
			BattleAction::makeDefend(stack));
	}
};
}

TEST_F(NewHorizonsBreakthroughDefendReductionTest, LegalBreakthroughScalesBattlecraftDefendAndMatchesAttackAndDetachedPreview)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectBreakthrough());
	const auto battlecraft = SecondarySkill(SecondarySkill::decode(BATTLECRAFT));
	defenderSideHero->setSecSkillLevel(battlecraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	acceptPerk(defenderSideHero, BATTLECRAFT, "new-horizons:battlecraft.entrench");
	ASSERT_EQ(newHorizonsBattlecraft::defendReductionPercent(defenderSideHero), 20);

	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1000);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	beginCombat();
	ASSERT_TRUE(defend(defender));
	ASSERT_TRUE(defender->defended());

	const BattleAttackInfo attackInfo(attacker, defender, 0, false);
	// Real Defend also changes Creature Defense, which Breakthrough already
	// pierces. Isolate the new reduction channel without removing that stance.
	defenderSideHero->setSecSkillLevel(battlecraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto beforeReduction = battle()->calculateDmgRange(attackInfo).damage;
	const auto baselineBeforeReduction = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(attackInfo).damage;
	});
	defenderSideHero->setSecSkillLevel(battlecraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto withBreakthrough = battle()->calculateDmgRange(attackInfo).damage;
	const auto baseline = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(attackInfo).damage;
	});
	ASSERT_GT(baseline.max, 0);
	EXPECT_NEAR(withBreakthrough.max, beforeReduction.max * 0.9, 1.0);
	EXPECT_NEAR(baseline.max, baselineBeforeReduction.max * 0.8, 1.0);

	BattleAttackInfo ranged(attacker, defender, 0, true);
	BattleAttackInfo nonPhysical(attacker, defender, 0, false);
	nonPhysical.physicalDamage = false;
	const auto rangedWithBreakthrough = battle()->calculateDmgRange(ranged).damage;
	const auto rangedWithoutBreakthrough = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(ranged).damage;
	});
	EXPECT_EQ(rangedWithBreakthrough.min, rangedWithoutBreakthrough.min);
	EXPECT_EQ(rangedWithBreakthrough.max, rangedWithoutBreakthrough.max);
	const auto nonPhysicalWithBreakthrough = battle()->calculateDmgRange(nonPhysical).damage;
	const auto nonPhysicalWithoutBreakthrough = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(nonPhysical).damage;
	});
	EXPECT_EQ(nonPhysicalWithBreakthrough.min, nonPhysicalWithoutBreakthrough.min);
	EXPECT_EQ(nonPhysicalWithBreakthrough.max, nonPhysicalWithoutBreakthrough.max);

	class ProjectionEnvironment final : public Environment
	{
		std::shared_ptr<CGameState> state;
	public:
		explicit ProjectionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
		const Services * services() const override { return LIBRARY; }
		const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
		const GameCb * game() const override { return state.get(); }
	} environment(gameState());
	// Compare equivalent all-knowing views. A player-scoped callback deliberately
	// hides the enemy hero's skills; that forecast cannot match this full view.
	std::shared_ptr<CBattleInfoCallback> callback(gameState(), battle());
	HypotheticBattle projection(&environment, callback);
	auto projectedAttacker = projection.getForUpdate(attacker->unitId());
	auto projectedDefender = projection.getForUpdate(defender->unitId());
	const auto detached = projection.calculateDmgRange(
		BattleAttackInfo(projectedAttacker.get(), projectedDefender.get(), 0, false)).damage;
	EXPECT_EQ(detached.min, withBreakthrough.min);
	EXPECT_EQ(detached.max, withBreakthrough.max);

	const auto firstPacket = server.attacks.size();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	const auto accepted = std::find_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(firstPacket),
		server.attacks.end(), [attacker, defender](const BattleAttack & packet)
	{
		return packet.stackAttacking == attacker->unitId() && !packet.counter()
			&& std::ranges::any_of(packet.bsa, [defender](const BattleStackAttacked & hit)
			{
				return hit.stackAttacked == defender->unitId();
			});
		});
	ASSERT_NE(accepted, server.attacks.end());
	const auto hit = std::find_if(accepted->bsa.begin(), accepted->bsa.end(), [defender](const BattleStackAttacked & item)
	{
		return item.stackAttacked == defender->unitId();
	});
	ASSERT_NE(hit, accepted->bsa.end());
	EXPECT_EQ(hit->damageAmount, withBreakthrough.max);
}

TEST_F(NewHorizonsBreakthroughDefendReductionTest, BreakthroughHalvesCombinedFractionalBulwarkDefendFields)
{
	startGameWithFortressDefender();
	ASSERT_NO_FATAL_FAILURE(selectBreakthrough());
	const auto battlecraft = SecondarySkill(SecondarySkill::decode(BATTLECRAFT));
	defenderSideHero->setSecSkillLevel(battlecraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	acceptPerk(defenderSideHero, BATTLECRAFT, "new-horizons:battlecraft.entrench");
	const auto bulwark = SecondarySkill(SecondarySkill::decode(BULWARK));
	defenderSideHero->setSecSkillLevel(bulwark, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	acceptPerk(defenderSideHero, BULWARK, "new-horizons:bulwarkOfTheMire.mireborn");
	defenderSideHero->setSecSkillLevel(bulwark, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	acceptPerk(defenderSideHero, BULWARK, "new-horizons:bulwarkOfTheMire.sharedCover");
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 37, ChangeValueMode::ABSOLUTE);
	gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = TerrainId::SWAMP;

	startBattle();
	ASSERT_EQ(battle()->getTerrainType(), TerrainId::SWAMP);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 1000);
	auto * cover = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1000);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 1000);
	forceMaximumDamage(attacker);
	cover->defending = true;
	cover->bulwarkPreemptiveUsed = true;
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const int battlecraftPercent = newHorizonsBattlecraft::defendReductionPercent(defenderSideHero);
	const int bulwarkBase = newHorizonsBulwark::reductionBasisPoints(MasteryLevel::ADVANCED, 37, true);
	const int bulwarkCombined = std::min(10000,
		bulwarkBase + newHorizonsBulwark::sharedCoverBasisPoints(bulwarkBase));
	ASSERT_EQ(battlecraftPercent, 20);
	ASSERT_EQ(bulwarkBase, 1805);
	ASSERT_EQ(bulwarkCombined, 2707);

	const BattleAttackInfo attackInfo(attacker, defender, 0, false);
	const auto withBreakthrough = battle()->calculateDmgRange(attackInfo).damage;
	const auto baseline = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(attackInfo).damage;
	});
	ASSERT_GT(baseline.max, 0);
	const double expectedRatio = ((1.0 - battlecraftPercent * 0.5 / 100.0)
		* (1.0 - bulwarkCombined * 0.5 / 10000.0))
		/ ((1.0 - battlecraftPercent / 100.0) * (1.0 - bulwarkCombined / 10000.0));
	EXPECT_NEAR(static_cast<double>(withBreakthrough.max) / baseline.max, expectedRatio, 0.01);

	// A capped full-field value remains a half-field value under Breakthrough;
	// the global reduction cap is applied after that scaling.
	defenderSideHero->setSecSkillLevel(battlecraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 500, ChangeValueMode::ABSOLUTE);
	const auto capWithBreakthrough = battle()->calculateDmgRange(attackInfo).damage.max;
	const auto capWithoutBreakthrough = withoutBreakthrough([&]
	{
		return battle()->calculateDmgRange(attackInfo).damage.max;
	});
	ASSERT_GT(capWithoutBreakthrough, 0);
	EXPECT_NEAR(static_cast<double>(capWithBreakthrough) / capWithoutBreakthrough, 2.5, 0.02);
}
