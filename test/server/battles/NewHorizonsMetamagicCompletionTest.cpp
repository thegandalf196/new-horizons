/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/NewHorizonsStun.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMetamagicCompletion.h"
#include "../../../lib/spells/NewHorizonsMagicalMemory.h"
#include "../../../lib/spells/NewHorizonsMetamagicArea.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"

namespace
{
using namespace newHorizonsMetamagic;
class NewHorizonsMetamagicCompletionTest : public HeroCommandFixture
{
protected:
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	testing::NiceMock<ServerCallbackMock> authority;
	testing::StrictMock<vstd::RNGMock> random;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
		ON_CALL(authority, getRNG()).WillByDefault(testing::Return(&random));
		ON_CALL(authority, apply(testing::Matcher<CPackForClient &>(testing::_)))
			.WillByDefault([&](CPackForClient & packet) { gameHandler->sendAndApply(packet); });
		ON_CALL(authority, apply(testing::Matcher<SetStackEffect &>(testing::_)))
			.WillByDefault([&](SetStackEffect & packet) { gameHandler->sendAndApply(packet); });
		ON_CALL(authority, apply(testing::Matcher<StacksInjured &>(testing::_)))
			.WillByDefault([&](StacksInjured & packet) { gameHandler->sendAndApply(packet); });
		ON_CALL(authority, apply(testing::Matcher<BattleUnitsChanged &>(testing::_)))
			.WillByDefault([&](BattleUnitsChanged & packet) { gameHandler->sendAndApply(packet); });
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
			3, ChangeValueMode::ABSOLUTE);
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? ally : enemy) = unit.get();
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
	}
	void addEffect(CStack & unit, SpellID spell, BonusType type, PlayerColor caster)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
			1, BonusSourceID(spell));
		bonus->turnsRemain = 3;
		bonus->spellCasterOwner = caster;
		unit.addNewBonus(bonus);
	}
	void learnAdvancedMetamagic(const char * advanced)
	{
		// External Expert rank still acquires exactly one perk per tier, in order.
		// Conserved Formula is inert in these successful completion/geometry cases.
		attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.conservedFormula"});
		attackerSideHero->applyPerkSelection({"new-horizons:metamagic", advanced});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", advanced));
	}
};
}

TEST_F(NewHorizonsMetamagicCompletionTest, ProductionRegistryUsesCapturedSingleFamilyCostWithoutInstalledFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto rules = battle()->getMagicRules();
	rules["spells"]["core:haste"]["costs"].Vector() = {JsonNode(7), JsonNode(7), JsonNode(7), JsonNode(7)};
	const auto definition = energyDefinition(rules, SpellID::HASTE);
	ASSERT_TRUE(definition);
	EXPECT_EQ(definition->listedMana, 7);
	rules["spells"]["core:haste"].Struct().erase("costs");
	EXPECT_FALSE(energyDefinition(rules, SpellID::HASTE));
	EXPECT_FALSE(energyDefinition(rules, SpellID::NONE));
}

TEST_F(NewHorizonsMetamagicCompletionTest, ActualRemovalUsesOneRollForCompositeAndNoSecondHeroCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addEffect(*enemy, SpellID::HASTE, BonusType::STACKS_SPEED, PlayerColor(1));
	addEffect(*enemy, SpellID::HASTE, BonusType::PRIMARY_SKILL, PlayerColor(1));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	const auto snapshots = captureRecipients(*battle(), *mechanics);
	const auto mana = energyDefinition(battle()->getMagicRules(), SpellID::HASTE);
	ASSERT_TRUE(mana);
	const auto initialHealth = enemy->getAvailableHealth();
	const auto initialMana = attackerSideHero->getManaAvailable();
	const auto castCount = server.casts.size();
	EXPECT_CALL(random, nextInt(0, 99)).Times(1).WillOnce(testing::Return(0));
	BattleStackAttacked originalHit;
	originalHit.stackAttacked = enemy->unitId();
	originalHit.damageAmount = 1;
	const std::array<uint32_t, 2> recipients{enemy->unitId(), enemy->unitId()};
	const std::array<BattleStackAttacked, 2> injuries{originalHit, originalHit};
	const auto receipt = completeCast(authority, *battle(), *mechanics, snapshots, recipients, injuries);
	EXPECT_EQ(receipt.removedMana, mana->listedMana);
	ASSERT_EQ(receipt.removedEffects.size(), 1);
	EXPECT_EQ(receipt.removedEffects.front().family, SpellID::HASTE);
	EXPECT_TRUE(battle()->getLocalSpellEffects(enemy->unitId()).empty());
	EXPECT_EQ(enemy->getAvailableHealth(), std::max<int64_t>(0, initialHealth - dischargeHitPoints(mana->listedMana)));
	ASSERT_GE(dischargeHitPoints(mana->listedMana), initialHealth);
	EXPECT_FALSE(enemy->alive());
	EXPECT_EQ(enemy->getUnusableRemains(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), initialMana);
	EXPECT_EQ(server.casts.size(), castCount);
}

