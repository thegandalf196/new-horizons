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
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include <vcmi/Environment.h>
#include <vstd/RNG.h>

#include <array>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <utility>
#include <vector>
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

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
		bool selectBloodDrinker = false, const std::string & enemyCreature = "core:pikeman",
		bool hypnotizedLegs = false)
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

		friendly = addStack(hypnotizedLegs ? BattleSide::DEFENDER : BattleSide::ATTACKER,
			creatureByName(friendlyCreature), BattleHex(3, 5), 2);
		enemy = addStack(hypnotizedLegs ? BattleSide::ATTACKER : BattleSide::DEFENDER,
			creatureByName(enemyCreature), BattleHex(12, 5), enemyCount);
		otherEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 1000);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(otherEnemy, nullptr);
		if(hypnotizedLegs)
			for(auto * unit : {friendly, enemy})
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
			EXPECT_FALSE(newHorizonsSorcery::isSpellLocked(*unit));
			if(battle()->battleGetOwner(unit) == PlayerColor(0))
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
		// Life Drain uses the first hostile minstd_rand draw: adjacent small seeds all roll zero.
		// Spread candidates, then verify the genuine branch with the cloned engine RNG.
		for(int candidateSeed = 0; candidateSeed < 256; ++candidateSeed)
		{
			gameHandler->randomizer->setSeed(candidateSeed * 104729);
			auto expected = mirrorCloneRandomizer();
			if(mirrorAdvanceResistance(*expected).at(recipientId) == resisted)
				return;
		}
		FAIL() << "Could not select the requested genuine 75% resistance branch";
	}

	struct MirrorScenario
	{
		bool mirror = false;
		std::optional<bool> originalResistance = std::nullopt;
		bool friendlyResistance = false;
		std::optional<bool> redirectedResistance = std::nullopt;
		bool bloodDrinker = false;
		bool hypnotizedLegs = false;
		bool puppetDamageTarget = false;
	};

	struct MirrorExpected
	{
		MirrorRecipientSnapshot units;
		std::vector<std::byte> random;
		std::set<uint32_t> reflected;
		std::set<uint32_t> resisted;
		std::vector<uint32_t> affected;
		std::vector<std::pair<uint32_t, int64_t>> injuries;
		BattleHex tile;
		int64_t damage = 0;
		int64_t healing = 0;
	};

	void mirrorPrepareDesiredScenario(const MirrorScenario & scenario)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(100, "core:angel", 1000,
			scenario.bloodDrinker ? MasteryLevel::BASIC : MasteryLevel::NONE,
			scenario.bloodDrinker, scenario.mirror ? "core:angel" : "core:pikeman", scenario.hypnotizedLegs));
		ASSERT_NO_FATAL_FAILURE(mirrorPrepareResources(friendly, lifeDrainSpell()));
		ASSERT_GT(friendly->getMaxHealth(), 180);
		injure(friendly, 150);
		if(scenario.mirror)
		{
			ASSERT_GT(enemy->getMaxHealth(), 180);
			injure(enemy, 180);
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_MIRROR, BonusSource::OTHER, 100, BonusSourceID()));
		}
		if(scenario.originalResistance)
		{
			ASSERT_FALSE(scenario.mirror);
			enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 75, BonusSourceID()));
			ASSERT_NO_FATAL_FAILURE(mirrorSelectResistanceBranch(enemy->unitId(), *scenario.originalResistance));
		}
		if(scenario.friendlyResistance || scenario.redirectedResistance)
			friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 75, BonusSourceID()));
		ASSERT_EQ(friendly->magicResistance(), scenario.friendlyResistance || scenario.redirectedResistance ? 75 : 0);
		ASSERT_EQ(enemy->magicResistance(), scenario.originalResistance ? 75 : 0);
		ASSERT_EQ(otherEnemy->magicResistance(), 0);
		ASSERT_FALSE(friendly->hasBonusOfType(BonusType::MAGIC_MIRROR));
		ASSERT_EQ(battle()->battleGetOwner(friendly), PlayerColor(0));
		ASSERT_EQ(battle()->battleGetOwner(enemy), PlayerColor(1));
		if(scenario.hypnotizedLegs)
		{
			ASSERT_EQ(friendly->unitSide(), BattleSide::DEFENDER);
			ASSERT_EQ(enemy->unitSide(), BattleSide::ATTACKER);
			ASSERT_EQ(battle()->battleGetActionController(friendly), PlayerColor(0));
			ASSERT_EQ(battle()->battleGetActionController(enemy), PlayerColor(1));
		}
		if(scenario.puppetDamageTarget)
		{
			const SpellID puppet(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
			SetStackEffect control;
			control.battleID = BattleID(0);
			control.toAdd.emplace_back(enemy->unitId(),
				std::vector<Bonus>{newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(0))});
			gameHandler->sendAndApply(control);
			ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), enemy));
			ASSERT_EQ(battle()->battleGetOwner(enemy), PlayerColor(1));
			ASSERT_EQ(battle()->battleGetActionController(enemy), PlayerColor(0));
		}
		if(scenario.redirectedResistance)
		{
			ASSERT_TRUE(scenario.mirror);
			ASSERT_NO_FATAL_FAILURE(mirrorSelectRedirectedResistanceBranch(*scenario.redirectedResistance));
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

	void mirrorExpectedResolution(const MirrorScenario & scenario, MirrorExpected & expected)
	{
		expected.units = mirrorRecipientSnapshot(*battle());
		expected.tile = enemy->getPosition();
		auto random = mirrorCloneRandomizer();
		const auto resistance = mirrorAdvanceResistance(*random);
		bool resisted = resistance.at(enemy->unitId());
		if(scenario.originalResistance)
		{
			ASSERT_EQ(resisted, *scenario.originalResistance);
		}
		const CStack * damageTarget = enemy;
		const CStack * healTarget = friendly;
		if(scenario.mirror)
		{
			ASSERT_LT(random->getDefault().nextInt(0, 99), 100);
			const std::vector<const battle::Unit *> recipients{friendly};
			ASSERT_EQ(*RandomGeneratorUtil::nextItem(recipients, random->getDefault()), friendly);
			resisted = random->getDefault().nextInt(0, 99) < friendly->magicResistance();
			if(scenario.redirectedResistance)
			{
				ASSERT_EQ(resisted, *scenario.redirectedResistance);
			}
			damageTarget = friendly;
			healTarget = enemy;
			expected.tile = friendly->getPosition();
			expected.reflected.insert(enemy->unitId());
		}
		if(resisted)
		{
			expected.resisted.insert(damageTarget->unitId());
			if(!scenario.mirror)
				expected.affected.push_back(friendly->unitId());
		}
		else
		{
			expected.affected = {damageTarget->unitId(), healTarget->unitId()};
			auto damaged = damageTarget->acquireState();
			int64_t damage = scenario.bloodDrinker ? 232 : 205;
			damaged->damage(damage);
			expected.damage = damage;
			expected.injuries.emplace_back(damageTarget->unitId(), damage);
			expected.units.at(damageTarget->unitId()) = {damaged->getAvailableHealth(), damaged->getCount()};
			auto healed = healTarget->acquireState();
			int64_t healing = damage * (scenario.bloodDrinker ? 75 : 60) / 100;
			expected.healing = healed->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
			expected.units.at(healTarget->unitId()) = {healed->getAvailableHealth(), healed->getCount()};
		}
		expected.random = mirrorRandomizerBytes(*random);
	}

	void verifyDesiredMirrorCase(const MirrorScenario & scenario)
	{
		ASSERT_NO_FATAL_FAILURE(mirrorPrepareDesiredScenario(scenario));
		const spells::Target aim{spells::Destination(enemy), spells::Destination(friendly)};
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
		const auto beforeCasts = server.castsOf(lifeDrainSpell()).size();
		const auto beforeInjuries = server.injuries.size();
		const auto defenderLedger = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		const auto casterLedger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
		auto expectedLedger = casterLedger;
		const auto round = battle()->battleGetRound();
		const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, lifeDrainSpell());
		ASSERT_TRUE(allowance);
		ASSERT_EQ(expectedLedger.remainingCounts(round).heroActions, 1u);
		ASSERT_EQ(expectedLedger.grants.size(), 1u);
		ASSERT_TRUE(expectedLedger.consumeAllowance(allowance->grantId,
			HeroActionAllowanceState::ActionKind::SPELL, round));
		ASSERT_EQ(attackerSideHero->getSpellCost(lifeDrainSpell().toSpell()), 5);
		spells::BattleCast liveProbe(battle(), attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
		auto liveMechanics = lifeDrainSpell().toSpell()->battleMechanics(&liveProbe);
		spells::detail::ProblemImpl problem;
		ASSERT_TRUE(liveMechanics->canBeCast(problem));
		ASSERT_TRUE(liveMechanics->canBeCastAt(aim));
		if(scenario.hypnotizedLegs)
		{
			EXPECT_FALSE(liveMechanics->canBeCastAt({spells::Destination(friendly), spells::Destination(enemy)}));
		}

		MirrorCharacterizationEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		HypotheticBattle projected(&environment, callback);
		spells::Target projectedAim{spells::Destination(projected.battleGetUnitByID(enemy->unitId())),
			spells::Destination(projected.battleGetUnitByID(friendly->unitId()))};
		spells::BattleCast preview(&projected, attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
		auto mechanics = lifeDrainSpell().toSpell()->battleMechanics(&preview);
		ASSERT_TRUE(mechanics->canBeCastAt(projectedAim));
		mechanics->castEval(projected.getServerCallback(), projectedAim);
		const auto forecastUnits = mirrorRecipientSnapshot(projected);
		EXPECT_EQ(beforeUnits.at(enemy->unitId()).first - forecastUnits.at(enemy->unitId()).first,
			scenario.bloodDrinker ? 232 : 205);
		EXPECT_EQ(forecastUnits.at(friendly->unitId()).first - beforeUnits.at(friendly->unitId()).first,
			scenario.bloodDrinker ? 150 : 123);
		EXPECT_EQ(forecastUnits.at(friendly->unitId()).second, beforeUnits.at(friendly->unitId()).second);
		EXPECT_EQ(mirrorRecipientSnapshot(*battle()), beforeUnits);
		EXPECT_EQ(mirrorResources(), beforeResources);
		EXPECT_EQ(mirrorRandomizerBytes(*gameHandler->randomizer), beforeRandom);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), casterLedger);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderLedger);
		EXPECT_EQ(server.startedActions.size(), beforeActions);
		EXPECT_EQ(server.castsOf(lifeDrainSpell()).size(), beforeCasts);

		MirrorExpected expected;
		ASSERT_NO_FATAL_FAILURE(mirrorExpectedResolution(scenario, expected));
		ASSERT_TRUE(cast(enemy, friendly));
		EXPECT_EQ(mirrorRecipientSnapshot(*battle()), expected.units);
		EXPECT_EQ(mirrorRandomizerBytes(*gameHandler->randomizer), expected.random);
		auto expectedResources = beforeResources;
		expectedResources[0] -= 1; // Four permanent Buffer, then one Normal.
		expectedResources[1] -= 4;
		expectedResources[3] += 5;
		EXPECT_EQ(mirrorResources(), expectedResources);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), expectedLedger);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), defenderLedger);
		EXPECT_EQ(battle()->battleGetRound(), round);
		EXPECT_EQ(server.startedActions.size(), beforeActions + 1);
		const auto casts = server.castsOf(lifeDrainSpell());
		ASSERT_EQ(casts.size(), beforeCasts + 1);
		const auto & receipt = casts.back().announcement;
		EXPECT_EQ(receipt.side, BattleSide::ATTACKER);
		EXPECT_TRUE(receipt.castByHero);
		EXPECT_TRUE(receipt.activeCast);
		EXPECT_FALSE(receipt.heroLuck);
		EXPECT_EQ(receipt.paidHeroManaCost, 5);
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
		if(scenario.mirror)
		{
			EXPECT_NE(expected.units, forecastUnits);
			EXPECT_EQ(enemy->getAvailableHealth() - beforeUnits.at(enemy->unitId()).first, expected.healing);
			EXPECT_EQ(enemy->getCount(), beforeUnits.at(enemy->unitId()).second);
			if(expected.damage > 0)
			{
				EXPECT_EQ(expected.healing, scenario.bloodDrinker ? 174 : 123);
				EXPECT_LT(friendly->getCount(), beforeUnits.at(friendly->unitId()).second);
			}
		}

	}

	void acceptBloodDrinkerThroughOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
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
	// Native grass gives both enemy Pikemen Initiative5; the Skeleton has4.
	// Let their ordinary activations finish before the attacker's Hero Action.
	const auto roundBefore = battle()->getRound();
	std::set<uint32_t> defended;
	for(int step = 0; step < 2; ++step)
	{
		const auto * defender = battle()->battleActiveUnit();
		ASSERT_TRUE(defender == enemy || defender == otherEnemy);
		ASSERT_TRUE(defended.insert(defender->unitId()).second);
		ASSERT_EQ(battle()->battleGetActionController(defender), PlayerColor(1));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeDefend(defender)));
	}
	ASSERT_EQ(battle()->getRound(), roundBefore);
	ASSERT_EQ(battle()->battleActiveUnit(), friendly);
	injure(friendly, 1);
	const auto countBefore = friendly->getCount();
	const auto allyBefore = friendly->getAvailableHealth();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	SCOPED_TRACE(::testing::Message() << "round=" << battle()->getRound()
		<< " active=" << active->unitId()
		<< " controller=" << ::testing::PrintToString(battle()->battleGetActionController(active))
		<< " friendlyInitiative=" << friendly->getInitiative()
		<< " enemyInitiative=" << enemy->getInitiative()
		<< " friendlyResistance=" << friendly->magicResistance()
		<< " enemyResistance=" << enemy->magicResistance());
	ASSERT_TRUE(friendly->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_EQ(friendly->getCount(), 2);
	ASSERT_TRUE(friendly->alive());
	ASSERT_TRUE(friendly->isValidTarget(false));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), lifeDrainSpell()));
	const auto rngBeforeProbe = mirrorRandomizerBytes(*gameHandler->randomizer);
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, lifeDrainSpell().toSpell());
	auto mechanics = lifeDrainSpell().toSpell()->battleMechanics(&spellCast);
	spells::Target pair{spells::Destination(enemy), spells::Destination(friendly)};
	spells::detail::ProblemImpl problem;
	const bool canCast = mechanics->canBeCast(problem);
	const bool canTarget = mechanics->canBeCastAt(pair, problem);
	std::vector<std::string> problems;
	problem.getAll(problems);
	EXPECT_TRUE(mechanics->isReceptive(enemy));
	EXPECT_TRUE(mechanics->isReceptive(friendly));
	EXPECT_TRUE(canCast) << ::testing::PrintToString(problems);
	EXPECT_TRUE(canTarget) << ::testing::PrintToString(problems);
	EXPECT_EQ(mirrorRandomizerBytes(*gameHandler->randomizer), rngBeforeProbe);
	ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
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

