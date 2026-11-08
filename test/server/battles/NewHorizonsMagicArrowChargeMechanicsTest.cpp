/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/ProxyCaster.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/SetMetamagicPointState.h"
#include "../../../lib/spells/DirectSpellEffectPreview.h"
#include "../../../lib/GameLibrary.h"
#include <limits>
#include <array>
#include <vcmi/Environment.h>

namespace
{
const SpellID ARROW(SpellID::MAGIC_ARROW);

class ArrowEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ArrowEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMagicArrowChargeMechanics : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	const CSpell * spell = nullptr;

	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		auto & arrow = rules["spells"]["core:magicArrow"];
		arrow["magicArrow"]["version"].Integer() = 1;
		arrow["directDamage"]["base"].Integer() = 20;
		arrow["directDamage"]["powerCoefficient"].Integer() = 10;
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int power = 200, int mana = 200, bool inscribed = true,
		std::optional<ArtifactPosition> scrollPosition = std::nullopt)
	{
		startGame();
		spell = ARROW.toSpell();
		ASSERT_NE(spell, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		if(inscribed)
			attackerSideHero->addSpellToSpellbook(ARROW);
		else
			attackerSideHero->removeAllSpells();
		if(scrollPosition)
			ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, ARROW, *scrollPosition));
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 200, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, mana);
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		ASSERT_NE(target, nullptr);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = battle()->battleGetAllUnits(false).front()->unitId();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::ATTACKER)
			{
				active.stack = unit->unitId();
				break;
			}
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
	}

	CStack * creatureCaster()
	{
		auto * creature = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 3), 10);
		if(!creature)
			return nullptr;
		creature->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER,
			BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(ARROW)));
		creature->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CASTS,
			BonusSource::OTHER, 1, BonusSourceID()));
		creature->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_SPELL_POWER,
			BonusSource::OTHER, 20, BonusSourceID()));
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = creature->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		return creature;
	}

	BattleAction action(int q, SpellID id = ARROW) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = id;
		result.spellOvercharge = q;
		result.aimToUnit(target);
		return result;
	}

	void rejectWithoutSpending(const BattleAction & request)
	{
		const auto mana = attackerSideHero->getManaAvailable();
		const auto health = target->getAvailableHealth();
		const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
		const auto started = server.startedActions.size();
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), request));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_EQ(target->getAvailableHealth(), health);
		EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts);
		EXPECT_EQ(server.startedActions.size(), started);
		EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
	}
};
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, FullPriceDamageAndDetachedPredictionAgree)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::Target destination{spells::Destination(target)};
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setOvercharge(2);
	const auto mechanics = spell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	EXPECT_EQ(mechanics->getEffectValue(), 620);
	EXPECT_EQ(mechanics->getTargetAwareManaCost(destination), 30);

	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = target->getAvailableHealth();
	ArrowEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast forecast(&predicted, attackerSideHero, spells::Mode::HERO, spell);
	forecast.setOvercharge(2);
	forecast.castEval(predicted.getServerCallback(), destination);
	EXPECT_EQ(health - predicted.battleGetUnitByID(target->unitId())->getAvailableHealth(), 620);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(2)));
	EXPECT_EQ(health - target->getAvailableHealth(), 620);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 30);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, WisdomDiscountsOnlyTheOrdinaryFourMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_EQ(battle()->battleGetSpellCost(spell, attackerSideHero), 3);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(2)));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 29);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, OldCheapChargeAffordabilityCannotSpendAnAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200, 6));
	// Six Mana paid for old q2; the redesigned q2 costs thirty.
	rejectWithoutSpending(action(2));
	rejectWithoutSpending(action(-1));
	rejectWithoutSpending(action(6));
	rejectWithoutSpending(action(std::numeric_limits<int32_t>::max()));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, StaleRawPowerCapacityIsRevalidated)
{
	ASSERT_NO_FATAL_FAILURE(prepare(150));
	spells::BattleCast original(battle(), attackerSideHero, spells::Mode::HERO, spell);
	original.setOvercharge(5);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(spell->battleMechanics(&original)->canBeCast(problem));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 149, ChangeValueMode::ABSOLUTE);
	rejectWithoutSpending(action(5));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, ContextualEffectPowerCannotRaiseTheRawCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(49));
	spells::BattleCast boosted(battle(), attackerSideHero, spells::Mode::HERO, spell);
	boosted.setEffectPower(100);
	boosted.setOvercharge(3);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(spell->battleMechanics(&boosted)->canBeCast(problem));
	boosted.setOvercharge(2);
	EXPECT_TRUE(spell->battleMechanics(&boosted)->canBeCast(problem));
	EXPECT_EQ(spell->battleMechanics(&boosted)->getEffectValue(), 320);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, OverchargerAddsOnlyOneLegalLevel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"});
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setOvercharge(6);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCast(problem));
	// Basic Sorcery adds 15 percent only to (1+6)*200.
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 1630);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(6)));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 140);
	EXPECT_EQ(health - target->getAvailableHealth(), 1630);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, RemovingTheQualifyingRankInvalidatesTheExtraLevel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"});
	spells::BattleCast original(battle(), attackerSideHero, spells::Mode::HERO, spell);
	original.setOvercharge(6);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(spell->battleMechanics(&original)->canBeCast(problem));
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	rejectWithoutSpending(action(6));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, EmpowerDoesNotTreatChargePaymentAsOrdinarySpellCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", "new-horizons:spellcraft.spellPenetration"});
	attackerSideHero->applyPerkSelection({"new-horizons:spellcraft", "new-horizons:spellcraft.empowerSpell"});
	EXPECT_EQ(newHorizonsMagic::empowerSpellBonusPercent(attackerSideHero->getMagicRules(), attackerSideHero, ARROW), 0);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setOvercharge(5);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 1460);
	EXPECT_EQ(spell->battleMechanics(&cast)->getTargetAwareManaCost({spells::Destination(target)}), 105);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, ExistingSingularityDiscountAppliesAfterTheFullChargePrice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
	const auto metamagic = SecondarySkill(SecondarySkill::decode("new-horizons:metamagic"));
	const std::array<const char *, 3> perks{"new-horizons:metamagic.lingeringMagic",
		"new-horizons:metamagic.spellFracture", "new-horizons:metamagic.arcaneSingularity"};
	for(size_t tier = 0; tier < perks.size(); ++tier)
	{
		attackerSideHero->setSecSkillLevel(metamagic, static_cast<int>(tier) + 1, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:metamagic", perks[tier]});
	}
	SetMetamagicPointState update;
	update.battleID = BattleID(0);
	update.side = BattleSide::ATTACKER;
	update.round = battle()->getRound();
	update.state = {12, true, false};
	gameHandler->sendAndApply(update);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setOvercharge(2);
	// The listed q2 payment is thirty, then Singularity applies its existing
	// whole-spell 25 percent discount and rounds up once: ceil(30*.75)=23.
	EXPECT_EQ(spell->battleMechanics(&cast)->getTargetAwareManaCost({spells::Destination(target)}), 23);
	setTestSpellPointTotal(attackerSideHero, 22);
	rejectWithoutSpending(action(2));
	setTestSpellPointTotal(attackerSideHero, 23);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(2)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, UnchargedDefaultAndSchoolSpellcraftPreviewShareOnePowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getOvercharge(), 0);
	EXPECT_EQ(mechanics->getEffectValue(), 397);
	EXPECT_EQ(mechanics->getTargetAwareManaCost({spells::Destination(target)}), 4);
	EXPECT_EQ(newHorizonsMagic::directSpellRawPotential(*attackerSideHero, *spell,
		attackerSideHero->getMagicRules(), 200, 18850, 0), 397);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, OtherSpellChargeAndMissingSpellKnowledgeStillReject)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID ice(SpellID::ICE_BOLT);
	attackerSideHero->addSpellToSpellbook(ice);
	rejectWithoutSpending(action(1, ice));
	attackerSideHero->removeAllSpells();
	rejectWithoutSpending(action(0));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, NonmanualAndCopiedCasterRoutesCannotRequestPaidCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = target->getAvailableHealth();
	for(const auto mode : {spells::Mode::MAGIC_MIRROR, spells::Mode::PASSIVE,
		spells::Mode::ENCHANTER, spells::Mode::SPELL_LIKE_ATTACK})
	{
		spells::BattleCast copy(battle(), attackerSideHero, mode, spell);
		copy.setOvercharge(1);
		spells::detail::ProblemImpl problem;
		EXPECT_FALSE(spell->battleMechanics(&copy)->canBeCast(problem));
	}
	spells::ProxyCaster proxy(attackerSideHero);
	spells::BattleCast forgedHero(battle(), &proxy, spells::Mode::HERO, spell);
	forgedHero.setOvercharge(1);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(spell->battleMechanics(&forgedHero)->canBeCast(problem));
	spells::BattleCast automatic(battle(), attackerSideHero, spells::Mode::HERO, spell);
	spells::AutomaticHeroCastReceipt receipt;
	receipt.kind = spells::AutomaticHeroCastKind::DIVINE_FURY;
	automatic.setAutomaticHeroCast(receipt);
	automatic.setOvercharge(1);
	EXPECT_FALSE(spell->battleMechanics(&automatic)->canBeCast(problem));
	EXPECT_FALSE(newHorizonsMagic::magicArrowSelectionValid(battle()->getMagicRules(), ARROW,
		attackerSideHero, 200, 1, false));
	EXPECT_FALSE(newHorizonsMagic::magicArrowSelectionValid(battle()->getMagicRules(), ARROW,
		nullptr, 200, 1));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, CreatureAbilityRetainsItsUnchargedCastButCannotBuyHeroCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * creature = creatureCaster();
	ASSERT_NE(creature, nullptr);
	spells::BattleCast cast(battle(), creature, spells::Mode::CREATURE_ACTIVE, spell);
	cast.setOvercharge(1);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(spell->battleMechanics(&cast)->canBeCast(problem));
	cast.setOvercharge(0);
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCast(problem));
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCastAt({spells::Destination(target)}, problem));
	const auto mana = attackerSideHero->getManaAvailable();
	cast.cast(gameHandler->spellEnv.get(), {spells::Destination(target)});
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, EquippedScrollKeepsItsManualHeroRouteAndPaysTheCompleteChargePrice)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200, 30, false, ArtifactPosition::MISC1));
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(ARROW));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(spell));
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(2)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(health - target->getAvailableHealth(), 620);
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(ARROW));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, BackpackScrollCannotBecomeCastingAccessThroughChargeSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200, 200, false, ArtifactPosition::BACKPACK_START));
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(ARROW));
	ASSERT_FALSE(attackerSideHero->canCastThisSpell(spell));
	rejectWithoutSpending(action(2));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, CreatureActionEnvelopeRejectsChargeBeforeSpendingItsActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * creature = creatureCaster();
	ASSERT_NE(creature, nullptr);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = target->getAvailableHealth();
	const auto started = server.startedActions.size();
	BattleAction request;
	request.actionType = EActionType::MONSTER_SPELL;
	request.side = BattleSide::ATTACKER;
	request.stackNumber = creature->unitId();
	request.spell = ARROW;
	request.aimToUnit(target);
	for(const int invalidCharge : {-1, 1, std::numeric_limits<int32_t>::max()})
	{
		request.spellOvercharge = invalidCharge;
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), request));
	}
	EXPECT_EQ(server.startedActions.size(), started);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(creature->castSpellThisTurn);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), creature->unitId());
	request.spellOvercharge = 0;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), request));
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, OrdinaryAiHeroProjectionAcceptsPaidChargeAndCommitsOnlyItsOwnReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto health = target->getAvailableHealth();
	ArrowEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	HypotheticBattle projected(&environment, parent);
	HypotheticBattle sibling(&environment, parent);
	const auto allowance = projected.prepareHeroSpellAllowance(BattleSide::ATTACKER, ARROW, false, false);
	ASSERT_TRUE(allowance);
	const auto * canonicalHero = projected.getSideHero(BattleSide::ATTACKER);
	ASSERT_NE(canonicalHero, nullptr);
	ASSERT_EQ(canonicalHero->id, attackerSideHero->id);
	spells::BattleCast forecast(&projected, canonicalHero, spells::Mode::HERO, spell);
	forecast.setOvercharge(2);
	const auto mechanics = spell->battleMechanics(&forecast);
	spells::detail::ProblemImpl problem;
	const spells::Target destination{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(destination, problem));
	EXPECT_EQ(mechanics->getTargetAwareManaCost(destination), 30);
	ASSERT_TRUE(projected.beginProjectedHeroAction(BattleSide::ATTACKER, *allowance));
	mechanics->castEval(projected.getServerCallback(), destination);
	ASSERT_TRUE(projected.projectAcceptedHeroSpell(BattleSide::ATTACKER, ARROW, target->unitId(),
		false, false, false, false, *allowance, {target->unitId()}));
	EXPECT_EQ(health - projected.battleGetUnitByID(target->unitId())->getAvailableHealth(), 620);
	EXPECT_FALSE(projected.battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
	EXPECT_TRUE(sibling.battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
	EXPECT_EQ(sibling.battleGetUnitByID(target->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, ARROW));
}

TEST_F(NewHorizonsMagicArrowChargeMechanics, AcceptedChargedSpellCanReflectWithoutBuyingASecondCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	ASSERT_NE(recipient, nullptr);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::ATTACKER && unit->unitId() != recipient->unitId())
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = recipient->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_MIRROR,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto protectedHealth = target->getAvailableHealth();
	const auto reflectedHealth = recipient->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(2)));
	EXPECT_EQ(target->getAvailableHealth(), protectedHealth);
	EXPECT_EQ(reflectedHealth - recipient->getAvailableHealth(), 620);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 30);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}
