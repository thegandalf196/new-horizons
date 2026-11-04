/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../client/windows/SpellbookEntryPresentation.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/spells/RecipientSpellEffectPreview.h"
#include "../../../lib/spells/BattleSpellMechanics.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace
{
constexpr auto meta = "new-horizons:metamagic";
constexpr int64_t ordinaryArrowDamage = 310; // 20 + 2 * 100 SP * Expert Sorcery 145%.

class NewHorizonsArcaneMarksBattleTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	bool expensiveMassBless = false;
	bool legacyMetamagic = false;
	bool opposingShooter = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		// This suite intentionally consumes CURRENT captured policy, never the
		// historical faction-perk fixture used by old extra-action tests.
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		ASSERT_EQ(perks["metamagicPolicy"]["version"].Integer(), 2);
		if(legacyMetamagic)
			perks.Struct().erase("metamagicPolicy");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(expensiveMassBless)
		{
			// Preserve the authored invariant that a mass variant costs three
			// times its base. The 240-Mana cast still distinguishes 7.5% from 7%.
			for(auto & cost : magic["spells"]["core:bless"]["costs"].Vector())
				cost.Integer() = 80;
			for(auto & cost : magic["spells"]["new-horizons:massBless"]["costs"].Vector())
				cost.Integer() = 240;
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}

	void skill(CGHeroInstance * hero, const char * id, int rank)
	{
		const int decoded = SecondarySkill::decode(id);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
	}

	void choose(CGHeroInstance * hero, const char * skillId, const char * perkId)
	{
		hero->applyPerkSelection({skillId, perkId});
		ASSERT_TRUE(hero->hasActivePerk(skillId, perkId));
	}

	void prepare(int rank, std::initializer_list<const char *> perks = std::initializer_list<const char *>(),
		int opposingRank = 0,
		bool shooter = false, bool litany = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			for(const char * school : {"new-horizons:sorceryMagic", "new-horizons:lightMagic",
				"new-horizons:shadowMagic"})
				ASSERT_NO_FATAL_FAILURE(skill(hero, school, MasteryLevel::EXPERT));
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
			hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
			giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			for(const char * name : {"core:magicArrow", "core:bless", "core:curse", "core:dispel",
				"new-horizons:focusMagic", "new-horizons:summonLivingArmor",
				"new-horizons:phantomArmy", "new-horizons:massBless"})
				hero->addSpellToSpellbook(SpellID(SpellID::decode(name)));
			setTestSpellPointTotal(hero, 1000);
			// Deliberate durable native test armies, not recruitment acceptance.
			const bool ranged = (hero == attackerSideHero && shooter) || (hero == defenderSideHero && opposingShooter);
			hero->setStackCount(SlotID(0), ranged ? 5 : 1000);
		}
		if(shooter)
			ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)),
				creatureByName("core:archer").toCreature()));
		if(opposingShooter)
			ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id, SlotID(0)),
				creatureByName("core:archer").toCreature()));
		ASSERT_NO_FATAL_FAILURE(skill(attackerSideHero, meta, rank));
		ASSERT_NO_FATAL_FAILURE(skill(defenderSideHero, meta, opposingRank));
		for(const auto * perk : perks)
			ASSERT_NO_FATAL_FAILURE(choose(attackerSideHero, meta, perk));
		if(litany)
		{
			ASSERT_NO_FATAL_FAILURE(choose(attackerSideHero, "new-horizons:lightMagic",
				"new-horizons:lightMagic.healer"));
			ASSERT_NO_FATAL_FAILURE(choose(attackerSideHero, "new-horizons:lightMagic",
				"new-horizons:lightMagic.litany"));
		}
		ASSERT_NO_FATAL_FAILURE(startBattle());
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(victim, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		activate(actor);
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack packet;
		packet.battleID = BattleID(0);
		packet.stack = unit->unitId();
		packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}

	void nextRound()
	{
		advanceRound();
		activate(actor);
	}

	void marks(CStack * unit, BattleSide caster, int count)
	{
		// Fixture setup through the ordinary detached-state UPDATE packet.
		auto copy = unit->acquireState();
		copy->arcaneMarks.set(caster, count);
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		packet.changedStacks.back().data = copy->save();
		gameHandler->sendAndApply(packet);
	}

	bool cast(SpellID spell, const CStack * target, BattleSide side = BattleSide::ATTACKER,
		int overcharge = 0, bool followup = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = side;
		action.spell = spell;
		action.spellOvercharge = overcharge;
		action.metamagicFollowup = followup;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side), action);
	}

	void prepareGaleArmy(std::array<CStack *, 5> & friends)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT, {"new-horizons:metamagic.manaAttunement"}));
		const SpellID gale(SpellID::decode("new-horizons:heavenlyGale"));
		attackerSideHero->addSpellToSpellbook(gale);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		friends = {actor,
			addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 3), 1000),
			addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000),
			addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 7), 1000),
			addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 7), 1000)};
		marks(friends[0], BattleSide::ATTACKER, 0);
		marks(friends[1], BattleSide::ATTACKER, 3);
		for(size_t index = 2; index < friends.size(); ++index)
			marks(friends[index], BattleSide::ATTACKER, 5);
		friends[3]->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(gale)));
		const SpellID lockSpell(SpellID::decode("new-horizons:spellLock"));
		auto lock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
			BonusSource::SPELL_EFFECT, 100, BonusSourceID(lockSpell));
		lock->turnsRemain = 3;
		friends[4]->addNewBonus(std::move(lock));
	}

	std::vector<std::byte> protectedPreviewState() const
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		wire.oser & CRandomGenerator::getDefault();
		wire.oser & battle()->activeStack;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			wire.oser & battle()->getHeroActionAllowances(side);
			wire.oser & battle()->getSide(side).castSpellsCount;
			wire.oser & battle()->getHeroCommandUsed(side);
			const auto * hero = battle()->battleGetFightingHero(side);
			wire.oser & hero->getNormalSpellPoints();
			wire.oser & hero->getBufferSpellPoints();
		}
		for(const auto * unit : battle()->battleGetAllUnits(false))
			wire.oser & unit->acquireState()->save();
		wire.oser & server.casts.size();
		wire.oser & server.startedActions.size();
		return wire.extractBuffer();
	}

	std::vector<std::byte> rng() const
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	}

	size_t breachMarks(const CStack * target) const
	{
		const SpellID breach(SpellID::decode("new-horizons:arcaneBreach"));
		return std::ranges::count_if(target->getExportedBonusList(), [breach](const auto & bonus)
		{
			return bonus->type == BonusType::COMBAT_EVENT_TRIGGER
				&& bonus->source == BonusSource::SPELL_EFFECT && bonus->sid == BonusSourceID(breach);
		});
	}
};
}

