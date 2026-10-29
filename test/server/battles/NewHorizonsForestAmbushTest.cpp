/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../../../lib/GameSettings.h"
#include <vstd/RNG.h>
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/NewHorizonsForestAmbush.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/entities/hero/NewHorizonsForestAmbush.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#endif
namespace
{
class NewHorizonsForestAmbushTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool enabled = true;
	bool ivor = true;
	bool upgraded = false;
	bool spells = false;
	bool breathEnemy = false;
	bool luckAllTargets = false;
	bool overwatchCounterfire = false;
	int enemyCount = 20;
	int elfCount = 5;
	CStack * elf = nullptr;
	CStack * enemy = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		if(luckAllTargets)
			map->overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(true));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(overwatchCounterfire)
		{
			// Author the defender's one Basic Archery choice before hero capture.
			auto & starts = rules["startingSkills"]["startingDevelopmentProfiles"]["core:valeska"]["startingPerks"].Vector();
			ASSERT_EQ(starts.size(), 1u);
			ASSERT_EQ(starts.front()["skill"].String(), "new-horizons:archery");
			ASSERT_EQ(starts.front()["perk"].String(), "new-horizons:archery.pointBlankShot");
			starts.front()["perk"] = JsonNode("new-horizons:archery.counterfire");
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		}
		if(captured)
			rules["additionalSpecialties"]["forestAmbush"]["enabled"].Bool() = enabled;
		else
			rules["additionalSpecialties"].Struct().erase("forestAmbush");
		// This is an authored complete capture; a merge would restore an omitted specialty.
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		ASSERT_EQ(map->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)
			["additionalSpecialties"].Struct().contains("forestAmbush"), captured);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(ivor ? "core:ivor" : "core:jenova")));
	}
	void prepare()
	{
		startGame();
		if(spells)
			for(auto * hero : {attackerSideHero, defenderSideHero})
			{
				giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
				for(const auto spell : {SpellID::MAGIC_ARROW, SpellID::FIREBALL, SpellID::DISPEL})
					hero->addSpellToSpellbook(spell);
				setTestSpellPointTotal(hero, 1000);
			}
		if(overwatchCounterfire)
		{
			const auto battlecraft = SecondarySkill(SecondarySkill::decode("new-horizons:battlecraft"));
			attackerSideHero->setSecSkillLevel(battlecraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.overwatch"});
			ASSERT_TRUE(newHorizonsBattlecraft::hasOverwatch(attackerSideHero));
			const auto archery = SecondarySkill(SecondarySkill::decode("new-horizons:archery"));
			defenderSideHero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(newHorizonsArchery::hasCounterfire(defenderSideHero));
		}
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		// Synthetic combat counts establish original slots, not acquisition evidence.
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(upgraded ? "core:grandElf" : "core:woodElf"), elfCount));
		// Breath needs a single-wide primary: a Centaur tail would overlap the Dragon.
		const auto reserveCreature = creatureByName(breathEnemy ? "core:pikeman" : "core:centaur");
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), reserveCreature, 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName(breathEnemy ? "core:greenDragon" : "core:archer"), enemyCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:centaur"), 1000));
		startBattle();
		elf = original(BattleSide::ATTACKER, SlotID(0));
		enemy = original(BattleSide::DEFENDER, SlotID(0));
		ASSERT_NE(elf, nullptr);
		ASSERT_NE(enemy, nullptr);
		auto * reserve = original(BattleSide::ATTACKER, SlotID(1));
		auto * opponentReserve = original(BattleSide::DEFENDER, SlotID(1));
		ASSERT_NE(reserve, nullptr);
		ASSERT_NE(opponentReserve, nullptr);
		reserve->setPosition(BattleHex(3, 2));
		opponentReserve->setPosition(BattleHex(13, 2));
		elf->setPosition(BattleHex(breathEnemy ? 5 : overwatchCounterfire ? 2 : 3, 5));
		enemy->setPosition(BattleHex(breathEnemy ? 2 : overwatchCounterfire ? 14 : 12, 5));
		if(breathEnemy)
		{
			reserve->setPosition(BattleHex(4, 5));
			ASSERT_FALSE(reserve->doubleWide());
			ASSERT_FALSE(enemy->getHexes().contains(reserve->getPosition()));
			ASSERT_TRUE(enemy->getSurroundingHexes().contains(reserve->getPosition()));
			ASSERT_TRUE(enemy->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH));
			const auto collateral = battle()->getPotentiallyAttackableHexes(
				enemy, reserve->getPosition(), enemy->getPosition());
			ASSERT_TRUE(collateral.friendlyCreaturePositions.contains(elf->getPosition()));
			bonus(enemy, BonusType::BLOCKS_RETALIATION, 1);
			bonus(enemy, BonusType::NO_LUCK, 1);
			forceMaximumDamage(enemy);
		}
		beginCombat();
	}
	CStack * original(BattleSide side, SlotID slot)
	{
		const auto units = battle()->battleGetStacksIf([=](const CStack * unit)
			{ return unit->unitSide() == side && unit->unitSlot() == slot; });
		EXPECT_EQ(units.size(), 1u);
		if(units.size() != 1)
			return nullptr;
		auto * result = battle()->getStack(units.front()->unitId());
		const auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		EXPECT_EQ(result->base, &hero->getStack(slot));
		EXPECT_FALSE(result->isSummoned());
		return result;
	}
	void activate(CStack * stack, BattleUnitTurnReason reason = BattleUnitTurnReason::TURN_QUEUE)
	{
		BattleSetActiveStack turn;
		turn.battleID = BattleID(0);
		turn.stack = stack->unitId();
		turn.reason = reason;
		gameHandler->sendAndApply(turn);
	}
	bool submit(CStack * actor, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(actor->unitSide()), action);
	}
	void conceal()
	{
		activate(elf);
		ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
		ASSERT_TRUE(elf->forestAmbush.concealed);
	}
	int shotLuck() const
	{
		return battle()->battleGetAttackLuck(elf, enemy, true);
	}
	void verifyTwoWorldLoads(bool expected)
	{
		startGame(); // World saves are exercised before combat, not as ongoing-battle saves.
		const auto id = attackerSideHero->id;
		auto world = gameState();
		for(int pass = 0; pass < 2; ++pass)
		{
			auto restored = std::make_shared<CGameState>();
			restored->preInit(LIBRARY);
			restored->loadFromMemory(world->saveToMemory());
			const auto heroes = restored->getMap().getObjects<CGHeroInstance>();
			const auto found = std::ranges::find_if(heroes, [id](const auto * hero) { return hero->id == id; });
			ASSERT_NE(found, heroes.end());
			EXPECT_EQ(newHorizonsHeroes::hasForestAmbush(*found), expected);
			EXPECT_EQ(newHorizonsHeroes::forestAmbushEnabled(*found), expected);
			world = restored;
		}
	}

	static void bonus(CStack * stack, BonusType type, int value)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			type, BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

