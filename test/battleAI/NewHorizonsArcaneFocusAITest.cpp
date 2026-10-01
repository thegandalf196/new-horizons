/*
 * NewHorizonsArcaneFocusAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto SPELLCRAFT_SKILL = "new-horizons:spellcraft";
const std::string ARCANE_FOCUS_PERK(newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS);

class ArcaneFocusEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ArcaneFocusEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ArcaneFocusAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	ArcaneFocusAICallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::vector<std::byte> serializedRandomState(CRandomGenerator & generator)
{
	CMemorySerializer serializer;
	serializer.oser & generator;
	return serializer.extractBuffer();
}
}

class NewHorizonsArcaneFocusAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<ArcaneFocusEnvironment> environment;
	std::shared_ptr<ArcaneFocusAICallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	bool selectArcaneFocusThroughLegalOffer()
	{
		const auto rankLookup = [this](const std::string & skillId)
		{
			return attackerSideHero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.perkId == ARCANE_FOCUS_PERK;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(attackerSideHero, offer, choice, seed, false);
			return attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, ARCANE_FOCUS_PERK);
		}
		return false;
	}

	void prepare(int32_t spellPower = 200)
	{
		startGame();
		const auto spellcraft = SecondarySkill(SecondarySkill::decode(SPELLCRAFT_SKILL));
		const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
		ASSERT_TRUE(spellcraft.hasValue());
		ASSERT_TRUE(sorcery.hasValue());
		attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE,
			ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(selectArcaneFocusThroughLegalOffer());
		ASSERT_TRUE(attackerSideHero->hasActivePerk(SPELLCRAFT_SKILL, ARCANE_FOCUS_PERK));

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(rightHex + 5), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<ArcaneFocusAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ArcaneFocusEnvironment>(gameState());
	}

	std::unique_ptr<spells::Mechanics> magicArrowMechanics(const CBattleInfoCallback * battleCallback,
		const spells::Mode mode = spells::Mode::HERO, int overcharge = 0) const
	{
		const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
		spells::BattleCast cast(battleCallback, attackerSideHero, mode, spell);
		cast.setOvercharge(overcharge);
		return spell->battleMechanics(&cast);
	}

	int64_t expectedMagicArrowDamage(const spells::Mechanics & mechanics, int overcharge) const
	{
		const auto damage = newHorizonsMagic::magicArrowDamage(
			battle()->getMagicRules(), SpellID(SpellID::MAGIC_ARROW), mechanics.getEffectPower(),
			mechanics.getEffectPowerDivisor(), overcharge,
			newHorizonsMagic::magicArrowOverchargeModifiers(attackerSideHero),
			mechanics.getSpellPowerCoefficientBasisPoints() / 100,
			mechanics.getEmpowerSpellBonusPercent());
		if(!damage)
			ADD_FAILURE() << "Magic Arrow must expose its saved-v3 direct damage formula";
		return damage.value_or(0);
	}
};

TEST_F(NewHorizonsArcaneFocusAITest, EvaluatorSubmitsFirstFocusedSpellWithoutPreviewSideEffects)
{
	// The New Horizons Spell Power divisor is 10: at 10 raw Spell Power, focused
	// Magic Arrow dealt only 46 damage before overcharge and a legal defensive
	// Order won. At 200, the non-overcharged focused hit is 548, while the 100
	// Ogre stack still survives both detached-projection casts below.
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	ASSERT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	const auto baseCoefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	ASSERT_EQ(baseCoefficient, 11'000);
	auto mechanics = magicArrowMechanics(callback->getBattle(BattleID(0)).get());
	ASSERT_NE(mechanics, nullptr);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 13'200);
	const auto damageFormula = newHorizonsMagic::spellDirectDamage(
		battle()->getMagicRules(), spell->getJsonKey());
	ASSERT_TRUE(damageFormula.has_value());
	const auto focusedDamage = damageFormula->evaluateBasisPoints(mechanics->getEffectPower(),
		mechanics->getEffectPowerDivisor(), 13'200);
	const auto ordinaryDamage = damageFormula->evaluateBasisPoints(mechanics->getEffectPower(),
		mechanics->getEffectPowerDivisor(), baseCoefficient);
	EXPECT_EQ(expectedMagicArrowDamage(*mechanics, 0), focusedDamage);
	EXPECT_GT(focusedDamage, ordinaryDamage);
	EXPECT_EQ(damageFormula->evaluateBasisPoints(0, mechanics->getEffectPowerDivisor(), 13'200),
		damageFormula->base) << "Arcane Focus leaves Magic Arrow's flat base untouched";

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	const auto activeHealthBefore = active->getAvailableHealth();
	auto * liveRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(liveRng, nullptr);
	const auto rngBefore = serializedRandomState(*liveRng);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	EXPECT_EQ(action.spell, SpellID(SpellID::MAGIC_ARROW));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(serializedRandomState(*liveRng), rngBefore);

	mechanics = magicArrowMechanics(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, action.spellOvercharge);
	ASSERT_NE(mechanics, nullptr);
	const auto expectedDamage = expectedMagicArrowDamage(*mechanics, action.spellOvercharge);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(enemyHealthBefore - enemy->getAvailableHealth(), expectedDamage);
	EXPECT_LT(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(serializedRandomState(*liveRng), rngBefore)
		<< "A deterministic Magic Arrow cast and its inspection must not consume live RNG";

	auto subsequentMechanics = magicArrowMechanics(callback->getBattle(BattleID(0)).get());
	ASSERT_NE(subsequentMechanics, nullptr);
	EXPECT_EQ(subsequentMechanics->getSpellPowerCoefficientBasisPoints(), baseCoefficient)
		<< "The accepted spell consumes Arcane Focus for the rest of the combat";
}

TEST_F(NewHorizonsArcaneFocusAITest, DetachedProjectionUsesFocusOnceAcrossRoundWithoutChangingLiveState)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projection(environment.get(), liveCallback);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto enemyHealthBefore = enemy->getAvailableHealth();
	const auto activeHealthBefore = active->getAvailableHealth();
	auto * liveRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(liveRng, nullptr);
	const auto rngBefore = serializedRandomState(*liveRng);

	const auto * firstProjectedTarget = projection.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(firstProjectedTarget, nullptr);
	spells::BattleCast firstCast(&projection, attackerSideHero, spells::Mode::HERO, spell);
	firstCast.setOvercharge(0);
	auto firstMechanics = spell->battleMechanics(&firstCast);
	ASSERT_NE(firstMechanics, nullptr);
	ASSERT_TRUE(firstMechanics->canBeCastAt(spells::Target{spells::Destination(firstProjectedTarget)}));
	EXPECT_EQ(firstMechanics->getSpellPowerCoefficientBasisPoints(), 13'200);
	const auto firstExpectedDamage = expectedMagicArrowDamage(*firstMechanics, 0);
	firstMechanics->castEval(projection.getServerCallback(),
		spells::Target{spells::Destination(firstProjectedTarget)});
	EXPECT_TRUE(projection.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(enemyHealthBefore - projection.battleGetUnitByID(enemy->unitId())->getAvailableHealth(),
		firstExpectedDamage);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(serializedRandomState(*liveRng), rngBefore);

	projection.nextRound();
	const auto * secondProjectedTarget = projection.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(secondProjectedTarget, nullptr);
	spells::BattleCast secondCast(&projection, attackerSideHero, spells::Mode::HERO, spell);
	secondCast.setOvercharge(0);
	auto secondMechanics = spell->battleMechanics(&secondCast);
	ASSERT_NE(secondMechanics, nullptr);
	EXPECT_EQ(secondMechanics->getSpellPowerCoefficientBasisPoints(), 11'000)
		<< "Round advancement does not renew the combat's first-spell bonus";
	const auto secondExpectedDamage = expectedMagicArrowDamage(*secondMechanics, 0);
	const auto healthBeforeSecondProjection = secondProjectedTarget->getAvailableHealth();
	ASSERT_TRUE(secondMechanics->canBeCastAt(spells::Target{spells::Destination(secondProjectedTarget)}));
	secondMechanics->castEval(projection.getServerCallback(),
		spells::Target{spells::Destination(secondProjectedTarget)});
	EXPECT_TRUE(projection.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(healthBeforeSecondProjection - projection.battleGetUnitByID(enemy->unitId())->getAvailableHealth(),
		secondExpectedDamage);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealthBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(active->getAvailableHealth(), activeHealthBefore);
	EXPECT_EQ(serializedRandomState(*liveRng), rngBefore);
}
