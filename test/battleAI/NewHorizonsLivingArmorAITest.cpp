/*
 * NewHorizonsLivingArmorAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/SummonEvaluation.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include <set>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
constexpr auto summonArmorKey = "new-horizons:summonLivingArmor";

SpellID summonArmorSpell()
{
	return SpellID(SpellID::decode(std::string(summonArmorKey)));
}

class LivingArmorEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit LivingArmorEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class LivingArmorCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	LivingArmorCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

std::vector<const battle::Unit *> armorSummons(const CBattleInfoCallback & battle)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		if(!unit || !unit->unitType() || unit->unitType()->getJsonKey() != "new-horizons:livingArmor"
			|| !unit->isSummoned())
			continue;

		const auto unitState = unit->acquireState();
		if(unitState && unitState->summonRoundsRemaining > 0)
			result.push_back(unit);
	}
	return result;
}
}

class NewHorizonsLivingArmorAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<LivingArmorEnvironment> environment;
	std::shared_ptr<LivingArmorCallback> callback;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void prepare(const int spellPower = 210)
	{
		useCommands = true;
		startGame();

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		const auto spell = summonArmorSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 10, ChangeValueMode::ABSOLUTE);
		const auto sorceryMagicId = SecondarySkill::decode(std::string("new-horizons:sorceryMagic"));
		ASSERT_GE(sorceryMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorceryMagicId), MasteryLevel::NONE,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		beginCombat();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		enemy = addStack(BattleSide::DEFENDER,
			creatureByName("core:peasant"), BattleHex(14, 5), 1);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);

		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -active->getMovementRange();
		active->addNewBonus(std::make_shared<Bonus>(immobilized));

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);

		callback = std::make_shared<LivingArmorCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<LivingArmorEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsLivingArmorAITest, BoundedLegalPlacementsProjectExactPoolAndExpireWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = summonArmorSpell().toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&preview);
	const auto candidates = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(candidates.size(), 12u);
	const auto repeated = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(repeated.size(), candidates.size());
	std::set<int> seen;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const auto & target = candidates[index];
		ASSERT_EQ(target.size(), 1u);
		EXPECT_EQ(target.front().unitValue, nullptr);
		EXPECT_TRUE(mechanics->canBeCastAt(target));
		EXPECT_TRUE(seen.insert(target.front().hexValue.toInt()).second);
		EXPECT_EQ(target.front().hexValue, repeated[index].front().hexValue);
	}
	const auto liveCount = battle()->battleGetAllUnits(false).size();
	const auto liveMana = attackerSideHero->getManaAvailable();
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, spell);
	const auto projectedMechanics = spell->battleMechanics(&projectedCast);
	projectedMechanics->castEval(projected.getServerCallback(), candidates.front());
	const auto summons = armorSummons(projected);
	ASSERT_EQ(summons.size(), 1u);
	const auto id = summons.front()->unitId();
	const int64_t expectedPool = 40 + attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)
		/ attackerSideHero->getEffectPowerDivisor(spell);
	EXPECT_EQ(summons.front()->getAvailableHealth(), expectedPool);
	EXPECT_EQ(summons.front()->getCount(), (expectedPool + 9) / 10);
	EXPECT_EQ(summons.front()->getPosition(), candidates.front().front().hexValue);
	EXPECT_EQ(summons.front()->acquireState()->summonRoundsRemaining, 3);
	EXPECT_FLOAT_EQ(summonEvaluation::createdStackValue(spell, summons.front(), false),
		static_cast<float>(expectedPool) * 12.0f);
	projected.nextRound();
	EXPECT_EQ(projected.battleGetUnitByID(id)->acquireState()->summonRoundsRemaining, 2);
	projected.nextRound();
	EXPECT_EQ(projected.battleGetUnitByID(id)->acquireState()->summonRoundsRemaining, 1);
	projected.nextRound();
	const auto * expired = projected.battleGetUnitByID(id);
	ASSERT_NE(expired, nullptr);
	EXPECT_FALSE(expired->alive());
	EXPECT_FLOAT_EQ(summonEvaluation::createdStackValue(spell, expired, false), 0.0f);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), liveCount);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveMana);
	EXPECT_TRUE(armorSummons(*battle()).empty());
}

TEST_F(NewHorizonsLivingArmorAITest, SelectsSummonAndSubmitsSharedLegalDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.spell, summonArmorSpell());
	const auto target = action.getTarget(battle());
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, summonArmorSpell().toSpell());
	const auto mechanics = summonArmorSpell().toSpell()->battleMechanics(&preview);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto summoned = armorSummons(*battle());
	ASSERT_EQ(summoned.size(), 1u);
	EXPECT_EQ(summoned.front()->getPosition(), target.front().hexValue);
	EXPECT_EQ(summoned.front()->getAvailableHealth(),
		40 + attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER)
			/ attackerSideHero->getEffectPowerDivisor(summonArmorSpell().toSpell()));
	EXPECT_EQ(summoned.front()->acquireState()->summonRoundsRemaining, 3);
}

TEST_F(NewHorizonsLivingArmorAITest, MagicDamageChoicePrefersVulnerableArmorOverOtherwiseEqualProtectedArmor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	const auto * vulnerable = addStack(BattleSide::DEFENDER,
		creatureByName("new-horizons:livingArmor"), BattleHex(12, 3), 30);
	auto * protectedArmor = addStack(BattleSide::DEFENDER,
		creatureByName("new-horizons:livingArmor"), BattleHex(12, 7), 30);
	ASSERT_NE(vulnerable, nullptr);
	ASSERT_NE(protectedArmor, nullptr);
	Bonus cancelVulnerability;
	cancelVulnerability.type = BonusType::MORE_DAMAGE_FROM_SPELL;
	cancelVulnerability.subtype = BonusSubtypeID();
	cancelVulnerability.val = -100;
	cancelVulnerability.duration = BonusDuration::ONE_BATTLE;
	protectedArmor->addNewBonus(std::make_shared<Bonus>(cancelVulnerability));
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.spell, SpellID::MAGIC_ARROW);
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_NE(target.front().unitValue, nullptr);
	EXPECT_EQ(target.front().unitValue->unitId(), vulnerable->unitId());
	EXPECT_EQ(vulnerable->getAvailableHealth(), 300);
	EXPECT_EQ(protectedArmor->getAvailableHealth(), 300);
}