TEST_F(NewHorizonsForestAmbushTest, CapturedWaitUsesDistinctNonmagicalProtectionAndDelayedFirstShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int before = shotLuck();
	ASSERT_TRUE(battle()->battleCanShoot(enemy, elf->getPosition()));
	ASSERT_NO_FATAL_FAILURE(conceal());
	EXPECT_FALSE(elf->hasBonusOfType(BonusType::SANCTIFIED));
	EXPECT_TRUE(newHorizonsForestAmbush::protectsDirectTarget(elf));
	EXPECT_EQ(shotLuck(), std::min(10, before + 5)) << "a concealed primary shot uses the existing Luck roll";
	EXPECT_FALSE(battle()->battleCanAttackUnit(enemy, elf));
	EXPECT_FALSE(battle()->battleCanShoot(enemy, elf->getPosition()));
	activate(elf);
	EXPECT_FALSE(elf->forestAmbush.concealed);
	ASSERT_TRUE(elf->forestAmbush.shotPending);
	EXPECT_EQ(shotLuck(), std::min(10, before + 5));
	BattleAttackInfo collateral(elf, enemy, 0, true);
	collateral.secondaryAttack = true;
	const auto normal = battle()->calculateDmgRange(collateral).damage;
	EXPECT_EQ(battle()->battleExpectedLuckDamage(collateral), normal.min + (normal.max - normal.min) / 2)
		<< "the ordinary primary-only Luck setting excludes collateral";
	ASSERT_TRUE(submit(elf, BattleAction::makeShotAttack(elf, enemy)));
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	EXPECT_EQ(shotLuck(), before);
}