TEST_F(NewHorizonsArcaneMarksBattleTest, FirstPaidHitUsesZeroMarksAndLaterHitsUsePriorBasicMarksCappedAtFive)
{
	prepare(MasteryLevel::BASIC);
	for(int castIndex = 0; castIndex < 7; ++castIndex)
	{
		if(castIndex != 0)
			nextRound();
		const int previous = std::min(castIndex, 5);
		ASSERT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), previous);
		const auto hp = victim->getAvailableHealth();
		const auto mana = attackerSideHero->getManaAvailable();
		ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, victim));
		EXPECT_EQ(hp - victim->getAvailableHealth(), ordinaryArrowDamage * (100 + 5 * previous) / 100);
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 4);
		EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), std::min(castIndex + 1, 5));
	}
}

TEST_F(NewHorizonsArcaneMarksBattleTest, ExpertPerMarkRidersAndSaturationAddInTheActualDamagePath)
{
	prepare(MasteryLevel::EXPERT, {"new-horizons:metamagic.arcaneAmplification",
		"new-horizons:metamagic.arcaneSaturation", "new-horizons:metamagic.arcanePerfection"});
	marks(victim, BattleSide::ATTACKER, 5);
	const auto hp = victim->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, victim));
	// (15 + 2 + 5) * 5 + 20 = 130 percentage points, not multiplied riders.
	EXPECT_EQ(hp - victim->getAvailableHealth(), 713);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 5);
	nextRound();
	ASSERT_EQ(victim->getMaxHealth(), 10);
	SetStackEffect cap;
	cap.battleID = BattleID(0);
	cap.toAdd.emplace_back(victim->unitId(), std::vector<Bonus>{Bonus(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 50, BonusSourceID())});
	gameHandler->sendAndApply(cap);
	const auto cappedHealth = victim->getAvailableHealth();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(cappedHealth - victim->getAvailableHealth(), 5); // Existing 50%-of-one-creature cap reapplied last.
}

