/*
 * NewHorizonsCrownAndAltarTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
constexpr auto skillId = "new-horizons:divineMandate";
constexpr auto perkId = "new-horizons:divineMandate.crownAndAltar";

class CrownEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CrownEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsCrownAndAltarTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * ward = nullptr;
	CStack * shooter = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void select(const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.perkId == perk && offers[choice].selection.skillId == skillId)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skillId, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}

	void prepare(bool selected = true)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		select("new-horizons:divineMandate.consecratedCasting", MasteryLevel::BASIC);
		select("new-horizons:divineMandate.purifyingMandate", MasteryLevel::ADVANCED);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
			select(perkId, MasteryLevel::EXPERT);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->initializeSpellPoints(attackerSideHero->manaLimit(), 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 50, ChangeValueMode::ABSOLUTE);
		for(const auto key : {"new-horizons:holyArmor", "new-horizons:heavenlyGale", "core:cure"})
			attackerSideHero->addSpellToSpellbook(SpellID(SpellID::decode(key)));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 100);
		ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(90), 100);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 100);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(auto * unit : {friendly, ward, shooter, enemy})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MORALE, BonusSource::OTHER, -100, BonusSourceID()));
		ASSERT_LT(battle()->battleGetMorale(friendly), 0);
	}

	void castLight()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::BLESS;
		action.aimToUnit(friendly);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		const auto allowance = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(allowance);
		ASSERT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	}

	bool cast(const char * key, const CStack * recipient)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID(SpellID::decode(key));
		action.aimToUnit(recipient);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	HeroOrderState current(HeroCommand command)
	{
		const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, command);
		EXPECT_TRUE(state);
		return state.value_or(HeroOrderState{});
	}
};
}



TEST_F(NewHorizonsCrownAndAltarTest, ActualSpellThenOrderCapturesOnlyFirstRecipientWithoutSharedPurpose)
{
	prepare();
	ASSERT_FALSE(newHorizonsDivineMandate::hasSharedPurposePerk(attackerSideHero));
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto order = current(HeroCommand::RIPOSTE);
	EXPECT_EQ(order.crownAndAltarRecipientUnitIds, std::vector<uint32_t>{friendly->unitId()});
	EXPECT_FALSE(ward->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus)));
	EXPECT_FALSE(friendly->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus)));
}

TEST_F(NewHorizonsCrownAndAltarTest, ActualOrderThenHolyArmorScalesPowerButPreservesFixedBase)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const SpellID spell(SpellID::decode("new-horizons:holyArmor"));
	spells::BattleCast query(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	ASSERT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	const auto expected = std::min<int64_t>(60, 30
		+ mechanics->scaleRecipientSpellPowerComponent(mechanics->getEffectPower(), 5, friendly));
	ASSERT_TRUE(cast("new-horizons:holyArmor", friendly));
	EXPECT_EQ(friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), expected);
}

TEST_F(NewHorizonsCrownAndAltarTest, GuardianSharedGetterPreservesRealCastleCrownAndOrdinaryFirstAction)
{
	prepare();
	const SpellID spell(SpellID::decode("new-horizons:guardianSpirit"));
	attackerSideHero->addSpellToSpellbook(spell);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 7, ChangeValueMode::ABSOLUTE);
	spells::BattleCast firstQuery(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto first = spell.toSpell()->battleMechanics(&firstQuery);
	EXPECT_EQ(first->getCrownAndAltarBonusPercent(friendly), 0);
	EXPECT_EQ(first->getGuardianSpiritHitPoints(friendly), 50
		+ first->scaleSpellPowerComponentWithCoefficientBasisPoints(14, 1, first->getSpellPowerCoefficientBasisPoints()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			friendly->unitId(), ward->unitId())));
	const auto allowance = battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, spell);
	ASSERT_TRUE(allowance);
	ASSERT_EQ(allowance->allowance, HeroActionAllowanceState::AllowanceKind::SPELL);
	ASSERT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	const auto & grants = battle()->getHeroActionAllowances(BattleSide::ATTACKER).grants;
	const auto grant = std::find_if(grants.begin(), grants.end(), [&allowance](const auto & entry)
		{ return entry.id == allowance->grantId; });
	ASSERT_NE(grant, grants.end());
	EXPECT_EQ(grant->divineMandateRecipients, (std::vector<uint32_t>{friendly->unitId(), ward->unitId()}));
	EXPECT_FALSE(vstd::contains(grant->divineMandateRecipients, shooter->unitId()));
	spells::BattleCast secondQuery(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto second = spell.toSpell()->battleMechanics(&secondQuery);
	ASSERT_EQ(second->getCrownAndAltarBonusPercent(friendly), 20);
	EXPECT_EQ(second->getCrownAndAltarBonusPercent(shooter), 0);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(spell), 0);
	const auto expected = 50 + second->scaleRecipientSpellPowerComponent(14, 1, friendly);
	EXPECT_EQ(second->getGuardianSpiritHitPoints(friendly), expected);
	EXPECT_EQ(second->getGuardianSpiritHitPoints(shooter), 50
		+ second->scaleSpellPowerComponentWithCoefficientBasisPoints(14, 1, second->getSpellPowerCoefficientBasisPoints()));
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast("new-horizons:guardianSpirit", friendly));
	EXPECT_EQ(friendly->guardianSpiritHitPoints, expected);
	EXPECT_EQ(friendly->guardianSpiritRoundsRemaining, 2);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsCrownAndAltarTest, GuardianRealCastleDetachedCrownMatchesPaidPoolWithoutLiveMutation)
{
	prepare();
	const SpellID spell(SpellID::decode("new-horizons:guardianSpirit"));
	attackerSideHero->addSpellToSpellbook(spell);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	CrownEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast query(&projected, attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	const auto * recipient = projected.battleGetUnitByID(friendly->unitId());
	ASSERT_EQ(mechanics->getCrownAndAltarBonusPercent(recipient), 20);
	spells::Target aim{spells::Destination(recipient)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	const auto expected = mechanics->getGuardianSpiritHitPoints(recipient);
	mechanics->castEval(projected.getServerCallback(), aim);
	const auto * detached = dynamic_cast<const battle::CUnitState *>(projected.battleGetUnitByID(friendly->unitId()));
	ASSERT_NE(detached, nullptr);
	EXPECT_EQ(detached->guardianSpiritHitPoints, expected);
	EXPECT_EQ(friendly->guardianSpiritHitPoints, 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(cast("new-horizons:guardianSpirit", friendly));
	EXPECT_EQ(friendly->guardianSpiritHitPoints, detached->guardianSpiritHitPoints);
}

TEST_F(NewHorizonsCrownAndAltarTest, RecipientAndSpecialtyArithmeticComposesBeforeSingleFloor)
{
	prepare();
	const SpellID spell(SpellID::decode("new-horizons:guardianSpirit"));
	attackerSideHero->addSpellToSpellbook(spell);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	spells::BattleCast query(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	ASSERT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	ASSERT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(spell), 0);
	// Pure numerical pipeline only: Castle Crown and Labetha's Conflux specialty
	// cannot both be admitted on this actual hero. Never forge either identity.
	ASSERT_EQ(mechanics->getWarcastingBonusPercent(), 0);
	ASSERT_EQ(mechanics->getEmpowerSpellBonusPercent(), 0);
	const int64_t coefficient = mechanics->getSpellPowerCoefficientBasisPoints();
	bool observedPrematureFloorDifference = false;
	for(int64_t numerator = 1; numerator <= 32; ++numerator)
	{
		const auto actual = mechanics->scaleRecipientSpellPowerComponentWithSpecialty(numerator, 1, friendly, 20);
		const auto expected = numerator * coefficient * 120 * 120 / (10000LL * 100 * 100);
		EXPECT_EQ(actual, expected);
		const auto crownOnly = mechanics->scaleRecipientSpellPowerComponent(numerator, 1, friendly);
		observedPrematureFloorDifference |= actual != crownOnly * 120 / 100;
		EXPECT_EQ(mechanics->scaleRecipientSpellPowerComponentWithSpecialty(numerator, 1, friendly, 0), crownOnly);
	}
	EXPECT_TRUE(observedPrematureFloorDifference);
}

TEST_F(NewHorizonsCrownAndAltarTest, ActualProtectThenArmySpellBoostsOnlyIntersectingRecipients)
{
	prepare();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			friendly->unitId(), ward->unitId())));
	const SpellID spell(SpellID::decode("new-horizons:heavenlyGale"));
	spells::BattleCast query(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	EXPECT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	EXPECT_EQ(mechanics->getCrownAndAltarBonusPercent(ward), 20);
	EXPECT_EQ(mechanics->getCrownAndAltarBonusPercent(shooter), 0);
	const auto expected = [&](const CStack * unit)
	{
		return std::min<int64_t>(8000, 5000 + mechanics->scaleRecipientSpellPowerComponent(15LL * mechanics->getEffectPower(), 1, unit));
	};
	const auto matched = expected(friendly);
	const auto unmatched = expected(shooter);
	ASSERT_GT(matched, unmatched);
	ASSERT_TRUE(cast("new-horizons:heavenlyGale", friendly));
	EXPECT_EQ(friendly->valOfBonuses(BonusType::HEAVENLY_GALE), matched);
	EXPECT_EQ(ward->valOfBonuses(BonusType::HEAVENLY_GALE), matched);
	EXPECT_EQ(shooter->valOfBonuses(BonusType::HEAVENLY_GALE), unmatched);
}

TEST_F(NewHorizonsCrownAndAltarTest, FirstActionIsNeverRetroactivelyStrengthened)
{
	prepare();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 100);
	ASSERT_TRUE(cast("new-horizons:holyArmor", friendly));
	const int original = friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	const auto state = current(HeroCommand::RIPOSTE);
	const auto & formula = battle()->getHeroCommandRules()["commands"]["riposte"]["effects"]["retaliationDamagePercent"];
	EXPECT_GT(heroCommands::coefficient(newHorizonsDivineMandate::crownOrderFormula(formula, true),
		*attackerSideHero, state.warcastingBonusPercent, state.divineMandateEfficiencyBonusPercent()),
		heroCommands::coefficient(formula, *attackerSideHero, state.warcastingBonusPercent,
			state.divineMandateEfficiencyBonusPercent()));
}

TEST_F(NewHorizonsCrownAndAltarTest, UnsupportedRecipientAndUnselectedPerkDoNotGainBonus)
{
	prepare(false);
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).crownAndAltarRecipientUnitIds.empty());
	EXPECT_FALSE(newHorizonsDivineMandate::hasCrownAndAltarPerk(attackerSideHero));
}

TEST_F(NewHorizonsCrownAndAltarTest, SecondActionFriendlyControllerAndPreparedCaptureStayIndependentOfPayment)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const SpellID spell(SpellID::decode("new-horizons:holyArmor"));
	spells::BattleCast query(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	ASSERT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	auto hostile = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(hostile);
	EXPECT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 0);
	friendly->removeBonus(hostile);
	ASSERT_TRUE(cast("new-horizons:holyArmor", friendly));
	EXPECT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	spells::BattleCast later(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	EXPECT_EQ(spell.toSpell()->battleMechanics(&later)->getCrownAndAltarBonusPercent(friendly), 0);
}

TEST_F(NewHorizonsCrownAndAltarTest, DetachedRealEffectMatchesLiveWithoutMutatingParentOrSibling)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	CrownEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto original = friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION);
	ASSERT_EQ(original, 0);
	ASSERT_EQ(parent->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	ASSERT_EQ(sibling->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	const SpellID spell(SpellID::decode("new-horizons:holyArmor"));
	spells::BattleCast projection(child.get(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	projection.castEval(child->getServerCallback(), {spells::Destination(child->battleGetUnitByID(friendly->unitId()))});
	EXPECT_GT(child->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	EXPECT_EQ(friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	EXPECT_EQ(parent->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	EXPECT_EQ(sibling->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), original);
	ASSERT_TRUE(cast("new-horizons:holyArmor", friendly));
	EXPECT_EQ(child->battleGetUnitByID(friendly->unitId())->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION),
		friendly->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION));
}

TEST(NewHorizonsCrownAndAltarProtocolTest, CapturedIntersectionRoundTripAndOldWriterPreflight)
{
	HeroOrderState order;
	order.command = HeroCommand::FOCUS_FIRE;
	order.issuedRound = 1;
	order.primaryTargetUnitId = 5;
	order.crownAndAltarRecipientUnitIds = {1, 3};
	order.crownAndAltarFocusFirePercent = 42;
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(wire.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(wire.iser & restored);
	EXPECT_EQ(restored, order);
	StartAction action(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	action.orderState = order;
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES;
	EXPECT_THROW(older.oser & action, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
	order.crownAndAltarRecipientUnitIds.clear();
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	JsonNode formula;
	formula["base"].Float() = 50;
	formula["attack"].Float() = 0.5;
	const auto scaled = newHorizonsDivineMandate::crownOrderFormula(formula, true);
	EXPECT_DOUBLE_EQ(scaled["base"].Float(), 50);
	EXPECT_DOUBLE_EQ(scaled["attack"].Float(), 0.6);
}

TEST_F(NewHorizonsCrownAndAltarTest, ActualCureKeepsFixedHealingAndBoostsOnlyCapturedPowerComponent)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	auto injured = friendly->acquireState();
	int64_t injury = friendly->getMaxHealth() - 1;
	injured->damage(injury);
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(friendly->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = injured->save();
	update.changedStacks.push_back(change);
	gameHandler->sendAndApply(update);
	const SpellID spell(SpellID::CURE);
	spells::BattleCast query(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&query);
	ASSERT_EQ(mechanics->getCrownAndAltarBonusPercent(friendly), 20);
	const auto boosted = mechanics->getRecipientEffectValue(friendly);
	ASSERT_GT(boosted, mechanics->getEffectValue());
	const auto before = friendly->getAvailableHealth();
	const auto expected = std::min<int64_t>(friendly->getMaxHealth() - 1,
		mechanics->applySpellBonus(boosted, friendly));
	ASSERT_TRUE(cast("core:cure", friendly));
	EXPECT_EQ(friendly->getAvailableHealth(), before + expected);
}