TEST_F(NewHorizonsMetamagicCompletionTest, UnresolvedOriginalOrAlreadyRemovedEnchantmentCannotRollOrPay)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addEffect(*enemy, SpellID::HASTE, BonusType::STACKS_SPEED, PlayerColor(1));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	const auto snapshots = captureRecipients(*battle(), *mechanics);
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	const auto noRecipient = completeCast(authority, *battle(), *mechanics, snapshots, {}, {});
	EXPECT_EQ(noRecipient.removedMana, 0);
	SetStackEffect removed;
	removed.battleID = battle()->getBattleID();
	removed.toRemove.emplace_back(enemy->unitId(), snapshots.at(enemy->unitId()).effects.effects);
	gameHandler->sendAndApply(removed);
	const std::array<uint32_t, 1> recipients{enemy->unitId()};
	BattleStackAttacked injury;
	injury.stackAttacked = enemy->unitId();
	const std::array<BattleStackAttacked, 1> injuries{injury};
	const auto receipt = completeCast(authority, *battle(), *mechanics, snapshots, recipients, injuries);
	EXPECT_EQ(receipt.removedMana, 0);
	EXPECT_TRUE(receipt.removedEffects.empty());
}

TEST_F(NewHorizonsMetamagicCompletionTest, MemoryPersistsThroughBonusSerializationAndBlocksOnlyExactFamilyPolarity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto marker = magicalMemoryMarker(SpellID::HASTE, ResolutionKind::OFFENSE);
	CMemorySerializer bytes;
	bytes.oser & marker;
	Bonus restored;
	bytes.iser & restored;
	ASSERT_TRUE(isMagicalMemory(restored));
	enemy->addNewBonus(std::make_shared<Bonus>(restored));
	ally->addNewBonus(std::make_shared<Bonus>(magicalMemoryMarker(SpellID::SLOW, ResolutionKind::RESTORATION)));
	EXPECT_TRUE(blocksEnchantment(*enemy, battle()->getMagicRules(), SpellID::HASTE, true, false));
	EXPECT_FALSE(blocksEnchantment(*enemy, battle()->getMagicRules(), SpellID::BLESS, true, false));
	EXPECT_FALSE(blocksEnchantment(*enemy, battle()->getMagicRules(), SpellID::HASTE, false, true));
	EXPECT_TRUE(blocksEnchantment(*ally, battle()->getMagicRules(), SpellID::SLOW, false, true));
	EXPECT_FALSE(blocksEnchantment(*ally, battle()->getMagicRules(), SpellID::CURSE, false, true));
	const auto projected = enemy->acquireState();
	EXPECT_TRUE(blocksEnchantment(*projected, battle()->getMagicRules(), SpellID::HASTE, true, false));
	const auto effects = collectRemovalCandidates(enemy->unitId(), PlayerColor(1),
		battle()->captureBattleEffects(enemy->unitId()), ResolutionKind::OFFENSE,
		[&](SpellID id) { return energyDefinition(battle()->getMagicRules(), id); });
	EXPECT_TRUE(effects.empty());
}

TEST_F(NewHorizonsMetamagicCompletionTest, MemoryFiltersReapplicationFromEitherCasterAndPreservesUnrelatedBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(magicalMemoryMarker(SpellID::HASTE, ResolutionKind::OFFENSE)));
	for(const auto caster : {PlayerColor(0), PlayerColor(1)})
	{
		Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
			1, BonusSourceID(SpellID(SpellID::HASTE)));
		haste.spellCasterOwner = caster;
		auto bless = haste;
		bless.sid = BonusSourceID(SpellID(SpellID::BLESS));
		SetStackEffect packet;
		packet.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{haste, bless});
		filterNewBonuses(*battle(), packet);
		ASSERT_EQ(packet.toAdd.size(), 1);
		ASSERT_EQ(packet.toAdd.front().second.size(), 1);
		EXPECT_EQ(packet.toAdd.front().second.front().sid, bless.sid);
	}
}