TEST_F(NewHorizonsArcaneMarksBattleTest, HeroesKeepSeparateCountersThroughDispelAndNoLegacyFollowupIsGranted)
{
	prepare(MasteryLevel::BASIC, {}, MasteryLevel::BASIC);
	ASSERT_TRUE(cast(SpellID::BLESS, actor));
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 1);
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(SpellID::MAGIC_ARROW, victim, BattleSide::ATTACKER, 0, true));
	EXPECT_FALSE(cast(SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicPendingCount, 0);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed, 0);
	activate(victim);
	ASSERT_TRUE(cast(SpellID::DISPEL, actor, BattleSide::DEFENDER));
	EXPECT_FALSE(actor->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 1); // Dispel did not remove this hero's mark.
	EXPECT_EQ(actor->metamagicMarks(BattleSide::DEFENDER), 1); // The other hero's resolved Dispel.
	nextRound();
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 1);
	EXPECT_EQ(actor->metamagicMarks(BattleSide::DEFENDER), 1);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, InitialResidualAndSummonedSeedsRemainDistinctInNativeEntryPaths)
{
	prepare(MasteryLevel::EXPERT, {"new-horizons:metamagic.arcaneConjuration",
		"new-horizons:metamagic.residualMagic", "new-horizons:metamagic.perfectImprint"});
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 2);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 2);
	EXPECT_EQ(actor->metamagicMarks(BattleSide::DEFENDER), 0);
	auto * late = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(9, 5), 1000);
	ASSERT_EQ(late->metamagicMarks(BattleSide::ATTACKER), 0); // Ordinary late ADD is not an initial stack.
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, late));
	EXPECT_EQ(late->metamagicMarks(BattleSide::ATTACKER), 2);
	nextRound();
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, late));
	EXPECT_EQ(late->metamagicMarks(BattleSide::ATTACKER), 3);
	nextRound();
	const auto beforeId = battle()->battleNextUnitId();
	BattleAction summon;
	summon.actionType = EActionType::HERO_SPELL;
	summon.side = BattleSide::ATTACKER;
	summon.spell = SpellID(SpellID::decode("new-horizons:summonLivingArmor"));
	summon.aimToHex(BattleHex(8, 5));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), summon));
	const auto * created = battle()->getStack(beforeId);
	ASSERT_NE(created, nullptr);
	ASSERT_TRUE(created->isSummoned());
	EXPECT_EQ(created->metamagicMarks(BattleSide::ATTACKER), 2);
	EXPECT_EQ(created->metamagicMarks(BattleSide::DEFENDER), 0);
	EXPECT_EQ(created->summonRoundsRemaining, 3); // Entry seed cannot amplify the spell that creates it.
}

TEST_F(NewHorizonsArcaneMarksBattleTest, PhantomUsesSourceHistoryButReceivesFreshConjurationSeeds)
{
	prepare(MasteryLevel::EXPERT, {"new-horizons:metamagic.arcaneConjuration"});
	marks(actor, BattleSide::ATTACKER, 5);
	const auto sourceHealth = actor->getAvailableHealth();
	const auto beforeId = battle()->battleNextUnitId();
	ASSERT_TRUE(cast(SpellID(SpellID::decode("new-horizons:phantomArmy")), actor));
	const auto * created = battle()->getStack(beforeId);
	ASSERT_NE(created, nullptr);
	EXPECT_GT(created->getPhantomInitialIntegrity(), 0);
	EXPECT_EQ(created->getCount(), actor->getCount());
	EXPECT_EQ(actor->getAvailableHealth(), sourceHealth);
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 5);
	EXPECT_EQ(created->metamagicMarks(BattleSide::ATTACKER), 2);
	EXPECT_EQ(created->getPhantomRoundsRemaining(), 3); // floor(2 * 175%), not floor(2 * 130%).
}

