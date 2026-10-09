/*
 * NewHorizonsPandemoniumTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsPandemoniumFixture.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/json/JsonBonus.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"

using NewHorizonsPandemoniumTest = NewHorizonsPandemoniumFixture;

TEST(NewHorizonsPandemoniumPowerTest, ExactBaselineRankAndWholeMasterFloor)
{
	for(size_t count = 0; count <= 5; ++count)
		EXPECT_EQ(newHorizonsPandemonium::rawPower(100, 10000, 0, 0, false, count), 45 * count);
	EXPECT_EQ(newHorizonsPandemonium::rawPower(160, 10000, 0, 0, false, 4), 240);
	EXPECT_EQ(newHorizonsPandemonium::rawPower(100, 14500, 0, 0, false, 2), 112);
	EXPECT_EQ(newHorizonsPandemonium::rawPower(100, 10000, 0, 0, true, 2), 112);
	EXPECT_EQ(newHorizonsPandemonium::rawPower(100, 10000, 20, 50, true, 2), 162);
	EXPECT_EQ(newHorizonsPandemonium::rawPower(0, 14500, 100, 100, false, 1), 20);
	EXPECT_THROW(newHorizonsPandemonium::rawPower(100, -1, 0, 0, false, 1), std::invalid_argument);
}

TEST_F(NewHorizonsPandemoniumTest, PaidExpertCastDamagesBothSidesAndLeavesZeroDebuffStackUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("slow"));
	enemy->addNewBonus(status("curse", BonusType::MORALE, SpellID::CURSE));
	friendly->addNewBonus(status("slow"));
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(battle()->getMagicRules(), attackerSideHero, spell());
	const auto enemyHP = enemy->getAvailableHealth();
	const auto friendlyHP = friendly->getAvailableHealth();
	const auto cleanHP = clean->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_EQ(spell().toSpell()->getLevel(), 5);
	const auto cost = battle()->battleGetSpellCost(spell().toSpell(), attackerSideHero);
	EXPECT_EQ(cost, 22);
	ASSERT_TRUE(cast());
	EXPECT_EQ(enemyHP - enemy->getAvailableHealth(), newHorizonsPandemonium::rawPower(100, coefficient, 0, 0, false, 2));
	EXPECT_EQ(friendlyHP - friendly->getAvailableHealth(), newHorizonsPandemonium::rawPower(100, coefficient, 0, 0, false, 1));
	EXPECT_EQ(clean->getAvailableHealth(), cleanHP);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
	EXPECT_FALSE(cast()) << "Accepted cast must spend the hero action";
}

TEST_F(NewHorizonsPandemoniumTest, SelectedMasterScalesActualPaidDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	enemy->addNewBonus(status("slow"));
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(battle()->getMagicRules(), attackerSideHero, spell());
	const auto before = enemy->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_EQ(before - enemy->getAvailableHealth(), newHorizonsPandemonium::rawPower(100, coefficient, 0, 0, true, 1));
}

TEST_F(NewHorizonsPandemoniumTest, DiseaseComponentsRefreshAndPhysicalPoisonCountLogicalStatusesOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("disease", BonusType::MORALE, SpellID::DISEASE));
	enemy->addNewBonus(status("disease", BonusType::LUCK, SpellID::DISEASE));
	enemy->addNewBonus(status("disease", BonusType::STACKS_SPEED, SpellID::DISEASE));
	enemy->addNewBonus(status("poison", BonusType::MORALE, SpellID::POISON));
	poison(enemy);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 2u);
	const auto before = enemy->getAvailableHealth();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(battle()->getMagicRules(), attackerSideHero, spell());
	ASSERT_TRUE(cast());
	EXPECT_EQ(before - enemy->getAvailableHealth(), newHorizonsPandemonium::rawPower(100, coefficient, 0, 0, false, 2));
}

TEST_F(NewHorizonsPandemoniumTest, NoQuarterProducerPairAndStoredPoisonAreDistinct)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::noQuarterRetaliationBonus()));
	enemy->addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::noQuarterMoralePenalty(true)));
	poison(enemy);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 2u);
	ASSERT_TRUE(cast());
}

TEST_F(NewHorizonsPandemoniumTest, RawNegativeAndInnateCreatureStatisticsDoNotCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::CREATURE_ABILITY, -10, BonusSourceID()));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::LUCK,
		BonusSource::OTHER, -10, BonusSourceID()));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 0u);
	const auto before = enemy->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_EQ(enemy->getAvailableHealth(), before);
}

TEST_F(NewHorizonsPandemoniumTest, SpellLockResistanceAndImmunityRemainNormalPerRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(auto * unit : {enemy, friendly, clean})
		unit->addNewBonus(status("slow"));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	const SpellID lock(SpellID::decode("new-horizons:spellLock"));
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock)));
	clean->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell())));
	const std::array<int64_t, 3> before{enemy->getAvailableHealth(), friendly->getAvailableHealth(), clean->getAvailableHealth()};
	ASSERT_TRUE(cast());
	EXPECT_EQ(enemy->getAvailableHealth(), before[0]);
	EXPECT_EQ(friendly->getAvailableHealth(), before[1]);
	EXPECT_EQ(clean->getAvailableHealth(), before[2]);
}

TEST_F(NewHorizonsPandemoniumTest, MagicalReductionAppliesOnceAfterDebuffPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("slow"));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	const auto expected = newHorizonsPandemonium::damage(*mechanics, enemy, 1);
	EXPECT_LT(expected, newHorizonsPandemonium::rawPower(*mechanics, 1));
	const auto before = enemy->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_EQ(before - enemy->getAvailableHealth(), expected);
}

TEST_F(NewHorizonsPandemoniumTest, NoManaRejectsBeforeAnyRecipientMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("slow"));
	setTestSpellPointTotal(attackerSideHero, 0);
	const auto before = enemy->getAvailableHealth();
	EXPECT_FALSE(cast());
	EXPECT_EQ(enemy->getAvailableHealth(), before);
}

TEST_F(NewHorizonsPandemoniumTest, ExistingHealthPacketAndPoisonStateRoundTrip)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	poison(enemy);
	ASSERT_TRUE(cast());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::UPDATE);
	packet.changedStacks.back().data = enemy->acquireState()->save();
	CMemorySerializer writer;
	writer.oser & packet;
	CMemorySerializer reader(writer.extractBuffer());
	BattleUnitsChanged restored;
	reader.iser & restored;
	ASSERT_EQ(restored.changedStacks.size(), 1u);
	auto detached = enemy->acquireState();
	detached->load(restored.changedStacks.front().data);
	EXPECT_EQ(detached->getAvailableHealth(), enemy->getAvailableHealth());
	EXPECT_EQ(detached->physicalPoisonBaseDamage, enemy->physicalPoisonBaseDamage);
	EXPECT_EQ(detached->physicalPoisonActivationsRemaining, enemy->physicalPoisonActivationsRemaining);
}

TEST_F(NewHorizonsPandemoniumTest, CapturedAllRecipientCountsSurviveCleanupAndLaterMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("slow"));
	friendly->addNewBonus(status("curse", BonusType::MORALE, SpellID::CURSE));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	mechanics->capturePandemoniumDebuffs();
	enemy->removeBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW))));
	clean->addNewBonus(status("slow"));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 0u);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*clean).count(), 1u);
	EXPECT_EQ(mechanics->getPandemoniumDebuffCount(enemy), 1);
	EXPECT_EQ(mechanics->getPandemoniumDebuffCount(friendly), 1);
	EXPECT_EQ(mechanics->getPandemoniumDebuffCount(clean), 0);
	mechanics->clearPandemoniumDebuffs();
	EXPECT_EQ(mechanics->getPandemoniumDebuffCount(enemy), 0);
	EXPECT_EQ(mechanics->getPandemoniumDebuffCount(clean), 1);
}

TEST_F(NewHorizonsPandemoniumTest, RegisteredDiseaseProducerAndHistoricalUntaggedComponentsDeduplicate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & effects = SpellID(SpellID::DISEASE).toSpell()->getLevelInfo(MasteryLevel::BASIC)
		.battleEffects["illness"]["bonus"].Struct();
	ASSERT_GE(effects.size(), 2u);
	for(const auto & [name, node] : effects)
	{
		auto effect = JsonUtils::parseBonus(node);
		ASSERT_NE(effect, nullptr);
		ASSERT_FALSE(effect->statusIdentity.empty()) << "Shipped producer must author identity";
		effect->source = BonusSource::SPELL_EFFECT;
		effect->sid = BonusSourceID(SpellID(SpellID::DISEASE));
		effect->turnsRemain = 3;
		enemy->addNewBonus(std::make_shared<Bonus>(*effect));
		// Old supported saves predate producer tagging; typed spell/type provenance survives.
		effect->statusTags.clear();
		effect->statusIdentity.clear();
		friendly->addNewBonus(std::make_shared<Bonus>(*effect));
	}
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 1u);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*friendly).count(), 1u);
	ASSERT_TRUE(cast());
}

TEST_F(NewHorizonsPandemoniumTest, FriendlyResistanceIsAppliedToFriendlyFire)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(status("slow"));
	enemy->addNewBonus(status("slow"));
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto before = friendly->getAvailableHealth();
	const auto enemyBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(cast());
	EXPECT_EQ(friendly->getAvailableHealth(), before);
	EXPECT_LT(enemy->getAvailableHealth(), enemyBefore);
}

TEST_F(NewHorizonsPandemoniumTest, PuppetFactoryAndStrippedHistoricalControlShareOneIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID puppet(SpellID::decode(std::string(newHorizonsPuppetMaster::SPELL_ID)));
	ASSERT_TRUE(puppet.hasValue());
	const auto marker = newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(0));
	ASSERT_FALSE(marker.statusIdentity.empty());
	EXPECT_NE(std::ranges::find(marker.statusTags, BonusStatusTag::DEBUFF), marker.statusTags.end());
	enemy->addNewBonus(std::make_shared<Bonus>(marker));
	auto legacy = marker;
	legacy.statusTags.clear();
	legacy.statusIdentity.clear();
	enemy->addNewBonus(std::make_shared<Bonus>(legacy));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).count(), 1u);
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).identities,
		(std::vector<std::string>{std::string(newHorizonsPuppetMaster::SPELL_ID)}));
}

TEST_F(NewHorizonsPandemoniumTest, ConvertedLegacySlowInitiativeRetainsStatusIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus legacy(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE, BonusSource::SPELL_EFFECT,
		-3, BonusSourceID(SpellID(SpellID::SLOW)));
	legacy.turnsRemain = 3;
	ASSERT_TRUE(legacy.statusTags.empty());
	enemy->addNewBonus(std::make_shared<Bonus>(legacy));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).identities,
		(std::vector<std::string>{"core:slow"}));
}

TEST_F(NewHorizonsPandemoniumTest, LegacyLockPolarityCountsHostileButNotProtectivePair)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spellLock(SpellID::decode("new-horizons:spellLock"));
	ASSERT_TRUE(spellLock.hasValue());
	Bonus resistance(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE, BonusSource::SPELL_EFFECT,
		100, BonusSourceID(spellLock));
	resistance.turnsRemain = 3;
	Bonus polarity(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SPELL_EFFECT,
		-3, BonusSourceID(spellLock));
	polarity.turnsRemain = 3;
	enemy->addNewBonus(std::make_shared<Bonus>(resistance));
	enemy->addNewBonus(std::make_shared<Bonus>(polarity));
	friendly->addNewBonus(std::make_shared<Bonus>(resistance));
	polarity.val = 3;
	friendly->addNewBonus(std::make_shared<Bonus>(polarity));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*enemy).identities,
		(std::vector<std::string>{"new-horizons:spellLock"}));
	EXPECT_EQ(newHorizonsDebuffStatuses::snapshot(*friendly).count(), 0u);
}
