/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../AI/BattleAI/ProjectedBestAction.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsFactionSpellDamage.h"
#include "../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"

namespace
{
namespace faction = newHorizonsFactionSpellDamage;
class RevisionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RevisionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class FactionRevisionProjectionTest : public HeroCommandFixture
{
protected:
	std::unique_ptr<RevisionEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
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
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void select(std::string_view skill, const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(skill))),
			rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({std::string(skill), perk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(skill), perk));
	}
	void prepare(std::string_view skill, const char * basic, const char * advanced = nullptr,
		const char * expert = nullptr, bool useBattleStartActor = false)
	{
		startGame();
		const char * hero = skill == "new-horizons:metamagic" ? "core:neela"
			: skill == "new-horizons:shroudOfMalassa" ? "core:lorelei" : "core:brissa";
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		if(basic) { ASSERT_NO_FATAL_FAILURE(select(skill, basic, MasteryLevel::BASIC)); }
		else attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(skill))),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(advanced) { ASSERT_NO_FATAL_FAILURE(select(skill, advanced, MasteryLevel::ADVANCED)); }
		if(expert) { ASSERT_NO_FATAL_FAILURE(select(skill, expert, MasteryLevel::EXPERT)); }
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		if(useBattleStartActor)
		{
			attackerSideHero->clearSlots();
			ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:angel"), 1000));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits())
		{
			if(useBattleStartActor && unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				actor = battle()->getStack(unit->unitId(), false);
			else
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		}
		gameHandler->sendAndApply(remove);
		if(!useBattleStartActor)
			actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(79), 1000);
		ASSERT_NE(actor, nullptr);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 1000);
		beginCombat();
		environment = std::make_unique<RevisionEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	std::shared_ptr<HypotheticBattle> branch()
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback);
	}
	std::vector<std::byte> rng() const
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	}
	StacksInjured injury(const battle::Unit & target, int64_t damage, HypotheticBattle & model)
	{
		StacksInjured packet;
		packet.battleID = model.getBattleID();
		auto & hit = packet.stacks.emplace_back();
		hit.stackAttacked = target.unitId();
		hit.damageAmount = damage;
		CStack::prepareAttacked(hit, *model.getServerCallback()->getRNG(), target.acquireState());
		return packet;
	}
};
}

TEST_F(FactionRevisionProjectionTest, PassiveMetamagicInjuryPacketsStayPrivateAcrossBranchGenerations)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto parent = branch();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto health = actor->getAvailableHealth();
	const auto random = rng();
	auto packet = injury(*child->battleGetUnitByID(actor->unitId()), 10, *child);
	child->getServerCallback()->apply(packet);
	EXPECT_EQ(child->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health - 10);
	EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(sibling->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(actor->getAvailableHealth(), health);
	auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
	packet = injury(*child->battleGetUnitByID(actor->unitId()), 10, *child);
	child->getServerCallback()->apply(packet);
	EXPECT_EQ(grandchild->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health - 10);
	EXPECT_EQ(child->battleGetUnitByID(actor->unitId())->getAvailableHealth(), health - 20);
	EXPECT_EQ(rng(), random);
}

TEST_F(FactionRevisionProjectionTest, ExportSubjectCapturesUnitsAndRejectsMutationUnderPassiveMetamagic)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto omniscient = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto exported = InitialExportBattleSubject::capture(omniscient, *battle());
	const auto before = exported->battleGetUnitByID(actor->unitId())->getPosition();
	auto projected = std::make_shared<HypotheticBattle>(environment.get(), exported);
	projected->moveUnit(actor->unitId(), BattleHex(82));
	EXPECT_EQ(exported->battleGetUnitByID(actor->unitId())->getPosition(), before);
	EXPECT_EQ(actor->getPosition(), before);
	EXPECT_THROW(exported->moveUnit(actor->unitId(), BattleHex(82)), std::logic_error);
}

