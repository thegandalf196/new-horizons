/*
 * NewHorizonsWisdomDecisionAITest.cpp, part of VCMI engine
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
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsWisdom.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto WISDOM_SKILL = "new-horizons:wisdom";
constexpr auto FAMILIARITY_PERK = "new-horizons:wisdom.spellFamiliarity";
constexpr auto INSIGHT_PERK = "new-horizons:wisdom.arcaneInsight";
constexpr auto ARCHMAGE_PERK = "new-horizons:wisdom.archmage";
constexpr auto SORCERY_MAGIC_SKILL = "new-horizons:sorceryMagic";

class WisdomDecisionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit WisdomDecisionEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class WisdomDecisionAICallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	WisdomDecisionAICallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsWisdomDecisionAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<WisdomDecisionEnvironment> environment;
	std::shared_ptr<WisdomDecisionAICallback> callback;

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

	std::vector<std::byte> nativeRandomState() const
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	}

	void prepare(int32_t spellPower = 0)
	{
		prepareBattle(spellPower, false);
	}

	void prepareArchmage(int32_t spellPower = 0, int32_t attackerCount = 100)
	{
		prepareBattle(spellPower, true, attackerCount);
	}

	bool selectWisdomPerkThroughLegalOffer(const std::string & perkId)
	{
		const auto rankLookup = [this](const std::string & skillId)
		{
			return attackerSideHero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
			const auto selected = std::find_if(offer.begin(), offer.end(), [&perkId](const auto & candidate)
			{
				return candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(attackerSideHero, offer, choice, seed, false);
			return attackerSideHero->hasActivePerk(WISDOM_SKILL, perkId);
		}
		return false;
	}

	void prepareBattle(int32_t spellPower, bool archmage, int32_t attackerCount = 100)
	{
		startGame();
		const auto wisdom = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL));
		ASSERT_TRUE(wisdom.hasValue());
		if(archmage)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:solmyr")));
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(FAMILIARITY_PERK));
			gameHandler->levelUpHero(attackerSideHero, wisdom, false);
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::ADVANCED);
			ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(INSIGHT_PERK));
			gameHandler->levelUpHero(attackerSideHero, wisdom, false);
			ASSERT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::EXPERT);
			ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(ARCHMAGE_PERK));
			const auto sorcery = SecondarySkill(SecondarySkill::decode(SORCERY_MAGIC_SKILL));
			ASSERT_TRUE(sorcery.hasValue());
			attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		}
		else
		{
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({WISDOM_SKILL, FAMILIARITY_PERK});
		}
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, FAMILIARITY_PERK));

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		if(archmage)
			attackerSideHero->addSpellToSpellbook(SpellID(SpellID::IMPLOSION));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!removed.changedStacks.empty())
			gameHandler->sendAndApply(removed);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), attackerCount);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<WisdomDecisionAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<WisdomDecisionEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsWisdomDecisionAITest, ProjectionCopiesAndAdvancesOnlyItsOwnSpellFamiliarity)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(attackerSideHero->getListedSpellCost(spell), 4);
	ASSERT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	const int wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(spell), 1, MasteryLevel::BASIC);
	const int firstCost = wisdomCost;
	const int repeatedCost = std::max(1, (wisdomCost * 95 + 99) / 100);
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), firstCost);

	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	EXPECT_FALSE(liveCallback->getBattle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(liveCallback->battleGetSpellCost(spell, attackerSideHero), firstCost);

	auto model = std::make_shared<HypotheticBattle>(environment.get(), liveCallback);
	auto beforeCastCopy = std::make_shared<HypotheticBattle>(environment.get(), model);
	EXPECT_FALSE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(beforeCastCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(model->battleGetSpellCost(spell, attackerSideHero), firstCost);

	const auto targetHealth = enemy->getAvailableHealth();
	const auto randomBefore = nativeRandomState();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto castCount = battle()->battleCastSpells(BattleSide::ATTACKER);
	EXPECT_NE(model->getServerCallback()->getRNG(), getRNG());
	const auto * projectedTarget = model->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::Target target{spells::Destination(projectedTarget)};
	spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(model->getServerCallback(), target);

	EXPECT_TRUE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_FALSE(beforeCastCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(beforeCastCopy->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(liveCallback->getBattle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(liveCallback->getBattle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_EQ(model->battleGetSpellCost(spell, attackerSideHero), repeatedCost);
	EXPECT_EQ(model->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 1u);
	EXPECT_EQ(beforeCastCopy->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 0u);
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 0u);
	EXPECT_EQ(liveCallback->battleGetSpellCost(spell, attackerSideHero), firstCost);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
	EXPECT_EQ(nativeRandomState(), randomBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), castCount);

	auto nestedCopy = std::make_shared<HypotheticBattle>(environment.get(), model);
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_EQ(nestedCopy->battleGetSpellCost(spell, attackerSideHero), repeatedCost);
	EXPECT_EQ(nestedCopy->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 1u);
	nestedCopy->nextRound();
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_EQ(nestedCopy->battleGetSpellCost(spell, attackerSideHero), repeatedCost);
	EXPECT_EQ(nestedCopy->getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 1u);
}

TEST_F(NewHorizonsWisdomDecisionAITest, InvalidTargetHeroCastEvalDoesNotAcquireFamiliarity)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), liveCallback);
	const auto firstCost = model.battleGetSpellCost(spell, attackerSideHero);

	const spells::Target invalidTarget{spells::Destination(BattleHex::INVALID)};
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->canBeCastAt(invalidTarget));
	mechanics->castEval(model.getServerCallback(), invalidTarget);

	EXPECT_FALSE(model.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_EQ(model.battleGetSpellCost(spell, attackerSideHero), firstCost);
	EXPECT_EQ(model.getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 0u);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsWisdomDecisionAITest, CreatureModeCastEvalDoesNotAcquireHeroFamiliarity)
{
	prepare();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto * creatureSpell = SpellID(SpellID::decode("core:fireballAbility")).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_NE(creatureSpell, nullptr);
	ASSERT_TRUE(creatureSpell->isCreatureAbility());

	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 2), 10);
	ASSERT_NE(creatureCaster, nullptr);
	Bonus creaturePower;
	creaturePower.type = BonusType::CREATURE_SPELL_POWER;
	creaturePower.val = 100;
	creatureCaster->addNewBonus(std::make_shared<Bonus>(creaturePower));

	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), liveCallback);
	const auto firstCost = model.battleGetSpellCost(spell, attackerSideHero);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto targetHealth = enemy->getAvailableHealth();
	const auto randomBefore = nativeRandomState();
	const auto * projectedTarget = model.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const spells::Target target{spells::Destination(projectedTarget)};
	spells::BattleCast cast(&model, creatureCaster, spells::Mode::CREATURE_ACTIVE, creatureSpell);
	const auto mechanics = creatureSpell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(model.getServerCallback(), target);

	EXPECT_FALSE(model.hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(model.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_EQ(model.battleGetSpellCost(spell, attackerSideHero), firstCost);
	EXPECT_EQ(model.getWisdomCombatState(BattleSide::ATTACKER).priorCasts(spell->getId()), 0u);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
	EXPECT_EQ(nativeRandomState(), randomBefore);
}

TEST_F(NewHorizonsWisdomDecisionAITest, EvaluatorSubmitsAHeroSpellWithoutConsumingLiveStateDuringEvaluation)
{
	prepare(9900);
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto targetHealth = enemy->getAvailableHealth();
	const auto randomBefore = nativeRandomState();
	const auto firstCastCost = battle()->battleGetSpellCost(spell, attackerSideHero);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(spell), 1, MasteryLevel::BASIC);

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
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
	EXPECT_EQ(nativeRandomState(), randomBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - firstCastCost);
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero),
		std::max(1, (wisdomCost * 95 + 99) / 100));
}

TEST_F(NewHorizonsWisdomDecisionAITest, LowerLevelProjectionPreservesArchmageForNestedModels)
{
	prepareArchmage();
	const auto * lowSpell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto * archmageSpell = SpellID(SpellID::IMPLOSION).toSpell();
	ASSERT_NE(lowSpell, nullptr);
	ASSERT_NE(archmageSpell, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(lowSpell->getId()), 1);
	ASSERT_EQ(battle()->battleGetSpellLevel(archmageSpell->getId()), 4);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCHMAGE_PERK));

	const auto archmageWisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(archmageSpell), 1, MasteryLevel::EXPERT);
	const auto firstArchmageCost = archmageWisdomCost;
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), liveCallback);
	EXPECT_EQ(model->battleGetSpellCost(archmageSpell, attackerSideHero), firstArchmageCost);

	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *model,
		BattleSide::ATTACKER, archmageSpell->getId()), 45);
	const auto * projectedTarget = model->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const spells::Target target{spells::Destination(projectedTarget)};
	spells::BattleCast cast(model.get(), attackerSideHero, spells::Mode::HERO, lowSpell);
	const auto mechanics = lowSpell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(model->getServerCallback(), target);

	EXPECT_TRUE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_EQ(model->battleGetSpellCost(archmageSpell, attackerSideHero),
		archmageWisdomCost)
		<< "Casting a distinct Level 1 spell does not discount this Level 4 spell";

	auto nestedCopy = std::make_shared<HypotheticBattle>(environment.get(), model);
	EXPECT_TRUE(nestedCopy->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_FALSE(nestedCopy->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(nestedCopy->battleGetSpellCost(archmageSpell, attackerSideHero),
		archmageWisdomCost);
	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *nestedCopy,
		BattleSide::ATTACKER, archmageSpell->getId()), 45);
	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *nestedCopy,
		BattleSide::ATTACKER, lowSpell->getId()), 0);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 1));
	EXPECT_EQ(liveCallback->battleGetSpellCost(archmageSpell, attackerSideHero), firstArchmageCost);
}

TEST_F(NewHorizonsWisdomDecisionAITest, LevelFourAndFiveKeepIndependentInsightAndPersistentArchmage)
{
	prepareArchmage();
	const auto * levelFour = SpellID(SpellID::IMPLOSION).toSpell();
	const auto * levelFive = SpellID(SpellID::ARMAGEDDON).toSpell();
	ASSERT_NE(levelFour, nullptr);
	ASSERT_NE(levelFive, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(levelFour->getId()), 4);
	ASSERT_EQ(battle()->battleGetSpellLevel(levelFive->getId()), 5);
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(levelFour), 1, MasteryLevel::EXPERT);
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), liveCallback);
	const auto bonus = [&](const HypotheticBattle & state, const CSpell * spell)
		{ return newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, state, BattleSide::ATTACKER, spell->getId()); };
	EXPECT_EQ(model->battleGetSpellCost(levelFour, attackerSideHero), wisdomCost);
	EXPECT_EQ(bonus(*model, levelFour), 45);
	EXPECT_EQ(bonus(*model, levelFive), 45);
	model->getServerCallback()->recordCompletedWisdomSpellCast(BattleSide::ATTACKER, levelFive->getId(), model->getRound(), false);
	model->getServerCallback()->recordCompletedHeroSpellCast(BattleSide::ATTACKER, 5);
	EXPECT_TRUE(model->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_FALSE(model->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(model->battleGetSpellCost(levelFour, attackerSideHero), wisdomCost);
	EXPECT_EQ(bonus(*model, levelFour), 45);
	EXPECT_EQ(bonus(*model, levelFive), 20);
	HypotheticBattle nestedCopy(environment.get(), model);
	EXPECT_TRUE(nestedCopy.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_FALSE(nestedCopy.hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(nestedCopy.battleGetSpellCost(levelFour, attackerSideHero), wisdomCost);
	EXPECT_EQ(bonus(nestedCopy, levelFour), 45);
	EXPECT_EQ(bonus(nestedCopy, levelFive), 20);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *battle(), BattleSide::ATTACKER, levelFive->getId()), 45);
}

TEST_F(NewHorizonsWisdomDecisionAITest, EvaluatorForecastsAndSubmitsAcceptedLevelFourSpell)
{
	// One allied Angel keeps legal Order candidates available at modest value;
	// the 100 enemy Angels preserve a clear target for Level 4 Implosion.
	prepareArchmage(2500, 1);
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	const auto * levelFiveSpell = SpellID(SpellID::ARMAGEDDON).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_NE(magicArrow, nullptr);
	ASSERT_NE(levelFiveSpell, nullptr);
	ASSERT_EQ(active->getCount(), 1);
	ASSERT_EQ(enemy->getCount(), 100);
	ASSERT_EQ(battle()->battleGetSpellLevel(spell->getId()), 4);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	ASSERT_TRUE(magicArrow->canBeCast(callback->getBattle(BattleID(0)).get(),
		spells::Mode::HERO, attackerSideHero));
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));

	const auto mana = attackerSideHero->getManaAvailable();
	const auto targetHealth = enemy->getAvailableHealth();
	const auto randomBefore = nativeRandomState();
	const auto wisdomCost = newHorizonsMagic::wisdomAdjustedCost(
		attackerSideHero->getListedSpellCost(spell), 1, MasteryLevel::EXPERT);
	const auto firstCastCost = wisdomCost;
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), firstCastCost);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));

	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle forecast(environment.get(), live);
	spells::BattleCast projectedCast(&forecast, attackerSideHero, spells::Mode::HERO, spell);
	const auto projectedMechanics = spell->battleMechanics(&projectedCast);
	ASSERT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, forecast,
		BattleSide::ATTACKER, spell->getId()), 45);
	projectedMechanics->castEval(forecast.getServerCallback(),
		{spells::Destination(forecast.battleGetUnitByID(enemy->unitId()))});
	const auto projectedDamage = targetHealth - forecast.battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	ASSERT_GT(projectedDamage, 0);

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, SpellID(SpellID::IMPLOSION));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), firstCastCost);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(enemy->getAvailableHealth(), targetHealth);
	EXPECT_EQ(nativeRandomState(), randomBefore);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 4));
	EXPECT_EQ(targetHealth - enemy->getAvailableHealth(), projectedDamage);
	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *battle(),
		BattleSide::ATTACKER, spell->getId()), 20);
	EXPECT_EQ(newHorizonsWisdom::effectivenessBonusPercent(attackerSideHero, *battle(),
		BattleSide::ATTACKER, levelFiveSpell->getId()), 45);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellLevel(BattleSide::ATTACKER, 5));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - firstCastCost);
	EXPECT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero),
		std::max(1, (wisdomCost * 95 + 99) / 100));
	EXPECT_EQ(battle()->battleGetSpellCost(levelFiveSpell, attackerSideHero),
		newHorizonsMagic::wisdomAdjustedCost(attackerSideHero->getListedSpellCost(levelFiveSpell),
			1, MasteryLevel::EXPERT))
		<< "A different Level 4 cast leaves Level 5 Familiarity unchanged";
}
