/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMetamagicPoints.h"

namespace
{
class NewHorizonsMetamagicPointsBattleTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void learnMetamagic(CGHeroInstance * hero, std::initializer_list<const char *> perks)
	{
		std::array<const char *, 3> choices{"new-horizons:metamagic.lingeringMagic",
			"new-horizons:metamagic.spellFracture", "new-horizons:metamagic.arcaneSingularity"};
		std::array<bool, 3> requested{};
		for(const auto * perk : perks)
		{
			const std::string_view id(perk);
			const int tier = id == "new-horizons:metamagic.arcaneCatalyst"
				|| id == "new-horizons:metamagic.arcaneShield" || id == "new-horizons:metamagic.manaConduit"
				|| id == "new-horizons:metamagic.lingeringMagic" ? 0
				: id == "new-horizons:metamagic.arcaneFeedback" || id == "new-horizons:metamagic.spellFracture"
				|| id == "new-horizons:metamagic.arcaneWellspring" || id == "new-horizons:metamagic.arcaneAttunement" ? 1 : 2;
			ASSERT_FALSE(requested[tier]) << "Fixture must select only one perk per tier";
			requested[tier] = true;
			choices[tier] = perk;
		}
		for(int tier = 0; tier < 3; ++tier)
		{
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				tier + 1, ChangeValueMode::ABSOLUTE);
			hero->applyPerkSelection({"new-horizons:metamagic", choices[tier]});
			ASSERT_TRUE(hero->hasActivePerk("new-horizons:metamagic", choices[tier]));
		}
	}

	void prepare(std::initializer_list<const char *> perks, bool opposingFeedback = false, bool adept = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(adept ? "core:halon" : "core:neela")));
		ASSERT_NO_FATAL_FAILURE(learnMetamagic(attackerSideHero, perks));
		if(opposingFeedback)
		{
			defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
			ASSERT_NO_FATAL_FAILURE(learnMetamagic(defenderSideHero, {"new-horizons:metamagic.arcaneFeedback"}));
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		for(auto * hero : {attackerSideHero, defenderSideHero})
			hero->setStackCount(SlotID(0), 1000);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(victim, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}

	std::vector<std::byte> rng() const
	{
		CMemorySerializer bytes;
		bytes.oser & *gameHandler->randomizer;
		return bytes.extractBuffer();
	}
};
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, RoundOneGrantsPointsBeforeShieldAndGrowthRefreshesShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneWellspring",
		"new-horizons:metamagic.arcaneShield"}));
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 3);
	EXPECT_EQ(actor->acquireState()->arcaneShieldHitPoints, battle::getMaximumHealth(*actor) * 3 / 100);
	const auto before = rng();
	BattleNextRound next;
	next.battleID = battle()->getBattleID();
	gameHandler->sendAndApply(next);
	newHorizonsMetamagic::publishRoundEffects(*gameHandler->spellcastEnvironment(), *battle());
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 4);
	EXPECT_EQ(actor->acquireState()->arcaneShieldHitPoints, battle::getMaximumHealth(*actor) * 4 / 100);
	EXPECT_EQ(rng(), before);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, EchoUsesActualReceiptTargetsOnceWithoutRngOrAnotherHeroCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneCatalyst", "new-horizons:metamagic.arcaneEcho"}));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	BattleStackAttacked original;
	original.stackAttacked = victim->unitId();
	original.damageAmount = 103;
	const std::array<uint32_t, 1> targets{victim->unitId()};
	const std::array<BattleStackAttacked, 1> injuries{original};
	const auto health = victim->getAvailableHealth();
	const auto before = rng();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = server.casts.size();
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, injuries);
	EXPECT_EQ(victim->getAvailableHealth(), health - 25);
	EXPECT_TRUE(battle()->getMetamagicPointState(BattleSide::ATTACKER).echoUsedThisRound);
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 2);
	EXPECT_EQ(rng(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(server.casts.size(), casts);
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, injuries);
	EXPECT_EQ(victim->getAvailableHealth(), health - 25);
	EXPECT_EQ(rng(), before);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, AutomaticSecondarySpellCannotEchoOrGenerateCatalystPoints)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneCatalyst", "new-horizons:metamagic.arcaneEcho"}));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	spells::AutomaticHeroCastReceipt automatic;
	automatic.kind = spells::AutomaticHeroCastKind::DIVINE_FURY;
	event.setAutomaticHeroCast(automatic);
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	BattleStackAttacked original;
	original.stackAttacked = victim->unitId();
	original.damageAmount = 100;
	const std::array<uint32_t, 1> targets{victim->unitId()};
	const std::array<BattleStackAttacked, 1> injuries{original};
	const auto health = victim->getAvailableHealth();
	const auto state = battle()->getMetamagicPointState(BattleSide::ATTACKER);
	const auto before = rng();
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, injuries);
	EXPECT_EQ(victim->getAvailableHealth(), health);
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER), state);
	EXPECT_EQ(rng(), before);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, PointGainRefreshesAttunement)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneAttunement", "new-horizons:metamagic.arcaneCatalyst"}));
	advanceRound();
	advanceRound();
	const auto before = actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, {}, {});
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 4);
	EXPECT_EQ(actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), before + 1);
	EXPECT_EQ(actor->acquireState()->arcaneShieldHitPoints, 0);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, AdeptPointGainDoesNotRefillOrRaiseTheExistingRoundShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneWellspring",
		"new-horizons:metamagic.arcaneShield"}, false, true));
	const auto shield = actor->acquireState()->arcaneShieldHitPoints;
	ASSERT_GT(shield, 0);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, {}, {});
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 4);
	EXPECT_EQ(actor->acquireState()->arcaneShieldHitPoints, shield);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, AcceptedCounteredSpellStillGrantsCatalystAndConsumesTheFirstDamagingEcho)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneCatalyst", "new-horizons:metamagic.arcaneEcho"}));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	event.setCounterspell(BattleSide::DEFENDER, true);
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	const auto health = victim->getAvailableHealth();
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, {}, {});
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 2);
	EXPECT_TRUE(battle()->getMetamagicPointState(BattleSide::ATTACKER).echoUsedThisRound);
	EXPECT_EQ(victim->getAvailableHealth(), health);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, RejectedUnaffordableCastCannotGeneratePointsOrConsumeEcho)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneCatalyst", "new-horizons:metamagic.arcaneEcho"}));
	setTestSpellPointTotal(attackerSideHero, 0);
	const auto state = battle()->getMetamagicPointState(BattleSide::ATTACKER);
	const auto before = rng();
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER), state);
	EXPECT_EQ(rng(), before);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, EchoLethalDamageStillUsesTheNativeRebirthConsequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneEcho"}));
	for(const auto type : {BonusType::REBIRTH, BonusType::CASTS})
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
			BonusSource::OTHER, type == BonusType::REBIRTH ? 100 : 1, BonusSourceID()));
	ASSERT_TRUE(victim->canCast());
	auto damaged = victim->acquireState();
	int64_t loss = damaged->getAvailableHealth() - 1;
	damaged->damage(loss);
	BattleUnitsChanged wound;
	wound.battleID = battle()->getBattleID();
	wound.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
	wound.changedStacks.back().data = damaged->save();
	gameHandler->sendAndApply(wound);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	BattleStackAttacked original;
	original.stackAttacked = victim->unitId();
	original.damageAmount = 100;
	const std::array<uint32_t, 1> targets{victim->unitId()};
	const std::array<BattleStackAttacked, 1> injuries{original};
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, injuries);
	EXPECT_TRUE(victim->alive());
	EXPECT_GT(victim->getCount(), 1);
	EXPECT_FALSE(victim->canCast());
	ASSERT_FALSE(server.injuries.empty());
	EXPECT_TRUE(server.injuries.back().stacks.front().willRebirth());
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, NewlyArrivingFriendlyUnitReceivesCurrentAttunementImmediatelyWithoutShield)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneAttunement", "new-horizons:metamagic.arcaneCatalyst"}));
	advanceRound();
	advanceRound();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, {}, {});
	auto * added = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 4), 10);
	ASSERT_NE(added, nullptr);
	EXPECT_EQ(added->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 1);
	EXPECT_EQ(added->acquireState()->arcaneShieldHitPoints, 0);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, FractureShortensOnlyBeneficialMagicalDurationsOnActualEnemyRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.spellFracture"}));
	for(const auto spell : {SpellID::HASTE, SpellID::SLOW})
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, spell == SpellID::HASTE ? 2 : -2, BonusSourceID(SpellID(spell)));
		bonus->turnsRemain = 2;
		victim->addNewBonus(bonus);
	}
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	const std::array<uint32_t, 1> targets{victim->unitId()};
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, {});
	auto effects = battle()->getLocalSpellEffects(victim->unitId());
	const auto haste = std::ranges::find(effects, BonusSourceID(SpellID(SpellID::HASTE)), &Bonus::sid);
	const auto slow = std::ranges::find(effects, BonusSourceID(SpellID(SpellID::SLOW)), &Bonus::sid);
	ASSERT_NE(haste, effects.end());
	ASSERT_NE(slow, effects.end());
	EXPECT_EQ(haste->turnsRemain, 1);
	EXPECT_EQ(slow->turnsRemain, 2);
	newHorizonsMetamagic::publishPointCompletion(*gameHandler->spellcastEnvironment(), *battle(), *mechanics, targets, {});
	effects = battle()->getLocalSpellEffects(victim->unitId());
	EXPECT_EQ(std::ranges::find(effects, BonusSourceID(SpellID(SpellID::HASTE)), &Bonus::sid), effects.end());
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, WellspringPointsAreEstablishedBeforeManaConduitRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.manaConduit", "new-horizons:metamagic.arcaneWellspring"}));
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 3);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 101);
	EXPECT_EQ(actor->acquireState()->arcaneShieldHitPoints, 0);
}

