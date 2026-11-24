/*
 * NewHorizonsWarcastTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../OriginalDataRequirement.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/HeroCommandRevision.h"
#include "../../../lib/battle/NewHorizonsWarcast.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/nativeRules/CapturedRuleEnvelope.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace
{
using namespace newHorizonsWarcast;

int64_t average(const DamageEstimation & estimate)
{
	return (estimate.damage.min + estimate.damage.max) / 2;
}

class NewHorizonsWarcastTest : public HeroCommandFixture
{
protected:
	NewHorizonsWarcastTest() { useRevisedOrders = true; }
	CStack * enchanted = nullptr;
	CStack * plain = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	/// Perks in tier order (Basic, Advanced, Expert); the Skill ends at `rank`.
	void learn(int rank, std::initializer_list<const char *> perks)
	{
		const SecondarySkill skill(SecondarySkill::decode(SKILL));
		int tier = 0;
		for(const auto * perk : perks)
		{
			attackerSideHero->setSecSkillLevel(skill, ++tier, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_THROW(attackerSideHero->applyPerkSelection({SKILL, perk})) << perk;
			ASSERT_TRUE(attackerSideHero->hasActivePerk(SKILL, perk)) << perk;
		}
		if(rank > 0)
			attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
	}

	void arena(int rank)
	{
		arena(rank, std::initializer_list<const char *>());
	}

	void arena(int rank, std::initializer_list<const char *> perks, const char * creature = "core:pikeman")
	{
		startGame();
		learn(rank, perks);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::BLESS, SpellID::CURSE})
			attackerSideHero->addSpellToSpellbook(SpellID(spell));
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 20, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 200);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		enchanted = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(93), 20);
		plain = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(72), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
		beginCombat();
		activate(enchanted);
		// The hero's own spell effect, as a cast leaves it, without changing damage.
		stamp(enchanted, SpellID(SpellID::BLESS), PlayerColor(0));
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = stack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	/// A spell effect as the given side's caster leaves it, without spending an action.
	void stamp(CStack * stack, SpellID spell, PlayerColor caster, int turns = 3)
	{
		Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 0, BonusSourceID(spell));
		effect.turnsRemain = turns;
		effect.spellCasterOwner = caster;
		SetStackEffect pack;
		pack.battleID = BattleID(0);
		pack.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{effect});
		gameHandler->sendAndApply(pack);
	}

	HeroOrderState order(HeroCommand command)
	{
		const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, command);
		EXPECT_TRUE(state);
		return state.value_or(HeroOrderState{});
	}

	int64_t strike(const CStack * attacker, const CStack * defender, bool retaliation = false)
	{
		BattleAttackInfo attack(attacker, defender, 0, false);
		attack.retaliation = retaliation;
		return average(battle()->battleEstimateDamage(attack));
	}
};
}

TEST_F(NewHorizonsWarcastTest, OnlyStacksCarryingTheHerosOwnBeneficialSpellAreWarcastAtCommit)
{
	arena(MasteryLevel::BASIC);
	stamp(plain, SpellID(SpellID::HASTE), PlayerColor(1));
	CStack * cast = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(53), 5);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLESS, cast)) << "a real hero cast forges too";
	advanceRound(); // The cast spent this round's Hero Action.
	activate(enchanted);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto state = order(HeroCommand::RIPOSTE);
	EXPECT_EQ(state.warcastPercentFor(enchanted->unitId()), 20);
	EXPECT_EQ(state.warcastPercentFor(plain->unitId()), 0) << "an enemy caster's spell does not count";
	EXPECT_EQ(state.warcastPercentFor(enemy->unitId()), 0);
	EXPECT_EQ(state.warcastPercentFor(cast->unitId()), 20);
	EXPECT_EQ(state.warcast.riders, 0);
}

TEST_F(NewHorizonsWarcastTest, RankLayeredEnchantmentAndGrandConvergenceSetTheShare)
{
	arena(MasteryLevel::EXPERT, {LAYERED_ENCHANTMENT, WARDED_RANKS, GRAND_CONVERGENCE});
	stamp(enchanted, SpellID(SpellID::STONE_SKIN), PlayerColor(0));
	stamp(plain, SpellID(SpellID::HASTE), PlayerColor(0));
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto state = order(HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(state.warcastPercentFor(enchanted->unitId()), 50 + 15 + 10);
	EXPECT_EQ(state.warcastPercentFor(plain->unitId()), 50 + 15);
	EXPECT_EQ(state.warcast.riders, HeroOrderState::WARCAST_WARDED);
}

TEST_F(NewHorizonsWarcastTest, ShareIsCapturedOnceAndADispelAfterCommitChangesNothing)
{
	arena(MasteryLevel::ADVANCED);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	SetStackEffect dispel;
	dispel.battleID = BattleID(0);
	std::vector<Bonus> removed;
	for(const auto effects = enchanted->getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT)); const auto & bonus : *effects)
		removed.push_back(*bonus);
	ASSERT_FALSE(removed.empty());
	dispel.toRemove.emplace_back(enchanted->unitId(), removed);
	gameHandler->sendAndApply(dispel);
	EXPECT_EQ(ownBeneficialSpellCount(*enchanted, PlayerColor(0)), 0);
	EXPECT_EQ(order(HeroCommand::RIPOSTE).warcastPercentFor(enchanted->unitId()), 35);
}

TEST_F(NewHorizonsWarcastTest, RiposteIsStrongerOnlyOnTheWarcastStack)
{
	arena(MasteryLevel::EXPERT);
	const auto ordinaryRetaliation = strike(plain, enemy, true);
	ASSERT_EQ(strike(enchanted, enemy, true), ordinaryRetaliation);
	const auto ordinaryIncoming = strike(enemy, plain);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_GT(strike(plain, enemy, true), ordinaryRetaliation);
	EXPECT_GT(strike(enchanted, enemy, true), strike(plain, enemy, true));
	EXPECT_LT(strike(enemy, plain), ordinaryIncoming);
	EXPECT_LT(strike(enemy, enchanted), strike(enemy, plain));
}

TEST_F(NewHorizonsWarcastTest, WithoutTheSkillTheSameOrderTreatsBothStacksAlike)
{
	arena(0);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_TRUE(order(HeroCommand::RIPOSTE).warcast.recipients.empty());
	EXPECT_EQ(strike(enchanted, enemy, true), strike(plain, enemy, true));
	EXPECT_EQ(strike(enemy, enchanted), strike(enemy, plain));
}

TEST_F(NewHorizonsWarcastTest, KindledEdgeAndEnchantedSteelRaiseWarcastAttacksWhileTheOrderLasts)
{
	arena(MasteryLevel::ADVANCED, {KINDLED_EDGE, ENCHANTED_STEEL}, "core:angel");
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 40, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	ASSERT_EQ(strike(enchanted, enemy), strike(plain, enemy));
	// Hold the Line changes only incoming damage, so any outgoing difference is the riders.
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_TRUE(riderActive(*battle(), enchanted, HeroOrderState::WARCAST_KINDLED));
	EXPECT_TRUE(riderActive(*battle(), enchanted, HeroOrderState::WARCAST_STEEL));
	EXPECT_FALSE(riderActive(*battle(), plain, HeroOrderState::WARCAST_KINDLED));
	EXPECT_GT(strike(enchanted, enemy), strike(plain, enemy));
	advanceRound();
	EXPECT_FALSE(riderActive(*battle(), enchanted, HeroOrderState::WARCAST_KINDLED)) << "the Order has ended";
}

TEST_F(NewHorizonsWarcastTest, WardedRanksAddsFifteenPercentMagicalReductionToWarcastStacks)
{
	arena(MasteryLevel::ADVANCED, {FIELD_ENCHANTER, WARDED_RANKS});
	if(!battle()->battleUsesNewHorizonsMultiplicativeMDR())
		GTEST_SKIP() << "Requires the multiplicative magical damage rules";
	ASSERT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(enchanted), 0);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(enchanted), WARDED_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(battle()->battleGetPerkMagicalReductionBasisPoints(plain), 0);
}

TEST_F(NewHorizonsWarcastTest, OpeningExposesAnEnemyHitByAWarcastStack)
{
	arena(MasteryLevel::ADVANCED, {KINDLED_EDGE, OPENING});
	EXPECT_EQ(openingSpellDamagePercent(*battle(), attackerSideHero, enemy), 0);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	activate(enchanted);
	ASSERT_TRUE(attack(enchanted, enemy->getPosition()));
	EXPECT_TRUE(enemy->hasBonus(CSelector([](const Bonus * bonus) { return isExposedMarker(bonus, BattleSide::ATTACKER); })));
	EXPECT_EQ(openingSpellDamagePercent(*battle(), attackerSideHero, enemy), OPENING_SPELL_DAMAGE_PERCENT);
	EXPECT_EQ(openingSpellDamagePercent(*battle(), attackerSideHero, enchanted), 0) << "only enemies";
	advanceRound();
	EXPECT_EQ(openingSpellDamagePercent(*battle(), attackerSideHero, enemy), OPENING_SPELL_DAMAGE_PERCENT)
		<< "Exposed lasts until the end of the next round";
	advanceRound();
	EXPECT_EQ(openingSpellDamagePercent(*battle(), attackerSideHero, enemy), 0);
}

TEST_F(NewHorizonsWarcastTest, FieldEnchanterCutsTwoManaOnStacksUnderAnOrder)
{
	arena(MasteryLevel::BASIC, {FIELD_ENCHANTER});
	const auto * bless = SpellID(SpellID::BLESS).toSpell();
	const auto cost = [&](const CStack * target)
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
		const auto mechanics = bless->battleMechanics(&cast);
		spells::Target aim;
		aim.emplace_back(target);
		return mechanics->getTargetAwareManaCost(aim);
	};
	const auto ordinary = cost(plain);
	ASSERT_GT(ordinary, 1);
	EXPECT_EQ(cost(enchanted), ordinary);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(cost(enchanted), std::max(1, ordinary - FIELD_ENCHANTER_MANA));
	EXPECT_EQ(cost(plain), std::max(1, ordinary - FIELD_ENCHANTER_MANA)) << "every stack is under Riposte";
	const auto * slow = SpellID(SpellID::SLOW).toSpell();
	spells::BattleCast hostile(battle(), attackerSideHero, spells::Mode::HERO, slow);
	spells::Target aim;
	aim.emplace_back(enemy);
	EXPECT_EQ(slow->battleMechanics(&hostile)->getTargetAwareManaCost(aim), battle()->battleGetSpellCost(slow, attackerSideHero));
}

TEST_F(NewHorizonsWarcastTest, CommandersMeditationRestoresThreeManaOncePerRound)
{
	arena(MasteryLevel::BASIC, {COMMANDERS_MEDITATION});
	ASSERT_GT(attackerSideHero->manaLimit(), 10);
	setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit() - 10);
	const auto before = attackerSideHero->getManaAvailable();
	ASSERT_LE(before + MEDITATION_MANA, attackerSideHero->manaLimit());
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_TRUE(order(HeroCommand::RIPOSTE).warcast.meditation);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), before + MEDITATION_MANA);
	const auto second = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE, {});
	if(second)
		EXPECT_FALSE(second->warcast.meditation) << "at most once per round";
}

TEST_F(NewHorizonsWarcastTest, SustainingCommandExtendsAnExpiringSpellOnce)
{
	arena(MasteryLevel::ADVANCED, {FIELD_ENCHANTER, SUSTAINING_COMMAND});
	stamp(plain, SpellID(SpellID::STONE_SKIN), PlayerColor(0), 1);
	const auto turns = [&]()
	{
		int result = 0;
		for(const auto effects = plain->getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT)); const auto & bonus : *effects)
			if(bonus->sid == BonusSourceID(SpellID(SpellID::STONE_SKIN)))
				result = std::max<int>(result, bonus->turnsRemain);
		return result;
	};
	ASSERT_EQ(turns(), 1);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(turns(), 2);
	EXPECT_TRUE(plain->hasBonus(CSelector(isSustainedMarker)));
	advanceRound();
	ASSERT_EQ(turns(), 1);
	activate(plain);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(turns(), 1) << "each spell is sustained once";
}

TEST_F(NewHorizonsWarcastTest, UnbrokenCommandCarriesTheOrderOnWarcastStacksIntoTheNextRound)
{
	arena(MasteryLevel::EXPERT, {FIELD_ENCHANTER, WARDED_RANKS, UNBROKEN_COMMAND});
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto state = order(HeroCommand::RIPOSTE);
	EXPECT_TRUE(state.warcast.unbroken);
	EXPECT_EQ(state.divineDisciplineRecipientUnitIds, std::vector<uint32_t>{enchanted->unitId()});
	const auto round = battle()->battleGetRound();
	advanceRound();
	const auto carried = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE);
	ASSERT_TRUE(carried);
	EXPECT_TRUE(carried->scheduledFor(enchanted->unitId(), round + 1));
	EXPECT_FALSE(carried->scheduledFor(plain->unitId(), round + 1));
}

TEST_F(NewHorizonsWarcastTest, VanguardRaisesWarcastAttackDefenseAndInitiativeButNotMorale)
{
	arena(MasteryLevel::EXPERT);
	ASSERT_TRUE(issue(HeroCommand::VANGUARD));
	const auto state = order(HeroCommand::VANGUARD);
	ASSERT_TRUE(state.revision);
	const auto boosted = heroCommands::activeVanguardBonuses(enchanted);
	const auto ordinary = heroCommands::activeVanguardBonuses(plain);
	ASSERT_TRUE(boosted);
	ASSERT_TRUE(ordinary);
	EXPECT_EQ(boosted->initiativePercent, std::min(state.revision->vanguardInitiativeCap,
		scaled(ordinary->initiativePercent, 50)));
	EXPECT_EQ(boosted->statBonus, std::min(state.revision->vanguardStatCap, scaled(ordinary->statBonus, 50)));
	EXPECT_EQ(battle()->battleGetMorale(enchanted), battle()->battleGetMorale(plain));
}

TEST_F(NewHorizonsWarcastTest, FocusFireWarcastsEveryShooterThroughAnEnemyCarryingTheHerosHex)
{
	arena(MasteryLevel::BASIC, {}, "core:archer");
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::CURSE, enemy));
	advanceRound(); // The cast spent this round's Hero Action.
	activate(enchanted);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, enemy->unitId())));
	const auto state = order(HeroCommand::FOCUS_FIRE);
	EXPECT_EQ(state.warcastPercentFor(enchanted->unitId()), 20);
	EXPECT_EQ(state.warcastPercentFor(plain->unitId()), 20);
	EXPECT_EQ(state.warcastPercentFor(enemy->unitId()), 0);
}

TEST(NewHorizonsWarcastPersistence, OrderSnapshotRoundTripsLoadsInertFromOlderFormatsAndRejectsDownsave)
{
	HeroOrderState source;
	source.command = HeroCommand::RIPOSTE;
	source.issuedRound = 3;
	source.warcast.recipients = {{2, 35}, {7, 45}};
	source.warcast.riders = HeroOrderState::WARCAST_KINDLED | HeroOrderState::WARCAST_OPENING;
	source.warcast.meditation = true;
	source.warcast.unbroken = true;
	source.divineDisciplineRecipientUnitIds = {2, 7};
	ASSERT_NO_THROW(source.validateShape());
	EXPECT_EQ(source.warcastPercentFor(7), 45);
	EXPECT_EQ(source.warcastPercentFor(3), 0);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & source);
	HeroOrderState restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored, source);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	HeroOrderState legacy = source;
	legacy.warcast.recipients.clear();
	legacy.warcast.riders = 0;
	legacy.warcast.meditation = false;
	legacy.warcast.unbroken = false;
	legacy.divineDisciplineRecipientUnitIds.clear();
	ASSERT_NO_THROW(old.oser & legacy);
	HeroOrderState legacyRestored = source;
	ASSERT_NO_THROW(old.iser & legacyRestored);
	EXPECT_TRUE(legacyRestored.warcast.empty());

	HeroOrderState unsorted = source;
	std::swap(unsorted.warcast.recipients[0], unsorted.warcast.recipients[1]);
	EXPECT_THROW(unsorted.validateShape(), std::runtime_error);
	HeroOrderState excessive = source;
	excessive.warcast.recipients[0].percent = 201;
	EXPECT_THROW(excessive.validateShape(), std::runtime_error);
}

namespace
{
JsonNode installedPerks()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
}

/// The perk registry an 0.94.0-beta.1 game captured: 32 Skills, the old Skill name.
JsonNode betaOneCapture()
{
	auto rules = installedPerks();
	rules.Struct().erase("warcastPolicy");
	rules["skills"].Struct().erase(SKILL);
	rules["skills"]["new-horizons:warcasting"]["name"].String() = "Warcasting";
	for(auto & perk : rules["skills"]["new-horizons:warcasting"]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:warcasting.battleFocus")
		{
			auto & details = perk["details"].String();
			const std::string current = "Expert Spell-Forge this totals";
			const auto at = details.find(current);
			if(at != std::string::npos)
				details.replace(at, current.size(), "Expert Warcasting this totals");
		}
	return rules;
}
}

TEST(NewHorizonsWarcastPersistence, NewGamesCaptureThirtyThreeSkillsAndTheirMarkersAgree)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto rules = installedPerks();
	EXPECT_TRUE(newHorizonsHeroes::usesWarcastPolicy(rules));
	EXPECT_EQ(newHorizonsHeroes::canonicalSkillCount(rules), newHorizonsHeroes::PERK_WARCAST_SKILL_COUNT);
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
	EXPECT_TRUE(newHorizonsHeroes::requiresWarcastFormat(rules));
	const auto old = betaOneCapture();
	EXPECT_FALSE(newHorizonsHeroes::usesWarcastPolicy(old));
	EXPECT_EQ(newHorizonsHeroes::canonicalSkillCount(old), newHorizonsHeroes::PERK_CANONICAL_SKILL_COUNT);
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(old));
	auto mismatched = rules;
	mismatched.Struct().erase("warcastPolicy");
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(mismatched), std::runtime_error);
	const SecondarySkill spellForge(SecondarySkill::decode("new-horizons:warcasting"));
	const SecondarySkill warcasting(SecondarySkill::decode(SKILL));
	EXPECT_EQ(spellForge.toSkill()->getNameTranslated(), "Spell-Forge");
	EXPECT_EQ(warcasting.toSkill()->getNameTranslated(), "Warcasting");
}

TEST(NewHorizonsWarcastPersistence, ABetaOneHeroWithOldWarcastingLoadsAsSpellForgeWithItsPerks)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto captured = betaOneCapture();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	const bool pooled = false;
	memory.oser & pooled;
	memory.oser & captured;
	std::vector<newHorizonsHeroes::PerkSelection> selections{{"new-horizons:warcasting", "new-horizons:warcasting.spellward"}};
	memory.oser & selections;
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(memory.iser & restored);
	const auto & rules = restored.getRules();
	EXPECT_EQ(rules["skills"]["new-horizons:warcasting"]["name"].String(), "Spell-Forge");
	EXPECT_FALSE(rules["skills"].Struct().contains(SKILL)) << "an old game never gains the new Skill";
	EXPECT_FALSE(newHorizonsHeroes::usesWarcastPolicy(rules));
	ASSERT_EQ(restored.getSelected().size(), 1u);
	EXPECT_EQ(restored.getSelected().front().perkId, "new-horizons:warcasting.spellward");
	for(const auto & perk : rules["skills"]["new-horizons:warcasting"]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:warcasting.battleFocus")
			EXPECT_NE(perk["details"].String().find("Expert Spell-Forge this totals"), std::string::npos);
	// Every rule other than the presentation is exactly what the old game captured.
	auto expected = captured;
	expected["skills"]["new-horizons:warcasting"] = rules["skills"]["new-horizons:warcasting"];
	EXPECT_EQ(rules, expected);
}

TEST(NewHorizonsWarcastPersistence, ACurrentCaptureCannotBeWrittenToTheBetaOneFormat)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	nativeRules::CapturedRulePool pool;
	pool.intern(nativeRules::CapturedRuleKind::Perks, installedPerks());
	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	EXPECT_THROW(older.oser & pool, std::runtime_error);

	CMemorySerializer forged;
	forged.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	forged.iser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	forged.oser & int64_t{1} & int64_t{static_cast<int64_t>(nativeRules::CapturedRuleKind::Perks)} & installedPerks() & int64_t{0};
	nativeRules::CapturedRulePool decoded;
	EXPECT_THROW(forged.iser & decoded, std::runtime_error);

	nativeRules::CapturedRulePool beta;
	beta.intern(nativeRules::CapturedRuleKind::Perks, betaOneCapture());
	CMemorySerializer stored;
	stored.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	stored.iser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH;
	ASSERT_NO_THROW(stored.oser & beta);
	nativeRules::CapturedRulePool restored;
	EXPECT_NO_THROW(stored.iser & restored);
	EXPECT_EQ(restored.documentCount(), 1u);
}