TEST_F(NewHorizonsForestAmbushTest, PriorActualAttackIncludingZeroDamageDisqualifiesWait)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto health = elf->getAvailableHealth();
	int64_t zero = 0;
	newHorizonsForestAmbush::damageFromAttack(*battle(), *elf, zero, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(elf->getAvailableHealth(), health);
	EXPECT_TRUE(elf->forestAmbush.attacked);
	activate(elf);
	ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
	EXPECT_FALSE(elf->forestAmbush.concealed);
	activate(elf);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, CollateralAttackRevealsButIncidentalDamageDoesNotInventAnAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	int64_t damage = 1;
	elf->damage(damage, false, battle::DamageProvenance::SPELL);
	EXPECT_TRUE(elf->forestAmbush.concealed);
	damage = 0;
	newHorizonsForestAmbush::damageFromAttack(*battle(), *elf, damage, battle::DamageProvenance::SPELL);
	EXPECT_FALSE(elf->forestAmbush.concealed) << "spell-like creature blow is still an attack";
	activate(elf);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, NormalLuckCapsAndNoLuckRemainAuthoritative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(elf);
	bonus(elf, BonusType::LUCK, 10);
	EXPECT_EQ(shotLuck(), 10);
	bonus(elf, BonusType::MAXIMUM_LUCK, 3);
	EXPECT_EQ(shotLuck(), 3);
	bonus(elf, BonusType::NO_LUCK, 1);
	EXPECT_EQ(shotLuck(), 0);
	bonus(elf, BonusType::MAX_LUCK, 1);
	EXPECT_EQ(shotLuck(), 3) << "existing MAX_LUCK / NO_LUCK priority is preserved";
	EXPECT_TRUE(elf->forestAmbush.shotPending) << "queries never consume the shot";
}

TEST_F(NewHorizonsForestAmbushTest, GrandElfDoubleShotConsumesOneRiderAndPreservesAllTargetsLuckPolicy)
{
	upgraded = true;
	luckAllTargets = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(elf);
	BattleAttackInfo primary(elf, enemy, 0, true);
	auto collateral = primary;
	collateral.secondaryAttack = true;
	EXPECT_EQ(battle()->battleExpectedLuckDamage(primary), battle()->battleExpectedLuckDamage(collateral))
		<< "one primary roll's Luck inherits under the existing all-targets policy";
	const auto prior = server.attacks.size();
	ASSERT_TRUE(submit(elf, BattleAction::makeShotAttack(elf, enemy)));
	const auto shots = std::count_if(server.attacks.begin() + prior, server.attacks.end(),
		[this](const BattleAttack & attack) { return attack.stackAttacking == elf->unitId() && attack.shot() && !attack.counter(); });
	EXPECT_EQ(shots, 2);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, LegacyCapturedAbsenceRetainsOriginalSpecialty)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasForestAmbush(attackerSideHero));
	activate(elf);
	ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
	EXPECT_FALSE(elf->forestAmbush.hasState());
	EXPECT_FALSE(elf->save()["state"].Struct().contains("forestAmbush"));
	EXPECT_FALSE(newHorizonsForestAmbush::protectsDirectTarget(elf));
}

TEST_F(NewHorizonsForestAmbushTest, DisabledCaptureSelectsIdentityWithoutGrantingConcealment)
{
	enabled = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsHeroes::hasForestAmbush(attackerSideHero));
	EXPECT_FALSE(newHorizonsHeroes::forestAmbushEnabled(attackerSideHero));
	activate(elf);
	ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
	EXPECT_FALSE(elf->forestAmbush.concealed);
}

