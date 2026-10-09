/*
 * NewHorizonsHandOfFateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the License available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/effects/Effects.h"
#include <vcmi/Environment.h>

namespace
{
constexpr auto handOfFateKey = "new-horizons:handOfFate";
constexpr auto chaosMagicKey = "new-horizons:chaosMagic";
constexpr auto fateDealerKey = "new-horizons:chaosMagic.fateDealer";

SpellID handOfFateSpell()
{
	return SpellID(SpellID::decode(handOfFateKey));
}

class HandOfFatePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HandOfFatePredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsHandOfFateTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * primary = nullptr;
	CStack * enemyCollateral = nullptr;
	const CSpell * spell = nullptr;
	bool enableFateDealer = false;
	bool selectFateDealer = true;

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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		const auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		if(enableFateDealer)
		{
			const auto & choices = perks["skills"][chaosMagicKey]["perks"].Vector();
			const auto found = std::find_if(choices.begin(), choices.end(), [](const JsonNode & perk)
			{
				return perk["id"].String() == fateDealerKey;
			});
			ASSERT_NE(found, choices.end());
			ASSERT_EQ((*found)["effect"]["status"].String(), "active")
				<< "Principal Fate Dealer cases must use the shipped active registry unchanged";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepare(int32_t spellPower = 100, int32_t primaryCount = 1000, bool addEnemyCollateral = false)
	{
		startGame();
		if(enableFateDealer)
		{
			const auto chaos = SecondarySkill(SecondarySkill::decode(chaosMagicKey));
			attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({chaosMagicKey, "new-horizons:chaosMagic.misfortuneWeaver"});
			attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			if(selectFateDealer)
				attackerSideHero->applyPerkSelection({chaosMagicKey, fateDealerKey});
			ASSERT_EQ(attackerSideHero->hasActivePerk(chaosMagicKey, fateDealerKey), selectFateDealer);
		}
		spell = handOfFateSpell().toSpell();
		ASSERT_NE(spell, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell->getId());
		const auto powerDivisor = attackerSideHero->getEffectPowerDivisor(spell);
		ASSERT_EQ(powerDivisor, 10);
		// The saved formula's coefficient25 / divisor10 already implements the
		// canonical 2.5 damage per raw Spell Power. Do not rescale the attribute.
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), spellPower);
		const auto formula = newHorizonsMagic::spellDirectDamage(attackerSideHero->getMagicRules(), handOfFateKey);
		ASSERT_TRUE(formula.has_value());
		ASSERT_EQ(formula->powerCoefficient, 25);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), primaryCount);
		if(addEnemyCollateral)
			enemyCollateral = addStack(BattleSide::DEFENDER,
				creatureByName("core:pikeman"), BattleHex(12, 2), 1000);
		beginCombat();
	}

	void damageStack(const CStack * stack, int64_t damage)
	{
		auto state = stack->acquireState();
		state->damage(damage);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -damage;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	BattleAction action() const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = handOfFateSpell();
		result.aimToUnit(primary);
		return result;
	}

	CStack * seededCollateralRecipient(const std::vector<CStack *> & candidates) const
	{
		// Hand of Fate is a negative magical spell. beforeCast consumes one seeded
		// resistance roll for every on-field non-turret stack before the Lua effect
		// draws the uniform collateral index. There are no Spell Locks or mirrors
		// in this fixture, so this mirrors that deterministic draw sequence.
		CRandomGenerator expectedRandom(BattleTestFixture::seed);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			expectedRandom.nextInt(0, 99);
		const auto chosen = expectedRandom.nextInt(1, static_cast<int>(candidates.size())) - 1;
		return candidates.at(static_cast<size_t>(chosen));
	}

	void resetCastRandomSequence()
	{
		gameHandler->randomizer->setSeed(BattleTestFixture::seed);
	}

	int seedForFateDealerDraws(const std::vector<CStack *> & candidates,
		int first, int second, int coin = 0) const
	{
		// Independently reproduce the ordinary beforeCast resistance prefix and
		// the two with-replacement draws; do not infer the result from HP changes.
		for(int seed = 0; seed < 10'000; ++seed)
		{
			CRandomGenerator random(seed);
			for(const auto * unit : battle()->battleGetAllUnits(false))
				random.nextInt(0, 99);
			const auto drawnFirst = random.nextInt(1, static_cast<int>(candidates.size())) - 1;
			const auto drawnSecond = random.nextInt(1, static_cast<int>(candidates.size())) - 1;
			if(drawnFirst != first || drawnSecond != second)
				continue;
			if(coin != 0 && random.nextInt(1, 2) != coin)
				continue;
			return seed;
		}
		ADD_FAILURE() << "No seed found for requested Fate Dealer draw sequence";
		return -1;
	}

	void castFateDealerWithDraws(const std::vector<CStack *> & candidates,
		int first, int second, int coin = 0)
	{
		const auto seed = seedForFateDealerDraws(candidates, first, second, coin);
		ASSERT_GE(seed, 0);
		gameHandler->randomizer->setSeed(seed);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), action()));
	}

	int64_t primaryForecast() const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		spells::Target aim;
		aim.emplace_back(primary);
		const auto spellTarget = mechanics->canonicalizeTarget(aim);
		spells::effects::SpellEffectValue forecast;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			const auto affected = effect.transformTarget(mechanics.get(), aim, spellTarget);
			forecast += effect.getHealthChange(mechanics.get(), affected);
			return false;
		});
		return -forecast.hpDelta;
	}
};

TEST_F(NewHorizonsHandOfFateTest, SavedFormulaScalesOnlySpellPowerByChaosAndSpellcraft)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), handOfFateSpell()));

	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&baseCast)->getEffectValue(), 320)
		<< "70 + 2.5 x 100 Spell Power";

	const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	ASSERT_TRUE(chaos.hasValue());
	ASSERT_TRUE(spellcraft.hasValue());
	attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	spells::BattleCast rankedCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto rankedMechanics = spell->battleMechanics(&rankedCast);
	EXPECT_EQ(rankedMechanics->getSpellPowerCoefficientBasisPoints(), 18'850);
	EXPECT_EQ(rankedMechanics->getEffectValue(), 541)
		<< "The fixed 70 damage remains unchanged; 2.5 x 100 is scaled by 145% School x 130% Spellcraft";
}

TEST_F(NewHorizonsHandOfFateTest, OverkillUsesActualPrimaryHealthLossForFriendlyCollateralAndForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 100));
	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell,
		MasteryLevel::NONE).find("Fate Dealer"), std::string::npos);
	damageStack(primary, 830);
	ASSERT_EQ(primary->getAvailableHealth(), 170);
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	EXPECT_EQ(primaryForecast(), 170)
		<< "The shared visible damage forecast covers the chosen primary target; the random spill is intentionally not predicted";

	resetCastRandomSequence();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), action()));

	EXPECT_EQ(primaryBefore - primary->getAvailableHealth(), 170);
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(), 85)
		<< "Only the friendly stack survives besides the selected primary, so it receives floor(170 / 2)";
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage, 255);
}

TEST_F(NewHorizonsHandOfFateTest, EmpoweredCollateralUsesCapturedPenetrationAndCurrentController)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	const auto warcasting = SecondarySkill(SecondarySkill::decode("new-horizons:warcasting"));
	ASSERT_TRUE(warcasting.hasValue());
	attackerSideHero->setSecSkillLevel(warcasting, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.martialChanneling"});
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.combatCasting"});
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	ASSERT_NE(enemyCollateral, nullptr);
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	battle()->getSide(BattleSide::ATTACKER).warcastingState.recordAcceptedAction(
		AlternatingHeroActionState::Action::ORDER, battle()->getRound(), 20);
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&event);
	EXPECT_EQ(mechanics->adjustRecipientDamage(enemyCollateral, 10000), 5750);
	EXPECT_EQ(mechanics->adjustRecipientDamage(friendly, 10000), 5000);
	const auto hypnosis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(hypnosis);
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(*hypnosis));
	ASSERT_EQ(battle()->battleGetOwner(friendly), defenderSideHero->getOwner());
	ASSERT_EQ(battle()->battleGetOwner(enemyCollateral), attackerSideHero->getOwner());
	EXPECT_EQ(mechanics->adjustRecipientDamage(friendly, 10000), 5750);
	EXPECT_EQ(mechanics->adjustRecipientDamage(enemyCollateral, 10000), 5000);
	friendly->removeBonus(hypnosis);
	enemyCollateral->removeBonuses(Selector::type()(BonusType::HYPNOTIZED));
	resetCastRandomSequence();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action()));
	const auto primaryLoss = primaryBefore - primary->getAvailableHealth();
	ASSERT_GT(primaryLoss, 0);
	const auto hostileExpected = spells::calculateMagicalDamageReduction(primaryLoss / 2,
		{50}, std::vector<int>{15}).damageWithPenetration;
	const auto friendlyExpected = spells::calculateMagicalDamageReduction(primaryLoss / 2,
		{50}, 0).damageWithPenetration;
	const auto friendlyLoss = friendlyBefore - friendly->getAvailableHealth();
	const auto enemyLoss = enemyBefore - enemyCollateral->getAvailableHealth();
	EXPECT_TRUE((friendlyLoss == friendlyExpected && enemyLoss == 0)
		|| (friendlyLoss == 0 && enemyLoss == hostileExpected));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).warcastingState.bonusFor(
		AlternatingHeroActionState::Action::SPELL, battle()->getRound()), 0);
}

TEST_F(NewHorizonsHandOfFateTest, UniformMixedSidePoolAppliesOnlyTheChosenRecipientsOwnDefense)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	ASSERT_NE(enemyCollateral, nullptr);
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();

	resetCastRandomSequence();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), action()));

	const auto friendlyLoss = friendlyBefore - friendly->getAvailableHealth();
	const auto enemyLoss = enemyBefore - enemyCollateral->getAvailableHealth();
	EXPECT_TRUE((friendlyLoss == 80 && enemyLoss == 0)
		|| (friendlyLoss == 0 && enemyLoss == 160))
		<< "One friendly or enemy candidate receives half of the 320 primary hit; 50% recipient MDR applies only if that friendly is selected";
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage, 320 + friendlyLoss + enemyLoss);
}

TEST_F(NewHorizonsHandOfFateTest, SelectedSpellImmuneRecipientDoesNotCauseReroll)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	ASSERT_NE(enemyCollateral, nullptr);
	const std::vector<CStack *> candidates{friendly, enemyCollateral};
	CStack * immune = seededCollateralRecipient(candidates);
	CStack * other = immune == friendly ? enemyCollateral : friendly;
	immune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(handOfFateSpell())));
	const auto immuneBefore = immune->getAvailableHealth();
	const auto otherBefore = other->getAvailableHealth();

	resetCastRandomSequence();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), action()));

	EXPECT_EQ(immuneBefore - immune->getAvailableHealth(), 0);
	EXPECT_EQ(otherBefore - other->getAvailableHealth(), 0)
		<< "The seeded recipient is immune; the other stack must not become a replacement target";
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage, 320);
}

TEST_F(NewHorizonsHandOfFateTest, SelectedMagicResistantRecipientDoesNotCauseReroll)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	ASSERT_NE(enemyCollateral, nullptr);
	const std::vector<CStack *> candidates{friendly, enemyCollateral};
	CStack * resistant = seededCollateralRecipient(candidates);
	CStack * other = resistant == friendly ? enemyCollateral : friendly;
	resistant->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto resistantBefore = resistant->getAvailableHealth();
	const auto otherBefore = other->getAvailableHealth();

	resetCastRandomSequence();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), action()));

	EXPECT_EQ(resistantBefore - resistant->getAvailableHealth(), 0);
	EXPECT_EQ(otherBefore - other->getAvailableHealth(), 0)
		<< "The selected stack's already-rolled 100% Magic Resistance prevents spill without a second draw or reroll";
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage, 320);
}

TEST_F(NewHorizonsHandOfFateTest, NoOtherSurvivingStackMeansNoCollateralHit)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000));

	// A valid hero spell cannot normally be submitted after a side has lost its
	// last living stack. Exercise the same authoritative spell effect on a
	// detached server projection with its only non-primary stack dead.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HandOfFatePredictionEnvironment environment(gameState());
	HypotheticBattle predicted(&environment, callback);
	auto projectedFriendly = predicted.getForUpdate(friendly->unitId());
	int64_t lethalDamage = projectedFriendly->getAvailableHealth();
	projectedFriendly->damage(lethalDamage);

	const auto * projectedPrimary = predicted.battleGetUnitByID(primary->unitId());
	ASSERT_NE(projectedPrimary, nullptr);
	const auto primaryBefore = projectedPrimary->getAvailableHealth();
	spells::BattleCast cast(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	spells::Target aim;
	aim.emplace_back(projectedPrimary);
	mechanics->castEval(predicted.getServerCallback(), aim);

	const auto * primaryAfter = predicted.battleGetUnitByID(primary->unitId());
	ASSERT_NE(primaryAfter, nullptr);
	EXPECT_EQ(primaryBefore - primaryAfter->getAvailableHealth(), 320);
	EXPECT_EQ(projectedFriendly->getAvailableHealth(), 0);
	EXPECT_EQ(primary->getAvailableHealth(), 10000)
		<< "Detached no-spill verification must not mutate the authoritative battle";
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerSingletonUsesActualOverkillLossOnlyOnce)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 100));
	const auto help = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell,
		MasteryLevel::ADVANCED);
	EXPECT_NE(help.find("Fate Dealer"), std::string::npos);
	EXPECT_NE(help.find("independently with replacement"), std::string::npos);
	EXPECT_NE(help.find("without a reroll"), std::string::npos);
	damageStack(primary, 830);
	const auto friendlyBefore = friendly->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly}, 0, 0, 2));
	EXPECT_EQ(primary->getAvailableHealth(), 0);
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(), 85);
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage, 255);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerMixedDrawsChooseHostileEvenWhenDrawnSecond)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1));
	const auto primaryLoss = primaryBefore - primary->getAvailableHealth();
	EXPECT_GT(primaryLoss, 0);
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(enemyBefore - enemyCollateral->getAvailableHealth(), primaryLoss / 2);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerDuplicateFriendlyDrawDoesNotForceAnUndrawnHostile)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 0, 2));
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(),
		(primaryBefore - primary->getAvailableHealth()) / 2);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), enemyBefore);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerSameSideDistinctDrawsUseFairCoin)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(enemyCollateral), attackerSideHero->getOwner());
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto secondBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1, 2));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(secondBefore - enemyCollateral->getAvailableHealth(),
		(primaryBefore - primary->getAvailableHealth()) / 2);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerSameSideFairCoinCanChooseFirstDraw)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto secondBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1, 1));
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(),
		(primaryBefore - primary->getAvailableHealth()) / 2);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), secondBefore);
}

TEST_F(NewHorizonsHandOfFateTest, ActiveButUnselectedFateDealerKeepsSingleUniformDraw)
{
	enableFateDealer = true;
	selectFateDealer = false;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	// A selected dealer would prefer the second, hostile draw. An unselected
	// dealer must retain the first uniform draw and never inspect that second.
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1));
	EXPECT_EQ(friendlyBefore - friendly->getAvailableHealth(),
		(primaryBefore - primary->getAvailableHealth()) / 2);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), enemyBefore);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerUsesCurrentControlInsteadOfPermanentBattleSide)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	auto * convertedFriendly = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 2), 1000);
	const auto hypnosis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	enemyCollateral->addNewBonus(hypnosis);
	convertedFriendly->addNewBonus(hypnosis);
	ASSERT_EQ(battle()->battleGetOwner(enemyCollateral), attackerSideHero->getOwner());
	ASSERT_NE(battle()->battleGetOwner(convertedFriendly), attackerSideHero->getOwner());
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto convertedEnemyBefore = enemyCollateral->getAvailableHealth();
	const auto convertedFriendlyBefore = convertedFriendly->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws(
		{friendly, enemyCollateral, convertedFriendly}, 1, 2));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), convertedEnemyBefore);
	EXPECT_EQ(convertedFriendlyBefore - convertedFriendly->getAvailableHealth(),
		(primaryBefore - primary->getAvailableHealth()) / 2);
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerChosenImmuneHostileDoesNotReroll)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(handOfFateSpell())));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), enemyBefore);
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage,
		primaryBefore - primary->getAvailableHealth());
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerChosenResistantHostileDoesNotReroll)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1000, true));
	enemyCollateral->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto friendlyBefore = friendly->getAvailableHealth();
	const auto enemyBefore = enemyCollateral->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(castFateDealerWithDraws({friendly, enemyCollateral}, 0, 1));
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyBefore);
	EXPECT_EQ(enemyCollateral->getAvailableHealth(), enemyBefore);
	ASSERT_EQ(server.castsOf(handOfFateSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(handOfFateSpell()).front().damage,
		primaryBefore - primary->getAvailableHealth());
}

TEST_F(NewHorizonsHandOfFateTest, FateDealerEmptyPoolLeavesDetachedPrimaryDamageUnchanged)
{
	enableFateDealer = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HandOfFatePredictionEnvironment environment(gameState());
	HypotheticBattle predicted(&environment, callback);
	auto projectedFriendly = predicted.getForUpdate(friendly->unitId());
	int64_t lethalDamage = projectedFriendly->getAvailableHealth();
	projectedFriendly->damage(lethalDamage);
	const auto * projectedPrimary = predicted.battleGetUnitByID(primary->unitId());
	ASSERT_NE(projectedPrimary, nullptr);
	const auto primaryBefore = projectedPrimary->getAvailableHealth();
	const auto forecast = primaryForecast();
	spells::BattleCast cast(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	spells::Target aim;
	aim.emplace_back(projectedPrimary);
	mechanics->castEval(predicted.getServerCallback(), aim);
	EXPECT_EQ(primaryBefore - predicted.battleGetUnitByID(primary->unitId())->getAvailableHealth(), forecast);
	EXPECT_EQ(projectedFriendly->getAvailableHealth(), 0);
	EXPECT_EQ(primary->getAvailableHealth(), 10000);
}
