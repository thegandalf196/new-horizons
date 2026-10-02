/*
 * NewHorizonsMineLayerTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto mineLayerPerk = "new-horizons:havocMagic.mineLayer";
constexpr int mineLayerRequiredRank = static_cast<int>(MasteryLevel::ADVANCED);
constexpr int pyromancerRequiredRank = static_cast<int>(MasteryLevel::BASIC);

bool activateMineLayerInFixture(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == mineLayerPerk;
	});
	if(found == perks.end())
		return false;

	// This test-local saved profile opts the row in independently of the shipped
	// activation state.
	(*found)["effect"]["status"].String() = "active";
	return true;
}

class MineLayerEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MineLayerEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMineLayerTest : public HeroCommandFixture
{
protected:
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
		if(!activateMineLayerInFixture(perkRules))
			throw std::runtime_error("Mine Layer is missing from the Havoc Magic perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill havocSkill() const
	{
		const auto decoded = SecondarySkill::decode(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId, int requiredRank)
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
				return candidate.selection.skillId == newHorizonsMagic::HAVOC_MAGIC_SKILL
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			ASSERT_EQ(selected->requiredRank, requiredRank);
			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL),
				std::string(perkId)));
			return;
		}

		FAIL() << "Perk was not available in a legal Havoc Magic offer: " << perkId;
	}

	void selectMineLayer(CGHeroInstance * hero)
	{
		const auto skill = havocSkill();
		ASSERT_TRUE(skill.hasValue());
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, newHorizonsMagic::HAVOC_PYROMANCER, pyromancerRequiredRank);

		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(newHorizonsMagic::HAVOC_MAGIC_SKILL,
			static_cast<int>(MasteryLevel::BASIC)));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getSecSkillLevel(skill), MasteryLevel::ADVANCED);
		acceptPerkThroughOffer(hero, mineLayerPerk, mineLayerRequiredRank);
		ASSERT_TRUE(hero->getPerkState().hasSelection(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL),
			std::string(mineLayerPerk)));
	}

	void prepareLandMine(int32_t spellPower = 0)
	{
		startGame();
		selectMineLayer(attackerSideHero);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		beginCombat();
	}

	BattleAction landMineAction(std::initializer_list<BattleHex> hexes) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::LAND_MINE;
		for(const auto & hex : hexes)
			action.aimToHex(hex);
		return action;
	}
};
}

TEST_F(NewHorizonsMineLayerTest, PyromancerUnlocksLegalAdvancedMineLayerOfferInSavedProfile)
{
	startGame();
	const auto skill = havocSkill();
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	acceptPerkThroughOffer(attackerSideHero, newHorizonsMagic::HAVOC_PYROMANCER, pyromancerRequiredRank);
	EXPECT_TRUE(attackerSideHero->getPerkState().canAdvanceSkillNormally(newHorizonsMagic::HAVOC_MAGIC_SKILL,
		static_cast<int>(MasteryLevel::BASIC)));

	gameHandler->levelUpHero(attackerSideHero, skill, false);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(skill), MasteryLevel::ADVANCED);
	acceptPerkThroughOffer(attackerSideHero, mineLayerPerk, mineLayerRequiredRank);

	EXPECT_TRUE(attackerSideHero->hasActivePerk(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL),
		std::string(mineLayerPerk)));
	const auto savedDefinition = newHorizonsHeroes::perkDefinition(
		attackerSideHero->getPerkState().rules, newHorizonsMagic::HAVOC_MAGIC_SKILL, mineLayerPerk);
	ASSERT_TRUE(savedDefinition.has_value());
	EXPECT_EQ(savedDefinition->effect["status"].String(), "active")
		<< "Only this map's saved perk-rules snapshot opts the Mine Layer row in";
}

TEST_F(NewHorizonsMineLayerTest, MechanicsAndDetachedAITargetsAddOneAfterBaseThresholdAndCap)
{
	prepareLandMine();
	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	ASSERT_NE(spell, nullptr);
	const std::array<std::pair<int32_t, int32_t>, 4> cases{{
		{0, 3}, {100, 4}, {200, 5}, {1'000'000, 5}
	}};
	for(const auto [spellPower, expectedCount] : cases)
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		cast.setEffectPower(spellPower);
		auto mechanics = spell->battleMechanics(&cast);
		ASSERT_NE(mechanics, nullptr);
		const auto baseCount = newHorizonsMagic::landMineHexCount(
			spellPower, mechanics->getSpellPowerCoefficientBasisPoints());
		EXPECT_EQ(mechanics->getNewHorizonsLandMinePatchCount(), baseCount + 1);
		EXPECT_EQ(mechanics->getNewHorizonsLandMinePatchCount(), expectedCount);
	}

	// The AI candidate generator runs against a detached battle projection and
	// must emit the same exact placement count as the live Mechanics accessor.
	const int32_t candidatePower = 200;
	auto environment = std::make_shared<MineLayerEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(environment.get(), callback);
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, spell);
	projectedCast.setEffectPower(candidatePower);
	auto projectedMechanics = spell->battleMechanics(&projectedCast);
	ASSERT_NE(projectedMechanics, nullptr);
	ASSERT_EQ(projectedMechanics->getNewHorizonsLandMinePatchCount(), 5);
	const auto targets = SpellTargetEvaluator::getViableTargets(projectedMechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_EQ(targets.front().size(), 5u);
	EXPECT_TRUE(projectedMechanics->canBeCastAt(targets.front()));
}

TEST_F(NewHorizonsMineLayerTest, ServerRejectsBaseCountThenPlacesAndSnapshotsTheBonusMine)
{
	prepareLandMine();
	const auto * spell = SpellID(SpellID::LAND_MINE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setEffectPower(0);
	auto mechanics = spell->battleMechanics(&cast);
	ASSERT_NE(mechanics, nullptr);
	ASSERT_EQ(mechanics->getNewHorizonsLandMinePatchCount(), 3);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		landMineAction({BattleHex(70), BattleHex(71)})))
		<< "The old base count is invalid when Mine Layer is active";
	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);

	const auto candidates = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(candidates.size(), 1u);
	ASSERT_EQ(candidates.front().size(), 3u);
	BattleAction accepted = landMineAction({});
	for(const auto & destination : candidates.front())
		accepted.aimToHex(destination.hexValue);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), accepted));
	ASSERT_EQ(battle()->obstacles.size(), 3u);
	for(const auto & obstacle : battle()->obstacles)
	{
		const auto * mine = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		ASSERT_NE(mine, nullptr);
		EXPECT_EQ(mine->ID, SpellID::LAND_MINE);
		EXPECT_EQ(mine->minimalDamage, 60);
		EXPECT_TRUE(mine->damageSnapshot);
		EXPECT_TRUE(mine->hidden);
	}
}