TEST_F(NewHorizonsForestAmbushTest, OtherHeroCannotUseTheNewCaptureForItsElves)
{
	ivor = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasForestAmbush(attackerSideHero));
	activate(elf);
	ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
	EXPECT_FALSE(elf->forestAmbush.concealed);
}

TEST_F(NewHorizonsForestAmbushTest, ActualIncomingNativeShotDisqualifiesWaitWithoutKillingTheElf)
{
	enemyCount = 1; // Survives a maximum/lucky Archer shot without relying on the seed.
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(enemy);
	ASSERT_TRUE(submit(enemy, BattleAction::makeShotAttack(enemy, elf)));
	ASSERT_TRUE(elf->alive());
	ASSERT_TRUE(elf->forestAmbush.attacked);
	activate(elf);
	ASSERT_TRUE(submit(elf, BattleAction::makeWait(elf)));
	EXPECT_FALSE(elf->forestAmbush.concealed);
	activate(elf);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, HostileSingleTargetIsRejectedWhileAreaSpellStillDamages)
{
	spells = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(enemy);
	const auto before = elf->save();
	const auto manaBefore = defenderSideHero->getManaAvailable();
	EXPECT_FALSE(castOn(defenderSideHero, SpellID::MAGIC_ARROW, elf));
	EXPECT_EQ(elf->save(), before);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), manaBefore);
	BattleAction area;
	area.actionType = EActionType::HERO_SPELL;
	area.side = BattleSide::DEFENDER;
	area.spell = SpellID::FIREBALL;
	area.aimToHex(elf->getPosition());
	const auto hp = elf->getAvailableHealth();
	ASSERT_TRUE(submit(enemy, area));
	EXPECT_LT(elf->getAvailableHealth(), hp);
	ASSERT_TRUE(elf->alive()) << "25-base Fireball at zero Spell Power is nonlethal to five Elves";
	EXPECT_TRUE(elf->forestAmbush.concealed);
	EXPECT_FALSE(elf->forestAmbush.attacked) << "an area spell is not a BattleAttack";
}

TEST_F(NewHorizonsForestAmbushTest, DispelDoesNotRemoveNonmagicalConcealment)
{
	spells = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(original(BattleSide::ATTACKER, SlotID(1)));
	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 3;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(elf->unitId(), std::vector<Bonus>{haste});
	gameHandler->sendAndApply(effect);
	ASSERT_TRUE(vstd::contains(elf->activeSpells(), SpellID::HASTE));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::DISPEL, elf));
	EXPECT_FALSE(vstd::contains(elf->activeSpells(), SpellID::HASTE));
	EXPECT_TRUE(elf->forestAmbush.concealed);
	EXPECT_FALSE(elf->hasBonusOfType(BonusType::SANCTIFIED));
}

TEST_F(NewHorizonsForestAmbushTest, RejectedDirectAttackCannotRevealOrSpendAnEnemyAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(enemy);
	const auto before = elf->save();
	const auto shots = enemy->shots.available();
	const auto count = server.attacks.size();
	EXPECT_FALSE(submit(enemy, BattleAction::makeShotAttack(enemy, elf)));
	EXPECT_EQ(elf->save(), before);
	EXPECT_EQ(enemy->shots.available(), shots);
	EXPECT_EQ(server.attacks.size(), count);
}