TEST_F(NewHorizonsMetamagicCompletionTest, WidenAddsOneHexRingToConventionalAreasWithoutChangingSingleTargetSpells)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learnAdvancedMetamagic("new-horizons:metamagic.widenSpell"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.widenSpell"));
	const auto * fireball = SpellID(SpellID::FIREBALL).toSpell();
	ASSERT_TRUE(conventionalArea(*fireball, 0));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fireball);
	const auto mechanics = fireball->battleMechanics(&cast);
	const auto central = mechanics->canonicalizeTarget({spells::Destination(BattleHex(8, 5))});
	EXPECT_EQ(central.size(), 19);
	const auto edge = mechanics->canonicalizeTarget({spells::Destination(BattleHex(1, 0))});
	EXPECT_LT(edge.size(), central.size());
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	EXPECT_FALSE(conventionalArea(*arrow, 0));
	spells::BattleCast bolt(battle(), attackerSideHero, spells::Mode::HERO, arrow);
	const auto single = arrow->battleMechanics(&bolt);
	EXPECT_EQ(widenedRings(*single, {0}), std::vector<int>({0}));
	EXPECT_FALSE(singleTargetFormula(*SpellID(SpellID::CHAIN_LIGHTNING).toSpell(), 0));
	EXPECT_TRUE(singleTargetFormula(*arrow, 0));
}

TEST_F(NewHorizonsMetamagicCompletionTest, GuaranteedDisruptionRemovesDefendAfterDamageWithoutResettingActionOrRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.disruptiveCasting"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.disruptiveCasting"));
	const auto stance = newHorizonsArmorer::buildDefendStance(enemy, false);
	for(const auto & bonus : stance.bonuses)
		enemy->addNewBonus(std::make_shared<Bonus>(bonus));
	enemy->defending = true;
	enemy->movedThisRound = true;
	enemy->defensiveStanceMeleeBonus = stance.meleeDefenseBonus;
	enemy->defensiveStanceRangedBonus = stance.rangedDefenseBonus;
	const auto retaliations = enemy->counterAttacks.available();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	BattleStackAttacked injury;
	injury.stackAttacked = enemy->unitId();
	injury.damageAmount = 1;
	const std::array<uint32_t, 1> recipients{enemy->unitId()};
	const std::array<BattleStackAttacked, 1> injuries{injury};
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	applyGuaranteedCompletion(authority, *battle(), *mechanics, recipients, injuries);
	EXPECT_FALSE(enemy->defended());
	EXPECT_TRUE(enemy->movedThisRound);
	EXPECT_EQ(enemy->counterAttacks.available(), retaliations);
	EXPECT_EQ(enemy->defensiveStanceMeleeBonus, 0);
	EXPECT_EQ(enemy->defensiveStanceRangedBonus, 0);
}

TEST_F(NewHorizonsMetamagicCompletionTest, GuaranteedPurgeRemovesOnePhysicalAfflictionWithNoHealingOrEnergy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.purgingFormula"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.purgingFormula"));
	ally->addNewBonus(std::make_shared<Bonus>(newHorizonsStun::marker()));
	for(const auto & bonus : newHorizonsGraveDust::records(1000, PlayerColor(1)))
		ally->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto health = ally->getAvailableHealth();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::HASTE).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	const std::array<uint32_t, 2> recipients{ally->unitId(), ally->unitId()};
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	applyGuaranteedCompletion(authority, *battle(), *mechanics, recipients, {});
	EXPECT_FALSE(newHorizonsStun::isStunned(*ally));
	EXPECT_TRUE(ally->hasBonus(CSelector(newHorizonsGraveDust::isEffect)));
	EXPECT_EQ(ally->getAvailableHealth(), health);
}

TEST_F(NewHorizonsMetamagicCompletionTest, FriendlyConstructUsesOneRestorationPoolBoundedByOriginalComposition)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * golem = addStack(BattleSide::ATTACKER, creatureByName("core:stoneGolem"), BattleHex(4, 5), 20);
	ASSERT_NE(golem, nullptr);
	const auto initialHealth = golem->getAvailableHealth();
	auto state = golem->acquireState();
	int64_t damage = std::min<int64_t>(initialHealth / 2, 400);
	state->damageShadowGiftSacrifice(damage);
	BattleUnitsChanged changed;
	changed.battleID = battle()->getBattleID();
	UnitChanges update(golem->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	changed.changedStacks.push_back(update);
	gameHandler->sendAndApply(changed);
	addEffect(*golem, SpellID::CURSE, BonusType::STACKS_SPEED, PlayerColor(1));
	addEffect(*golem, SpellID::SLOW, BonusType::PRIMARY_SKILL, PlayerColor(1));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::HASTE).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	const auto snapshots = captureRecipients(*battle(), *mechanics);
	const auto before = golem->getAvailableHealth();
	EXPECT_CALL(random, nextInt(0, 99)).Times(2).WillRepeatedly(testing::Return(0));
	const std::array<uint32_t, 1> recipients{golem->unitId()};
	const auto receipt = completeCast(authority, *battle(), *mechanics, snapshots, recipients, {});
	ASSERT_GT(receipt.removedMana, 0);
	EXPECT_EQ(golem->getAvailableHealth(), std::min(initialHealth, before + restorationHitPoints(receipt.removedMana)));
	EXPECT_LE(golem->getCount(), 20);
}