TEST_F(NewHorizonsArcaneMarksBattleTest, ProspectiveMassDiscountKeepsItsFractionAndPreviewConsumesNoRng)
{
	expensiveMassBless = true;
	prepare(MasteryLevel::BASIC, {"new-horizons:metamagic.manaAttunement"}, 0, false, true);
	std::array<CStack *, 4> friends{actor,
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 3), 1000),
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000),
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 7), 1000)};
	marks(friends[0], BattleSide::ATTACKER, 3);
	marks(friends[1], BattleSide::ATTACKER, 3);
	const SpellID massBless(SpellID::decode("new-horizons:massBless"));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, massBless.toSpell());
	const auto mechanics = massBless.toSpell()->battleMechanics(&event);
	const spells::Target target{spells::Destination(actor)};
	const auto beforeRng = rng();
	EXPECT_EQ(mechanics->getTargetAwareManaCost(target), 222); // 240 * (1 - 0.15 * 2/4).
	EXPECT_EQ(rng(), beforeRng);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(massBless, actor));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 222);
	for(size_t index = 0; index < friends.size(); ++index)
		EXPECT_EQ(friends[index]->metamagicMarks(BattleSide::ATTACKER), index < 2 ? 4 : 1);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, SelectionBoundAdmitsMarkedTargetBelowOrdinaryManaAndPaidCastUsesExactCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, {"new-horizons:metamagic.manaAttunement"}));
	const SpellID hex(SpellID::decode("new-horizons:hexOfPain"));
	attackerSideHero->addSpellToSpellbook(hex);
	marks(victim, BattleSide::ATTACKER, 3);
	const auto ordinary = battle()->battleGetSpellCost(hex.toSpell(), attackerSideHero);
	ASSERT_EQ(ordinary, 8);
	setTestSpellPointTotal(attackerSideHero, 7);
	const auto before = protectedPreviewState();
	const auto selection = newHorizonsMetamagic::minimumSelectionCost(*attackerSideHero,
		*hex.toSpell(), battle(), ordinary);
	EXPECT_EQ(selection, 7);
	EXPECT_LE(selection, attackerSideHero->getManaAvailable());
	EXPECT_GT(ordinary, attackerSideHero->getManaAvailable());
	EXPECT_EQ(protectedPreviewState(), before);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, hex.toSpell());
	const auto mechanics = hex.toSpell()->battleMechanics(&event);
	EXPECT_EQ(mechanics->getTargetAwareManaCost({spells::Destination(victim)}), selection);
	setTestSpellPointTotal(attackerSideHero, 6);
	const auto rejected = protectedPreviewState();
	EXPECT_FALSE(cast(hex, victim));
	EXPECT_EQ(protectedPreviewState(), rejected);
	setTestSpellPointTotal(attackerSideHero, 7);
	ASSERT_TRUE(cast(hex, victim));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 4);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, SelectionBoundRetainsOrdinaryCostWithoutBattleOwnQualifyingOrReceptiveTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, {"new-horizons:metamagic.manaAttunement"}));
	const SpellID hex(SpellID::decode("new-horizons:hexOfPain"));
	const auto ordinary = battle()->battleGetSpellCost(hex.toSpell(), attackerSideHero);
	ASSERT_EQ(ordinary, 8);
	EXPECT_EQ(newHorizonsMetamagic::minimumSelectionCost(*attackerSideHero,
		*hex.toSpell(), nullptr, ordinary), ordinary);
	marks(victim, BattleSide::ATTACKER, 2);
	marks(victim, BattleSide::DEFENDER, 5);
	EXPECT_EQ(newHorizonsMetamagic::minimumSelectionCost(*attackerSideHero,
		*hex.toSpell(), battle(), ordinary), ordinary);
	marks(victim, BattleSide::ATTACKER, 3);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(hex)));
	const auto before = protectedPreviewState();
	EXPECT_EQ(newHorizonsMetamagic::minimumSelectionCost(*attackerSideHero,
		*hex.toSpell(), battle(), ordinary), ordinary);
	EXPECT_EQ(protectedPreviewState(), before);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, SelectionBoundRetainsOrdinaryCostForCapturedLegacyPolicy)
{
	legacyMetamagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC, {"new-horizons:metamagic.manaAttunement"}));
	ASSERT_FALSE(newHorizonsMetamagic::usesArcaneMarks(attackerSideHero->getPerkState().rules));
	const SpellID hex(SpellID::decode("new-horizons:hexOfPain"));
	marks(victim, BattleSide::ATTACKER, 3);
	const auto ordinary = battle()->battleGetSpellCost(hex.toSpell(), attackerSideHero);
	ASSERT_EQ(ordinary, 8);
	EXPECT_EQ(newHorizonsMetamagic::minimumSelectionCost(*attackerSideHero,
		*hex.toSpell(), battle(), ordinary), ordinary);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, PaidOverchargeUsesPrecastThresholdAndRejectsUnaffordableRequestsWithoutMutation)
{
	prepare(MasteryLevel::BASIC, {"new-horizons:metamagic.manaAttunement"});
	marks(victim, BattleSide::ATTACKER, 2);
	setTestSpellPointTotal(attackerSideHero, 7);
	const auto hp = victim->getAvailableHealth();
	EXPECT_FALSE(cast(SpellID::MAGIC_ARROW, victim, BattleSide::ATTACKER, 4)); // Ordinary4 + surcharge4 >7.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 7);
	EXPECT_EQ(victim->getAvailableHealth(), hp);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 2);
	marks(victim, BattleSide::ATTACKER, 3);
	ASSERT_TRUE(cast(SpellID::MAGIC_ARROW, victim, BattleSide::ATTACKER, 4)); // ceil(8 * 85%) =7.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 4);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, FocusMagicStillUsesItsOwnThreeMarkPoolDuringActualRangedAttacks)
{
	prepare(MasteryLevel::EXPERT, {}, 0, true);
	marks(actor, BattleSide::ATTACKER, 5);
	marks(victim, BattleSide::ATTACKER, 4);
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	for(int hit = 0; hit < 4; ++hit)
	{
		if(hit != 0)
			nextRound();
		if(hit == 0 || hit == 2)
			ASSERT_TRUE(cast(focus, actor)); // Refresh normally before its ordinary three-round expiry.
		ASSERT_TRUE(actor->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(focus))));
		const auto previousHealth = victim->getAvailableHealth();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeShotAttack(actor, victim)));
		ASSERT_LT(victim->getAvailableHealth(), previousHealth);
		EXPECT_EQ(breachMarks(victim), static_cast<size_t>(std::min(hit + 1, 3)));
		EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 4);
		EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 5);
	}
}