TEST_F(FactionRevisionProjectionTest, PassiveMetamagicRoundTransitionMatchesLiveWithoutCreatingResources)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto projected = branch();
	auto sibling = branch();
	const auto oldRound = battle()->getRound();
	const auto oldGuardian = actor->acquireState()->guardianSpiritHitPoints;
	const auto mana = attackerSideHero->getManaAvailable();
	const auto random = rng();
	projected->nextRound();
	EXPECT_EQ(sibling->getRound(), oldRound);
	EXPECT_EQ(battle()->getRound(), oldRound);
	EXPECT_EQ(actor->acquireState()->guardianSpiritHitPoints, oldGuardian);
	BattleNextRound next;
	next.battleID = battle()->getBattleID();
	gameHandler->sendAndApply(next);
	EXPECT_EQ(projected->getRound(), battle()->getRound());
	EXPECT_EQ(projected->getForUpdate(actor->unitId())->guardianSpiritHitPoints, oldGuardian);
	EXPECT_EQ(actor->acquireState()->guardianSpiritHitPoints, oldGuardian);
	EXPECT_EQ(projected->getProjectedManaAvailable(BattleSide::ATTACKER), mana);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(rng(), random);
}

TEST_F(FactionRevisionProjectionTest, PassiveMetamagicCastEvaluationMatchesSingleResolvedHitWithoutSourceMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto projected = branch();
	auto sibling = branch();
	const auto random = rng();
	const auto health = victim->getAvailableHealth();
	spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO,
		SpellID(SpellID::MAGIC_ARROW).toSpell());
	const battle::Target target{battle::Destination(projected->battleGetUnitByID(victim->unitId()))};
	auto mechanics = cast.getSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	const auto damage = mechanics->adjustEffectValue(target.front().unitValue);
	cast.castEval(projected->getServerCallback(), target);
	EXPECT_EQ(victim->getAvailableHealth(), health);
	EXPECT_EQ(sibling->battleGetUnitByID(victim->unitId())->getAvailableHealth(), health);
	EXPECT_EQ(projected->battleGetUnitByID(victim->unitId())->getAvailableHealth(), health - damage);
	EXPECT_EQ(rng(), random);
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(projected->battleGetUnitByID(victim->unitId())->getAvailableHealth(), victim->getAvailableHealth());
	EXPECT_TRUE(projected->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(sibling->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(FactionRevisionProjectionTest, ExposureUsesFlankingHeroProvenanceAndExpiresAtVictimActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:shroudOfMalassa", "new-horizons:shroudOfMalassa.shadowHex",
		"new-horizons:shroudOfMalassa.exposedWeakness"));
	// An attacker to the right is behind the defender's left-facing front.
	auto * flanker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(81), 10);
	auto projected = branch();
	auto sibling = branch();
	auto target = projected->getForUpdate(victim->unitId());
	auto striker = projected->getForUpdate(flanker->unitId());
	BattleAttackInfo attack(striker.get(), target.get(), 0, false);
	ASSERT_TRUE(projected->battleIsShroudFlankingAttack(attack));
	PhysicalStrikeProjection strike(projected.get());
	int64_t damage = 1;
	strike.damage(*projected, attack, *target, damage, battle::DamageProvenance::PHYSICAL_CREATURE);
	strike.finish(striker->unitId());
	EXPECT_EQ(faction::bonusPercent(*projected, attackerSideHero, target.get()), 35);
	EXPECT_EQ(faction::bonusPercent(*sibling, attackerSideHero, sibling->battleGetUnitByID(victim->unitId())), 15);
	EXPECT_EQ(faction::bonusPercent(*projected, defenderSideHero, target.get()), 0);
	projected->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(faction::bonusPercent(*projected, attackerSideHero, target.get()), 15);
}

