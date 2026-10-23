/*
 * NewHorizonsStormOfDaggersTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include <vcmi/Environment.h>
#include <vstd/RNG.h>

#include <array>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <utility>
#include <vector>
#include "../../../lib/spells/Problem.h"

#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto stormOfDaggersKey = "new-horizons:stormOfDaggers";

SpellID stormOfDaggersSpell()
{
	return SpellID(SpellID::decode(stormOfDaggersKey));
}
}


namespace
{
class MirrorCharacterizationEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MirrorCharacterizationEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

using MirrorRecipientSnapshot = std::map<uint32_t, std::pair<int64_t, int32_t>>;

MirrorRecipientSnapshot mirrorRecipientSnapshot(const CBattleInfoCallback & callback)
{
	MirrorRecipientSnapshot result;
	for(const auto * unit : callback.battleGetAllUnits(false))
		result.emplace(unit->unitId(), std::make_pair(unit->getAvailableHealth(), unit->getCount()));
	return result;
}

std::vector<std::byte> mirrorRandomizerBytes(GameRandomizer & randomizer)
{
	CMemorySerializer wire;
	wire.oser & randomizer;
	return wire.extractBuffer();
}

}

class NewHorizonsStormOfDaggersTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	std::vector<CStack *> enemies;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		rules["warcasting"] = JsonNode(false);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 100, int enemyCount = 6, bool hypnotizedLegs = false)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto spell = stormOfDaggersSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		friendly = addStack(hypnotizedLegs ? BattleSide::DEFENDER : BattleSide::ATTACKER,
			creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		for(int index = 0; index < enemyCount; ++index)
			enemies.push_back(addStack(hypnotizedLegs && index == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER,
				creatureByName("core:pikeman"),
				BattleHex(12, 2 + index), 1000));
		if(hypnotizedLegs)
			for(auto * unit : {friendly, enemies.front()})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
					BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
	}

	// Desired provisional compound-Mirror behavior. Ordinary legality remains unchanged.
	std::array<int64_t, 8> mirrorResources() const
	{
		const auto & attacker = battle()->getSide(BattleSide::ATTACKER);
		const auto & defender = battle()->getSide(BattleSide::DEFENDER);
		return {attackerSideHero->getNormalSpellPoints(), attackerSideHero->getBufferSpellPoints(),
			attacker.temporaryBufferRemaining, attacker.acceptedHeroManaSpent,
			defenderSideHero->getNormalSpellPoints(), defenderSideHero->getBufferSpellPoints(),
			defender.temporaryBufferRemaining, defender.acceptedHeroManaSpent};
	}

	std::unique_ptr<GameRandomizer> mirrorCloneRandomizer()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		auto result = std::make_unique<GameRandomizer>(*gameState());
		wire.iser & *result;
		return result;
	}

	std::map<uint32_t, bool> mirrorAdvanceResistance(GameRandomizer & randomizer)
	{
		std::map<uint32_t, bool> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(newHorizonsSorcery::isSpellLocked(*unit))
				continue;
			result.emplace(unit->unitId(), randomizer.getDefault().nextInt(0, 99) < unit->magicResistance());
		}
		return result;
	}

	void mirrorPrepareResources(const CStack * active, SpellID spell)
	{
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			ASSERT_LE(hero->luckVal(), 0);
		}
		// Four permanent Buffer points exercise the normal-pool remainder charge.
		// Both temporary origins are explicitly zero and must stay zero.
		attackerSideHero->initializeSpellPoints(20, 4);
		defenderSideHero->initializeSpellPoints(17, 3);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			ASSERT_EQ(battle()->getSide(side).temporaryBufferRemaining, 0);
			ASSERT_FALSE(battle()->getAdverseCombatRerollState(side).available());
		}
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), PlayerColor(0));
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), spell));
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, spell));
	}

	void mirrorSelectResistanceBranch(uint32_t recipientId, bool resisted)
	{
		for(int candidateSeed = 0; candidateSeed < 256; ++candidateSeed)
		{
			gameHandler->randomizer->setSeed(candidateSeed);
			auto expected = mirrorCloneRandomizer();
			if(mirrorAdvanceResistance(*expected).at(recipientId) == resisted)
				return;
		}
		FAIL() << "Could not select the requested genuine 75% resistance branch";
	}

	void mirrorPrepareNativeFaerieDragonProducer()
	{
		// Authored-map creature identity, with its shipped intrinsic ability:
		// no test-added MAGIC_MIRROR bonus and no hero-access override.
		auto * dragon = addStack(BattleSide::DEFENDER, creatureByName("core:fairieDragon"),
			BattleHex(12, 9), 10);
		ASSERT_NE(dragon, nullptr);
		const auto mirrors = dragon->getAllBonuses(Selector::type()(BonusType::MAGIC_MIRROR));
		ASSERT_EQ(mirrors->size(), 1u);
		const auto mirror = dragon->getBonus(Selector::type()(BonusType::MAGIC_MIRROR));
		ASSERT_TRUE(mirror);
		ASSERT_EQ(mirror->source, BonusSource::CREATURE_ABILITY);
		ASSERT_EQ(mirror->sid, BonusSourceID(dragon->creatureId()));
		ASSERT_EQ(mirror->val, 20);
		ASSERT_EQ(dragon->valOfBonuses(BonusType::MAGIC_MIRROR), 20);
		ASSERT_EQ(dragon->magicResistance(), 0);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), SpellID::MAGIC_MIRROR));
		ASSERT_FALSE(newHorizonsMagic::spellAllowedByHeroRoster(battle()->getMagicRules(), SpellID::MAGIC_MIRROR));
		enemies[0] = dragon;
		for(int candidateSeed = 0; candidateSeed < 256; ++candidateSeed)
		{
			gameHandler->randomizer->setSeed(candidateSeed);
			auto expected = mirrorCloneRandomizer();
			mirrorAdvanceResistance(*expected);
			if(expected->getDefault().nextInt(0, 99) < 20)
				return; // Leave the live stream at this seed, before any resistance draw.
		}
		FAIL() << "Could not select the shipped Faerie Dragon's genuine 20% Mirror branch";
	}

	void mirrorPrepareTriggeredHeroLuck()
	{
		ASSERT_EQ(newHorizonsHeroLuck::damageMultiplierPercent(battle()->getMagicRules()), 200);
		ASSERT_EQ(newHorizonsHeroLuck::manaRefundPercent(battle()->getMagicRules()), 50);
		const SecondarySkill luck(SecondarySkill::decode("new-horizons:luck"));
		ASSERT_TRUE(luck.hasValue());
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->setSecSkillLevel(luck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({"new-horizons:luck", "new-horizons:luck.fortuneSFavor"});
			hero->setSecSkillLevel(luck, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({"new-horizons:luck", "new-horizons:luck.gambler"});
			hero->setSecSkillLevel(luck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({"new-horizons:luck", "new-horizons:luck.twistOfFate"});
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::LUCK, BonusSource::OTHER, 1 - hero->luckVal(), BonusSourceID()));
			ASSERT_EQ(hero->luckVal(), 1);
			ASSERT_TRUE(newHorizonsHeroLuck::hasErraticMana(hero));
			ASSERT_TRUE(newHorizonsHeroLuck::hasDruidicLuck(hero));
		}
		// Select a real triggered branch under the installed chance curve; no
		// probability override, fabricated receipt or live-stream probe.
		for(int candidate = 0; candidate < 256; ++candidate)
		{
			gameHandler->randomizer->setSeed(candidate * 104729);
			auto random = mirrorCloneRandomizer();
			const auto receipt = random->rollHeroLuck(attackerSideHero->id, attackerSideHero->luckVal());
			if(receipt && receipt->triggered(newHorizonsHeroLuck::chanceScalePercent(
				battle()->getMagicRules(), newHorizonsHeroLuck::Proc::ErraticMana))
				&& receipt->triggered(newHorizonsHeroLuck::chanceScalePercent(
					battle()->getMagicRules(), newHorizonsHeroLuck::Proc::DruidicLuck)))
				return;
		}
		FAIL() << "No genuine jointly triggered parent Hero Luck branch in bounded seeds";
	}

	struct MirrorScenario
	{
		int mirrorIndex = -1; // 0 first, 1 later, 2 both original recipients.
		std::optional<bool> originalResistance = std::nullopt;
		bool nativeProducer = false;
		std::optional<bool> redirectedResistance = std::nullopt;
		bool lockedRedirect = false;
		bool mirrorOnRedirect = false;
		bool puppetReflector = false;
		bool triggeredHeroLuck = false;
		bool hypnotizedLegs = false;
	};

	struct MirrorExpected
	{
		MirrorRecipientSnapshot units;
		std::vector<std::byte> random;
		std::optional<newHorizonsHeroLuck::Receipt> heroLuck = std::nullopt;
		std::set<uint32_t> reflected;
		std::set<uint32_t> resisted;
		std::vector<uint32_t> affected;
		std::vector<std::pair<uint32_t, int64_t>> injuries;
		BattleHex tile;
		int64_t damage = 0;
	};

	void mirrorPrepareDesiredScenario(const MirrorScenario & scenario)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(100, 6, scenario.hypnotizedLegs));
		if(scenario.nativeProducer)
		{
			ASSERT_EQ(scenario.mirrorIndex, 0);
			ASSERT_NO_FATAL_FAILURE(mirrorPrepareNativeFaerieDragonProducer());
		}
		ASSERT_NO_FATAL_FAILURE(mirrorPrepareResources(friendly, stormOfDaggersSpell()));
		for(int index = 0; index < 2; ++index)
			if(!scenario.nativeProducer && (scenario.mirrorIndex == index || scenario.mirrorIndex == 2))
				enemies[index]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
					BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));
		if(scenario.originalResistance)
		{
			ASSERT_EQ(scenario.mirrorIndex, -1);
			enemies[0]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 75, BonusSourceID()));
			ASSERT_NO_FATAL_FAILURE(mirrorSelectResistanceBranch(enemies[0]->unitId(), *scenario.originalResistance));
		}
		if(scenario.redirectedResistance)
			friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 75, BonusSourceID()));
		if(scenario.lockedRedirect)
		{
			const SpellID lock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
			auto seal = std::make_shared<Bonus>(BonusDuration::N_TURNS,
				BonusType::MAGIC_RESISTANCE, BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock));
			seal->turnsRemain = 2;
			friendly->addNewBonus(seal);
			const auto marker = friendly->getBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(lock))
				.And(Selector::type()(BonusType::MAGIC_RESISTANCE)));
			ASSERT_EQ(marker, seal);
			ASSERT_EQ(marker->val, 100);
			ASSERT_TRUE(Bonus::NTurns(marker.get()));
			ASSERT_EQ(marker->turnsRemain, 2);
			ASSERT_TRUE(newHorizonsSorcery::isSpellLocked(*friendly));
		}
		if(scenario.mirrorOnRedirect)
			friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));
		if(scenario.puppetReflector)
		{
			const SpellID puppet(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
			SetStackEffect control;
			control.battleID = BattleID(0);
			control.toAdd.emplace_back(enemies[0]->unitId(),
				std::vector<Bonus>{newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(0))});
			gameHandler->sendAndApply(control);
			ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), enemies[0]));
			ASSERT_EQ(enemies[0]->unitSide(), BattleSide::DEFENDER);
			ASSERT_EQ(battle()->battleGetActionController(enemies[0]), PlayerColor(0));
		}
		ASSERT_EQ(battle()->battleGetOwner(friendly), PlayerColor(0));
		ASSERT_EQ(battle()->battleGetOwner(enemies[0]), PlayerColor(1));
		if(scenario.hypnotizedLegs)
		{
			ASSERT_EQ(friendly->unitSide(), BattleSide::DEFENDER);
			ASSERT_EQ(enemies[0]->unitSide(), BattleSide::ATTACKER);
			ASSERT_EQ(battle()->battleGetActionController(friendly), PlayerColor(0));
			ASSERT_EQ(battle()->battleGetActionController(enemies[0]), PlayerColor(1));
		}
		ASSERT_EQ(enemies[0]->magicResistance(), scenario.originalResistance ? 75 : 0);
		ASSERT_EQ(enemies[1]->magicResistance(), 0);
		// Absolute Spell Lock is recognized by its marker; ordinary chance is still capped.
		ASSERT_EQ(friendly->magicResistance(), scenario.lockedRedirect || scenario.redirectedResistance ? 75 : 0);
		if(scenario.redirectedResistance)
		{
			ASSERT_EQ(scenario.mirrorIndex, 0);
			ASSERT_NO_FATAL_FAILURE(mirrorSelectRedirectedResistanceBranch(*scenario.redirectedResistance));
		}
		if(scenario.triggeredHeroLuck)
		{
			ASSERT_NO_FATAL_FAILURE(mirrorPrepareTriggeredHeroLuck());
		}
	}

	void mirrorSelectRedirectedResistanceBranch(bool resisted)
	{
		const std::vector<const battle::Unit *> recipients{friendly};
		for(int candidate = 0; candidate < 256; ++candidate)
		{
			gameHandler->randomizer->setSeed(candidate * 104729);
			auto expected = mirrorCloneRandomizer();
			mirrorAdvanceResistance(*expected);
			expected->getDefault().nextInt(0, 99);
			RandomGeneratorUtil::nextItem(recipients, expected->getDefault());
			if((expected->getDefault().nextInt(0, 99) < 75) == resisted)
				return;
		}
		FAIL() << "Could not select redirected genuine 75% resistance branch";
	}

	void mirrorExpectedResolution(const MirrorScenario & scenario,
		const std::vector<CStack *> & selected, MirrorExpected & expected)
	{
		expected.units = mirrorRecipientSnapshot(*battle());
		expected.tile = selected[0]->getPosition();
		auto random = mirrorCloneRandomizer();
		if(scenario.triggeredHeroLuck)
		{
			expected.heroLuck = random->rollHeroLuck(attackerSideHero->id, attackerSideHero->luckVal());
			ASSERT_TRUE(expected.heroLuck);
			for(const auto proc : {newHorizonsHeroLuck::Proc::ErraticMana, newHorizonsHeroLuck::Proc::DruidicLuck})
			{
				ASSERT_TRUE(expected.heroLuck->triggered(newHorizonsHeroLuck::chanceScalePercent(battle()->getMagicRules(), proc)));
				expected.heroLuck->mark(proc);
			}
		}
		const auto resistance = mirrorAdvanceResistance(*random);
		if(scenario.originalResistance)
		{
			ASSERT_EQ(resistance.at(selected[0]->unitId()), *scenario.originalResistance);
		}
		std::map<uint32_t, decltype(friendly->acquireState())> states;
		const std::vector<const battle::Unit *> recipients{friendly};
		for(int index = 0; index < 2; ++index)
		{
			const auto * recipient = selected[index];
			bool resisted = resistance.at(recipient->unitId());
			if(scenario.mirrorIndex == index || scenario.mirrorIndex == 2)
			{
				ASSERT_LT(random->getDefault().nextInt(0, 99), scenario.nativeProducer ? 20 : 100);
				ASSERT_EQ(*RandomGeneratorUtil::nextItem(recipients, random->getDefault()), friendly);
				expected.reflected.insert(recipient->unitId());
				expected.tile = friendly->getPosition();
				recipient = friendly;
				resisted = scenario.lockedRedirect;
				if(!scenario.lockedRedirect)
					resisted = random->getDefault().nextInt(0, 99) < friendly->magicResistance();
				if(scenario.redirectedResistance)
				{
					ASSERT_EQ(resisted, *scenario.redirectedResistance);
				}
			}
			if(resisted)
			{
				expected.resisted.insert(recipient->unitId());
				continue;
			}
			const auto id = recipient->unitId();
			if(!vstd::contains(expected.affected, id))
				expected.affected.push_back(id);
			states.try_emplace(id, recipient->acquireState());
			int64_t damage = scenario.triggeredHeroLuck ? 224 : 112; // N remains the original two selected stacks.
			states.at(id)->damage(damage);
			expected.damage += damage;
			expected.injuries.emplace_back(id, damage);
		}
		for(const auto & [id, state] : states)
			expected.units.at(id) = {state->getAvailableHealth(), state->getCount()};
		expected.random = mirrorRandomizerBytes(*random);
	}

	void verifyDesiredMirrorCase(const MirrorScenario & scenario)
	{
		ASSERT_NO_FATAL_FAILURE(mirrorPrepareDesiredScenario(scenario));
		const std::vector<CStack *> selected{enemies[0], enemies[1]};
		const spells::Target aim{spells::Destination(selected[0]), spells::Destination(selected[1])};
		const auto recipients = battle()->getUnitsIf([this](const battle::Unit * unit)
		{
			return battle()->battleGetOwner(unit) == PlayerColor(0) && unit->isValidTarget(false)
				&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
		});
		ASSERT_EQ(recipients.size(), 1u);
		ASSERT_EQ(recipients.front()->unitId(), friendly->unitId());
		const auto beforeUnits = mirrorRecipientSnapshot(*battle());
		const auto beforeResources = mirrorResources();
		const auto beforeRandom = mirrorRandomizerBytes(*gameHandler->randomizer);
		const auto beforeActions = server.startedActions.size();
		const auto beforeCasts = server.castsOf(stormOfDaggersSpell()).size();
		const auto beforeInjuries = server.injuries.size();
		const auto defenderLedger = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		const auto casterLedger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
		auto expectedLedger = casterLedger;
		const auto round = battle()->battleGetRound();
		const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, stormOfDaggersSpell());
		ASSERT_TRUE(allowance);
		ASSERT_EQ(expectedLedger.remainingCounts(round).heroActions, 1u);
		ASSERT_EQ(expectedLedger.grants.size(), 1u);
		ASSERT_TRUE(expectedLedger.consumeAllowance(allowance->grantId,
			HeroActionAllowanceState::ActionKind::SPELL, round));
		ASSERT_EQ(attackerSideHero->getSpellCost(stormOfDaggersSpell().toSpell()), 8);
		spells::BattleCast liveProbe(battle(), attackerSideHero, spells::Mode::HERO, stormOfDaggersSpell().toSpell());
		auto liveMechanics = stormOfDaggersSpell().toSpell()->battleMechanics(&liveProbe);
		ASSERT_TRUE(liveMechanics->setStormOfDaggersTargetCount(2));
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(liveMechanics->canBeCast(problem));
		ASSERT_TRUE(liveMechanics->canBeCastAt(aim));
		if(scenario.hypnotizedLegs)
		{
			EXPECT_FALSE(liveMechanics->canBeCastAt({spells::Destination(friendly), spells::Destination(enemies[1])}));
		}

		MirrorCharacterizationEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		HypotheticBattle projected(&environment, callback);
		spells::Target projectedAim;
		for(const auto * unit : selected)
			projectedAim.emplace_back(projected.battleGetUnitByID(unit->unitId()));
		spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO, stormOfDaggersSpell().toSpell());
		auto mechanics = stormOfDaggersSpell().toSpell()->battleMechanics(&preview);
		ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(2));
		ASSERT_TRUE(mechanics->canBeCastAt(projectedAim));
		mechanics->castEval(projected.getServerCallback(), projectedAim);
		const auto forecastUnits = mirrorRecipientSnapshot(projected);
		for(const auto * unit : selected)
			EXPECT_EQ(beforeUnits.at(unit->unitId()).first - forecastUnits.at(unit->unitId()).first, 112);
		EXPECT_EQ(mirrorRecipientSnapshot(*battle()), beforeUnits);
		EXPECT_EQ(mirrorResources(), beforeResources);
		EXPECT_EQ(mirrorRandomizerBytes(*gameHandler->randomizer), beforeRandom);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderLedger);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), casterLedger);
		EXPECT_EQ(server.startedActions.size(), beforeActions);
		EXPECT_EQ(server.castsOf(stormOfDaggersSpell()).size(), beforeCasts);

		MirrorExpected expected;
		ASSERT_NO_FATAL_FAILURE(mirrorExpectedResolution(scenario, selected, expected));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(selected)));
		EXPECT_EQ(mirrorRecipientSnapshot(*battle()), expected.units);
		EXPECT_EQ(mirrorRandomizerBytes(*gameHandler->randomizer), expected.random);
		auto expectedResources = beforeResources;
		expectedResources[0] -= 4; // Four permanent Buffer, then four Normal.
		expectedResources[1] -= 4;
		expectedResources[3] += 8;
		if(scenario.triggeredHeroLuck)
			expectedResources[0] += 4; // One refund4 restores original Normal charge, not Buffer.
		EXPECT_EQ(mirrorResources(), expectedResources);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), expectedLedger);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderLedger);
		EXPECT_EQ(battle()->battleGetRound(), round);
		EXPECT_EQ(server.startedActions.size(), beforeActions + 1);
		const auto casts = server.castsOf(stormOfDaggersSpell());
		ASSERT_EQ(casts.size(), beforeCasts + 1);
		const auto & receipt = casts.back().announcement;
		EXPECT_EQ(receipt.side, BattleSide::ATTACKER);
		EXPECT_TRUE(receipt.castByHero);
		EXPECT_TRUE(receipt.activeCast);
		EXPECT_EQ(receipt.heroLuck, expected.heroLuck);
		EXPECT_EQ(receipt.paidHeroManaCost, 8);
		EXPECT_EQ(receipt.paidCounterspellManaCost, 0);
		EXPECT_EQ(receipt.reflectedCres, expected.reflected);
		EXPECT_EQ(receipt.affectedCres, expected.affected);
		EXPECT_EQ(receipt.resistedCres, expected.resisted);
		EXPECT_EQ(receipt.tile, expected.tile);
		EXPECT_EQ(casts.back().damage, expected.damage);
		std::vector<std::pair<uint32_t, int64_t>> injuries;
		for(size_t index = beforeInjuries; index < server.injuries.size(); ++index)
			for(const auto & injury : server.injuries[index].stacks)
				injuries.emplace_back(injury.stackAttacked, injury.damageAmount);
		EXPECT_EQ(injuries, expected.injuries);
		if(scenario.mirrorIndex >= 0)
		{
			EXPECT_NE(expected.units, forecastUnits); // Approximation does not predict random redirects.
		}

	}

	BattleAction action(const std::vector<CStack *> & targets) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = stormOfDaggersSpell();
		for(const auto * target : targets)
			result.aimToUnit(target);
		return result;
	}
};

TEST_F(NewHorizonsStormOfDaggersTest, SharedForecastMatchesCanonicalPoolAndEqualSplitTable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spellId = stormOfDaggersSpell();
	const auto * spell = spellId.toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spellId));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spellId, MasteryLevel::NONE), 8);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->isNewHorizonsStormOfDaggers());
	EXPECT_EQ(mechanics->getEffectValue(), 195);

	const std::array<int64_t, 5> expectedTotals{195, 224, 254, 283, 312};
	const std::array<int64_t, 5> expectedPerTarget{195, 112, 85, 71, 62};
	for(int32_t count = 1; count <= 5; ++count)
	{
		EXPECT_EQ(mechanics->getStormOfDaggersTotalDamage(count), expectedTotals[count - 1]) << count;
		EXPECT_EQ(mechanics->getStormOfDaggersDamagePerTarget(count), expectedPerTarget[count - 1]) << count;
	}

	// The authored table rounds independently: at N=3, each target receives 85
	// even though the displayed rounded total is 254. The half values round up.
	EXPECT_EQ(mechanics->getStormOfDaggersTotalDamage(3), 254);
	EXPECT_EQ(mechanics->getStormOfDaggersDamagePerTarget(3), 85);
	EXPECT_FALSE(mechanics->setStormOfDaggersTargetCount(0));
	EXPECT_FALSE(mechanics->setStormOfDaggersTargetCount(6));
	ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(3));
	EXPECT_EQ(mechanics->getEffectValue(), 85);
}

TEST_F(NewHorizonsStormOfDaggersTest, SavedSorceryRankScalesOnlySpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = stormOfDaggersSpell();
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	ASSERT_TRUE(sorcery.hasValue());

	const std::array<int64_t, 4> expectedPool{195, 218, 240, 263};
	const std::array<int64_t, 4> expectedPowerTerm{150, 173, 195, 218};
	for(size_t rank = 0; rank < expectedPool.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(sorcery, static_cast<MasteryLevel::Type>(rank), ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getEffectValue(), expectedPool[rank]);
		EXPECT_EQ(mechanics->getEffectValue() - 45, expectedPowerTerm[rank]);
	}

	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
	spells::BattleCast half(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	EXPECT_EQ(spell.toSpell()->battleMechanics(&half)->getStormOfDaggersDamagePerTarget(1), 47);
}

TEST_F(NewHorizonsStormOfDaggersTest, AuthoritativeCastDamagesEveryChosenEnemyWithPerTargetResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<CStack *> selected{enemies[0], enemies[1], enemies[2]};
	selected[1]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	const std::array<int64_t, 3> beforeHealth{
		selected[0]->getAvailableHealth(), selected[1]->getAvailableHealth(), selected[2]->getAvailableHealth()};
	const auto beforeMana = attackerSideHero->getManaAvailable();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, stormOfDaggersSpell().toSpell());
	auto mechanics = stormOfDaggersSpell().toSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(3));
	const auto resistedForecast = mechanics->adjustEffectValue(selected[1]);
	ASSERT_LT(resistedForecast, mechanics->getStormOfDaggersDamagePerTarget(3));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(selected)));
	EXPECT_EQ(beforeHealth[0] - selected[0]->getAvailableHealth(), 85);
	EXPECT_EQ(beforeHealth[1] - selected[1]->getAvailableHealth(), resistedForecast);
	EXPECT_EQ(beforeHealth[2] - selected[2]->getAvailableHealth(), 85);
	EXPECT_EQ(beforeMana - attackerSideHero->getManaAvailable(), 8);
	ASSERT_EQ(server.castsOf(stormOfDaggersSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(stormOfDaggersSpell()).front().damage,
		85 + resistedForecast + 85);
}

TEST_F(NewHorizonsStormOfDaggersTest, CombatLogNamesEveryResolvedTargetWithMitigatedDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<CStack *> selected{enemies[0], enemies[1]};
	selected[1]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	const std::array<int64_t, 2> health{
		selected[0]->getAvailableHealth(), selected[1]->getAvailableHealth()};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(selected)));
	const auto casts = server.castsOf(stormOfDaggersSpell());
	ASSERT_EQ(casts.size(), 1u);
	const auto & lines = casts.front().logLines;
	for(size_t index = 0; index < selected.size(); ++index)
	{
		const auto damage = health[index] - selected[index]->getAvailableHealth();
		const auto expected = "Storm of Daggers deals " + std::to_string(damage) + " damage to "
			+ selected[index]->unitType()->getNamePluralTranslated() + ".";
		EXPECT_EQ(std::count(lines.begin(), lines.end(), expected), 1);
	}
	EXPECT_EQ(std::count_if(lines.begin(), lines.end(), [](const auto & line)
	{
		return line.starts_with("Storm of Daggers deals ");
	}), 2);
	EXPECT_TRUE(std::none_of(lines.begin(), lines.end(), [](const auto & line)
	{
		return line.starts_with("Storm of Daggers does ");
	}));
}

TEST_F(NewHorizonsStormOfDaggersTest, CombatLogNamesNoCasualtyRecipientWithoutInventingKills)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	const auto * selected = enemies.front();
	enemies.front()->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 95, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	const auto health = selected->getAvailableHealth();
	const auto count = selected->getCount();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action({enemies.front()})));
	const auto damage = health - selected->getAvailableHealth();
	ASSERT_GT(damage, 0);
	ASSERT_EQ(selected->getCount(), count);
	const auto casts = server.castsOf(stormOfDaggersSpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().killed, 0u);
	const auto expected = "Storm of Daggers deals " + std::to_string(damage) + " damage to "
		+ selected->unitType()->getNamePluralTranslated() + ".";
	EXPECT_EQ(std::count(casts.front().logLines.begin(), casts.front().logLines.end(), expected), 1);
}

TEST_F(NewHorizonsStormOfDaggersTest, InvalidFullTargetVectorRejectsAtomicallyBeforeActionOrMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::array<int64_t, 2> beforeHealth{
		enemies[0]->getAvailableHealth(), enemies[1]->getAvailableHealth()};
	const auto beforeMana = attackerSideHero->getManaAvailable();
	const auto beforeActions = server.startedActions.size();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action({enemies[0], enemies[0]})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action({enemies[0], friendly})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action(enemies)));

	EXPECT_EQ(beforeHealth[0], enemies[0]->getAvailableHealth());
	EXPECT_EQ(beforeHealth[1], enemies[1]->getAvailableHealth());
	EXPECT_EQ(beforeMana, attackerSideHero->getManaAvailable());
	EXPECT_EQ(beforeActions, server.startedActions.size());
	EXPECT_TRUE(server.castsOf(stormOfDaggersSpell()).empty());
}

// Current-defect characterizations: update only after a reviewed compound-Mirror
// policy and corresponding production fix, not to declare these outcomes correct.
TEST_F(NewHorizonsStormOfDaggersTest, MirrorCharacterizationOrdinaryPairControl)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionFirstRecipientRedirectsOnlyItsFixedShare)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionLaterRecipientRedirectsOnlyItsFixedShare)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 1}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorCharacterizationSeventyFivePercentResistanceRejectsOnlyItsRecipient)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorCharacterizationSeventyFivePercentResistanceCanFailWithoutChangingSplit)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = false}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionInnateFaerieDragonTwentyPercentProducer)
{
	// Real shipped creature producer; current defect expectations stay separate
	// from any future decision about the compound spell's desired Mirror policy.
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .nativeProducer = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionBothRecipientsCollideWithoutMergingShares)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 2}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionRedirectedSeventyFivePercentResistanceRejectsShare)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .redirectedResistance = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionRedirectedSeventyFivePercentResistanceCanFail)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .redirectedResistance = false}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionRedirectedSpellLockRejectsOnlyItsShare)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .lockedRedirect = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionOneBounceEvenWhenBothArmiesHaveMirror)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .mirrorOnRedirect = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionPuppetReflectorRetainsTrueAllegiance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .puppetReflector = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MirrorResolutionHypnotizeUsesEffectiveAllegianceForBothLegs)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .hypnotizedLegs = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, CompoundNoMirrorHypnotizedTargetRetainsHostileResistance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true, .hypnotizedLegs = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, CompoundNoMirrorPuppetTargetRetainsHostileResistance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true, .puppetReflector = true}));
}

TEST_F(NewHorizonsStormOfDaggersTest, MixedReflectedSharesInheritOneTriggeredLuckReceiptAndOneRefund)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirrorIndex = 0, .triggeredHeroLuck = true}));
}