TEST_F(NewHorizonsArcaneMarksBattleTest, RealityWarpMarksBothAcceptedEndpointsWithoutAmplifyingTransferredBonuses)
{
	prepare(MasteryLevel::EXPERT);
	skill(attackerSideHero, "new-horizons:chaosMagic", MasteryLevel::ADVANCED);
	const SpellID warp(SpellID::decode("new-horizons:realityWarp"));
	attackerSideHero->addSpellToSpellbook(warp);
	marks(actor, BattleSide::ATTACKER, 3);
	marks(victim, BattleSide::ATTACKER, 1);
	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		7, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 4;
	haste.spellCasterOwner = PlayerColor(0);
	SetStackEffect seed;
	seed.battleID = BattleID(0);
	seed.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{haste});
	gameHandler->sendAndApply(seed);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = warp;
	action.aimToUnit(actor);
	action.aimToUnit(victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(actor->metamagicMarks(BattleSide::ATTACKER), 4);
	EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 2);
	const auto transferred = victim->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE))).And(Selector::type()(BonusType::STACKS_SPEED)));
	ASSERT_EQ(transferred->size(), 1u);
	EXPECT_EQ(transferred->front()->val, 7);
	EXPECT_EQ(transferred->front()->turnsRemain, 4);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedRecastRecalculatesHolyArmorWithoutDuplicateOrStalePotency)
{
	prepare(MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID holy(SpellID::decode("new-horizons:holyArmor"));
	attackerSideHero->addSpellToSpellbook(holy);
	const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(holy))
		.And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION));
	ASSERT_TRUE(cast(holy, actor));
	ASSERT_EQ(actor->getBonuses(source)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(source)->front()->val, 30);
	EXPECT_EQ(actor->getBonuses(source)->front()->turnsRemain, 2);
	nextRound();
	marks(actor, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(holy, actor));
	const auto refreshed = actor->getBonuses(source);
	ASSERT_EQ(refreshed->size(), 1u);
	EXPECT_EQ(refreshed->front()->val, 52); // floor(30 * 175%).
	EXPECT_EQ(refreshed->front()->turnsRemain, 3); // floor(2 * 175%).
	Bonus longer = *refreshed->front();
	longer.turnsRemain = 6;
	SetStackEffect extend;
	extend.battleID = BattleID(0);
	extend.toUpdate.emplace_back(actor->unitId(), std::vector<Bonus>{longer});
	gameHandler->sendAndApply(extend);
	nextRound();
	ASSERT_TRUE(cast(holy, actor));
	ASSERT_EQ(actor->getBonuses(source)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(source)->front()->turnsRemain, 5); // Keep the longer surviving native timer.
}

TEST_F(NewHorizonsArcaneMarksBattleTest, RepeatedFiveMarkFrailtyKeepsItsSixtyPercentCumulativeCap)
{
	prepare(MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 300, ChangeValueMode::ABSOLUTE);
	const SpellID frailty(SpellID::decode("new-horizons:frailty"));
	attackerSideHero->addSpellToSpellbook(frailty);
	marks(victim, BattleSide::ATTACKER, 5);
	const int baseDefense = victim->unitType()->getBaseDefense();
	ASSERT_GT(baseDefense, 0);
	for(int index = 0; index < 3; ++index)
	{
		if(index != 0) nextRound();
		ASSERT_TRUE(cast(frailty, victim));
		const auto effects = victim->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(frailty)).And(Selector::type()(BonusType::PRIMARY_SKILL)));
		ASSERT_EQ(effects->size(), 1u);
		ASSERT_NE(effects->front()->parameters, nullptr);
		const int expectedTotal = index == 0 ? 3500 : 6000;
		EXPECT_EQ(effects->front()->parameters->toNumber(), expectedTotal);
		EXPECT_EQ(effects->front()->val, -(baseDefense * expectedTotal / 10000));
		EXPECT_EQ(victim->metamagicMarks(BattleSide::ATTACKER), 5);
	}
}

