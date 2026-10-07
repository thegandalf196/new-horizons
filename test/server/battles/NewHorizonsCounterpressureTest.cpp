/*
 * NewHorizonsCounterpressureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license is available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view spellcraftSkillId = "new-horizons:spellcraft";
constexpr std::string_view spellPenetrationId = "new-horizons:spellcraft.spellPenetration";
constexpr std::string_view counterpressureId = "new-horizons:spellcraft.counterpressure";

bool activateCounterpressure(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(spellcraftSkillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == counterpressureId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class CounterpressurePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CounterpressurePredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsCounterpressureTest : public HeroCommandFixture
{
protected:
	CStack * attackerCaster = nullptr;
	CStack * attackerVictim = nullptr;
	CStack * defenderCaster = nullptr;
	CStack * defenderVictim = nullptr;

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
		if(!activateCounterpressure(perkRules))
			throw std::runtime_error("Missing Counterpressure from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	SecondarySkill spellcraft() const
	{
		const int decoded = SecondarySkill::decode(std::string(spellcraftSkillId));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void grantCounterpressure(CGHeroInstance * hero)
	{
		for(int index = 0; index < LIBRARY->skillh->size(); ++index)
			hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);

		hero->setSecSkillLevel(spellcraft(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({std::string(spellcraftSkillId), std::string(spellPenetrationId)});
		hero->applyPerkSelection({std::string(spellcraftSkillId), std::string(counterpressureId)});
		ASSERT_TRUE(hero->hasActivePerk(std::string(spellcraftSkillId), std::string(counterpressureId)));
	}

	void prepare(bool selectCounterpressure = true)
	{
		startGame();
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->removeAllSpells();
			hero->addSpellToSpellbook(SpellID(SpellID::MAGIC_ARROW));
			hero->addSpellToSpellbook(SpellID(SpellID::SLOW));
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 50, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 1000);
		}
		if(selectCounterpressure)
			grantCounterpressure(attackerSideHero);

		startBattle();
		removeDeployedUnits();
		attackerCaster = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1);
		attackerVictim = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(leftHex + 2), 1000);
		defenderCaster = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
		defenderVictim = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex + 2), 1000);
		ASSERT_NE(attackerCaster, nullptr);
		ASSERT_NE(attackerVictim, nullptr);
		ASSERT_NE(defenderCaster, nullptr);
		ASSERT_NE(defenderVictim, nullptr);
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwnerHero(attackerVictim), attackerSideHero);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	bool castPaid(BattleSide side, const CStack * actor, SpellID spell, const CStack * target)
	{
		if(battle()->battleActiveUnit() != actor)
			activate(actor);

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side), action);
	}

	void activate(const CStack * actor)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = actor->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	int32_t castSpellPowerBonus(CGHeroInstance * caster, SpellID spellId,
		const CBattleInfoCallback * selectedBattle = nullptr) const
	{
		const auto * spell = spellId.toSpell();
		spells::BattleCast castEvent(selectedBattle ? selectedBattle : battle(), caster,
			spells::Mode::HERO, spell);
		return spell->battleMechanics(&castEvent)->getCastSpellPowerComponentBonusPercent();
	}

	int64_t forecastDamage(CGHeroInstance * caster, SpellID spellId, const battle::Unit * target,
		const CBattleInfoCallback * selectedBattle = nullptr) const
	{
		const auto * spell = spellId.toSpell();
		spells::BattleCast castEvent(selectedBattle ? selectedBattle : battle(), caster,
			spells::Mode::HERO, spell);
		return spell->battleMechanics(&castEvent)->adjustEffectValue(target);
	}

	int64_t fixedLevelPowerTerm(CGHeroInstance * caster, SpellID spellId,
		const CBattleInfoCallback * selectedBattle = nullptr) const
	{
		const auto * spell = spellId.toSpell();
		spells::BattleCast castEvent(selectedBattle ? selectedBattle : battle(), caster,
			spells::Mode::HERO, spell);
		// calculateRawEffectValue takes (Spell Power multiplier, fixed level-power multiplier).
		return spell->battleMechanics(&castEvent)->calculateRawEffectValue(0, 1);
	}

	std::optional<int> seedForResistanceDraw(const CStack * target, int expectedDraw) const
	{
		const auto units = battle()->battleGetAllUnits(false);
		const auto targetIt = std::ranges::find_if(units, [target](const auto * unit)
			{ return unit->unitId() == target->unitId(); });
		if(targetIt == units.end())
			return std::nullopt;

		for(int candidateSeed = 1; candidateSeed < 100000; ++candidateSeed)
		{
			CRandomGenerator candidate(candidateSeed);
			int targetDraw = -1;
			for(const auto * unit : units)
			{
				const int draw = candidate.nextInt(0, 99);
				if(unit->unitId() == target->unitId())
					targetDraw = draw;
			}
			if(targetDraw == expectedDraw)
				return candidateSeed;
		}
		return std::nullopt;
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
}

TEST_F(NewHorizonsCounterpressureTest, AcceptedEnemyDamageArmsTheOtherSideAndItsNextSpellUsesThenConsumesTheBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(magicArrow, nullptr);
	const auto ordinarySpellCost = attackerSideHero->getSpellCost(magicArrow);
	const auto manaBeforeTrigger = attackerSideHero->getManaAvailable();
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), 0);
	const auto fixedTermBefore = fixedLevelPowerTerm(attackerSideHero, SpellID(SpellID::MAGIC_ARROW));
	EXPECT_GT(fixedTermBefore, 0);
	const auto enemyTargetHealth = attackerVictim->getAvailableHealth();
	ASSERT_TRUE(castPaid(BattleSide::DEFENDER, defenderCaster,
		SpellID(SpellID::MAGIC_ARROW), attackerVictim));
	EXPECT_LT(attackerVictim->getAvailableHealth(), enemyTargetHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeTrigger)
		<< "The other hero's accepted cast must not charge the Spell Response recipient";

	const auto round = battle()->getRound();
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(round));
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::DEFENDER).hasState());
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)),
		newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT);
	EXPECT_EQ(fixedLevelPowerTerm(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), fixedTermBefore)
		<< "Counterpressure does not multiply Magic Arrow's fixed level-power term";
	EXPECT_EQ(attackerSideHero->getSpellCost(magicArrow), ordinarySpellCost)
		<< "Counterpressure modifies only the Spell Power component, not listed Mana cost";

	const auto expectedDamage = forecastDamage(attackerSideHero, SpellID(SpellID::MAGIC_ARROW), defenderVictim);
	const auto manaBeforeResponse = attackerSideHero->getManaAvailable();
	const auto acceptedCastsBefore = battle()->getSide(BattleSide::ATTACKER).castSpellsCount;
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	const auto ownTargetHealth = defenderVictim->getAvailableHealth();
	ASSERT_TRUE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::MAGIC_ARROW), defenderVictim));
	EXPECT_EQ(ownTargetHealth - defenderVictim->getAvailableHealth(), expectedDamage)
		<< "The accepted cast must use the captured Spell Power component before readiness is consumed";
	EXPECT_EQ(manaBeforeResponse - attackerSideHero->getManaAvailable(), ordinarySpellCost)
		<< "The response adds no extra cast or Mana charge";
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER))
		<< "The accepted response is still exactly the ordinary hero spell action";
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, acceptedCastsBefore + 1)
		<< "The response is recorded as one normal spell cast, without an extra action";
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::ATTACKER).hasState());
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), 0);
}

TEST_F(NewHorizonsCounterpressureTest, DetachedEnemyDamageArmsOnlyTheRecipientAndItsNextCastConsumesOnlyTheCopy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Both projected hero casts must see their actual opposing target and hero.
	// A player-scoped callback hides the enemy side and can make castEval apply
	// effects without representing an accepted cast.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	CounterpressurePredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto materializationBonus = Bonus(BonusDuration::ONE_BATTLE, BonusType::MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	projected.addUnitBonus(attackerVictim->unitId(), {materializationBonus});
	projected.addUnitBonus(defenderVictim->unitId(), {materializationBonus});
	const auto * projectedAttackerVictim = projected.battleGetUnitByID(attackerVictim->unitId());
	const auto * projectedDefenderVictim = projected.battleGetUnitByID(defenderVictim->unitId());
	ASSERT_NE(projectedAttackerVictim, nullptr);
	ASSERT_NE(projectedDefenderVictim, nullptr);

	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::ATTACKER).hasState());
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::DEFENDER).hasState());
	EXPECT_FALSE(defenderSideHero->hasActivePerk(std::string(spellcraftSkillId), std::string(counterpressureId)));
	const auto liveAttackerHealth = attackerVictim->getAvailableHealth();
	const auto * magicArrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast enemyCast(&projected, defenderSideHero, spells::Mode::HERO, magicArrow);
	const auto enemyMechanics = magicArrow->battleMechanics(&enemyCast);
	spells::Target projectedEnemyAim{spells::Destination(projectedAttackerVictim)};
	ASSERT_TRUE(enemyMechanics->canBeCastAt(projectedEnemyAim));
	enemyMechanics->castEval(projected.getServerCallback(), projectedEnemyAim);
	EXPECT_LT(projected.battleGetUnitByID(attackerVictim->unitId())->getAvailableHealth(), liveAttackerHealth);
	EXPECT_TRUE(projected.getSpellResponseState(BattleSide::ATTACKER).isReadyAt(projected.getRound()))
		<< "A detached enemy cast must arm its recipient side even when only that recipient owns the perk";
	EXPECT_FALSE(projected.getSpellResponseState(BattleSide::DEFENDER).hasState());
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::ATTACKER).hasState())
		<< "Detached damage must not arm the authoritative battle";

	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW), &projected),
		newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT);
	const auto liveDefenderHealth = defenderVictim->getAvailableHealth();
	const auto * projectedOwnTarget = projected.battleGetUnitByID(defenderVictim->unitId());
	ASSERT_NE(projectedOwnTarget, nullptr);
	spells::BattleCast ownCast(&projected, attackerSideHero, spells::Mode::HERO, magicArrow);
	const auto ownMechanics = magicArrow->battleMechanics(&ownCast);
	spells::Target projectedOwnAim{spells::Destination(projectedOwnTarget)};
	ASSERT_TRUE(ownMechanics->canBeCastAt(projectedOwnAim));
	const auto projectedDamage = ownMechanics->adjustEffectValue(projectedOwnTarget);
	ownMechanics->castEval(projected.getServerCallback(), projectedOwnAim);
	EXPECT_EQ(liveDefenderHealth, defenderVictim->getAvailableHealth());
	EXPECT_EQ(liveAttackerHealth, attackerVictim->getAvailableHealth());
	EXPECT_EQ(projected.getSpellResponseState(BattleSide::ATTACKER).hasState(), false);
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW), &projected), 0);
	EXPECT_EQ(battle()->getSpellResponseState(BattleSide::ATTACKER).hasState(), false)
		<< "Projected consumption must not clear or otherwise mutate live response state";
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), 0);

	const auto projectedOwnHealth = projected.battleGetUnitByID(defenderVictim->unitId())->getAvailableHealth();
	EXPECT_EQ(liveDefenderHealth - projectedOwnHealth, projectedDamage)
		<< "Detached next-cast damage must use the consumed response snapshot exactly once";
}

TEST_F(NewHorizonsCounterpressureTest, DetachedStatusRefreshSnapshotsAnExistingSpellEffectWithoutChangingLiveBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));

	// Seed a real SPELL_EFFECT bonus in the detached target. The refresh is
	// evaluated through the normal projected spell/effect-recorder path, but no
	// hero has Counterpressure selected for this regression.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	CounterpressurePredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto slowSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::SLOW))).And(Selector::type()(BonusType::STACKS_INITIATIVE));
	Bonus priorSlow(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE,
		BonusSource::SPELL_EFFECT, -10, BonusSourceID(SpellID(SpellID::SLOW)));
	priorSlow.turnsRemain = 1;
	projected.addUnitBonus(attackerVictim->unitId(), {priorSlow});

	const auto * projectedTarget = projected.battleGetUnitByID(attackerVictim->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const auto existingSlow = projectedTarget->getBonuses(slowSelector);
	ASSERT_TRUE(existingSlow && existingSlow->size() == 1);
	EXPECT_EQ(existingSlow->front()->turnsRemain, 1);
	const auto liveTargetHealth = attackerVictim->getAvailableHealth();
	EXPECT_TRUE(attackerVictim->getBonuses(slowSelector)->empty());

	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	ASSERT_NE(slow, nullptr);
	spells::BattleCast cast(&projected, defenderSideHero, spells::Mode::HERO, slow);
	const auto mechanics = slow->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projectedTarget)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);

	const auto refreshedSlow = projected.battleGetUnitByID(attackerVictim->unitId())->getBonuses(slowSelector);
	ASSERT_TRUE(refreshedSlow && refreshedSlow->size() == 1);
	EXPECT_GT(refreshedSlow->front()->turnsRemain, 1)
		<< "The projected accepted Slow refreshes the existing timed spell effect";
	EXPECT_EQ(attackerVictim->getAvailableHealth(), liveTargetHealth);
	EXPECT_TRUE(attackerVictim->getBonuses(slowSelector)->empty())
		<< "Applying the projected refresh must not mutate the authoritative stack";
}

TEST_F(NewHorizonsCounterpressureTest, DebuffArmsThroughTheNextRoundButNotAfterIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castPaid(BattleSide::DEFENDER, defenderCaster,
		SpellID(SpellID::SLOW), attackerVictim));
	EXPECT_TRUE(attackerVictim->hasBonusOfType(BonusType::STACKS_SPEED));

	auto round = battle()->getRound();
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(round));
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)),
		newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT);

	advanceRound();
	round = battle()->getRound();
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(round));
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)),
		newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT);

	advanceRound();
	round = battle()->getRound();
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(round));
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), 0);
}

TEST_F(NewHorizonsCounterpressureTest, RejectedOwnSpellLeavesReadinessForTheNextAcceptedSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castPaid(BattleSide::DEFENDER, defenderCaster,
		SpellID(SpellID::MAGIC_ARROW), attackerVictim));
	ASSERT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(battle()->getRound()));

	EXPECT_FALSE(castPaid(BattleSide::ATTACKER, attackerCaster,
		SpellID(SpellID::MAGIC_ARROW), attackerVictim));
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(battle()->getRound()));
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)),
		newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT);
}

TEST_F(NewHorizonsCounterpressureTest, AcceptedOrderAndCreatureSpellDoNotConsumeHeroSpellReadiness)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castPaid(BattleSide::DEFENDER, defenderCaster,
		SpellID(SpellID::MAGIC_ARROW), attackerVictim));
	ASSERT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(battle()->getRound()));

	activate(attackerCaster);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(battle()->getRound()));

	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(leftHex + 4), 1);
	ASSERT_NE(creatureCaster, nullptr);
	const auto arrow = SpellID(SpellID::MAGIC_ARROW);
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(arrow)));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));

	const auto * spell = arrow.toSpell();
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, spell);
	spells::Target target{spells::Destination(defenderVictim)};
	const auto mechanics = spell->battleMechanics(&creatureCast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	creatureCast.cast(gameHandler->spellEnv.get(), target);
	EXPECT_TRUE(battle()->getSpellResponseState(BattleSide::ATTACKER).isReadyAt(battle()->getRound()));
}

TEST_F(NewHorizonsCounterpressureTest, AcceptedButResistedEnemySpellDoesNotArmReadiness)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerVictim->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_EQ(attackerVictim->magicResistance(), 75)
		<< "New Horizons caps the raw resistance at75%, leaving the spell accepted and probabilistic";
	const auto resistSeed = seedForResistanceDraw(attackerVictim, 74);
	ASSERT_TRUE(resistSeed.has_value()) << "Find a deterministic roll below the 75% resistance threshold";
	activate(defenderCaster);
	gameHandler->randomizer->setSeed(*resistSeed);
	const auto healthBefore = attackerVictim->getAvailableHealth();

	ASSERT_TRUE(castPaid(BattleSide::DEFENDER, defenderCaster,
		SpellID(SpellID::MAGIC_ARROW), attackerVictim));
	EXPECT_EQ(attackerVictim->getAvailableHealth(), healthBefore)
		<< "The accepted enemy cast was fully resisted and applied no damage";
	EXPECT_FALSE(battle()->getSpellResponseState(BattleSide::ATTACKER).hasState());
	EXPECT_EQ(castSpellPowerBonus(attackerSideHero, SpellID(SpellID::MAGIC_ARROW)), 0);
}