TEST_F(FactionRevisionProjectionTest, ConduitAndAreaBonusAreCapturedWithoutRebornStacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalConduit",
		"new-horizons:elementalRebirth.elementalCataclysm"));
	auto projected = branch();
	const auto random = rng();
	const auto ordinary = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, projected.get(), true, SpellID::MAGIC_ARROW);
	const auto area = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, projected.get(), true, SpellID::FIREBALL);
	const auto chain = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, projected.get(), true, SpellID::CHAIN_LIGHTNING);
	EXPECT_EQ(ordinary.factionSpellDamageBonus, 15);
	EXPECT_EQ(area.factionSpellDamageBonus, 40);
	EXPECT_EQ(chain.factionSpellDamageBonus, 15);
	EXPECT_EQ(projected->getElementalAttunementMask(BattleSide::ATTACKER), 0);
	EXPECT_EQ(rng(), random);
}

TEST_F(FactionRevisionProjectionTest, PassiveMetamagicProjectionConsumesOnlyItsPrivateOrdinaryAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto projected = branch();
	auto sibling = branch();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto random = rng();
	ASSERT_TRUE(projected->projectHeroSpellAllowance(BattleSide::ATTACKER, SpellID::MAGIC_ARROW, victim->unitId()));
	EXPECT_FALSE(projected->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::MAGIC_ARROW));
	EXPECT_TRUE(sibling->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::MAGIC_ARROW));
	EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::MAGIC_ARROW));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(rng(), random);
}

TEST_F(FactionRevisionProjectionTest, ConfluxDeathChangesNeitherConduitStrengthNorBattlefieldRoster)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalConduit"));
	auto projected = branch();
	auto sibling = branch();
	const auto random = rng();
	const auto count = projected->battleGetAllUnits(false).size();
	const auto original = projected->getForUpdate(actor->unitId());
	auto death = injury(*original, original->getAvailableHealth(), *projected);
	projected->getServerCallback()->apply(death);
	EXPECT_FALSE(projected->battleGetUnitByID(actor->unitId())->alive());
	EXPECT_EQ(projected->battleGetAllUnits(false).size(), count);
	EXPECT_EQ(newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, projected.get(), true,
		SpellID::MAGIC_ARROW).factionSpellDamageBonus, 15);
	EXPECT_EQ(projected->getElementalAttunementMask(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(sibling->battleGetUnitByID(actor->unitId())->alive());
	EXPECT_TRUE(actor->alive());
	EXPECT_EQ(rng(), random);
}

TEST_F(FactionRevisionProjectionTest, PassiveMetamagicEntryKeepsBothCreationPathsEquivalentWithoutInitiativeGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:metamagic", nullptr));
	auto projected = branch();
	auto sibling = branch();
	battle::ProjectedBattleWorldArena arena(*battle());
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO,
		SpellID(SpellID::MAGIC_ARROW).toSpell());
	const battle::Target target{battle::Destination(projected->battleGetUnitByID(victim->unitId()))};
	ASSERT_TRUE(cast.getSpell()->battleMechanics(&cast)->canBeCastAt(target));
	cast.castEval(projected->getServerCallback(), target);
	arena.copyEnvironmentValues(battle::ProjectedUnitEnvironmentState::capture(*projected));
	battle::UnitInfo info;
	info.id = arena.nextUnitId();
	info.type = creatureByName("core:earthElemental");
	info.count = 10;
	info.side = BattleSide::ATTACKER;
	info.position = BattleHex(83);
	info.summoned = true;
	JsonNode data;
	info.save(data);
	const auto & arenaUnit = arena.addUnit(info.id, data);
	projected->addUnit(info.id, data);
	const auto * branchUnit = projected->battleGetUnitByID(info.id);
	ASSERT_NE(branchUnit, nullptr);
	EXPECT_EQ(branchUnit->getInitiative(), arenaUnit.getInitiative());
	EXPECT_EQ(branchUnit->getMaxHealth(), arenaUnit.getMaxHealth());
	EXPECT_EQ(branchUnit->getAvailableHealth(), arenaUnit.getAvailableHealth());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(sibling->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(sibling->battleGetUnitByID(info.id), nullptr);
}