TEST_F(NewHorizonsArcaneMarksBattleTest, LocalSpellQueryExcludesInheritedAurasWithoutTouchingStateOrRng)
{
	prepare(MasteryLevel::BASIC);
	Bonus aura(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		7, BonusSourceID(SpellID(SpellID::HASTE)));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(aura));
	const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	ASSERT_TRUE(actor->hasBonus(source));
	const auto beforeRng = rng();
	const auto hp = actor->getAvailableHealth();
	const auto local = battle()->getLocalSpellEffects(actor->unitId());
	EXPECT_TRUE(std::ranges::none_of(local, [](const Bonus & value)
	{
		return value.sid == BonusSourceID(SpellID(SpellID::HASTE));
	}));
	EXPECT_TRUE(actor->hasBonus(source));
	EXPECT_EQ(actor->getAvailableHealth(), hp);
	EXPECT_EQ(rng(), beforeRng);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedHydraRecastReplacesCapacityWithoutScalingAbsoluteHealthOrHealing)
{
	prepare(MasteryLevel::EXPERT);
	skill(attackerSideHero, "new-horizons:natureMagic", MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID hydra(SpellID::decode("new-horizons:hydrasVitality"));
	attackerSideHero->addSpellToSpellbook(hydra);
	ASSERT_EQ(actor->getMaxHealth(), 10);
	ASSERT_TRUE(cast(hydra, actor));
	EXPECT_EQ(actor->getMaxHealth(), 12); // floor(10 *125%), no preexisting marks.
	nextRound();
	marks(actor, BattleSide::ATTACKER, 5);
	const auto hp = actor->getAvailableHealth();
	ASSERT_TRUE(cast(hydra, actor));
	EXPECT_EQ(actor->getMaxHealth(), 14); // 25% *175% =43.75%; floor(10 *143.75%).
	EXPECT_EQ(actor->getAvailableHealth(), hp); // Increased capacity is not healing.
	const auto capacity = actor->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(hydra)).And(Selector::type()(BonusType::STACK_HEALTH)));
	ASSERT_EQ(capacity->size(), 1u);
	EXPECT_EQ(capacity->front()->val, 14); // Not the independently re-scaled absolute cap24.
}

TEST_F(NewHorizonsArcaneMarksBattleTest, RecipientStatusPreviewUsesTheActualHydraMagnitudeAndTimerWithoutSimulation)
{
	prepare(MasteryLevel::EXPERT);
	skill(attackerSideHero, "new-horizons:natureMagic", MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID hydra(SpellID::decode("new-horizons:hydrasVitality"));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, hydra.toSpell());
	const auto mechanics = hydra.toSpell()->battleMechanics(&cast);
	const auto before = rng();
	const auto ordinary = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, actor);
	ASSERT_TRUE(ordinary);
	ASSERT_TRUE(ordinary->effect.primary);
	EXPECT_EQ(ordinary->effect.primary->value, 25'000'000);
	EXPECT_EQ(ordinary->effect.primary->scale, 1'000'000);
	EXPECT_EQ(ordinary->durationRounds, 3);
	marks(actor, BattleSide::ATTACKER, 5);
	const auto marked = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, actor);
	ASSERT_TRUE(marked);
	EXPECT_EQ(marked->effect.primary->value, 43'750'000);
	EXPECT_EQ(marked->durationRounds, 5);
	EXPECT_EQ(actor->getMaxHealth(), 10); // Read-only; actual cast/recast covered separately.
	EXPECT_EQ(rng(), before);
	spells::BattleCast arrow(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::MAGIC_ARROW).toSpell());
	EXPECT_FALSE(newHorizonsMagic::recipientSpellEffectPreview(*arrow.getSpell()->battleMechanics(&arrow), actor));
}