TEST_F(NewHorizonsMetamagicCompletionTest, RecyclingPreservesTemporaryBufferProvenanceAndCombinedPaymentCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(!attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.manaRecycling"))
		attackerSideHero->applyPerkSelection({"new-horizons:metamagic", "new-horizons:metamagic.manaRecycling"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.manaRecycling"));
	attackerSideHero->setNormalSpellPoints(attackerSideHero->manaLimit());
	const auto normal = attackerSideHero->getNormalSpellPoints();
	const auto buffer = attackerSideHero->getBufferSpellPoints();
	const auto temporary = battle()->getTemporaryBufferRemaining(BattleSide::ATTACKER);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::HASTE).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	CompletionReceipt receipt;
	receipt.removedMana = 100;
	EXPECT_EQ(refundCast(authority, *battle(), *mechanics, receipt, {0, 0, 4}, {0, 0, 3}, false, false), 1);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normal);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), buffer + 1);
	EXPECT_EQ(battle()->getTemporaryBufferRemaining(BattleSide::ATTACKER), temporary + 1);
}

TEST_F(NewHorizonsMetamagicCompletionTest, HexWeavingUsesPrecastHostilityAfterSuccessfulNegativeEffectsAndControlFlip)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(learnAdvancedMetamagic("new-horizons:metamagic.hexWeaving"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.hexWeaving"));
	addEffect(*enemy, SpellID::HASTE, BonusType::STACKS_SPEED, PlayerColor(1));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::SORROW).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	ASSERT_TRUE(cast.getSpell()->isNegative());
	ASSERT_FALSE(cast.getSpell()->isDamage());
	const auto snapshots = captureRecipients(*battle(), *mechanics);
	ASSERT_EQ(snapshots.at(enemy->unitId()).originalOwner, PlayerColor(1));
	mechanics->applyEffects(&authority, {spells::Destination(enemy)}, false, false);
	ASSERT_FALSE(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::SORROW))))->empty());
	// A real control packet changes the effective allegiance before the accepted
	// negative spell's completion stage. Current ownership cannot reclassify it.
	SetStackEffect controlled;
	controlled.battleID = battle()->getBattleID();
	controlled.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(controlled);
	ASSERT_EQ(battle()->battleGetOwner(enemy), PlayerColor(0));
	const auto mana = energyDefinition(battle()->getMagicRules(), SpellID::HASTE);
	ASSERT_TRUE(mana);
	EXPECT_CALL(random, nextInt(0, 99)).Times(1).WillOnce(testing::Return(0));
	const std::array<uint32_t, 1> resolved{enemy->unitId()};
	const auto receipt = completeCast(authority, *battle(), *mechanics, snapshots, resolved, {});
	EXPECT_EQ(receipt.removedMana, mana->listedMana);
	ASSERT_EQ(receipt.removedEffects.size(), 1);
	EXPECT_EQ(receipt.removedEffects.front().kind, ResolutionKind::OFFENSE);
	EXPECT_EQ(receipt.removedEffects.front().family, SpellID::HASTE);
}

TEST_F(NewHorizonsMetamagicCompletionTest, UnknownPrecastOwnerCannotInferEnergyEligibilityFromCurrentAllegiance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addEffect(*enemy, SpellID::HASTE, BonusType::STACKS_SPEED, PlayerColor(1));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = cast.getSpell()->battleMechanics(&cast);
	auto snapshots = captureRecipients(*battle(), *mechanics);
	ASSERT_EQ(snapshots.at(enemy->unitId()).originalOwner, PlayerColor(1));
	snapshots.at(enemy->unitId()).originalOwner = PlayerColor::CANNOT_DETERMINE;
	const auto before = battle()->captureBattleEffects(enemy->unitId());
	EXPECT_CALL(random, nextInt(testing::_, testing::_)).Times(0);
	BattleStackAttacked injury;
	injury.stackAttacked = enemy->unitId();
	injury.damageAmount = 1;
	const std::array<uint32_t, 1> resolved{enemy->unitId()};
	const std::array<BattleStackAttacked, 1> injuries{injury};
	const auto receipt = completeCast(authority, *battle(), *mechanics, snapshots, resolved, injuries);
	EXPECT_EQ(receipt.removedMana, 0);
	EXPECT_TRUE(receipt.removedEffects.empty());
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(before, battle()->captureBattleEffects(enemy->unitId())));
}
