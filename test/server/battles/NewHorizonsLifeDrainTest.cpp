/*
 * NewHorizonsLifeDrainTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto shadowMagicSkillId = "new-horizons:shadowMagic";
constexpr auto bloodDrinkerPerkId = "new-horizons:shadowMagic.bloodDrinker";

SpellID lifeDrainSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL)));
}

bool setPerkActive(JsonNode & rules, std::string_view skillId, std::string_view perkId)
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

class NewHorizonsLifeDrainTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	CStack * otherEnemy = nullptr;

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
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, shadowMagicSkillId, bloodDrinkerPerkId))
			throw std::runtime_error("Missing Blood Drinker from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void prepare(int spellPower = 100, const std::string & friendlyCreature = "core:angel",
		int enemyCount = 1000, MasteryLevel::Type shadowMagicRank = MasteryLevel::NONE,
		bool selectBloodDrinker = false)
	{
		startGame();
		const auto spell = lifeDrainSpell();
		ASSERT_NE(spell, SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		const int shadowMagicId = SecondarySkill::decode(shadowMagicSkillId);
		ASSERT_GE(shadowMagicId, 0);
		const SecondarySkill shadowMagic(shadowMagicId);
		attackerSideHero->setSecSkillLevel(shadowMagic, shadowMagicRank, ChangeValueMode::ABSOLUTE);
		if(selectBloodDrinker)
			acceptBloodDrinkerThroughOffer(attackerSideHero);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName(friendlyCreature), BattleHex(3, 5), 2);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), enemyCount);
		otherEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 1000);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(otherEnemy, nullptr);
		beginCombat();
	}

	void acceptBloodDrinkerThroughOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == shadowMagicSkillId
					&& candidate.selection.perkId == bloodDrinkerPerkId;
			});
			if(selected == offer.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(MasteryLevel::BASIC));
			const auto choice = static_cast<std::size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(shadowMagicSkillId, bloodDrinkerPerkId));
			return;
		}

		FAIL() << bloodDrinkerPerkId << " never appeared in a legal Basic Shadow Magic perk offer";
	}

	void injure(const CStack * stack, int64_t amount)
	{
		auto state = stack->acquireState();
		state->damage(amount);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -amount;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	BattleAction action(const CStack * first, const CStack * second) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = lifeDrainSpell();
		if(first)
			result.aimToUnit(first);
		if(second)
			result.aimToUnit(second);
		return result;
	}

	bool cast(const CStack * first, const CStack * second) const
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(first, second));
	}
};
}

TEST_F(NewHorizonsLifeDrainTest, CanonicalDamageAndHealingUseActualInflictedHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
	EXPECT_FALSE(attackerSideHero->hasActivePerk(shadowMagicSkillId, bloodDrinkerPerkId));
	EXPECT_EQ(lifeDrainSpell().toSpell()->battleMechanics(&spellCast)->getEffectValue(), 205);
	ASSERT_GT(friendly->getMaxHealth(), 150);
	injure(friendly, 150);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	const auto countBefore = friendly->getCount();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	spells::Target targetPair;
	targetPair.emplace_back(enemy, enemy->getPosition());
	targetPair.emplace_back(friendly, friendly->getPosition());
	spells::detail::ProblemImpl problem;
	auto mechanics = lifeDrainSpell().toSpell()->battleMechanics(&spellCast);
	const bool available = mechanics->canBeCast(problem);
	std::vector<std::string> problems;
	problem.getAll(problems);
	EXPECT_TRUE(available) << ::testing::PrintToString(problems);
	EXPECT_TRUE(mechanics->isReceptive(enemy));
	EXPECT_TRUE(mechanics->isReceptive(friendly));
	EXPECT_TRUE(mechanics->canBeCastAt(targetPair, problem));
	EXPECT_EQ(mechanics->getAffectedStacks(targetPair).size(), 2);

	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 205);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, 123);
	EXPECT_EQ(friendly->getCount(), countBefore);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 5);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Life Drain restores 123 health") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsLifeDrainTest, BasicShadowMagicUnselectedBloodDrinkerKeepsSixtyPercentHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1000, MasteryLevel::BASIC));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(shadowMagicSkillId, bloodDrinkerPerkId));
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
	EXPECT_EQ(lifeDrainSpell().toSpell()->battleMechanics(&spellCast)->getEffectValue(), 232);
	injure(friendly, 150);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_EQ(actualDamage, 232);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 60 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, AcceptedBasicBloodDrinkerRaisesHealingToSeventyFivePercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1000, MasteryLevel::BASIC, true));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowMagicSkillId, bloodDrinkerPerkId));
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
	EXPECT_EQ(lifeDrainSpell().toSpell()->battleMechanics(&spellCast)->getEffectValue(), 232)
		<< "Basic Shadow Magic's 115% coefficient is shared with the unselected comparison";
	injure(friendly, 180);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_EQ(actualDamage, 232);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 75 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, ExactEnemyThenFriendPairIsValidatedBeforeSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto actionsBefore = server.startedActions.size();
	const auto healthBefore = enemy->getAvailableHealth();

	EXPECT_FALSE(cast(enemy, nullptr));
	EXPECT_FALSE(cast(friendly, enemy));
	EXPECT_FALSE(cast(enemy, otherEnemy));
	EXPECT_FALSE(cast(friendly, friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), healthBefore);
	EXPECT_TRUE(server.castsOf(lifeDrainSpell()).empty());
}

TEST_F(NewHorizonsLifeDrainTest, AFullHealthAllyIsStillALegalRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(enemyBefore - enemy->getAvailableHealth(), 205);
	EXPECT_EQ(friendly->getAvailableHealth(), allyBefore);
}

TEST_F(NewHorizonsLifeDrainTest, HealingRepairsOnlySurvivorsAndNeverRestoresDeadCreatures)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maxHealth = friendly->getMaxHealth();
	injure(friendly, maxHealth + 20);
	ASSERT_EQ(friendly->getCount(), 1);
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(friendly->getCount(), 1);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, 20);
}

TEST_F(NewHorizonsLifeDrainTest, OverkillCannotBeConvertedIntoExtraHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1));
	injure(friendly, 150);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_LT(actualDamage, 205);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 60 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, DamageReductionAlsoReducesDependentHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	injure(friendly, 150);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_LT(actualDamage, 205);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 60 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, UndeadFriendlyStacksCanReceiveHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:skeleton"));
	injure(friendly, 1);
	const auto countBefore = friendly->getCount();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(friendly->getCount(), countBefore);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, 1);
}

TEST_F(NewHorizonsLifeDrainTest, BloodDrinkerUsesOnlyActualDamageAfterOverkill)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1, MasteryLevel::BASIC, true));
	injure(friendly, 150);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_GT(actualDamage, 0);
	EXPECT_LT(actualDamage, 232);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 75 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, BloodDrinkerUsesDamageAfterMagicalReduction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1000, MasteryLevel::BASIC, true));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	injure(friendly, 180);
	const auto enemyBefore = enemy->getAvailableHealth();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	const auto actualDamage = enemyBefore - enemy->getAvailableHealth();
	EXPECT_GT(actualDamage, 0);
	EXPECT_LT(actualDamage, 232);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, actualDamage * 75 / 100);
}

TEST_F(NewHorizonsLifeDrainTest, BloodDrinkerRepairsOnlySurvivorsAndNeverResurrects)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1000, MasteryLevel::BASIC, true));
	const auto maxHealth = friendly->getMaxHealth();
	injure(friendly, maxHealth + 20);
	ASSERT_EQ(friendly->getCount(), 1);
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(friendly->getCount(), 1);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, 20);
}

TEST_F(NewHorizonsLifeDrainTest, NonLivingFriendlyStacksCanReceiveHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:stoneGargoyle"));
	injure(friendly, 1);
	const auto countBefore = friendly->getCount();
	const auto allyBefore = friendly->getAvailableHealth();
	ASSERT_TRUE(cast(enemy, friendly));
	EXPECT_EQ(friendly->getCount(), countBefore);
	EXPECT_EQ(friendly->getAvailableHealth() - allyBefore, 1);
}