TEST_F(NewHorizonsArcaneMarksBattleTest, HydraPreviewKeepsTheExistingLocalRecastTimer)
{
	prepare(MasteryLevel::EXPERT);
	skill(attackerSideHero, "new-horizons:natureMagic", MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID hydra(SpellID::decode("new-horizons:hydrasVitality"));
	attackerSideHero->addSpellToSpellbook(hydra);
	marks(actor, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(hydra, actor)); // Incoming timer5.
	nextRound(); // Existing local timer4.
	marks(actor, BattleSide::ATTACKER, 0);
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, hydra.toSpell());
	const auto mechanics = hydra.toSpell()->battleMechanics(&preview);
	const auto value = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, actor);
	ASSERT_TRUE(value);
	EXPECT_EQ(value->durationRounds, 4); // Native refresh keeps max(existing4,new3).
	ASSERT_TRUE(cast(hydra, actor));
	const auto capacity = actor->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(hydra)).And(Selector::type()(BonusType::STACK_HEALTH)));
	ASSERT_EQ(capacity->size(), 1u);
	EXPECT_EQ(capacity->front()->turnsRemain, 4);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedChaosShieldRecastAmplifiesItsActualLocalReductionUnderCap)
{
	prepare(MasteryLevel::EXPERT);
	ASSERT_NO_FATAL_FAILURE(skill(attackerSideHero, "new-horizons:chaosMagic", MasteryLevel::EXPERT));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID shield(SpellID::decode("new-horizons:shieldOfChaos"));
	attackerSideHero->addSpellToSpellbook(shield);
	ASSERT_TRUE(cast(shield, actor));
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(shield))
		.And(Selector::type()(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 5000);
	nextRound();
	marks(actor, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(shield, actor));
	const auto applied = actor->getBonuses(selector);
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 8000);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedMisfortuneRecastReplacesTheChanceFactorUsingItsNeutralDeficit)
{
	prepare(MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 40, ChangeValueMode::ABSOLUTE);
	const SpellID misfortune(SpellID::MISFORTUNE);
	attackerSideHero->addSpellToSpellbook(misfortune);
	ASSERT_TRUE(cast(misfortune, victim));
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(misfortune))
		.And(Selector::type()(BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS));
	ASSERT_EQ(victim->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(victim->getBonuses(selector)->front()->val, 6500);
	nextRound();
	marks(victim, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(misfortune, victim));
	const auto applied = victim->getBonuses(selector);
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 3875);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedHolyArmorRecastRetainsItsNativeFinalReductionCap)
{
	prepare(MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID spell(SpellID::decode("new-horizons:holyArmor"));
	attackerSideHero->addSpellToSpellbook(spell);
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION));
	ASSERT_TRUE(cast(spell, actor));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 30);
	nextRound();
	marks(actor, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(spell, actor));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 52);
	nextRound();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(cast(spell, actor));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 60);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, MarkedHeavenlyGaleRecastRetainsItsNativeFinalReductionCap)
{
	prepare(MasteryLevel::EXPERT);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	const SpellID spell(SpellID::decode("new-horizons:heavenlyGale"));
	attackerSideHero->addSpellToSpellbook(spell);
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::HEAVENLY_GALE));
	ASSERT_TRUE(cast(spell, actor));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 5000);
	nextRound();
	marks(actor, BattleSide::ATTACKER, 5);
	ASSERT_TRUE(cast(spell, actor));
	ASSERT_EQ(actor->getBonuses(selector)->size(), 1u);
	EXPECT_EQ(actor->getBonuses(selector)->front()->val, 8000);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, SevenTimedRecipientPreviewsMatchActualNativeCastsWithoutRngOrManaUse)
{
	opposingShooter = true;
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(skill(attackerSideHero, "new-horizons:chaosMagic", MasteryLevel::EXPERT));
	ASSERT_NO_FATAL_FAILURE(skill(attackerSideHero, "new-horizons:natureMagic", MasteryLevel::EXPERT));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	struct Case
	{
		const char * spell;
		BonusType type;
		bool hostile;
		bool durationOnly;
	};
	const std::array cases{
		Case{"new-horizons:holyArmor", BonusType::SPELL_DAMAGE_REDUCTION, false, false},
		Case{"new-horizons:heavenlyGale", BonusType::HEAVENLY_GALE, false, false},
		Case{"new-horizons:shieldOfChaos", BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, false, false},
		Case{"new-horizons:entangle", BonusType::BIND_EFFECT, true, true},
		Case{"core:misfortune", BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, true, false},
		Case{"core:forgetfulness", BonusType::FORGETFULL, true, true},
		Case{"core:bless", BonusType::ALWAYS_MAXIMUM_DAMAGE, false, true}
	};
	for(const auto & row : cases)
	{
		SCOPED_TRACE(row.spell);
		nextRound();
		const SpellID spell(SpellID::decode(row.spell));
		attackerSideHero->addSpellToSpellbook(spell);
		auto * recipient = row.hostile ? victim : actor;
		marks(recipient, BattleSide::ATTACKER, 5);
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&event);
		const auto randomBefore = rng();
		const auto manaBefore = attackerSideHero->getManaAvailable();
		const auto preview = newHorizonsMagic::recipientSpellEffectPreview(*mechanics, recipient);
		ASSERT_TRUE(preview);
		ASSERT_TRUE(preview->effect.primary);
		ASSERT_TRUE(preview->durationRounds);
		EXPECT_EQ(rng(), randomBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		ASSERT_TRUE(cast(spell, recipient));
		const auto applied = recipient->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spell)).And(Selector::type()(row.type)));
		ASSERT_EQ(applied->size(), 1u);
		EXPECT_EQ(preview->durationRounds, applied->front()->turnsRemain);
		EXPECT_EQ(preview->effect.primary->value,
			row.durationOnly ? applied->front()->turnsRemain : applied->front()->val);
	}
}