// Provisional desired resolution; these tests do not themselves amend accepted canon.
TEST_F(NewHorizonsLifeDrainTest, MirrorCharacterizationOrdinaryOrderedPairControl)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionRedirectPreservesOrderedDamageHealingPair)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorCharacterizationSeventyFivePercentResistanceRejectsDamageAndDependentHeal)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorCharacterizationSeventyFivePercentResistanceCanFailAndHeal)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = false}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorCharacterizationFriendlySeventyFivePercentResistanceDoesNotRejectHealing)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.friendlyResistance = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionRedirectedSeventyFivePercentResistanceRejectsBothLegs)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true, .redirectedResistance = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionRedirectedSeventyFivePercentResistanceCanFailAndHeal)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true, .redirectedResistance = false}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionUsesOriginBloodDrinkerAndNeverResurrects)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true, .bloodDrinker = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionHypnotizeUsesEffectiveAllegianceForBothLegs)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true, .hypnotizedLegs = true}));
}

TEST_F(NewHorizonsLifeDrainTest, MirrorResolutionPuppetControlsActionsWithoutChangingAllegiance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.mirror = true, .puppetDamageTarget = true}));
}

TEST_F(NewHorizonsLifeDrainTest, CompoundNoMirrorHypnotizedTargetRetainsHostileResistance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true, .hypnotizedLegs = true}));
}

TEST_F(NewHorizonsLifeDrainTest, CompoundNoMirrorPuppetTargetRetainsHostileResistance)
{
	ASSERT_NO_FATAL_FAILURE(verifyDesiredMirrorCase({.originalResistance = true, .puppetDamageTarget = true}));
}
