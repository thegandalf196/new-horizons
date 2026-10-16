/*
 * NewHorizonsPlagueActionCompletionAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include <vcmi/ServerCallback.h>
#include <array>
#include <map>
#include "../../AI/BattleAI/HypotheticBattle.h"

namespace
{
class PlagueFixtureSpellHandler final : public CSpellHandler
{
public:
	using CSpellHandler::loadFromJson;
};

class PlagueActionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PlagueActionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class PlagueActionCallback final : public CBattleCallback
{
public:
	explicit PlagueActionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction &) override {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};

class NewHorizonsPlagueActionCompletionAITest : public HeroCommandFixture
{
protected:
	CStack * caster = nullptr;
	CStack * target = nullptr;
	CStack * ally = nullptr;
	std::shared_ptr<PlagueActionEnvironment> environment;
	SpellID plague() const { return SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID))); }
	const CSpell * arrow() const { return SpellID(SpellID::MAGIC_ARROW).toSpell(); }

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto & chance : rules["morale"]["goodChance"].Vector())
			chance.Integer() = 0;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(bool infect = true, bool defend = false)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(plague());
		setTestSpellPointTotal(defenderSideHero, 1000);
		if(defend)
		{
			const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:armorer"));
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		caster = addStack(BattleSide::ATTACKER, creatureByName("core:fairieDragon"), BattleHex(4, 5), 1000);
		target = addStack(BattleSide::DEFENDER, creatureByName(defend ? "core:archer" : "core:peasant"),
			BattleHex(11, 5), 100000);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(2, 2), 100000);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 2), 100000), nullptr);
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1000,
			BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), caster);
		ASSERT_TRUE(caster->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
		if(infect)
		{
			ASSERT_TRUE(castOn(defenderSideHero, plague(), caster));
		}
		environment = std::make_shared<PlagueActionEnvironment>(gameState());
	}

	std::shared_ptr<const Bonus> marker() const
	{
		const auto bonuses = caster->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plague())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? nullptr : bonuses->front();
	}

	float forecast(PlayerColor player = PlayerColor(0), const CStack * recipient = nullptr)
	{
		auto callback = std::make_shared<PlagueActionCallback>(player);
		callback->onBattleStarted(battle());
		BattleEvaluator evaluator(environment, callback, caster, player, BattleID(0),
			battle()->playerToSide(player), 1.0f, 2);
		PossibleSpellcast candidate;
		candidate.spell = arrow();
		candidate.dest = {spells::Destination(recipient ? recipient : target)};
		evaluator.evaluateCreatureSpellcast(caster, candidate);
		return candidate.value;
	}

	bool actualCast()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeCreatureSpellcast(caster, {spells::Destination(target)}, arrow()->id));
	}

	void prepareBeneficial(int casterCount, int allyCount)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(false));
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		// Remove the unrelated reserve Peasants as well: they otherwise become
		// the saturated best target and hide the useful Bloodlust delta.
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		caster = addStack(BattleSide::ATTACKER, creatureByName("core:ogreMage"), BattleHex(4, 5), casterCount);
		// Isolate the useful allied buff from defensive self-retaliation value;
		// the spent caster is already excluded from ordinary future-pressure value.
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
		// The same substantial per-creature strike in both cases avoids the
		// one-HP Peasant's integer damage floor. Only the ally/caster counts vary.
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"),
			BattleHex(9, 5), allyCount);
		ally->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
		// Large HP headroom prevents a lethal-cap plateau. A separated stationary
		// enemy cannot inflate its own casualty value by attacking the buffed ally
		// or the spent caster; the ally can still fly into melee without a counter.
		target = addStack(BattleSide::DEFENDER, creatureByName("core:boneDragon"), BattleHex(13, 5), 100000);
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
		ASSERT_FALSE(target->ableToRetaliate());
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -100, BonusSourceID()));
		ASSERT_EQ(target->getMovementRange(), 0u);
		ASSERT_TRUE(ally->hasBonusOfType(BonusType::NO_RETALIATION));
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = caster->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(battle()->battleActiveUnit(), caster);
		ASSERT_TRUE(caster->hasBonusOfType(BonusType::SPELLCASTER, BonusSubtypeID(SpellID(SpellID::BLOODLUST))));
		ASSERT_TRUE(castOn(defenderSideHero, plague(), caster));
		ASSERT_NE(marker(), nullptr);
	}

	float beneficialTickDps(const battle::Unit * recipient)
	{
		auto model = std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		spells::BattleCast cast(model.get(), caster, spells::Mode::CREATURE_ACTIVE,
			SpellID(SpellID::BLOODLUST).toSpell());
		cast.castEval(model->getServerCallback(), {spells::Destination(recipient)});
		model->getForUpdate(caster->unitId())->removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
		newHorizonsPlague::completeActivation(*model, *model->getServerCallback(), caster->unitId());
		EXPECT_EQ(model->getProjectedPlagueHits().size(), 1u);
		DamageCache cache;
		float cost = 0;
		for(const auto & hit : model->getProjectedPlagueHits())
		{
			EXPECT_EQ(hit.targetController, PlayerColor(0));
			cost += AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
				static_cast<uint64_t>(std::min(hit.finalizedDamage, hit.preHitTarget->getAvailableHealth())), cache, model);
		}
		return cost;
	}

	BattleEvaluator beneficialEvaluator()
	{
		auto callback = std::make_shared<PlagueActionCallback>(PlayerColor(0));
		callback->onBattleStarted(battle());
		return BattleEvaluator(environment, callback, caster, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
	}
};
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, SkippingPaidCastForecastMatchesActualDamageMinusObligatoryTick)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NE(marker(), nullptr);
	const auto casterBefore = caster->getAvailableHealth();
	const auto targetBefore = target->getAvailableHealth();
	const auto charges = caster->casts.available();
	const auto value = forecast();
	EXPECT_EQ(caster->getAvailableHealth(), casterBefore);
	EXPECT_EQ(target->getAvailableHealth(), targetBefore);
	EXPECT_EQ(marker()->turnsRemain, 3);
	ASSERT_TRUE(actualCast());
	EXPECT_EQ(caster->casts.available(), charges - 1);
	ASSERT_LT(caster->getAvailableHealth(), casterBefore);
	ASSERT_LT(target->getAvailableHealth(), targetBefore);
	EXPECT_FLOAT_EQ(value, (targetBefore - target->getAvailableHealth())
		- (casterBefore - caster->getAvailableHealth()));
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 2);
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, CompletedCastExpiresTemporaryMitigationBeforeActualAndProjectedTick)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus mitigation(BonusDuration::STACK_ACTIVATION, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 100, BonusSourceID());
	mitigation.subtype = BonusSubtypeID(SpellSchool::ANY);
	caster->addNewBonus(std::make_shared<Bonus>(mitigation));
	const auto before = caster->getAvailableHealth();
	const auto targetBefore = target->getAvailableHealth();
	const auto value = forecast();
	ASSERT_TRUE(caster->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
	ASSERT_TRUE(actualCast());
	EXPECT_FALSE(caster->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
	ASSERT_LT(caster->getAvailableHealth(), before);
	EXPECT_FLOAT_EQ(value, targetBefore - target->getAvailableHealth() - (before - caster->getAvailableHealth()));
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, RequiredTickCostCanMakeAnOtherwiseUsefulCreatureCastNegative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Actual target has only one HP remaining; the caster still pays the entire
	// mandatory finalized tick cost, rather than having that cost rejected as collateral.
	auto state = target->acquireState();
	int64_t injury = target->getAvailableHealth() - 1;
	state->damage(injury);
	UnitChanges change(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	change.healthDelta = -injury;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);
	ASSERT_EQ(target->getAvailableHealth(), 1);
	EXPECT_LT(forecast(), -1);
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, UninfectedCasterKeepsOriginalHpScoreAndRealCastCosts)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_EQ(marker(), nullptr);
	const auto own = caster->getAvailableHealth();
	const auto enemy = target->getAvailableHealth();
	const auto value = forecast();
	ASSERT_TRUE(actualCast());
	EXPECT_EQ(caster->getAvailableHealth(), own);
	EXPECT_FLOAT_EQ(value, enemy - target->getAvailableHealth());
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, ChangedCasterAllegianceUsesReceiptOwnerInExistingHpScale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus control(BonusDuration::N_TURNS, BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::HYPNOTIZE)));
	control.turnsRemain = 2;
	caster->addNewBonus(std::make_shared<Bonus>(control));
	ASSERT_EQ(caster->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(battle()->battleGetOwner(caster), PlayerColor(1));
	const auto before = caster->getAvailableHealth();
	const auto infected = forecast(PlayerColor(1), ally);
	const auto saved = marker();
	ASSERT_NE(saved, nullptr);
	caster->removeBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plague())));
	const auto clean = forecast(PlayerColor(1), ally);
	EXPECT_LT(infected, clean);
	EXPECT_EQ(caster->getAvailableHealth(), before);
	EXPECT_TRUE(caster->hasBonusOfType(BonusType::HYPNOTIZED));
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, NonSkippingCastDoesNotConsumeOngoingPlagueInItsHeldActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Synthetic protocol capability control: do not alter any installed spell
	// permanently or pretend the shipped arrow is ordinarily a free cast.
	const auto index = arrow()->getIndex();
	const auto original = LIBRARY->spellh->objects.at(index);
	JsonNode definition;
	definition["name"].String() = "Non-skipping Plague fixture arrow";
	definition["type"].String() = "combat";
	definition["targetType"].String() = "CREATURE";
	definition["level"].Integer() = original->getLevel();
	definition["power"].Integer() = original->getBasePower();
	definition["canCastWithoutSkip"].Bool() = true;
	definition["flags"]["damage"].Bool() = original->isDamage();
	definition["flags"]["offensive"].Bool() = original->isOffensive();
	definition["flags"]["negative"].Bool() = original->isNegative();
	definition["targetCondition"] = original->targetCondition;
	const std::array<std::string, GameConstants::SPELL_SCHOOL_LEVELS> levels = {"none", "basic", "advanced", "expert"};
	for(size_t rank = 0; rank < levels.size(); ++rank)
	{
		const auto & source = original->getLevelInfo(static_cast<int32_t>(rank));
		auto & level = definition["levels"][levels[rank]];
		level["cost"].Integer() = source.cost;
		level["power"].Integer() = source.power;
		level["range"].String() = "0";
		level["targetModifier"]["smart"].Bool() = source.smartTarget;
		level["targetModifier"]["clearAffected"].Bool() = source.clearAffected;
		level["effects"] = source.effects;
		level["cumulativeEffects"] = source.cumulativeEffects;
		level["battleEffects"] = source.battleEffects;
	}
	definition.setModScope("vcmi-test");
	PlagueFixtureSpellHandler handler;
	auto synthetic = handler.loadFromJson("vcmi-test", definition, "plagueNonSkippingArrow", index);
	// Copy resolved school identities without adding post-load identifier requests.
	synthetic->schools = original->schools;
	// Retain the installed identity used by captured New Horizons damage rules.
	synthetic->identifier = original->identifier;
	synthetic->modScope = original->modScope;
	handler.objects.push_back(synthetic);
	handler.afterLoadFinalization();
	struct Restore
	{
		std::shared_ptr<CSpell> original;
		size_t index;
		~Restore() { LIBRARY->spellh->objects.at(index) = original; }
	} restore{original, static_cast<size_t>(index)};
	LIBRARY->spellh->objects.at(index) = synthetic;
	ASSERT_TRUE(arrow()->canCastWithoutSkip());
	const auto before = caster->getAvailableHealth();
	const auto enemy = target->getAvailableHealth();
	const auto value = forecast();
	ASSERT_TRUE(actualCast());
	EXPECT_EQ(caster->getAvailableHealth(), before);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
	EXPECT_EQ(battle()->battleActiveUnit(), caster);
	EXPECT_FALSE(caster->moved());
	EXPECT_FLOAT_EQ(value, enemy - target->getAvailableHealth());
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, TimeStoppedCasterCannotCompletePlagueThroughRejectedSpellPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto stop = SpellID(SpellID::decode("new-horizons:timeStop"));
	defenderSideHero->addSpellToSpellbook(stop);
	advanceRound();
	ASSERT_TRUE(castOn(defenderSideHero, stop, caster));
	ASSERT_TRUE(caster->isTimeStopped());
	const auto before = caster->getAvailableHealth();
	const auto duration = marker()->turnsRemain;
	EXPECT_LT(forecast(), 0);
	EXPECT_FALSE(actualCast());
	EXPECT_EQ(caster->getAvailableHealth(), before);
	EXPECT_EQ(marker()->turnsRemain, duration);
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, RepeatedCastValuationDoesNotMutateLiveOrExistingSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(),
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto live = caster->save();
	const auto parentState = parent->getForUpdate(caster->unitId())->save();
	const auto siblingState = sibling->getForUpdate(caster->unitId())->save();
	EXPECT_FLOAT_EQ(forecast(), forecast());
	EXPECT_EQ(caster->save(), live);
	EXPECT_EQ(parent->getForUpdate(caster->unitId())->save(), parentState);
	EXPECT_EQ(sibling->getForUpdate(caster->unitId())->save(), siblingState);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_TRUE(sibling->getProjectedPlagueHits().empty());
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, PerkBoundDefendChoiceThenAcceptedDefendExpiresProtectionAndTicksOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	caster->casts.use(caster->casts.available());
	caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -100, BonusSourceID()));
	ASSERT_EQ(caster->getMovementRange(0), 0);
	ASSERT_FALSE(caster->acquireState()->waitedThisTurn);
	Bonus protection(BonusDuration::STACK_ACTIVATION, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
	caster->addNewBonus(std::make_shared<Bonus>(protection));
	const auto before = caster->getAvailableHealth();
	auto callback = std::make_shared<PlagueActionCallback>(PlayerColor(0));
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, caster, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto chosen = evaluator.selectStackAction(caster);
	ASSERT_EQ(chosen.actionType, EActionType::DEFEND);
	EXPECT_EQ(caster->getAvailableHealth(), before);
	EXPECT_TRUE(caster->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chosen));
	EXPECT_FALSE(caster->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
	EXPECT_LT(caster->getAvailableHealth(), before);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 2);
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, HostileLockDefendTicksAndSpreadsBeforeSealExpiryThenCountsDownOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true)); // Existing real paid Plague fixture.
	auto * spreadTarget = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(5, 5), 1000);
	ASSERT_NE(spreadTarget, nullptr);
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsSorcery::SORCERY_MAGIC_SKILL)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	// Plague captured SP100; the ordinary SP0 seal lasts one round.
	defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID seal(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	defenderSideHero->addSpellToSpellbook(seal);
	advanceRound(); // Existing round packet refreshes the paid hero allowance.
	ASSERT_EQ(battle()->battleActiveUnit(), caster);
	const auto manaBeforeSeal = defenderSideHero->getManaAvailable();
	ASSERT_TRUE(castOn(defenderSideHero, seal, caster));
	EXPECT_LT(defenderSideHero->getManaAvailable(), manaBeforeSeal);
	ASSERT_NE(marker(), nullptr);
	ASSERT_NE(marker()->parameters, nullptr);
	ASSERT_EQ(marker()->turnsRemain, 3);
	ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*caster));
	ASSERT_EQ(newHorizonsSorcery::spellLockPreservesBeneficial(*caster), std::optional<bool>(false));
	ASSERT_TRUE(newHorizonsSorcery::durationPausedBySpellLock(*caster, *marker()));
	const auto seals = caster->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(seal)).And(Selector::type()(BonusType::NONE)));
	ASSERT_EQ(seals->size(), 1u);
	ASSERT_EQ(seals->front()->turnsRemain, 1);
	const auto round = battle()->battleGetRound();
	const auto initialParameters = marker()->parameters->toCustom<JsonNode>();
	ASSERT_LT(initialParameters["lastProcessedRound"].Integer(), round);
	ASSERT_EQ(initialParameters["casterSide"].Integer(), static_cast<int>(BattleSide::DEFENDER));
	const int64_t tick = marker()->val;
	ASSERT_GT(tick, 0);
	ASSERT_EQ(defenderSideHero->getSpellBonus(plague().toSpell(), tick, caster), tick);
	ASSERT_EQ(caster->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 0);
	ASSERT_EQ(caster->getGuardianSpiritHitPoints(), 0);
	ASSERT_EQ(battle()->battleGetHoldTheLineMagicalReductionBasisPoints(caster), 0);
	ASSERT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(caster), 0);
	const auto health = caster->getAvailableHealth();
	ASSERT_GT(health, 2 * tick);
	ASSERT_FALSE(newHorizonsPlague::hasPlague(spreadTarget));
	// Retain the existing Pavise Defend-choice preconditions. Temporary protection
	// must expire before Plague damage even when the hostile seal pauses duration.
	caster->casts.use(caster->casts.available());
	caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER, -100, BonusSourceID()));
	ASSERT_EQ(caster->getMovementRange(0), 0);
	ASSERT_FALSE(caster->acquireState()->waitedThisTurn);
	caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_ACTIVATION,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 100,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(),
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	std::map<uint32_t, JsonNode> parentBefore, siblingBefore, liveBefore;
	for(const auto * unit : battle()->battleGetAllUnits(true))
	{
		parentBefore.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
		siblingBefore.emplace(unit->unitId(), sibling->getForUpdate(unit->unitId())->save());
		liveBefore.emplace(unit->unitId(), unit->acquireState()->save());
	}
	auto callback = std::make_shared<PlagueActionCallback>(PlayerColor(0));
	callback->onBattleStarted(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	BattleEvaluator evaluator(environment, callback, parent, cache, caster, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto chosen = evaluator.selectStackAction(caster);
	ASSERT_EQ(chosen.actionType, EActionType::DEFEND);
	for(const auto & [id, snapshot] : liveBefore)
		EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), snapshot);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), chosen));
	EXPECT_FALSE(caster->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
	EXPECT_EQ(caster->getAvailableHealth(), health - tick);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
	EXPECT_EQ(marker()->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round);
	EXPECT_EQ(marker()->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(),
		initialParameters["spreadAttempts"].Integer() + 1);
	EXPECT_TRUE(newHorizonsSorcery::isSpellLocked(*caster));
	const auto spread = spreadTarget->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(plague())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
	ASSERT_EQ(spread->size(), 1u);
	EXPECT_EQ(spread->front()->val, tick);
	// Re-enter only the shared adapter on an already-completed detached state:
	// this proves deduplication without inventing another accepted live activation.
	const auto checkDedup = [&]()
	{
		auto completed = std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		std::map<uint32_t, JsonNode> before;
		for(const auto * unit : battle()->battleGetAllUnits(true))
			before.emplace(unit->unitId(), completed->getForUpdate(unit->unitId())->save());
		newHorizonsPlague::completeActivation(*completed, *completed->getServerCallback(), caster->unitId());
		for(const auto & [id, snapshot] : before)
			EXPECT_EQ(completed->getForUpdate(id)->save(), snapshot);
		EXPECT_TRUE(completed->getProjectedPlagueHits().empty());
	};
	checkDedup();
	for(size_t answered = 0; answered < battle()->stacks.size() + 1
		&& (battle()->battleGetRound() == round || battle()->battleActiveUnit() != caster); ++answered)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, caster);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleGetRound(), round + 1);
	ASSERT_EQ(battle()->battleActiveUnit(), caster);
	ASSERT_FALSE(newHorizonsSorcery::isSpellLocked(*caster));
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
	EXPECT_EQ(caster->getAvailableHealth(), health - tick);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(caster)));
	EXPECT_EQ(caster->getAvailableHealth(), health - 2 * tick);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 2);
	EXPECT_EQ(marker()->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round + 1);
	EXPECT_EQ(marker()->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(),
		initialParameters["spreadAttempts"].Integer() + 2);
	checkDedup();
	for(const auto & [id, snapshot] : parentBefore)
		EXPECT_EQ(parent->getForUpdate(id)->save(), snapshot);
	for(const auto & [id, snapshot] : siblingBefore)
		EXPECT_EQ(sibling->getForUpdate(id)->save(), snapshot);
	EXPECT_TRUE(parent->getProjectedPlagueHits().empty());
	EXPECT_TRUE(sibling->getProjectedPlagueHits().empty());
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, InfectedPositiveBuffRemainsSelectableWhenDpsBenefitExceedsRequiredTick)
{
	ASSERT_NO_FATAL_FAILURE(prepareBeneficial(1, 1000));
	auto evaluator = beneficialEvaluator();
	const auto before = caster->save();
	const auto allyBefore = ally->save();
	const auto selected = evaluator.findBestCreatureSpell(caster);
	ASSERT_TRUE(selected);
	ASSERT_EQ(selected->spell->getId(), SpellID(SpellID::BLOODLUST));
	ASSERT_EQ(selected->dest.size(), 1u);
	ASSERT_NE(selected->dest.front().unitValue, nullptr);
	const auto * recipient = selected->dest.front().unitValue;
	const float benefit = evaluator.beneficialCreatureSpellOutcomeValue(caster, recipient, selected->spell);
	const float cost = beneficialTickDps(recipient);
	ASSERT_GT(cost, 0);
	ASSERT_GT(benefit, cost);
	EXPECT_FLOAT_EQ(selected->value, benefit - cost);
	EXPECT_EQ(caster->save(), before);
	EXPECT_EQ(ally->save(), allyBefore);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
}

TEST_F(NewHorizonsPlagueActionCompletionAITest, InfectedPositiveBuffIsDeclinedWhenDpsTickCostExceedsEveryUsefulBuff)
{
	ASSERT_NO_FATAL_FAILURE(prepareBeneficial(1000, 1));
	auto evaluator = beneficialEvaluator();
	const auto * spell = SpellID(SpellID::BLOODLUST).toSpell();
	const auto before = caster->save();
	const float benefit = evaluator.beneficialCreatureSpellOutcomeValue(caster, ally, spell);
	const float cost = beneficialTickDps(ally);
	ASSERT_GT(benefit, 0);
	ASSERT_GT(cost, benefit);
	EXPECT_FLOAT_EQ(evaluator.beneficialCreatureSpellOutcomeValue(caster, caster, spell), 0);
	EXPECT_FALSE(evaluator.findBestCreatureSpell(caster));
	EXPECT_EQ(caster->save(), before);
	ASSERT_NE(marker(), nullptr);
	EXPECT_EQ(marker()->turnsRemain, 3);
}