TEST_F(NewHorizonsArcaneMarksBattleTest, GaleBookPreviewMatchesMixedMarkedRecipientsAndNeverSpendsState)
{
	std::array<CStack *, 5> friends{};
	ASSERT_NO_FATAL_FAILURE(prepareGaleArmy(friends));
	const SpellID gale(SpellID::decode("new-horizons:heavenlyGale"));
	const auto before = protectedPreviewState();
	const std::array<int64_t, 3> magnitudes{5000, 7250, 8000};
	const std::array<int32_t, 3> durations{2, 2, 3};
	for(int reopen = 0; reopen < 3; ++reopen)
	{
		const auto preview = spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
			*gale.toSpell(), battle());
		ASSERT_TRUE(preview);
		EXPECT_TRUE(preview->canCast);
		EXPECT_EQ(preview->manaCost, 12); // ceil(13 * (1 - 15% * 2/3)).
		ASSERT_EQ(preview->recipients.size(), 3u);
		for(size_t index = 0; index < 3; ++index)
		{
			const auto & row = preview->recipients[index];
			EXPECT_EQ(row.unitId, friends[index]->unitId());
			ASSERT_TRUE(row.effect.effect.primary);
			EXPECT_EQ(row.effect.effect.primary->value, magnitudes[index]);
			EXPECT_EQ(row.effect.effect.primary->scale, 100);
			EXPECT_EQ(row.effect.durationRounds, durations[index]);
		}
		EXPECT_EQ(protectedPreviewState(), before);
	}
	const auto preview = spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
		*gale.toSpell(), battle());
	const auto mana = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = gale;
	action.aimToHex(BattleHex::INVALID);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), preview->manaCost);
	const auto source = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(gale))
		.And(Selector::type()(BonusType::HEAVENLY_GALE));
	for(size_t index = 0; index < 3; ++index)
	{
		const auto applied = friends[index]->getBonuses(source);
		ASSERT_EQ(applied->size(), 1u);
		EXPECT_EQ(applied->front()->val, preview->recipients[index].effect.effect.primary->value);
		EXPECT_EQ(applied->front()->turnsRemain, preview->recipients[index].effect.durationRounds);
	}
	for(const auto * excluded : {friends[3], friends[4], victim})
		EXPECT_TRUE(excluded->getBonuses(source)->empty());
	const auto afterCast = protectedPreviewState();
	const auto spent = spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
		*gale.toSpell(), battle());
	ASSERT_TRUE(spent);
	EXPECT_FALSE(spent->canCast);
	EXPECT_EQ(protectedPreviewState(), afterCast);
}

TEST_F(NewHorizonsArcaneMarksBattleTest, GaleBookPreviewUsesExactMassAffordabilityAndRejectsOtherFamilies)
{
	std::array<CStack *, 5> friends{};
	ASSERT_NO_FATAL_FAILURE(prepareGaleArmy(friends));
	const SpellID gale(SpellID::decode("new-horizons:heavenlyGale"));
	for(const int mana : {11, 12})
	{
		setTestSpellPointTotal(attackerSideHero, mana);
		const auto before = protectedPreviewState();
		const auto preview = spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
			*gale.toSpell(), battle());
		ASSERT_TRUE(preview);
		EXPECT_EQ(preview->manaCost, 12);
		EXPECT_EQ(preview->canCast, mana == 12);
		EXPECT_EQ(preview->recipients.size(), 3u);
		EXPECT_EQ(protectedPreviewState(), before);
	}
	EXPECT_FALSE(spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
		*SpellID(SpellID::BLESS).toSpell(), battle()));
	EXPECT_FALSE(spellbookPresentation::heavenlyGaleArmyPreview(*attackerSideHero,
		*gale.toSpell(), nullptr));
}