TEST_F(NewHorizonsForestAmbushTest, UnusedShotExpiresAtActualDefendEndAndCannotRearmThisRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(elf);
	ASSERT_TRUE(elf->forestAmbush.shotPending);
	ASSERT_TRUE(submit(elf, BattleAction::makeDefend(elf)));
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	activate(elf, BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(elf->forestAmbush.concealed);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, CurrentUnitPacketAndDescriptorRoundTripPreserveStateAndLegacyAbsenceClears)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	UnitChanges update(elf->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = elf->save();
	CMemorySerializer bytes;
	ASSERT_NO_THROW(bytes.oser & update);
	UnitChanges restored;
	ASSERT_NO_THROW(bytes.iser & restored);
	auto detached = elf->acquireState();
	detached->forestAmbush = {};
	detached->load(restored.data);
	EXPECT_EQ(detached->forestAmbush, elf->forestAmbush);
	auto legacy = restored.data;
	legacy["state"].Struct().erase("forestAmbush");
	detached->load(legacy);
	EXPECT_EQ(detached->forestAmbush, battle::ForestAmbushState{});
	CStackBasicDescriptor base(creatureByName("core:woodElf"), 5);
	CStack descriptor(&base, PlayerColor(0), 100, BattleSide::ATTACKER);
	descriptor.initialPosition = BattleHex(4, 5);
	descriptor.waiting = elf->waiting;
	descriptor.waitedThisTurn = elf->waitedThisTurn;
	descriptor.forestAmbush = elf->forestAmbush;
	CMemorySerializer stackBytes;
	ASSERT_NO_THROW(stackBytes.oser & descriptor);
	CStack loaded;
	stackBytes.iser.cb = gameState().get();
	ASSERT_NO_THROW(stackBytes.iser & loaded);
	loaded.localInit(battle());
	EXPECT_EQ(loaded.forestAmbush, elf->forestAmbush);
}

TEST_F(NewHorizonsForestAmbushTest, UnsupportedPacketAndCapturedPolicyRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges unit(elf->unitId(), UnitChanges::EOperation::UPDATE);
	unit.data = elf->save();
	update.changedStacks.push_back(unit);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(old.oser & update, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	auto context = newHorizonsHeroes::SpecialtySerializationContext::fromHandler(old.oser);
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(
		attackerSideHero->getPrimaryGrowthRules(), context), std::runtime_error);
}

TEST_F(NewHorizonsForestAmbushTest, NewCloneStartsWithIndependentStateAndKeepsOrdinaryFamilyEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	auto * clone = addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(3, 7), 5);
	ASSERT_NE(clone, nullptr);
	clone->cloned = true;
	EXPECT_EQ(clone->forestAmbush, battle::ForestAmbushState{});
	EXPECT_TRUE(newHorizonsForestAmbush::entitled(*battle(), clone));
	newHorizonsForestAmbush::acceptWait(*battle(), *clone);
	EXPECT_TRUE(clone->forestAmbush.concealed);
	clone->forestAmbush.receiveAttack();
	EXPECT_TRUE(elf->forestAmbush.concealed);
}

