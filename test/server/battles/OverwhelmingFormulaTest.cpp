/*
 * OverwhelmingFormulaTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/spells/ObstacleCasterProxy.h"

namespace
{
constexpr auto spellcraft = "new-horizons:spellcraft";
constexpr auto formula = "new-horizons:spellcraft.overwhelmingFormula";
constexpr auto side = BattleSide::ATTACKER;

std::shared_ptr<Bonus> reduction(int percent)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, percent, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
}

class FormulaEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FormulaEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class OverwhelmingFormulaTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void acquire(const std::string & perk, int rank)
	{
		const auto rankLookup = [this](const std::string & skill)
		{
			return attackerSideHero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			const auto found = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
			{
				return offer.selection.skillId == spellcraft && offer.selection.perkId == perk;
			});
			if(found == offers.end())
				continue;
			ASSERT_EQ(found->requiredRank, rank);
			gameHandler->levelUpHero(attackerSideHero, offers,
				static_cast<size_t>(std::distance(offers.begin(), found)), seed, false);
			ASSERT_TRUE(attackerSideHero->hasActivePerk(spellcraft, perk));
			return;
		}
		FAIL() << "Missing legitimate active perk offer: " << perk;
	}

	void prepare()
	{
		startGame();
		const int decoded = SecondarySkill::decode(spellcraft);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		// Real prerequisites, without Spell Penetration's independent 20%.
		// Their numerical spell bonuses are included in each cast's raw value.
		ASSERT_NO_FATAL_FAILURE(acquire("new-horizons:spellcraft.arcaneFocus", MasteryLevel::BASIC));
		gameHandler->levelUpHero(attackerSideHero, skill, false);
		ASSERT_NO_FATAL_FAILURE(acquire("new-horizons:spellcraft.empowerSpell", MasteryLevel::ADVANCED));
		gameHandler->levelUpHero(attackerSideHero, skill, false);
		ASSERT_NO_FATAL_FAILURE(acquire(formula, MasteryLevel::EXPERT));
		ASSERT_FALSE(attackerSideHero->hasActivePerk(spellcraft, "new-horizons:spellcraft.spellPenetration"));
		const int fireMagic = SecondarySkill::decode("new-horizons:fireMagic");
		ASSERT_GE(fireMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(fireMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		const int havocMagic = SecondarySkill::decode("new-horizons:havocMagic");
		ASSERT_GE(havocMagic, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(havocMagic), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		attackerSideHero->addSpellToSpellbook(SpellID::LAND_MINE);
		attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(side, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10000);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		beginCombat();
		activateActor();
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), OverwhelmingFormulaState{});
	}

	void activateActor()
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = actor->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void nextHeroAction()
	{
		advanceRound();
		activateActor();
	}

	int64_t rawDamage(SpellID spell) const
	{
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		return spell.toSpell()->battleMechanics(&event)->getEffectValue();
	}

	bool cast(SpellID spell, const CStack * victim, bool area = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		if(area)
			action.aimToHex(victim->getPosition());
		else
			action.aimToUnit(victim);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	std::optional<int> resistanceSeed(const CStack * victim) const
	{
		// Match the mechanics' eager one-draw-per-unit order, including units
		// that are not recipients. The NH cap is 75%, not guaranteed resistance.
		for(int seed = 1; seed < 100000; ++seed)
		{
			CRandomGenerator random(seed);
			for(const auto * unit : battle()->battleGetAllUnits(false))
			{
				const int draw = random.nextInt(0, 99);
				if(unit->unitId() == victim->unitId() && draw == 0)
					return seed;
			}
		}
		return std::nullopt;
	}

	std::shared_ptr<const SpellCreatedObstacle> createHazard(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		if(spell == SpellID::FIRE_WALL)
		{
			action.aimToHex(BattleHex(70));
			action.spellFireWallDirection = BattleHex::RIGHT;
		}
		else
		{
			spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
			const auto mechanics = spell.toSpell()->battleMechanics(&event);
			const auto candidates = SpellTargetEvaluator::getViableTargets(mechanics.get());
			if(candidates.empty())
			{
				ADD_FAILURE() << "No legal mine placement";
				return {};
			}
			for(const auto & destination : candidates.front())
				action.aimToHex(destination.hexValue);
		}
		if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action))
		{
			ADD_FAILURE() << "Hero hazard creation request rejected";
			return {};
		}
		for(const auto & obstacle : battle()->obstacles)
			if(obstacle->ID == spell.getNum())
				return std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
		ADD_FAILURE() << "Accepted creation did not install a hazard";
		return {};
	}

	void trigger(const SpellCreatedObstacle & obstacle, const battle::Unit * victim,
		HypotheticBattle * projected = nullptr)
	{
		spells::ObstacleCasterProxy caster(PlayerColor(0), attackerSideHero, obstacle);
		const auto * spell = obstacle.getTrigger().toSpell();
		ASSERT_NE(spell, nullptr);
		spells::BattleCast event(projected ? static_cast<const CBattleInfoCallback *>(projected) : battle(),
			&caster, spells::Mode::PASSIVE, spell);
		// Production Fire Wall uses this option to include both armies. The
		// proxy seam also exercises allied mine damage without inventing movement.
		event.setForceNonSmartTargeting(true);
		const spells::Target aim{spells::Destination(victim)};
		if(projected)
			spell->battleMechanics(&event)->castEval(projected->getServerCallback(), aim);
		else
			event.cast(gameHandler->spellEnv.get(), aim);
	}

	void checkHazardFriendlyThenHostile(SpellID spell)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto obstacle = createHazard(spell);
		ASSERT_NE(obstacle, nullptr);
		ASSERT_TRUE(obstacle->damageSnapshot);
		const auto identity = spells::capturedOverwhelmingFormula(obstacle->capturedMdrPenetration);
		ASSERT_TRUE(identity.has_value());
		EXPECT_EQ(identity->side, side);
		EXPECT_EQ(identity->token, 1u);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).lastCandidateCastToken, identity->token);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 0u);
		// Defense is installed AFTER creation: the stored payload is identity,
		// not a snapshot of a particular target's reduction.
		actor->addNewBonus(reduction(50));
		target->addNewBonus(reduction(80));
		const auto friendlyBefore = actor->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(trigger(*obstacle, actor));
		EXPECT_EQ(friendlyBefore - actor->getAvailableHealth(), obstacle->minimalDamage * 50 / 100);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 0u);
		const auto hostileBefore = target->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(trigger(*obstacle, target));
		EXPECT_EQ(hostileBefore - target->getAvailableHealth(), obstacle->minimalDamage * 60 / 100);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, identity->token);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).lastCandidateCastToken, identity->token)
			<< "PASSIVE damage uses the creation token, not another registered Hero cast";
	}

	void checkDirectCastPreemptsPendingHazard(SpellID spell)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto obstacle = createHazard(spell);
		ASSERT_NE(obstacle, nullptr);
		const auto identity = spells::capturedOverwhelmingFormula(obstacle->capturedMdrPenetration);
		ASSERT_TRUE(identity.has_value());
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 0u);
		target->addNewBonus(reduction(50));
		nextHeroAction();
		const auto raw = rawDamage(SpellID::MAGIC_ARROW);
		const auto directBefore = target->getAvailableHealth();
		ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
		EXPECT_EQ(directBefore - target->getAvailableHealth(), raw * 75 / 100);
		const auto winner = battle()->getOverwhelmingFormulaState(side);
		EXPECT_EQ(winner.winningCastToken, 2u);
		EXPECT_NE(winner.winningCastToken, identity->token);
		const auto triggerBefore = target->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(trigger(*obstacle, target));
		EXPECT_EQ(triggerBefore - target->getAvailableHealth(), obstacle->minimalDamage * 50 / 100);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), winner);
	}

	void checkDetachedHazard(SpellID spell)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto obstacle = createHazard(spell);
		ASSERT_NE(obstacle, nullptr);
		const auto pending = battle()->getOverwhelmingFormulaState(side);
		ASSERT_EQ(pending.winningCastToken, 0u);
		target->addNewBonus(reduction(80));
		FormulaEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		HypotheticBattle projected(&environment, callback);
		HypotheticBattle sibling(&environment, callback);
		const auto * projectedTarget = projected.battleGetUnitByID(target->unitId());
		ASSERT_NE(projectedTarget, nullptr);
		const auto before = projectedTarget->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(trigger(*obstacle, projectedTarget, &projected));
		const auto * after = projected.battleGetUnitByID(target->unitId());
		ASSERT_NE(after, nullptr);
		const auto predictedDamage = before - after->getAvailableHealth();
		EXPECT_EQ(predictedDamage, obstacle->minimalDamage * 60 / 100);
		EXPECT_EQ(projected.getOverwhelmingFormulaState(side).winningCastToken, pending.lastCandidateCastToken);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), pending);
		EXPECT_EQ(sibling.getOverwhelmingFormulaState(side), pending);
		const auto liveBefore = target->getAvailableHealth();
		ASSERT_NO_FATAL_FAILURE(trigger(*obstacle, target));
		EXPECT_EQ(liveBefore - target->getAvailableHealth(), predictedDamage);
		EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), projected.getOverwhelmingFormulaState(side));
		EXPECT_EQ(sibling.getOverwhelmingFormulaState(side), pending);
	}
};
}

TEST_F(OverwhelmingFormulaTest, FirstActualProtectedInjuryIgnoresHalfMdrButSecondCastDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(reduction(50));
	const auto firstRaw = rawDamage(SpellID::MAGIC_ARROW);
	ASSERT_GT(firstRaw, 0);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(before - target->getAvailableHealth(), firstRaw * 75 / 100);
	const auto winner = battle()->getOverwhelmingFormulaState(side);
	EXPECT_EQ(winner.lastCandidateCastToken, 1u);
	EXPECT_EQ(winner.winningCastToken, 1u);

	nextHeroAction();
	const auto secondRaw = rawDamage(SpellID::MAGIC_ARROW);
	const auto secondBefore = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(secondBefore - target->getAvailableHealth(), secondRaw * 50 / 100);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, winner.winningCastToken);
}

TEST_F(OverwhelmingFormulaTest, UnprotectedFirstDamageRegistersButDoesNotConsumeWinner)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto raw = rawDamage(SpellID::MAGIC_ARROW);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(before - target->getAvailableHealth(), raw);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).lastCandidateCastToken, 1u);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 0u);

	nextHeroAction();
	target->addNewBonus(reduction(80));
	const auto protectedRaw = rawDamage(SpellID::MAGIC_ARROW);
	const auto protectedBefore = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(protectedBefore - target->getAvailableHealth(), protectedRaw * 60 / 100);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 2u);
}

TEST_F(OverwhelmingFormulaTest, ImmuneRejectedRequestDoesNotRegisterOrConsume)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(reduction(50));
	auto immunity = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW)));
	target->addNewBonus(immunity);
	const auto before = target->getAvailableHealth();
	const auto allowanceBefore = battle()->getHeroActionAllowances(side);
	ASSERT_FALSE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), OverwhelmingFormulaState{});
	EXPECT_EQ(battle()->getHeroActionAllowances(side), allowanceBefore);
	target->removeBonus(immunity);
	const auto raw = rawDamage(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(before - target->getAvailableHealth(), raw * 75 / 100);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 1u);
}

TEST_F(OverwhelmingFormulaTest, AcceptedResistedCastWithZeroInjuryDoesNotConsume)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(reduction(50));
	auto resistance = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID());
	target->addNewBonus(resistance);
	ASSERT_GT(target->magicResistance(), 0);
	const auto seed = resistanceSeed(target);
	ASSERT_TRUE(seed.has_value());
	gameHandler->randomizer->setSeed(*seed);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).lastCandidateCastToken, 1u);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 0u);

	nextHeroAction();
	target->removeBonus(resistance);
	const auto raw = rawDamage(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(before - target->getAvailableHealth(), raw * 75 / 100);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 2u);
}

TEST_F(OverwhelmingFormulaTest, WinningAreaCastKeepsHalfPenetrationForEveryProtectedTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(reduction(50));
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"),
		BattleHex(11, 5), 10000);
	ASSERT_NE(second, nullptr);
	second->addNewBonus(reduction(80));
	const auto raw = rawDamage(SpellID::FIREBALL);
	ASSERT_GT(raw, 0);
	const auto firstBefore = target->getAvailableHealth();
	const auto secondBefore = second->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::FIREBALL, target, true));
	EXPECT_EQ(firstBefore - target->getAvailableHealth(), raw * 75 / 100);
	EXPECT_EQ(secondBefore - second->getAvailableHealth(), raw * 60 / 100);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).lastCandidateCastToken, 1u);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side).winningCastToken, 1u);
}

TEST_F(OverwhelmingFormulaTest, DetachedActualDamageMatchesRequestAndDoesNotConsumeLiveOrSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(reduction(50));
	FormulaEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	HypotheticBattle sibling(&environment, callback);
	const auto * projectedTarget = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	spells::BattleCast event(&projected, attackerSideHero, spells::Mode::HERO,
		SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&event);
	const auto raw = mechanics->getEffectValue();
	const auto before = projectedTarget->getAvailableHealth();
	// castEval is the existing detached effect execution seam, not a server
	// request or proof of the complete AI Hero Action lifecycle.
	mechanics->castEval(projected.getServerCallback(), spells::Target{spells::Destination(projectedTarget)});
	const auto * after = projected.battleGetUnitByID(target->unitId());
	ASSERT_NE(after, nullptr);
	const auto projectedDamage = before - after->getAvailableHealth();
	EXPECT_EQ(projectedDamage, raw * 75 / 100);
	EXPECT_EQ(projected.getOverwhelmingFormulaState(side).winningCastToken, 1u);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), OverwhelmingFormulaState{});
	EXPECT_EQ(sibling.getOverwhelmingFormulaState(side), OverwhelmingFormulaState{});
	const auto liveBefore = target->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(liveBefore - target->getAvailableHealth(), projectedDamage);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(side), projected.getOverwhelmingFormulaState(side));
	EXPECT_EQ(sibling.getOverwhelmingFormulaState(side), OverwhelmingFormulaState{});
}

TEST_F(OverwhelmingFormulaTest, MineCreationDoesNotConsumeAndPassiveDamageUsesCurrentHostilityAndMdr)
{
	ASSERT_NO_FATAL_FAILURE(checkHazardFriendlyThenHostile(SpellID::LAND_MINE));
}

TEST_F(OverwhelmingFormulaTest, FireWallCreationDoesNotConsumeAndPassiveDamageUsesCurrentHostilityAndMdr)
{
	ASSERT_NO_FATAL_FAILURE(checkHazardFriendlyThenHostile(SpellID::FIRE_WALL));
}

TEST_F(OverwhelmingFormulaTest, DirectProtectedInjuryPreemptsPreviouslyCreatedMine)
{
	ASSERT_NO_FATAL_FAILURE(checkDirectCastPreemptsPendingHazard(SpellID::LAND_MINE));
}

TEST_F(OverwhelmingFormulaTest, DirectProtectedInjuryPreemptsPreviouslyCreatedFireWall)
{
	ASSERT_NO_FATAL_FAILURE(checkDirectCastPreemptsPendingHazard(SpellID::FIRE_WALL));
}

TEST_F(OverwhelmingFormulaTest, DetachedMineTriggerClaimsOnlyItsOwnStateAndMatchesRealPassiveDamage)
{
	ASSERT_NO_FATAL_FAILURE(checkDetachedHazard(SpellID::LAND_MINE));
}

TEST_F(OverwhelmingFormulaTest, DetachedFireWallTriggerClaimsOnlyItsOwnStateAndMatchesRealPassiveDamage)
{
	ASSERT_NO_FATAL_FAILURE(checkDetachedHazard(SpellID::FIRE_WALL));
}
