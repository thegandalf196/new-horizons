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
#include "../../lib/battle/BattleForm.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
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
	bool fullCommandRules = false;

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
		if(!fullCommandRules)
		{
			for(auto & command : commandRules["commands"].Struct())
				for(auto & effect : command.second["effects"].Struct())
				{
					effect.second["base"].Float() = 0;
					effect.second["attack"].Float() = 0;
					effect.second["defense"].Float() = 0;
				}
		}
		heroCommands::validateRules(commandRules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commandRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void preparePolymorph(bool shapeshifter = false, bool delayedEnemy = false, bool rangedThreat = false)
	{
		fullCommandRules = true;
		startGame();
		const SpellID spell(SpellID::decode("new-horizons:polymorph"));
		ASSERT_TRUE(spell.hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 1000);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(shapeshifter)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.shapeshifter"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.shapeshifter"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 1), 2000);
		target = rangedThreat
			? addStack(BattleSide::DEFENDER, creatureByName("core:powerLich"), BattleHex(14, 5), 1000)
			: addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(rightHex), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(target, nullptr);
		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -static_cast<int32_t>(active->getMovementRange());
		active->addNewBonus(std::make_shared<Bonus>(immobilized));
		beginCombat();
		if(delayedEnemy)
		{
			ASSERT_EQ(battle()->battleActiveUnit(), target);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(target->unitSide()), BattleAction::makeWait(target)));
			ASSERT_EQ(battle()->battleActiveUnit(), active);
			ASSERT_TRUE(target->waited());
			ASSERT_TRUE(target->willMove(0));
		}
		else
			ASSERT_TRUE(advanceUntilNextActivation(active));
		callback = std::make_shared<BattleFormAITestCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<BattleFormEnvironment>(gameState());
	}

	CStack * prepareBattleStack(const std::string & species = "core:ogre")
	{
		startGame();
		startBattle();
		auto * stack = addStack(BattleSide::ATTACKER, creatureByName(species), BattleHex(leftHex), 12);
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

TEST_F(NewHorizonsBattleFormAITest, ShapeshifterWeightsExactlyMatchTwoIndependentDrawsAndSignedForecast)
{
	ASSERT_NO_FATAL_FAILURE(preparePolymorph(true));
	const SpellID spell(SpellID::decode("new-horizons:polymorph"));
	const auto board = callback->getBattle(BattleID(0));
	spells::BattleCast cast(board.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&cast);
	const auto * effect = mechanics->findEffect<spells::effects::BattleFormEffect>();
	ASSERT_NE(effect, nullptr);
	const auto forms = effect->formsForTarget(mechanics.get(), target);
	const auto weighted = effect->weightedFormsForTarget(mechanics.get(), target);
	ASSERT_GT(forms.size(), 1u);
	ASSERT_EQ(weighted.size(), forms.size());
	std::map<CreatureID, int64_t> values;
	std::map<CreatureID, uint64_t> expectedWeights;
	for(const auto & form : forms)
	{
		auto outcome = std::make_shared<HypotheticBattle>(environment.get(), board);
		const auto converted = outcome->getForUpdate(target->unitId());
		converted->beginBattleForm(form.creature, effect->getDuration());
		values[form.creature] = static_cast<int64_t>(converted->getCount()) * form.creature.toCreature()->getAIValue();
	}
	for(const auto & first : forms)
		for(const auto & second : forms)
		{
			const auto firstKey = std::pair(values.at(first.creature), first.creature);
			const auto secondKey = std::pair(values.at(second.creature), second.creature);
			++expectedWeights[firstKey <= secondKey ? first.creature : second.creature];
		}
	const auto baseline = projectedOffensivePressure(environment.get(), board, target->unitId(), effect->getDuration(), nullptr);
	double expectedScore = 0;
	uint64_t totalWeight = 0;
	for(const auto & outcome : weighted)
	{
		EXPECT_EQ(outcome.weight, expectedWeights.at(outcome.form.creature));
		EXPECT_EQ(outcome.totalWeight, forms.size() * forms.size());
		totalWeight += outcome.weight;
		const auto pressure = projectedOffensivePressure(environment.get(), board,
			target->unitId(), effect->getDuration(), &outcome.form);
		expectedScore += (baseline - pressure) * static_cast<double>(outcome.weight) / outcome.totalWeight;
	}
	EXPECT_EQ(totalWeight, forms.size() * forms.size());
	const auto liveState = target->save();
	RandomStateArchive rngBefore;
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	ASSERT_NE(rng, nullptr);
	rng->serialize(rngBefore);
	const auto actual = SpellTargetEvaluator::battleFormExpectedOffensiveValue(mechanics.get(), effect,
		{spells::Destination(target)}, environment.get(), board);
	ASSERT_TRUE(actual);
	expectedScore *= 1.0 - static_cast<double>(target->magicResistance()) / 100.0;
	EXPECT_NEAR(*actual, expectedScore, 0.02);
	EXPECT_EQ(target->save(), liveState);
	RandomStateArchive rngAfter;
	rng->serialize(rngAfter);
	EXPECT_EQ(rngAfter.state, rngBefore.state);
}

TEST_F(NewHorizonsBattleFormAITest, RegisteredPolymorphActualPaidAICompetesWithUnmodifiedOrders)
{
	ASSERT_NO_FATAL_FAILURE(preparePolymorph(true, true, true));
	const SpellID spell(SpellID::decode("new-horizons:polymorph"));
	ASSERT_TRUE(target->waited());
	ASSERT_TRUE(target->willMove(0));
	const auto board = callback->getBattle(BattleID(0));
	spells::BattleCast forecastCast(board.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto forecastMechanics = spell.toSpell()->battleMechanics(&forecastCast);
	const auto * effect = forecastMechanics->findEffect<spells::effects::BattleFormEffect>();
	ASSERT_NE(effect, nullptr);
	const auto forecast = SpellTargetEvaluator::battleFormExpectedOffensiveValue(forecastMechanics.get(), effect,
		{spells::Destination(target)}, environment.get(), board);
	ASSERT_TRUE(forecast);
	ASSERT_GT(*forecast, 0.0f) << "The lawful delayed enemy activation must offer a beneficial form forecast";
	const auto liveState = target->save();
	const auto mana = attackerSideHero->getManaAvailable();
	RandomStateArchive rngBefore;
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	ASSERT_NE(rng, nullptr);
	rng->serialize(rngBefore);
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL)
		<< "selected command=" << static_cast<int>(action.command)
		<< ", spell=" << action.spell.getNum() << ", Polymorph forecast value=" << *forecast;
	ASSERT_EQ(action.spell, spell);
	EXPECT_EQ(target->save(), liveState);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	RandomStateArchive rngAfter;
	rng->serialize(rngAfter);
	EXPECT_EQ(rngAfter.state, rngBefore.state);
	const auto hp = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 12);
	EXPECT_TRUE(target->hasBattleForm());
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_TRUE(target->hasBonus(CSelector(battle::isPolymorphMarker)));
}

