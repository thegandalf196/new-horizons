/*
 * NewHorizonsEspritDeCorpsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../client/battle/NewHorizonsBattleStatus.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsDiscipline.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/modding/CModHandler.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
class EspritEnvironment final : public ::Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit EspritEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsEspritDeCorpsTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate native New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][newHorizonsDiscipline::SKILL]["perks"].Vector();
		const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
		{
			return perk["id"].String() == newHorizonsDiscipline::ESPRIT_DE_CORPS;
		});
		if(found == perks.end())
			throw std::runtime_error("Missing Esprit de Corps registry entry");
		(*found)["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void basicDiscipline(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(newHorizonsDiscipline::SKILL);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}

	void selectEsprit(CGHeroInstance * hero)
	{
		basicDiscipline(hero);
		const auto rankLookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [](const auto & offer)
			{
				return offer.selection.skillId == newHorizonsDiscipline::SKILL
					&& offer.selection.perkId == newHorizonsDiscipline::ESPRIT_DE_CORPS;
			});
			if(selected == offers.end())
				continue;
			gameHandler->levelUpHero(hero, offers, static_cast<size_t>(std::distance(offers.begin(), selected)), seed, false);
			ASSERT_TRUE(newHorizonsDiscipline::hasEspritDeCorps(hero));
			return;
		}
		FAIL() << "Esprit never appeared in a legal Basic offer";
	}

	void setArmy(CGHeroInstance * hero, const std::vector<std::string> & creatures)
	{
		hero->clearSlots();
		for(size_t index = 0; index < creatures.size(); ++index)
			ASSERT_TRUE(hero->setCreature(SlotID(static_cast<int>(index)), creatureByName(creatures[index]), 1));
		hero->updateMoraleBonusFromArmy();
	}

	void mixedArmy(CGHeroInstance * hero)
	{
		setArmy(hero, {"core:pikeman", "core:centaur", "core:goblin", "core:skeleton"});
	}

	CStack * livingUnit(BattleSide side)
	{
		for(auto * stack : battle()->battleGetAllStacks())
			if(stack->unitSide() == side && stack->unitType()->getId() == creatureByName("core:pikeman"))
				return const_cast<CStack *>(stack);
		return nullptr;
	}

	std::shared_ptr<Bonus> addMorale(CBonusSystemNode & target, BonusSource source, int value, bool enemy = false)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE, source, value, BonusSourceID());
		bonus->appliedByEnemy = enemy;
		target.addNewBonus(bonus);
		return bonus;
	}
};
}

TEST_F(NewHorizonsEspritDeCorpsTest, ActualMixedFactionAndUndeadContributionsEachLoseOnePoint)
{
	startGame();
	basicDiscipline(attackerSideHero);
	mixedArmy(attackerSideHero);
	const auto original = attackerSideHero->getBonusesOfType(BonusType::MORALE);
	std::vector<std::pair<const Bonus *, int>> negativeArmy;
	for(const auto & bonus : *original)
		if(bonus->source == BonusSource::ARMY && bonus->val < 0)
			negativeArmy.emplace_back(bonus.get(), bonus->val);
	ASSERT_EQ(negativeArmy.size(), 2);
	EXPECT_EQ(attackerSideHero->moraleVal(), -2); // Basic +1, mixed -2, Undead -1.
	selectEsprit(attackerSideHero);
	EXPECT_EQ(attackerSideHero->moraleVal(), 0);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(0))->moraleVal(), 0);
	EXPECT_EQ(attackerSideHero->getStackPtr(SlotID(3))->moraleVal(), 0) << "Undead remain Morale immune";
	TConstBonusListPtr help;
	EXPECT_EQ(attackerSideHero->moraleValAndBonusList(help), 0);
	ASSERT_NE(help, nullptr);
	EXPECT_EQ(help->totalValue(), 0);
	for(const auto & [bonus, value] : negativeArmy)
		EXPECT_EQ(bonus->val, value) << "Readback never edits the exported composition producer";
}

TEST_F(NewHorizonsEspritDeCorpsTest, PositiveAndZeroArmyCompositionDoNotGainAdditionalMorale)
{
	startGame();
	selectEsprit(attackerSideHero);
	setArmy(attackerSideHero, {"core:pikeman"});
	EXPECT_EQ(attackerSideHero->moraleVal(), 2);
	EXPECT_EQ(newHorizonsDiscipline::espritDeCorpsMoraleAdjustment(attackerSideHero, *attackerSideHero), 0);
	setArmy(attackerSideHero, {"core:pikeman", "core:centaur"});
	EXPECT_EQ(attackerSideHero->moraleVal(), 1);
	EXPECT_EQ(newHorizonsDiscipline::espritDeCorpsMoraleAdjustment(attackerSideHero, *attackerSideHero), 0);
}

TEST_F(NewHorizonsEspritDeCorpsTest, LiveBattleMatchesAdventureWithoutDoubleApplyingCompositionRecovery)
{
	startGame();
	selectEsprit(attackerSideHero);
	mixedArmy(attackerSideHero);
	startBattle();
	auto * unit = livingUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	EXPECT_EQ(unit->moraleVal(), -2) << "Battle units retain raw contributions for the shared callback";
	const auto info = battle()->battleGetMoraleInfo(unit);
	EXPECT_EQ(info.espritDeCorpsAdjustment, 2);
	EXPECT_EQ(info.steadfastAdjustment, 0);
	EXPECT_EQ(info.real, 0);
	EXPECT_EQ(info.effective, attackerSideHero->moraleVal());
}

TEST_F(NewHorizonsEspritDeCorpsTest, EnemyAndNonArmyPenaltiesRemainUnchanged)
{
	startGame();
	selectEsprit(attackerSideHero);
	mixedArmy(attackerSideHero);
	startBattle();
	auto * unit = livingUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	const auto enemySpell = addMorale(*unit, BonusSource::SPELL_EFFECT, -3, true);
	const auto artifact = addMorale(*unit, BonusSource::ARTIFACT_INSTANCE, -2);
	const auto enemyArmy = addMorale(*unit, BonusSource::ARMY, -1, true);
	EXPECT_EQ(battle()->battleGetMorale(unit), -6);
	EXPECT_EQ(battle()->battleGetMoraleInfo(unit).espritDeCorpsAdjustment, 2);
	EXPECT_EQ(enemySpell->val, -3);
	EXPECT_EQ(artifact->val, -2);
	EXPECT_EQ(enemyArmy->val, -1);
}

TEST_F(NewHorizonsEspritDeCorpsTest, OpposingHeroPerkDoesNotRepairAnUnselectedArmiesComposition)
{
	startGame();
	basicDiscipline(attackerSideHero);
	mixedArmy(attackerSideHero);
	selectEsprit(defenderSideHero);
	startBattle();
	auto * unit = livingUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	EXPECT_EQ(attackerSideHero->moraleVal(), -2);
	EXPECT_EQ(battle()->battleGetMorale(unit), -2);
	EXPECT_EQ(battle()->battleGetMoraleInfo(unit).espritDeCorpsAdjustment, 0);
}

TEST_F(NewHorizonsEspritDeCorpsTest, DetachedProjectionUsesBeneficiaryPerkAndPreservesLiveBonuses)
{
	startGame();
	selectEsprit(attackerSideHero);
	mixedArmy(attackerSideHero);
	startBattle();
	auto * unit = livingUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	const auto penalty = addMorale(*unit, BonusSource::SPELL_EFFECT, -3, true);
	EspritEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = branch->getForUpdate(unit->unitId());
	EXPECT_EQ(branch->battleGetMorale(projected.get()), battle()->battleGetMorale(unit));
	EXPECT_EQ(branch->battleGetMoraleInfo(projected.get()).espritDeCorpsAdjustment, 2);
	EXPECT_EQ(branch->battleGetMorale(projected.get()), -3);
	branch->removeUnitBonus(unit->unitId(), {*penalty});
	EXPECT_EQ(branch->battleGetMorale(projected.get()), 0);
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(unit->unitId()).get()), -3);
	EXPECT_EQ(battle()->battleGetMorale(unit), -3);
	EXPECT_EQ(penalty->val, -3);
}

TEST_F(NewHorizonsEspritDeCorpsTest, ExistingMoraleHelpSnapshotReportsEspritAndSuppressesImmuneSources)
{
	startGame();
	selectEsprit(attackerSideHero);
	mixedArmy(attackerSideHero);
	startBattle();
	auto * unit = livingUnit(BattleSide::ATTACKER);
	ASSERT_NE(unit, nullptr);
	const auto info = battle()->battleGetMoraleInfo(unit);
	const auto readback = newHorizonsBattleStatus::makeBattleMoraleReadback(true, info.real, info.effective,
		info.standardBearerBonus, info.firstRoundModifier, info.steadfastAdjustment,
		info.commandingPresenceFloorApplied, info.furyUnboundFloorApplied, false, {}, info.espritDeCorpsAdjustment);
	EXPECT_TRUE(readback.hasSources());
	EXPECT_EQ(readback.espritDeCorpsAdjustment, 2);
	const auto immune = newHorizonsBattleStatus::makeBattleMoraleReadback(true, info.real, 0,
		0, 0, 0, false, false, true, {}, info.espritDeCorpsAdjustment);
	EXPECT_FALSE(immune.hasSources());
	EXPECT_EQ(immune.espritDeCorpsAdjustment, 0);
}