TEST_F(NewHorizonsForestAmbushTest, NativeBreathCollateralRevealsConcealedElf)
{
	breathEnemy = true;
	enemyCount = 1;
	elfCount = 20; // Survive the Dragon's creature-Attack advantage; this tests reveal while alive.
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	auto * primary = original(BattleSide::ATTACKER, SlotID(1));
	ASSERT_NE(primary, nullptr);
	activate(enemy);
	ASSERT_TRUE(attack(enemy, primary->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	const auto & hits = server.attacks.back().bsa;
	ASSERT_TRUE(std::ranges::any_of(hits, [this](const auto & hit)
		{ return hit.stackAttacked == elf->unitId() && hit.isSecondary(); }));
	EXPECT_TRUE(elf->alive());
	EXPECT_TRUE(elf->forestAmbush.attacked);
	EXPECT_FALSE(elf->forestAmbush.concealed);
}

#ifdef ENABLE_BATTLE_AI
namespace
{
class AmbushEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AmbushEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
template<typename T> std::vector<std::byte> ambushBytes(const T & value)
{
	CMemorySerializer bytes;
	bytes.oser & value;
	return bytes.extractBuffer();
}
}
TEST_F(NewHorizonsForestAmbushTest, HypotheticalWaitRevealAndSelectedDoubleShotMatchWithoutParentMutationOrRng)
{
	upgraded = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(elf);
	AmbushEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](auto *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	model->makeWait(elf);
	ASSERT_TRUE(model->getForUpdate(elf->unitId())->forestAmbush.concealed);
	auto sibling = std::make_shared<HypotheticBattle>(&env, model);
	auto branch = std::make_shared<HypotheticBattle>(&env, model);
	branch->nextTurn(elf->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto actor = branch->getForUpdate(elf->unitId());
	auto victim = branch->getForUpdate(enemy->unitId());
	ASSERT_TRUE(actor->forestAmbush.shotPending);
	const auto actorBefore = actor->save();
	const auto nativeBefore = elf->save();
	const auto rng = ambushBytes(*gameHandler->randomizer);
	const auto gameRng = ambushBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	DamageCache cache;
	cache.buildDamageCache(branch, BattleSide::ATTACKER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), victim.get(), 0, true),
		actor->getPosition(), cache, branch);
	ASSERT_NE(preview.attackerState, nullptr);
	EXPECT_FALSE(preview.attackerState->forestAmbush.shotPending);
	auto replay = std::make_shared<HypotheticBattle>(&env, branch);
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	exchange.trackAttack(preview, replay, replayCache);
	EXPECT_EQ(replay->getForUpdate(elf->unitId())->forestAmbush, preview.attackerState->forestAmbush);
	EXPECT_EQ(replay->getForUpdate(elf->unitId())->shots.available(), preview.attackerState->shots.available());
	EXPECT_EQ(actor->save(), actorBefore);
	EXPECT_TRUE(model->getForUpdate(elf->unitId())->forestAmbush.concealed);
	EXPECT_TRUE(sibling->getForUpdate(elf->unitId())->forestAmbush.concealed);
	EXPECT_EQ(elf->save(), nativeBefore);
	EXPECT_EQ(ambushBytes(*gameHandler->randomizer), rng);
	EXPECT_EQ(ambushBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
}

TEST_F(NewHorizonsForestAmbushTest, PreWaitCachedPreviewRefreshesOnlyTheSameGeometryInDelayedBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(elf);
	AmbushEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](auto *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto cached = AttackPossibility::evaluate(BattleAttackInfo(elf, enemy, 0, true),
		elf->getPosition(), cache, model);
	auto waited = std::make_shared<HypotheticBattle>(&env, model);
	waited->makeWait(elf);
	waited->nextTurn(elf->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto actor = waited->getForUpdate(elf->unitId());
	auto target = waited->getForUpdate(enemy->unitId());
	ASSERT_TRUE(actor->forestAmbush.shotPending);
	const auto fresh = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 0, true),
		cached.from, cache, waited);
	auto staleReplay = std::make_shared<HypotheticBattle>(&env, waited);
	auto freshReplay = std::make_shared<HypotheticBattle>(&env, waited);
	BattleExchangeVariant staleExchange, freshExchange;
	DamageCache staleCache, freshCache;
	staleExchange.trackAttack(cached, staleReplay, staleCache);
	freshExchange.trackAttack(fresh, freshReplay, freshCache);
	EXPECT_EQ(staleReplay->getForUpdate(enemy->unitId())->getAvailableHealth(),
		freshReplay->getForUpdate(enemy->unitId())->getAvailableHealth());
	EXPECT_EQ(staleReplay->getForUpdate(elf->unitId())->forestAmbush,
		freshReplay->getForUpdate(elf->unitId())->forestAmbush);
	EXPECT_TRUE(actor->forestAmbush.shotPending);
}

TEST_F(NewHorizonsForestAmbushTest, ConcealedPrimaryRefusesStaleSelectedReplayWithoutBranchOrRngMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(enemy);
	AmbushEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](auto *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	const auto old = AttackPossibility::evaluate(BattleAttackInfo(enemy, elf, 0, true), enemy->getPosition(), cache, model);
	auto branch = std::make_shared<HypotheticBattle>(&env, model);
	branch->makeWait(branch->battleGetUnitByID(elf->unitId()));
	auto actor = branch->getForUpdate(enemy->unitId());
	auto target = branch->getForUpdate(elf->unitId());
	const auto actorBefore = actor->save(), targetBefore = target->save();
	const auto rng = ambushBytes(*gameHandler->randomizer);
	const auto gameRng = ambushBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	EXPECT_EQ(exchange.trackAttack(old, branch, replayCache), 0);
	EXPECT_EQ(actor->save(), actorBefore);
	EXPECT_EQ(target->save(), targetBefore);
	EXPECT_EQ(ambushBytes(*gameHandler->randomizer), rng);
	EXPECT_EQ(ambushBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_FALSE(model->getForUpdate(elf->unitId())->forestAmbush.hasState());
}

TEST_F(NewHorizonsForestAmbushTest, BreathPreviewSelectedReplayAndNativeAgreeOnCollateralReveal)
{
	breathEnemy = true;
	enemyCount = 1;
	elfCount = 20; // Survive the Dragon's creature-Attack advantage; this tests reveal while alive.
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(enemy);
	auto * primary = original(BattleSide::ATTACKER, SlotID(1));
	AmbushEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](auto *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(enemy, primary, 0, false), enemy->getPosition(), cache, model);
	const auto found = std::ranges::find_if(preview.affectedUnits, [this](const auto & unit)
		{ return unit->unitId() == elf->unitId(); });
	ASSERT_NE(found, preview.affectedUnits.end());
	EXPECT_TRUE((*found)->forestAmbush.attacked);
	EXPECT_FALSE((*found)->forestAmbush.concealed);
	auto replay = std::make_shared<HypotheticBattle>(&env, model);
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	exchange.trackAttack(preview, replay, replayCache);
	EXPECT_EQ(replay->getForUpdate(elf->unitId())->forestAmbush, (*found)->forestAmbush);
	ASSERT_TRUE(attack(enemy, primary->getPosition()));
	EXPECT_EQ(elf->forestAmbush, (*found)->forestAmbush);
}
#endif

TEST_F(NewHorizonsForestAmbushTest, ContinuationsPreserveShotButExtraGenuineActivationExpiresIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(elf);
	ASSERT_TRUE(elf->forestAmbush.shotPending);
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION,
		BattleUnitTurnReason::ACTION_REJECTED})
	{
		activate(elf, reason);
		EXPECT_TRUE(elf->forestAmbush.shotPending);
		EXPECT_FALSE(elf->forestAmbush.concealed);
	}
	activate(elf, BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	EXPECT_FALSE(elf->forestAmbush.concealed);
}

