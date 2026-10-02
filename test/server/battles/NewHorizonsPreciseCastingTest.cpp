/*
 * NewHorizonsPreciseCastingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../SpellPointTestUtils.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr auto spellcraftSkillId = "new-horizons:spellcraft";
constexpr auto arcaneFocusPerkId = "new-horizons:spellcraft.arcaneFocus";
constexpr auto preciseCastingPerkId = "new-horizons:spellcraft.preciseCasting";

SpellID spellNamed(const std::string & name)
{
	return SpellID(SpellID::decode(name));
}

bool setPerkStatus(JsonNode & rules, std::string_view perkId, std::string_view status)
{
	auto & perks = rules["skills"][spellcraftSkillId]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = status;
	return true;
}

class PreciseCastingPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PreciseCastingPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsPreciseCastingTest : public HeroCommandFixture
{
protected:
	const CSpell * spell = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		// This registry entry is still planned globally; activate only the saved-profile test fixture.
		if(!setPerkStatus(perkRules, preciseCastingPerkId, "active"))
			throw std::runtime_error("Missing Precise Casting from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill spellcraft() const
	{
		const int decoded = SecondarySkill::decode(spellcraftSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId,
		MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [perkId](const auto & candidate)
			{
				return candidate.selection.skillId == spellcraftSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(requiredRank));
			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(std::string(spellcraftSkillId), std::string(perkId)));
			return;
		}

		FAIL() << perkId << " never appeared in a legal Spellcraft perk offer";
	}

	void prepare(std::string_view spellName, bool selectPreciseCasting = true)
	{
		startGame();

		spell = spellNamed(std::string(spellName)).toSpell();
		ASSERT_NE(spell, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(spell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		const auto spellcraftSkill = spellcraft();
		ASSERT_TRUE(spellcraftSkill.hasValue());
		attackerSideHero->setSecSkillLevel(spellcraftSkill, MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(attackerSideHero, arcaneFocusPerkId, MasteryLevel::BASIC);
		attackerSideHero->setSecSkillLevel(spellcraftSkill, MasteryLevel::ADVANCED,
			ChangeValueMode::ABSOLUTE);
		if(selectPreciseCasting)
			acceptPerkThroughOffer(attackerSideHero, preciseCastingPerkId, MasteryLevel::ADVANCED);

		startBattle();
		removeDeployedUnits();
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

	void beginScenarioCombat()
	{
		beginCombat();
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), attackerSideHero->getOwner());
	}

	std::vector<BattleHex> adjacentHexesOutsideCenter(const CStack * center, const BattleHex & aimHex) const
	{
		std::vector<BattleHex> result;
		for(const auto hex : BattleHexArray::getNeighbouringTiles(aimHex))
		{
			if(center->getHexes().contains(hex))
				continue;
			result.push_back(hex);
			if(result.size() == 2)
				break;
		}
		return result;
	}

	std::map<uint32_t, int64_t> detachedDamageAt(const BattleHex & aimHex)
	{
		const auto * liveBattle = battle();
		auto callback = std::make_shared<CPlayerBattleCallback>(liveBattle, attackerSideHero->getOwner());
		PreciseCastingPredictionEnvironment environment(gameState());
		HypotheticBattle predicted(&environment, callback);
		spells::BattleCast cast(&predicted, attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		spells::detail::ProblemImpl problem;
		const spells::Target aim{spells::Destination(aimHex)};
		EXPECT_TRUE(mechanics->canBeCast(problem));
		EXPECT_TRUE(mechanics->canBeCastAt(aim, problem));
		mechanics->castEval(predicted.getServerCallback(), aim);

		std::map<uint32_t, int64_t> damage;
		for(const auto * original : liveBattle->battleGetAllStacks())
		{
			const auto * projected = predicted.battleGetUnitByID(original->unitId());
			if(projected)
				damage.emplace(original->unitId(),
					original->getAvailableHealth() - projected->getAvailableHealth());
		}
		return damage;
	}

	bool castAtHex(const BattleHex & aimHex)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		action.aimToHex(aimHex);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	bool castGlobalSpell()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell->getId();
		action.stackNumber = -1;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}
};
}

TEST_F(NewHorizonsPreciseCastingTest, AcceptedFireballExcludesOnlyItsFriendlyCenterInLiveAndDetachedBattle)
{
	prepare("core:fireball");
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(spellcraftSkillId),
		std::string(preciseCastingPerkId)));

	const BattleHex centerHex(8, 5);
	auto * center = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), centerHex, 1000);
	ASSERT_NE(center, nullptr);
	const auto adjacent = adjacentHexesOutsideCenter(center, centerHex);
	ASSERT_EQ(adjacent.size(), 2u);
	auto * adjacentAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), adjacent[0], 1000);
	auto * adjacentEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), adjacent[1], 1000);
	auto * distant = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 0), 1000);
	ASSERT_NE(adjacentAlly, nullptr);
	ASSERT_NE(adjacentEnemy, nullptr);
	ASSERT_NE(distant, nullptr);
	beginScenarioCombat();

	const auto centerBefore = center->getAvailableHealth();
	const auto allyBefore = adjacentAlly->getAvailableHealth();
	const auto enemyBefore = adjacentEnemy->getAvailableHealth();
	const auto distantBefore = distant->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto detached = detachedDamageAt(centerHex);
	ASSERT_TRUE(detached.contains(center->unitId()));
	ASSERT_TRUE(detached.contains(adjacentAlly->unitId()));
	ASSERT_TRUE(detached.contains(adjacentEnemy->unitId()));
	ASSERT_TRUE(detached.contains(distant->unitId()));
	EXPECT_EQ(detached.at(center->unitId()), 0);
	EXPECT_EQ(detached.at(adjacentAlly->unitId()), 25);
	EXPECT_EQ(detached.at(adjacentEnemy->unitId()), 25);
	EXPECT_EQ(detached.at(distant->unitId()), 0);
	EXPECT_EQ(center->getAvailableHealth(), centerBefore);
	EXPECT_EQ(adjacentAlly->getAvailableHealth(), allyBefore);
	EXPECT_EQ(adjacentEnemy->getAvailableHealth(), enemyBefore);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	ASSERT_TRUE(castAtHex(centerHex));
	EXPECT_EQ(centerBefore - center->getAvailableHealth(), 0);
	EXPECT_EQ(allyBefore - adjacentAlly->getAvailableHealth(), 25);
	EXPECT_EQ(enemyBefore - adjacentEnemy->getAvailableHealth(), 25);
	EXPECT_EQ(distant->getAvailableHealth(), distantBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - attackerSideHero->getSpellCost(spell));
}

TEST_F(NewHorizonsPreciseCastingTest, ActivePreciseCastingHasNoEffectUntilTheAdvancedPerkIsSelected)
{
	prepare("core:fireball", false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(std::string(spellcraftSkillId),
		std::string(preciseCastingPerkId)));
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(spellcraft()), MasteryLevel::ADVANCED);

	const BattleHex centerHex(8, 5);
	auto * center = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), centerHex, 1000);
	ASSERT_NE(center, nullptr);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 4), 1000);
	ASSERT_NE(enemy, nullptr);
	beginScenarioCombat();
	const auto centerBefore = center->getAvailableHealth();
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto detached = detachedDamageAt(centerHex);
	ASSERT_TRUE(detached.contains(center->unitId()));
	ASSERT_TRUE(detached.contains(enemy->unitId()));
	EXPECT_EQ(detached.at(center->unitId()), 25);
	EXPECT_EQ(detached.at(enemy->unitId()), 25);
	ASSERT_TRUE(castAtHex(centerHex));
	EXPECT_EQ(centerBefore - center->getAvailableHealth(), 25);
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 25);
}

TEST_F(NewHorizonsPreciseCastingTest, HypnotizedDoubleWideCenterUsesCurrentControl)
{
	prepare("core:meteorShower");
	const BattleHex deploymentHex(rightHex);
	auto * center = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), deploymentHex, 1000);
	ASSERT_NE(center, nullptr);
	center->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(center->doubleWide());
	ASSERT_EQ(center->getHexes().size(), 2u);
	ASSERT_EQ(battle()->battleGetOwner(center), attackerSideHero->getOwner())
		<< "Precise Casting treats a stack currently controlled by the caster as friendly";
	const BattleHex aimHex = center->getHexes()[1];
	const auto adjacent = adjacentHexesOutsideCenter(center, aimHex);
	ASSERT_EQ(adjacent.size(), 2u);
	auto * adjacentAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), adjacent[0], 1000);
	auto * adjacentEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), adjacent[1], 1000);
	ASSERT_NE(adjacentAlly, nullptr);
	ASSERT_NE(adjacentEnemy, nullptr);
	beginScenarioCombat();

	const auto centerBefore = center->getAvailableHealth();
	const auto allyBefore = adjacentAlly->getAvailableHealth();
	const auto enemyBefore = adjacentEnemy->getAvailableHealth();
	const auto detached = detachedDamageAt(aimHex);
	ASSERT_TRUE(detached.contains(center->unitId()));
	ASSERT_TRUE(detached.contains(adjacentAlly->unitId()));
	ASSERT_TRUE(detached.contains(adjacentEnemy->unitId()));
	EXPECT_EQ(detached.at(center->unitId()), 0);
	EXPECT_EQ(detached.at(adjacentAlly->unitId()), 110);
	EXPECT_EQ(detached.at(adjacentEnemy->unitId()), 110);
	ASSERT_TRUE(castAtHex(aimHex));
	EXPECT_EQ(centerBefore - center->getAvailableHealth(), 0);
	EXPECT_EQ(allyBefore - adjacentAlly->getAvailableHealth(), 110);
	EXPECT_EQ(enemyBefore - adjacentEnemy->getAvailableHealth(), 110);
}

TEST_F(NewHorizonsPreciseCastingTest, ArmageddonRemainsIndiscriminateWithPreciseCastingSelected)
{
	prepare("core:armageddon");
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(spellcraftSkillId),
		std::string(preciseCastingPerkId)));
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 7), 1000);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 1000);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginScenarioCombat();

	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(castGlobalSpell());
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(), 150);
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 150);
}