TEST_F(NewHorizonsBattleFormAITest, DetachedRoundClockPausesForTimeStopAndFinalSpellLockRound)
{
	auto * stack = prepareBattleStack();
	ASSERT_NE(stack, nullptr);
	const auto liveState = stack->save();
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto unit = branch->getForUpdate(stack->unitId());
	unit->beginBattleForm(creatureByName("core:ogreMage"), 2);
	const SpellID polymorph(SpellID::decode("new-horizons:polymorph"));
	const auto marker = battle::polymorphMarker(polymorph, PlayerColor(0));
	branch->addUnitBonus(unit->unitId(), {marker});
	Bonus stop(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	branch->addUnitBonus(unit->unitId(), {stop});
	branch->nextRound();
	EXPECT_EQ(unit->getBattleFormRoundsRemaining(), 2);
	branch->removeUnitBonus(unit->unitId(), {marker});
	EXPECT_TRUE(unit->hasBattleForm());
	EXPECT_TRUE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	branch->removeUnitBonus(unit->unitId(), {stop});
	const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	Bonus resistance(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE, BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock));
	resistance.turnsRemain = 1;
	Bonus preserve(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SPELL_EFFECT, -1, BonusSourceID(lock));
	preserve.turnsRemain = 1;
	branch->addUnitBonus(unit->unitId(), {resistance, preserve});
	branch->nextRound();
	EXPECT_EQ(unit->getBattleFormRoundsRemaining(), 2) << "Capture Spell Lock before aging its last round";
	branch->nextRound();
	EXPECT_EQ(unit->getBattleFormRoundsRemaining(), 1);
	branch->nextRound();
	EXPECT_FALSE(unit->hasBattleForm());
	EXPECT_FALSE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	EXPECT_EQ(unit->unitType()->getId(), stack->unitType()->getId());
	EXPECT_EQ(stack->save(), liveState);
}

TEST_F(NewHorizonsBattleFormAITest, DetachedNoSpaceExpiryAndDispelKeepMarkerUntilLegalReturn)
{
	auto * stack = prepareBattleStack("core:boneDragon");
	ASSERT_NE(stack, nullptr);
	const auto liveState = stack->save();
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto unit = branch->getForUpdate(stack->unitId());
	unit->beginBattleForm(creatureByName("core:peasant"), 1);
	const SpellID polymorph(SpellID::decode("new-horizons:polymorph"));
	const auto marker = battle::polymorphMarker(polymorph, PlayerColor(0));
	branch->addUnitBonus(unit->unitId(), {marker});
	std::vector<uint32_t> blockers;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable() || branch->battleGetUnitByPos(hex, false))
			continue;
		battle::UnitInfo info;
		info.id = branch->battleNextUnitId();
		info.type = creatureByName("core:pikeman");
		info.count = 1;
		info.side = BattleSide::ATTACKER;
		info.position = hex;
		JsonNode data;
		info.save(data);
		branch->addUnit(info.id, data);
		blockers.push_back(info.id);
	}
	const auto hp = unit->getAvailableHealth();
	const auto position = unit->getPosition();
	branch->nextRound();
	EXPECT_TRUE(unit->hasBattleForm());
	EXPECT_TRUE(unit->isBattleFormRestorationPending());
	EXPECT_TRUE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	branch->removeUnitBonus(unit->unitId(), {marker});
	EXPECT_TRUE(unit->hasBattleForm());
	EXPECT_TRUE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	EXPECT_EQ(unit->getAvailableHealth(), hp);
	EXPECT_EQ(unit->getPosition(), position);
	for(const auto id : blockers)
		branch->removeUnit(id);
	branch->nextRound();
	EXPECT_FALSE(unit->hasBattleForm());
	EXPECT_FALSE(unit->isBattleFormRestorationPending());
	EXPECT_FALSE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	EXPECT_EQ(unit->getAvailableHealth(), hp);
	EXPECT_EQ(stack->save(), liveState);
}