TEST_F(NewHorizonsMetamagicPointsBattleTest, SingleTargetMirrorCompletesOriginalManualCastWithoutEchoingItsReflectedInjury)
{
	ASSERT_NO_FATAL_FAILURE(prepare({"new-horizons:metamagic.arcaneCatalyst", "new-horizons:metamagic.arcaneEcho"}, true));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_MIRROR,
		BonusSource::OTHER, 100, BonusSourceID()));
	const auto victimHealth = victim->getAvailableHealth();
	const auto actorHealth = actor->getAvailableHealth();
	const auto originalCasts = server.casts.size();
	const auto originalInjuries = server.injuries.size();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	const auto & own = battle()->getMetamagicPointState(BattleSide::ATTACKER);
	const auto & enemy = battle()->getMetamagicPointState(BattleSide::DEFENDER);
	EXPECT_EQ(own.points, 2);
	EXPECT_TRUE(own.echoUsedThisRound);
	EXPECT_EQ(enemy.points, 2);
	EXPECT_EQ(server.casts.size(), originalCasts + 1);
	EXPECT_EQ(victim->getAvailableHealth(), victimHealth);
	EXPECT_LT(actor->getAvailableHealth(), actorHealth);
	ASSERT_FALSE(server.casts.empty());
	EXPECT_TRUE(server.casts.back().announcement.reflectedCres.contains(victim->unitId()));
	EXPECT_EQ(std::ranges::count_if(std::span(server.injuries).subspan(originalInjuries), [this](const auto & packet)
	{
		return std::ranges::any_of(packet.stacks, [this](const auto & injury)
			{ return injury.stackAttacked == actor->unitId() && injury.damageAmount > 0; });
	}), 1);
}