TEST_F(NewHorizonsForestAmbushTest, RoundBoundaryClearsAttackHistoryAndAllowsOneFreshWait)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	newHorizonsForestAmbush::receiveAttack(*battle(), *elf);
	BattleNextRound round;
	round.battleID = BattleID(0);
	gameHandler->sendAndApply(round);
	EXPECT_EQ(elf->forestAmbush, battle::ForestAmbushState{});
	ASSERT_NO_FATAL_FAILURE(conceal());
}

TEST_F(NewHorizonsForestAmbushTest, IvorDoesNotConcealCreaturesOutsideTheExactElfFamily)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * centaur = original(BattleSide::ATTACKER, SlotID(1));
	ASSERT_NE(centaur, nullptr);
	ASSERT_FALSE(newHorizonsForestAmbush::entitled(*battle(), centaur));
	activate(centaur);
	ASSERT_TRUE(submit(centaur, BattleAction::makeWait(centaur)));
	EXPECT_FALSE(centaur->forestAmbush.concealed);
}

TEST_F(NewHorizonsForestAmbushTest, MalformedConcealedUnitUpdateRejectsBeforeResettingAnyExistingValues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	const auto before = elf->save();
	for(int kind = 0; kind < 3; ++kind)
	{
		auto malformed = before;
		if(kind == 0) malformed["state"]["waiting"].Bool() = false;
		if(kind == 1) malformed["state"]["forestAmbush"]["concealed"].Integer() = 1;
		if(kind == 2) malformed["state"]["forestAmbush"]["shotPending"].Bool() = true;
		EXPECT_THROW(elf->load(malformed), std::runtime_error);
		EXPECT_EQ(elf->save(), before);
	}
}

