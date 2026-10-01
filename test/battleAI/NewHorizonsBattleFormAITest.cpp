/*
 * NewHorizonsBattleFormAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license is available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../SpellPointTestUtils.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/effects/BattleForm.h"

namespace
{
struct RandomStateArchive
{
	bool saving = true;
	std::string state;
	void operator&(std::string & value) { state = value; }
};

class ScopeExit final
{
	std::function<void()> callback;

public:
	explicit ScopeExit(std::function<void()> callback_)
		: callback(std::move(callback_))
	{}

	~ScopeExit()
	{
		callback();
	}
};

class BattleFormEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit BattleFormEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BattleFormAITestCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	BattleFormAITestCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};

CSelector creatureNativeSelector(CreatureID creature)
{
	const BonusSourceID sourceId(creature);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::CREATURE_ABILITY && bonus->sid == sourceId;
	});
}

CSelector creatureRankSelector(CreatureID creature)
{
	const BonusSourceID sourceId(creature);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::STACK_EXPERIENCE && bonus->sid == sourceId;
	});
}

CSelector exactSpellBonusSelector(SpellID spell)
{
	const BonusSourceID sourceId(spell);
	return CSelector([sourceId](const Bonus * bonus)
	{
		return bonus && bonus->source == BonusSource::SPELL_EFFECT && bonus->sid == sourceId;
	});
}

class ScopedCreatureBonus final
{
	CCreature * creature;
	Bonus bonus;

public:
	ScopedCreatureBonus(CCreature * creature_, Bonus bonus_)
		: creature(creature_),
		  bonus(std::move(bonus_))
	{
		creature->addNewBonus(std::make_shared<Bonus>(bonus));
	}

	~ScopedCreatureBonus()
	{
		creature->removeBonuses(CSelector([this](const Bonus * candidate)
		{
			return candidate
				&& candidate->source == bonus.source
				&& candidate->sid == bonus.sid
				&& candidate->type == bonus.type
				&& candidate->val == bonus.val;
		}));
	}
};

class RankedStackInstance final : public CStackInstance
{
	int rank;

public:
	RankedStackInstance(CreatureID creature, int32_t count, int rank_)
		: CStackInstance(nullptr, creature, count, true),
		  rank(rank_)
	{
		attachToSource(*creature.toCreature());
	}

	int getExpRank() const override
	{
		return rank;
	}
};

class ScopedStackRank final
{
	CStack * stack;
	const CStackInstance * originalBase;
	RankedStackInstance rankedBase;

public:
	ScopedStackRank(CStack * stack_, CreatureID creature, int rank)
		: stack(stack_),
		  originalBase(stack_->base),
		  rankedBase(creature, stack_->unitBaseAmount(), rank)
	{
		stack->base = &rankedBase;
		stack->nodeHasChanged();
	}

	~ScopedStackRank()
	{
		stack->base = originalBase;
		stack->nodeHasChanged();
	}
};

float projectedOffensivePressure(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & battleState,
	uint32_t unitId,
	int32_t duration,
	const spells::effects::BattleFormEffect::BattleFormCandidate * candidate)
{
	auto projectedBattle = std::make_shared<HypotheticBattle>(environment, battleState);
	auto projectedTarget = projectedBattle->getForUpdate(unitId);
	if(candidate)
	{
		projectedTarget->beginBattleForm(candidate->creature, duration);
		projectedTarget->setPosition(candidate->landing);
	}

	DamageCache damageCache;
	PotentialTargets actions(projectedTarget.get(), damageCache, projectedBattle);
	const float actionValue = actions.berserk
		? actions.expectedBerserkActionValue()
		: (actions.possibleAttacks.empty() ? 0.0f : actions.possibleAttacks.front().attackValue());

	float total = 0.0f;
	for(int turn = 0; turn < std::min(duration, 2); ++turn)
		if(projectedTarget->willMove(turn))
			total += actionValue;
	return total;
}
}

class NewHorizonsBattleFormAITest : public HeroCommandFixture
{
protected:
	std::shared_ptr<BattleFormEnvironment> environment;
	std::shared_ptr<BattleFormAITestCallback> callback;
	CStack * active = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
		JsonNode commandRules = combatRules["combat"]["heroCommands"];
		// Keep the real Hero Action/Order allowance path active while isolating the
		// battle-form cast from independent command-scoring heuristics.
		for(auto & command : commandRules["commands"].Struct())
			for(auto & effect : command.second["effects"].Struct())
			{
				effect.second["base"].Float() = 0;
				effect.second["attack"].Float() = 0;
				effect.second["defense"].Float() = 0;
			}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	CStack * prepareBattleStack()
	{
		startGame();
		startBattle();
		auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(leftHex), 12);
		auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:orc"), BattleHex(rightHex + 5), 10);
		if(!stack || !enemy)
			return nullptr;
		beginCombat();
		environment = std::make_shared<BattleFormEnvironment>(gameState());
		callback = std::make_shared<BattleFormAITestCallback>();
		callback->onBattleStarted(battle());
		return stack;
	}

	bool advanceUntilNextActivation(const CStack * wanted)
	{
		for(int attempt = 0; attempt < 32; ++attempt)
		{
			const auto * current = battle()->battleActiveUnit();
			if(!current)
				return false;
			if(current == wanted)
				return true;
			const auto player = battle()->sideToPlayer(current->unitSide());
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
				BattleAction::makeDefend(current)))
				return false;
		}
		return false;
	}

	void prepareSelectionBattle()
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 1), 100000);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(rightHex), 1);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(target, nullptr);
		active->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));

		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -static_cast<int32_t>(active->getMovementRange());
		active->addNewBonus(std::make_shared<Bonus>(immobilized));

		beginCombat();
		ASSERT_TRUE(advanceUntilNextActivation(active));
		callback = std::make_shared<BattleFormAITestCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<BattleFormEnvironment>(gameState());
	}

	template<typename Callback>
	void withBattleFormMagicArrow(Callback && testBody)
	{
		auto * spell = const_cast<CSpell *>(SpellID(SpellID::MAGIC_ARROW).toSpell());
		ASSERT_NE(spell, nullptr);

		std::array<JsonNode, GameConstants::SPELL_SCHOOL_LEVELS> originalBattleEffects;
		for(int32_t level = 0; level < GameConstants::SPELL_SCHOOL_LEVELS; ++level)
			originalBattleEffects[level] = spell->getLevelInfo(level).battleEffects;

		ScopeExit restore([&]
		{
			for(int32_t level = 0; level < GameConstants::SPELL_SCHOOL_LEVELS; ++level)
				const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(level)).battleEffects = originalBattleEffects[level];
			spell->setupMechanics();
		});

		JsonNode testBattleEffects;
		testBattleEffects["battleForm"]["type"] = JsonNode("core:battleForm");
		testBattleEffects["battleForm"]["duration"] = JsonNode(2);
		for(int32_t level = 0; level < GameConstants::SPELL_SCHOOL_LEVELS; ++level)
			const_cast<CSpell::LevelInfo &>(spell->getLevelInfo(level)).battleEffects = testBattleEffects;
		spell->setupMechanics();

		std::forward<Callback>(testBody)(spell);
	}
};

TEST_F(NewHorizonsBattleFormAITest, AuthoritativeAndDetachedViewsReplaceNestedNativeBonusesWithoutLosingBuffs)
{
	auto * stack = prepareBattleStack();
	ASSERT_NE(stack, nullptr);
	const CreatureID original = creatureByName("core:ogre");
	const CreatureID firstForm = creatureByName("core:ogreMage");
	const CreatureID secondForm = creatureByName("core:demon");

	const Bonus rankGated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::STACK_EXPERIENCE, 73, BonusSourceID(firstForm));
	auto rankGatedBonus = rankGated;
	rankGatedBonus.limiter = std::make_shared<RankRangeLimiter>(0);
	ScopedCreatureBonus rankBonus(const_cast<CCreature *>(firstForm.toCreature()), std::move(rankGatedBonus));

	const SpellID speedSpell(SpellID::HASTE);
	const Bonus spellBuff(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 9, BonusSourceID(speedSpell));
	stack->addNewBonus(std::make_shared<Bonus>(spellBuff));

	const auto stackId = stack->unitId();
	const auto owner = stack->unitOwner();
	const auto side = stack->unitSide();
	const auto slot = stack->unitSlot();
	const auto position = stack->getPosition();
	const auto initiative = stack->getInitiative();
	ASSERT_FALSE(stack->getBonuses(creatureNativeSelector(original))->empty());

	stack->beginBattleForm(firstForm, 2);
	EXPECT_EQ(stack->unitType()->getId(), firstForm);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureRankSelector(firstForm))->empty())
		<< "Rank-limited native abilities must be evaluated against the original stack rank";
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

	stack->beginBattleForm(secondForm, 2);
	EXPECT_EQ(stack->unitType()->getId(), secondForm);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

	stack->beginBattleForm(original, 2);
	EXPECT_EQ(stack->unitType()->getId(), original);
	EXPECT_FALSE(stack->getBonuses(creatureNativeSelector(original))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	EXPECT_FALSE(stack->getBonuses(exactSpellBonusSelector(speedSpell))->empty());
	EXPECT_EQ(stack->unitId(), stackId);
	EXPECT_EQ(stack->unitOwner(), owner);
	EXPECT_EQ(stack->unitSide(), side);
	EXPECT_EQ(stack->unitSlot(), slot);
	EXPECT_EQ(stack->getPosition(), position);
	EXPECT_EQ(stack->getInitiative(), initiative);

	const auto liveNativeBeforeProjection = stack->getBonuses(creatureNativeSelector(original))->size();
	{
		// Give the real stack a synthetic rank context so nested projections prove
		// that effective native RankRangeLimiters still see its original rank.
		ScopedStackRank rankContext(stack, original, 1);
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		auto projectedFirst = parent->getForUpdate(stackId);
		const auto initialTreeVersion = projectedFirst->getTreeVersion();
		projectedFirst->beginBattleForm(firstForm, 2);
		EXPECT_NE(projectedFirst->getTreeVersion(), initialTreeVersion);
		EXPECT_TRUE(projectedFirst->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_FALSE(projectedFirst->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto projectedSecond = child->getForUpdate(stackId);
		projectedSecond->beginBattleForm(secondForm, 2);
		EXPECT_TRUE(projectedSecond->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_TRUE(projectedSecond->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_TRUE(projectedSecond->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_FALSE(projectedSecond->getBonuses(creatureNativeSelector(secondForm))->empty());
		EXPECT_FALSE(projectedSecond->getBonuses(exactSpellBonusSelector(speedSpell))->empty());

		auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
		auto projectedOriginal = grandchild->getForUpdate(stackId);
		projectedOriginal->beginBattleForm(original, 2);
		EXPECT_FALSE(projectedOriginal->getBonuses(creatureNativeSelector(original))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureNativeSelector(firstForm))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureRankSelector(firstForm))->empty());
		EXPECT_TRUE(projectedOriginal->getBonuses(creatureNativeSelector(secondForm))->empty());
		EXPECT_FALSE(projectedOriginal->getBonuses(exactSpellBonusSelector(speedSpell))->empty());
		EXPECT_EQ(projectedOriginal->unitId(), stackId);
		EXPECT_EQ(projectedOriginal->unitOwner(), owner);
		EXPECT_EQ(projectedOriginal->unitSide(), side);
		EXPECT_EQ(projectedOriginal->unitSlot(), slot);
		EXPECT_EQ(projectedOriginal->getPosition(), position);
		EXPECT_EQ(projectedOriginal->getInitiative(), initiative);
	}

	EXPECT_EQ(stack->unitType()->getId(), original);
	EXPECT_EQ(stack->getBonuses(creatureNativeSelector(original))->size(), liveNativeBeforeProjection);
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(firstForm))->empty());
	EXPECT_TRUE(stack->getBonuses(creatureNativeSelector(secondForm))->empty());
	stack->endBattleForm();
}

TEST_F(NewHorizonsBattleFormAITest, ExpectedValueUsesSignedMeanOfFullPoolAndEvaluatorSelectsTheInjectedEffect)
{
	const auto ogre = creatureByName("core:ogre");
	const auto cyclopKing = creatureByName("core:cyclopKing");
	ScopedCreatureBonus nativeOgreDamage(const_cast<CCreature *>(ogre.toCreature()),
		Bonus(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::CREATURE_ABILITY,
			10000, BonusSourceID(ogre), BonusCustomSubtype::creatureDamageBoth));
	ScopedCreatureBonus nativeCyclopKingDamage(const_cast<CCreature *>(cyclopKing.toCreature()),
		Bonus(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::CREATURE_ABILITY,
			50000, BonusSourceID(cyclopKing), BonusCustomSubtype::creatureDamageBoth));
	// These synthetic native abilities must be present when the fixture stacks are
	// constructed. Adding a prototype bonus afterward can make the first baseline
	// view stale until a later same-species battle-form projection refreshes it.
	prepareSelectionBattle();

	withBattleFormMagicArrow([&](CSpell * spell)
	{
		const auto liveStateBefore = target->save();
		RandomStateArchive randomBefore;
		auto * liveRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
		ASSERT_NE(liveRng, nullptr);
		liveRng->serialize(randomBefore);
		const auto battleCallback = callback->getBattle(BattleID(0));
		spells::BattleCast cast(battleCallback.get(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		const auto * battleForm = mechanics->findEffect<spells::effects::BattleFormEffect>();
		ASSERT_NE(battleForm, nullptr);
		ASSERT_EQ(mechanics->getTargetTypes(), (std::vector<spells::AimType>{spells::AimType::CREATURE}));

		const auto viableTargets = SpellTargetEvaluator::getViableTargets(mechanics.get());
		ASSERT_EQ(viableTargets.size(), 1u);
		ASSERT_EQ(viableTargets.front().size(), 1u);
		ASSERT_EQ(viableTargets.front().front().unitValue, target);
		const auto targetSnapshot = spells::Target{spells::Destination(target)};

		const auto candidates = battleForm->formsForTarget(mechanics.get(), target);
		ASSERT_GT(candidates.size(), 2u);
		ASSERT_TRUE(vstd::contains_if(candidates, [ogre](const auto & candidate)
		{
			return candidate.creature == ogre;
		}));
		ASSERT_TRUE(vstd::contains_if(candidates, [cyclopKing](const auto & candidate)
		{
			return candidate.creature == cyclopKing;
		}));

		const float baselinePressure = projectedOffensivePressure(environment.get(), battleCallback,
			target->unitId(), battleForm->getDuration(), nullptr);
		double signedMean = 0.0;
		std::vector<float> signedDeltas;
		signedDeltas.reserve(candidates.size());
		int beneficialOutcomes = 0;
		int harmfulOutcomes = 0;
		for(const auto & candidate : candidates)
		{
			const float pressure = projectedOffensivePressure(environment.get(), battleCallback,
				target->unitId(), battleForm->getDuration(), &candidate);
			const float delta = baselinePressure - pressure;
			signedDeltas.push_back(delta);
			signedMean += delta;
			beneficialOutcomes += delta > 0.01f;
			harmfulOutcomes += delta < -0.01f;
		}
		ASSERT_GT(beneficialOutcomes, 0);
		ASSERT_GT(harmfulOutcomes, 0)
			<< "The overpowered Cyclop King form must remain a signed penalty in the mean";
		const float mean = static_cast<float>(signedMean / candidates.size());
		const float applicationChance = 1.0f - static_cast<float>(target->magicResistance()) / 100.0f;
		const auto expectedScore = mean * applicationChance;
		const auto actualScore = SpellTargetEvaluator::battleFormExpectedOffensiveValue(
			mechanics.get(), battleForm, targetSnapshot, environment.get(), battleCallback);
		ASSERT_TRUE(actualScore);
		EXPECT_NEAR(*actualScore, expectedScore, 0.02f);
		EXPECT_GT(expectedScore, 0.0f)
			<< "The uniformly sampled pool should be favorable on average in this fixture";
		RNGStub midpointGenerator;
		const auto midpointIndex = midpointGenerator.nextInt64(0, candidates.size() - 1);
		EXPECT_GT(std::abs(expectedScore - signedDeltas[midpointIndex] * applicationChance), 0.1f)
			<< "The expected value must not collapse to RNGStub's midpoint sample";

		EXPECT_EQ(target->unitType()->getId(), ogre);
		EXPECT_EQ(target->getPosition(), BattleHex(rightHex));

		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		ASSERT_TRUE(evaluator.canCastSpell());
		ASSERT_TRUE(evaluator.attemptCastingSpell(active));
		ASSERT_EQ(callback->submitted.size(), 1u);
		const auto action = callback->submitted.front();
		ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
		EXPECT_EQ(action.spell, SpellID(SpellID::MAGIC_ARROW));
		const auto selectedTarget = action.getTarget(battleCallback.get());
		ASSERT_EQ(selectedTarget.size(), 1u);
		EXPECT_EQ(selectedTarget.front().unitValue, target);
		EXPECT_EQ(target->save(), liveStateBefore);
		RandomStateArchive randomAfter;
		liveRng->serialize(randomAfter);
		EXPECT_EQ(randomAfter.state, randomBefore.state);

		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_TRUE(vstd::contains_if(candidates, [this](const auto & candidate)
		{
			return candidate.creature == target->unitType()->getId();
		})) << "The temporarily injected effect must resolve to a member of the shared runtime pool";
	});
}
