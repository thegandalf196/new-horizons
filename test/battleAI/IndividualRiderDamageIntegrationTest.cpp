/*
 * IndividualRiderDamageIntegrationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"

namespace
{
class IndividualRiderDamageIntegrationTest : public HeroCommandFixture
{
protected:
	using Snapshot = std::vector<std::vector<std::byte>>;
	using Branch = std::shared_ptr<HypotheticBattle>;
	bool schoolRider = false;
	CStack * actor = nullptr;
	CStack * target = nullptr;
	CStack * other = nullptr;
	std::unique_ptr<projectedHitTest::Environment> environment;

	static SpellID vines()
	{
		return SpellID(SpellID::decode(std::string(newHorizonsVengefulVines::SPELL_KEY)));
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		magic.setOverrideFlag(true);
		ASSERT_EQ(magic["schoolSkills"].Struct().size(), 6u);
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(magic));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		auto & riders = rules["individualSpecialties"];
		riders["version"].Integer() = 1;
		riders["graveReaper"]["percentagePointsPerLevel"].Integer() = 1;
		riders["graveReaper"]["maximumPercentagePoints"].Integer() = 20;
		// Deliberately unlike installed 5/100: forecasts must use the captured policy.
		riders["warlocksFormula"]["percentPerLevel"].Integer() = 3;
		riders["warlocksFormula"]["maximumPercent"].Integer() = 60;
		rules.Struct().erase("schoolSpellcraftSpecialties");
		if(schoolRider)
		{
			auto & school = rules["schoolSpellcraftSpecialties"];
			school["version"].Integer() = 1;
			for(const auto key : {"overcaster", "darkSavant", "primalFormula", "mireHexer"})
			{
				school[key]["percentPerLevel"].Integer() = 3;
				school[key]["maximumPercent"].Integer() = 60;
			}
		}
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool school = false)
	{
		schoolRider = school;
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(schoolRider ? "core:gird" : "core:malekith")));
		attackerSideHero->level = 10;
		for(const auto & [school, skill] : attackerSideHero->getMagicRules()["schoolSkills"].Struct())
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill.String())),
				MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(vines());
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		const auto policy = newHorizonsHeroes::individualSpecialtyRules(attackerSideHero->getPrimaryGrowthRules());
		ASSERT_TRUE(policy);
		ASSERT_EQ(policy->warlocksFormula.percentPerLevel, 3);
		ASSERT_EQ(policy->warlocksFormula.maximumPercent, 60);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 9), 1000);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 1000);
		other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 1000);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(other, nullptr);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = actor->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
	}

	BattleAction action(SpellID spell) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = spell;
		if(spell == vines())
			for(const auto hex : {BattleHex(8, 5), BattleHex(9, 5), BattleHex(10, 5)})
				result.aimToHex(hex);
		else
			result.aimToUnit(target);
		return result;
	}

	Snapshot nativeSnapshot()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}

	Snapshot branchSnapshot(const Branch & branch)
	{
		Snapshot result;
		for(const auto * unit : branch->battleGetAllUnits(false))
		{
			result.push_back(projectedHitTest::serialized(unit->acquireState()->save()));
			std::vector<JsonNode> bonuses;
			const auto ownedBonuses = unit->getBonuses(Selector::all);
			for(const auto & bonus : *ownedBonuses)
				bonuses.push_back(bonus->toJsonNode());
			result.push_back(projectedHitTest::serialized(bonuses));
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			result.push_back(projectedHitTest::serialized(branch->getHeroActionAllowances(side)));
			result.push_back(projectedHitTest::serialized(branch->getHeroOrderStates(side)));
			result.push_back(projectedHitTest::serialized(branch->getCrossSchoolFormulaState(side)));
			result.push_back(projectedHitTest::serialized(branch->getOverwhelmingFormulaState(side)));
			result.push_back(projectedHitTest::serialized(branch->getSpellResponseState(side)));
			result.push_back(projectedHitTest::serialized(branch->getUsedSpells(side)));
		}
		result.push_back(projectedHitTest::serialized(branch->getActiveStackID()));
		result.push_back(projectedHitTest::serialized(branch->battleGetRound()));
		return result;
	}

	void warm(const Branch & branch)
	{
		for(const auto * unit : branch->battleGetAllUnits(false))
			(void)branch->getForUpdate(unit->unitId())->counterAttacks.available();
	}

	void project(const Branch & branch, const PossibleSpellcast & candidate, const CBattleInfoCallback & live)
	{
		const auto admission = heroActionProjection::admitCandidate(*branch, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		uint32_t id = target->unitId();
		const auto projection = battleEvaluatorProjection::projectSpell(*branch, live, attackerSideHero,
			BattleSide::ATTACKER, candidate, admission, id, {});
		ASSERT_TRUE(projection.accepted);
	}
};
}

TEST_F(IndividualRiderDamageIntegrationTest, ActualVinesBoostsOnlyPowerDamageAndKeepsFixedSlowAndDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto castAction = action(vines());
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, vines().toSpell());
	const auto mechanics = vines().toSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(castAction.getTarget(battle())));
	ASSERT_EQ(mechanics->getEffectPower(), 200);
	ASSERT_EQ(mechanics->getEffectPowerDivisor(), 10);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 14950);
	EXPECT_EQ(mechanics->getDirectDamageSpellPowerCoefficientBasisPoints(), 15985);
	EXPECT_EQ(mechanics->getEffectDuration(), 29);
	// 20 fixed + floor(11*200/10 * 1.15 Nature * 1.39 strengthened Spellcraft).
	const int64_t expected = 20 + 11LL * 200 * 115 * 139 / (10 * 100 * 100);
	ASSERT_EQ(expected, 371);
	EXPECT_EQ(mechanics->getEffectValue(), expected);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto actorHealth = actor->getAvailableHealth();
	const auto health = target->getAvailableHealth(), otherHealth = other->getAvailableHealth();
	const auto speed = target->getMovementRange();
	const auto initiative = target->getInitiative();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), castAction));
	EXPECT_EQ(health - target->getAvailableHealth(), expected);
	EXPECT_EQ(otherHealth - other->getAvailableHealth(), expected);
	EXPECT_EQ(actor->getAvailableHealth(), actorHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 5);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts + 1);
	EXPECT_EQ(target->getMovementRange(), speed - 2);
	EXPECT_EQ(target->getInitiative(), initiative);
	const auto slow = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(vines()))
		.And(Selector::type()(BonusType::STACKS_MOVEMENT_RANGE)));
	ASSERT_EQ(slow->size(), 1u);
	EXPECT_EQ(slow->front()->val, -2);
	EXPECT_EQ(slow->front()->turnsRemain, 2);
}

TEST_F(IndividualRiderDamageIntegrationTest, DetachedOriginalCastKeepsCapturedCoefficientAndBranchIsolationOnceOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::MAGIC_ARROW);
	// No Sorcery rank: 20 fixed + floor(20*200/10 * 1.39 Spellcraft).
	const int64_t expected = 20 + 20LL * 200 * 139 / (10 * 100);
	ASSERT_EQ(expected, 576);
	const auto originalHealth = target->getAvailableHealth();
	const auto policyBefore = attackerSideHero->getPrimaryGrowthRules();
	const auto magicBefore = attackerSideHero->getMagicRules();
	const auto nativeBefore = nativeSnapshot();
	int64_t projectedHealth = 0;
	{
		const auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		warm(parent);
		auto first = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		warm(first);
		warm(sibling);
		const auto parentBefore = branchSnapshot(parent), siblingBefore = branchSnapshot(sibling);
		PossibleSpellcast candidate;
		candidate.spell = spell.toSpell();
		candidate.dest = {spells::Destination(target)};
		spells::BattleCast preview(first.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
		const auto mechanics = candidate.spell->battleMechanics(&preview);
		EXPECT_EQ(mechanics->getHeroCaster()->getPrimaryGrowthRules(), policyBefore);
		EXPECT_EQ(mechanics->getDirectDamageSpellPowerCoefficientBasisPoints(), 13900);
		EXPECT_EQ(mechanics->getEffectValue(), expected);
		ASSERT_NO_FATAL_FAILURE(project(first, candidate, *live));
		projectedHealth = first->battleGetUnitByID(target->unitId())->getAvailableHealth();
		EXPECT_EQ(originalHealth - projectedHealth, expected);
		EXPECT_EQ(branchSnapshot(parent), parentBefore);
		EXPECT_EQ(branchSnapshot(sibling), siblingBefore);
		EXPECT_EQ(nativeSnapshot(), nativeBefore);
		// The same branch cannot spend this Hero Action twice; rebuilding a preview is read-only.
		const auto secondAdmission = heroActionProjection::admitCandidate(*first, BattleSide::ATTACKER, candidate);
		EXPECT_FALSE(secondAdmission.admitted);
		spells::BattleCast repeated(first.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
		EXPECT_EQ(candidate.spell->battleMechanics(&repeated)->getEffectValue(), expected);
		EXPECT_EQ(first->battleGetUnitByID(target->unitId())->getAvailableHealth(), projectedHealth);
		ASSERT_NO_FATAL_FAILURE(project(sibling, candidate, *live));
		EXPECT_EQ(sibling->battleGetUnitByID(target->unitId())->getAvailableHealth(), projectedHealth);
		EXPECT_EQ(branchSnapshot(parent), parentBefore);
		EXPECT_EQ(nativeSnapshot(), nativeBefore);
		EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), policyBefore);
		EXPECT_EQ(attackerSideHero->getMagicRules(), magicBefore);
	} // All detached providers retire before the immutable native source changes.
	EXPECT_EQ(nativeSnapshot(), nativeBefore);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(spell)));
	EXPECT_EQ(target->getAvailableHealth(), projectedHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(IndividualRiderDamageIntegrationTest, GirdDetachedMixedVinesKeepsCapturedRuleAndParentSiblingNativeIsolation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const int64_t expected = 20 + 11LL * 200 * 115 * 139 / (10 * 100 * 100);
	ASSERT_EQ(expected, 371);
	const auto nativeBefore = nativeSnapshot();
	const auto health = target->getAvailableHealth();
	int64_t projectedHealth = 0;
	{
		const auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		warm(parent);
		auto first = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		warm(first);
		warm(sibling);
		const auto parentBefore = branchSnapshot(parent), siblingBefore = branchSnapshot(sibling);
		PossibleSpellcast candidate;
		candidate.spell = vines().toSpell();
		for(const auto hex : {BattleHex(8, 5), BattleHex(9, 5), BattleHex(10, 5)})
			candidate.dest.emplace_back(hex);
		spells::BattleCast original(first.get(), attackerSideHero, spells::Mode::HERO, candidate.spell);
		const auto mechanics = original.mechanicsForTarget(candidate.dest);
		EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 15985);
		EXPECT_EQ(mechanics->getDirectDamageSpellPowerCoefficientBasisPoints(), 15985);
		EXPECT_EQ(mechanics->getEffectValue(), expected);
		ASSERT_NO_FATAL_FAILURE(project(first, candidate, *live));
		projectedHealth = first->battleGetUnitByID(target->unitId())->getAvailableHealth();
		EXPECT_EQ(health - projectedHealth, expected);
		EXPECT_EQ(first->battleGetUnitByID(other->unitId())->getAvailableHealth(), other->getAvailableHealth() - expected);
		EXPECT_FALSE(heroActionProjection::admitCandidate(*first, BattleSide::ATTACKER, candidate).admitted);
		EXPECT_EQ(branchSnapshot(parent), parentBefore);
		EXPECT_EQ(branchSnapshot(sibling), siblingBefore);
		EXPECT_EQ(nativeSnapshot(), nativeBefore);
		ASSERT_NO_FATAL_FAILURE(project(sibling, candidate, *live));
		EXPECT_EQ(sibling->battleGetUnitByID(target->unitId())->getAvailableHealth(), projectedHealth);
		EXPECT_EQ(branchSnapshot(parent), parentBefore);
		EXPECT_EQ(nativeSnapshot(), nativeBefore);
	}
	EXPECT_EQ(nativeSnapshot(), nativeBefore);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(vines())));
	EXPECT_EQ(target->getAvailableHealth(), projectedHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 5);
}