TEST_F(NewHorizonsForestAmbushTest, CapturedEnabledIdentitySurvivesTwoWorldSaveLoads)
{
	ASSERT_NO_FATAL_FAILURE(verifyTwoWorldLoads(true));
}

TEST_F(NewHorizonsForestAmbushTest, HistoricalCapturedAbsenceSurvivesTwoWorldSaveLoads)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(verifyTwoWorldLoads(false));
}

TEST_F(NewHorizonsForestAmbushTest, CapturedIdentityRejectsOlderWorldWriterBeforeItsPrefix)
{
	startGame();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(old.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsForestAmbushTest, ConcealedOverwatchUsesExistingLuckThenRevealsWithoutRearmingAtDelayedActivation)
{
	overwatchCounterfire = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto baseLuck = shotLuck();
	ASSERT_NO_FATAL_FAILURE(conceal());
	ASSERT_TRUE(battle()->battleOverwatchReady(elf));
	EXPECT_EQ(shotLuck(), std::min(10, baseLuck + 5));
	const auto ammo = elf->shots.available();
	const auto counters = elf->counterAttacks.available();
	const auto firstAttack = server.attacks.size();
	activate(enemy);
#ifdef ENABLE_BATTLE_AI
	AmbushEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](auto *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	auto branch = std::make_shared<HypotheticBattle>(&env, model);
	const auto rng = ambushBytes(*gameHandler->randomizer);
	const auto projected = branch->projectVoluntaryMovement(enemy->unitId(), BattleHex(11, 5));
	ASSERT_EQ(projected.size(), 1u);
	const auto projectedForest = branch->getForUpdate(elf->unitId())->forestAmbush;
	EXPECT_FALSE(projectedForest.concealed);
	EXPECT_FALSE(projectedForest.shotPending);
	EXPECT_TRUE(model->getForUpdate(elf->unitId())->forestAmbush.concealed);
	EXPECT_EQ(ambushBytes(*gameHandler->randomizer), rng);
#endif
	ASSERT_TRUE(submit(enemy, BattleAction::makeMove(enemy, BattleHex(11, 5))));
	const auto shotsBy = [&](uint32_t actor)
	{
		return std::count_if(server.attacks.begin() + firstAttack, server.attacks.end(),
			[actor](const BattleAttack & attack) { return attack.stackAttacking == actor && attack.shot(); });
	};
	EXPECT_EQ(shotsBy(elf->unitId()), 1);
	EXPECT_EQ(shotsBy(enemy->unitId()), 0) << "existing counter-flag recursion guard excludes answering Overwatch";
	EXPECT_FALSE(elf->forestAmbush.concealed);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	EXPECT_EQ(elf->shots.available(), ammo - 1);
	EXPECT_EQ(elf->counterAttacks.available(), counters);
	EXPECT_TRUE(battle()->battleCanShoot(enemy, elf->getPosition())) << "Forest Ambush no longer protects the revealed source";
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(elf->forestAmbush, projectedForest);
#endif
	activate(elf);
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	EXPECT_EQ(shotLuck(), baseLuck);
}

TEST_F(NewHorizonsForestAmbushTest, RevealedDelayedFirstShotCanReceiveOrdinaryCounterfire)
{
	overwatchCounterfire = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(conceal());
	activate(elf);
	ASSERT_FALSE(elf->forestAmbush.concealed);
	ASSERT_TRUE(elf->forestAmbush.shotPending);
	const auto first = server.attacks.size();
	ASSERT_TRUE(submit(elf, BattleAction::makeShotAttack(elf, enemy)));
	EXPECT_TRUE(std::any_of(server.attacks.begin() + first, server.attacks.end(), [this](const auto & attack)
		{ return attack.stackAttacking == enemy->unitId() && attack.shot() && attack.counter(); }));
	EXPECT_FALSE(elf->forestAmbush.shotPending);
	EXPECT_FALSE(elf->forestAmbush.concealed);
}
