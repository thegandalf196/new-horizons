/*
 * NewHorizonsHexSnapshotAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/NewHorizonsHexOfPain.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
// A local virtual hook measures this actor's snapshots without a global counter
// or any change to the production forecast/state ownership contract.
class SnapshotCountingStack final : public StackWithBonuses
{
public:
	mutable unsigned snapshotCalls = 0;
	SnapshotCountingStack(const HypotheticBattle * owner, const battle::CUnitState * source)
		: StackWithBonuses(owner, source) {}
	std::shared_ptr<battle::CUnitState> acquireState() const override
	{
		++snapshotCalls;
		return StackWithBonuses::acquireState();
	}
};

class HexSnapshotEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HexSnapshotEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHexSnapshotAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepareAdditionalScene(bool withHex, bool withCleave, CStack * & actor,
		CStack * & target, CStack * & secondary)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		const auto spell = SpellID(SpellID::decode(std::string(newHorizonsHexOfPainAI::SPELL_ID)));
		auto * caster = withCleave ? defenderSideHero : attackerSideHero;
		caster->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		caster->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(caster, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		caster->addSpellToSpellbook(spell);
		setTestSpellPointTotal(caster, 1000);
		if(withCleave)
		{
			const SecondarySkill offense(SecondarySkill::decode(newHorizonsOffense::SKILL));
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"});
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(withCleave ? "core:peasant" : "core:pikeman"),
			BattleHex(7, 5), withCleave ? 1000 : 100);
		target = addStack(BattleSide::DEFENDER, creatureByName(withCleave ? "core:peasant" : "core:pikeman"),
			BattleHex(8, 5), withCleave ? 1 : 101);
		secondary = withCleave
			? addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(9, 5), 10000) : nullptr;
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		forceMaximumDamage(actor);
		forceMaximumDamage(target);
		if(withCleave)
			blockRetaliation(actor);
		else
		{
			ASSERT_FALSE(actor->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
			ASSERT_TRUE(target->ableToRetaliate());
		}
		battle()->nextRound();
		if(withHex)
		{
			ASSERT_TRUE(castOn(caster, spell, withCleave ? actor : target));
		}
		ASSERT_EQ(newHorizonsHexOfPainAI::hasEffect(withCleave ? actor : target), withHex);
	}

	std::vector<std::byte> randomizerSnapshot() const
	{
		CMemorySerializer memory;
		memory.oser & *gameHandler->randomizer;
		return memory.extractBuffer();
	}

	void checkAppliedRetaliation(bool withHex)
	{
		CStack * actor = nullptr;
		CStack * target = nullptr;
		CStack * secondary = nullptr;
		ASSERT_NO_FATAL_FAILURE(prepareAdditionalScene(withHex, false, actor, target, secondary));
		HexSnapshotEnvironment environment(gameState());
		auto parent = std::make_shared<HypotheticBattle>(&environment,
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		const auto liveActor = actor->acquireState()->save();
		const auto liveTarget = target->acquireState()->save();
		const auto parentActor = parent->getForUpdate(actor->unitId())->save();
		const auto parentTarget = parent->getForUpdate(target->unitId())->save();
		const auto siblingActor = sibling->getForUpdate(actor->unitId())->save();
		const auto siblingTarget = sibling->getForUpdate(target->unitId())->save();
		const auto orderBefore = parent->getHeroOrderStates(BattleSide::ATTACKER);
		const auto focusBefore = parent->getFocusFireState(BattleSide::ATTACKER);
		const auto rngBefore = randomizerSnapshot();

		// Each case is an independent real branch: one surviving response, then
		// (only with Hex) a normally wounded retaliator killed by its own Hex.
		for(const bool lethalHex : {false, true})
		{
			if(lethalHex && !withHex)
				continue;
			SCOPED_TRACE(lethalHex ? "lethal reactive Hex" : "surviving retaliation");
			auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
			auto source = branch->getForUpdate(actor->unitId());
			auto victim = branch->getForUpdate(target->unitId());
			DamageCache cache;
			cache.buildDamageCache(branch, BattleSide::ATTACKER);
			const auto fullHealthDamage = cache.getDamage(source.get(), victim.get(), branch);
			ASSERT_GT(fullHealthDamage, 0);
			ASSERT_LT(fullHealthDamage + 1, victim->getAvailableHealth());
			if(lethalHex)
			{
				// Use the real health damage operation on this branch, not a
				// fabricated count/HP ledger or a live-world post-state repair.
				int64_t wound = victim->getAvailableHealth() - fullHealthDamage - 1;
				victim->damage(wound, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			}
			ASSERT_TRUE(victim->ableToRetaliate());
			ASSERT_FALSE(source->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
			ASSERT_FALSE(victim->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
			ASSERT_FALSE(branch->hasReadyNativeRebirth(victim.get()));
			const auto sourceBefore = source->save();
			const auto victimBefore = victim->save();
			const auto attackerAllowance = branch->getHeroActionAllowances(BattleSide::ATTACKER);
			const auto defenderAllowance = branch->getHeroActionAllowances(BattleSide::DEFENDER);
			const auto defenderOrders = branch->getHeroOrderStates(BattleSide::DEFENDER);
			const auto defenderFocus = branch->getFocusFireState(BattleSide::DEFENDER);
			const auto markerOwner = victim->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
			int64_t flat = 0;
			int64_t share = 0;
			std::vector<JsonNode> markerBefore;
			for(const auto & bonus : *markerOwner)
			{
				markerBefore.push_back(bonus->toJsonNode());
				if(bonus->sid.toString() == newHorizonsHexOfPainAI::SPELL_ID)
				{
					flat = bonus->val;
					share = bonus->parameters->toCustom<JsonNode>()["damageSharePercent"].Integer();
				}
			}
			ASSERT_EQ(newHorizonsHexOfPainAI::hasEffect(victim.get()), withHex);
			if(withHex)
			{
				ASSERT_GT(flat, 0);
				ASSERT_GT(share, 0);
			}

			// Independently advance ordinary physical health/ammo on a sibling,
			// price the reactive loss BEFORE applying it, and never call trackAttack
			// or projectHexOfPainStrike to construct the expected score.
			auto expected = std::make_shared<HypotheticBattle>(&environment, branch);
			auto expectedActor = expected->getForUpdate(actor->unitId());
			auto expectedTarget = expected->getForUpdate(target->unitId());
			DamageCache expectedCache;
			expectedCache.buildDamageCache(expected, BattleSide::ATTACKER);
			int64_t mainDamage = expectedCache.getDamage(expectedActor.get(), expectedTarget.get(), expected);
			ASSERT_EQ(mainDamage, fullHealthDamage);
			ASSERT_LT(mainDamage, expectedTarget->getAvailableHealth());
			const float mainValue = AttackPossibility::calculateDamageReduce(expectedActor.get(), expectedTarget.get(),
				mainDamage, expectedCache, expected);
			expectedTarget->damage(mainDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			expectedActor->afterAttack(false, false, true);
			ASSERT_TRUE(expectedTarget->ableToRetaliate());
			BattleAttackInfo counter(expectedTarget.get(), expectedActor.get(), 0, false);
			counter.retaliation = true;
			int64_t counterDamage = expected->battleExpectedLuckDamage(counter);
			ASSERT_GT(counterDamage, 0);
			ASSERT_LT(counterDamage, expectedActor->getAvailableHealth());
			const float counterValue = AttackPossibility::calculateDamageReduce(expectedTarget.get(), expectedActor.get(),
				counterDamage, expectedCache, expected);
			expectedActor->damage(counterDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			expectedTarget->afterAttack(false, true, true);
			const auto beforeHex = expectedTarget->acquireState();
			int64_t hexDamage = withHex ? flat + counterDamage * share / 100 : 0;
			ASSERT_EQ(attackerSideHero->getSpellBonus(SpellID(SpellID::decode(std::string(newHorizonsHexOfPainAI::SPELL_ID))).toSpell(),
				hexDamage, expectedTarget.get()), hexDamage);
			const auto actualHexDamage = std::min(hexDamage, beforeHex->getAvailableHealth());
			const float hexValue = actualHexDamage > 0
				? AttackPossibility::calculateDamageReduce(nullptr, beforeHex.get(), actualHexDamage, expectedCache, expected) : 0;
			if(withHex)
			{
				ASSERT_GT(hexValue, 0);
				if(lethalHex)
				{
					ASSERT_GE(hexDamage, beforeHex->getAvailableHealth());
				}
				else
				{
					ASSERT_LT(hexDamage, beforeHex->getAvailableHealth());
				}
				expectedTarget->damage(hexDamage, false, battle::DamageProvenance::SPELL);
			}
			BattleExchangeVariant exchange;
			const float score = exchange.trackAttack(source, victim, false, true, cache, branch, false, true);
			EXPECT_FLOAT_EQ(score, mainValue + hexValue - counterValue);
			EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, mainValue + hexValue);
			EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, counterValue);
			EXPECT_EQ(source->save(), expectedActor->save());
			EXPECT_EQ(victim->save(), expectedTarget->save());
			EXPECT_EQ(victim->alive(), !lethalHex);
			EXPECT_LT(source->getAvailableHealth(), actor->getAvailableHealth());
			EXPECT_EQ(victim->counterAttacks.available(), beforeHex->counterAttacks.available());
			std::vector<JsonNode> markerAfter;
			const auto afterOwner = victim->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
			for(const auto & bonus : *afterOwner)
				markerAfter.push_back(bonus->toJsonNode());
			EXPECT_EQ(markerAfter, markerBefore);
			EXPECT_NE(source->save(), sourceBefore);
			EXPECT_NE(victim->save(), victimBefore);
			EXPECT_EQ(randomizerSnapshot(), rngBefore);
			EXPECT_EQ(actor->acquireState()->save(), liveActor);
			EXPECT_EQ(target->acquireState()->save(), liveTarget);
			EXPECT_EQ(parent->getForUpdate(actor->unitId())->save(), parentActor);
			EXPECT_EQ(parent->getForUpdate(target->unitId())->save(), parentTarget);
			EXPECT_EQ(sibling->getForUpdate(actor->unitId())->save(), siblingActor);
			EXPECT_EQ(sibling->getForUpdate(target->unitId())->save(), siblingTarget);
			EXPECT_EQ(parent->getHeroOrderStates(BattleSide::ATTACKER), orderBefore);
			EXPECT_EQ(parent->getFocusFireState(BattleSide::ATTACKER), focusBefore);
			EXPECT_EQ(branch->getHeroActionAllowances(BattleSide::ATTACKER), attackerAllowance);
			EXPECT_EQ(branch->getHeroActionAllowances(BattleSide::DEFENDER), defenderAllowance);
			EXPECT_EQ(branch->getHeroOrderStates(BattleSide::ATTACKER), orderBefore);
			EXPECT_EQ(branch->getHeroOrderStates(BattleSide::DEFENDER), defenderOrders);
			EXPECT_EQ(branch->getFocusFireState(BattleSide::ATTACKER), focusBefore);
			EXPECT_EQ(branch->getFocusFireState(BattleSide::DEFENDER), defenderFocus);
		}
	}

	void checkActiveHexCleave()
	{
		CStack * actor = nullptr;
		CStack * target = nullptr;
		CStack * secondary = nullptr;
		ASSERT_NO_FATAL_FAILURE(prepareAdditionalScene(true, true, actor, target, secondary));
		ASSERT_NE(secondary, nullptr);
		HexSnapshotEnvironment environment(gameState());
		auto parent = std::make_shared<HypotheticBattle>(&environment,
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		const auto liveActor = actor->acquireState()->save();
		const auto liveTarget = target->acquireState()->save();
		const auto liveSecondary = secondary->acquireState()->save();
		const auto parentActor = parent->getForUpdate(actor->unitId())->save();
		const auto parentTarget = parent->getForUpdate(target->unitId())->save();
		const auto parentSecondary = parent->getForUpdate(secondary->unitId())->save();
		const auto siblingActor = sibling->getForUpdate(actor->unitId())->save();
		const auto siblingTarget = sibling->getForUpdate(target->unitId())->save();
		const auto siblingSecondary = sibling->getForUpdate(secondary->unitId())->save();
		const auto rngBefore = randomizerSnapshot();
		for(const bool evaluateOnly : {true, false})
		{
			SCOPED_TRACE(evaluateOnly ? "active Hex Cleave probe" : "active Hex applied Cleave");
			auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
			auto source = branch->getForUpdate(actor->unitId());
			auto victim = branch->getForUpdate(target->unitId());
			auto extraVictim = branch->getForUpdate(secondary->unitId());
			const auto sourceBefore = source->save();
			const auto victimBefore = victim->save();
			const auto extraBefore = extraVictim->save();
			const auto attackerAllowance = branch->getHeroActionAllowances(BattleSide::ATTACKER);
			const auto defenderAllowance = branch->getHeroActionAllowances(BattleSide::DEFENDER);
			const auto attackerOrders = branch->getHeroOrderStates(BattleSide::ATTACKER);
			const auto defenderOrders = branch->getHeroOrderStates(BattleSide::DEFENDER);
			const auto attackerFocus = branch->getFocusFireState(BattleSide::ATTACKER);
			const auto defenderFocus = branch->getFocusFireState(BattleSide::DEFENDER);
			const auto markerOwner = source->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
			ASSERT_EQ(markerOwner->size(), 1u);
			const auto markerBefore = markerOwner->front()->toJsonNode();
			const int64_t flat = markerOwner->front()->val;
			const int64_t share = markerOwner->front()->parameters->toCustom<JsonNode>()["damageSharePercent"].Integer();
			ASSERT_GT(flat, 0);
			ASSERT_GT(share, 0);
			ASSERT_TRUE(branch->battleCanTriggerCleave(source.get()));
			const auto * selected = branch->battleSelectCleaveTarget(source.get(), victim.get());
			ASSERT_NE(selected, nullptr);
			ASSERT_EQ(selected->unitId(), extraVictim->unitId());
			ASSERT_FALSE(branch->hasReadyNativeRebirth(victim.get()));
			ASSERT_FALSE(source->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));

			auto expected = std::make_shared<HypotheticBattle>(&environment, branch);
			auto expectedActor = expected->getForUpdate(actor->unitId());
			auto expectedTarget = expected->getForUpdate(target->unitId());
			auto expectedSecondary = expected->getForUpdate(secondary->unitId());
			DamageCache expectedCache;
			expectedCache.buildDamageCache(expected, BattleSide::ATTACKER);
			int64_t mainDamage = expectedCache.getDamage(expectedActor.get(), expectedTarget.get(), expected);
			ASSERT_GE(mainDamage, expectedTarget->getAvailableHealth());
			const auto mainHealthLoss = expectedTarget->getAvailableHealth();
			const float mainValue = AttackPossibility::calculateDamageReduce(expectedActor.get(), expectedTarget.get(),
				mainHealthLoss, expectedCache, expected);
			auto projectedActor = evaluateOnly ? expectedActor->acquireState()
				: std::static_pointer_cast<battle::CUnitState>(expectedActor);
			if(!evaluateOnly)
				expectedTarget->damage(mainDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			projectedActor->afterAttack(false, false, true);
			const auto beforeMainHex = projectedActor->acquireState();
			int64_t mainHexDamage = flat + mainHealthLoss * share / 100;
			ASSERT_LT(mainHexDamage, projectedActor->getAvailableHealth());
			const auto spell = SpellID(SpellID::decode(std::string(newHorizonsHexOfPainAI::SPELL_ID)));
			ASSERT_EQ(defenderSideHero->getSpellBonus(spell.toSpell(), mainHexDamage, projectedActor.get()), mainHexDamage);
			const float mainHexValue = AttackPossibility::calculateDamageReduce(nullptr, beforeMainHex.get(),
				mainHexDamage, expectedCache, expected);
			ASSERT_GT(mainHexValue, 0);
			// The existing read-only probe prices Hex on its isolated event branch;
			// it does not apply that event's HP to its post-attack attacker copy.
			if(!evaluateOnly)
				projectedActor->damage(mainHexDamage, false, battle::DamageProvenance::SPELL);

			BattleAttackInfo cleaveAttack(projectedActor.get(), expectedSecondary.get(), 0, false);
			cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
			int64_t cleaveDamage = expected->battleExpectedLuckDamage(cleaveAttack);
			ASSERT_GT(cleaveDamage, 0);
			ASSERT_LT(cleaveDamage, expectedSecondary->getAvailableHealth());
			const float cleaveValue = AttackPossibility::calculateDamageReduce(projectedActor.get(), expectedSecondary.get(),
				cleaveDamage, expectedCache, expected);
			if(!evaluateOnly)
				expectedSecondary->damage(cleaveDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			projectedActor->cleaveUsedThisActivation = true;
			projectedActor->afterAttack(false, false, true);
			const auto beforeCleaveHex = projectedActor->acquireState();
			int64_t cleaveHexDamage = flat + cleaveDamage * share / 100;
			ASSERT_LT(cleaveHexDamage, beforeCleaveHex->getAvailableHealth());
			ASSERT_EQ(defenderSideHero->getSpellBonus(spell.toSpell(), cleaveHexDamage, projectedActor.get()), cleaveHexDamage);
			const float cleaveHexValue = AttackPossibility::calculateDamageReduce(nullptr, beforeCleaveHex.get(),
				cleaveHexDamage, expectedCache, expected);
			ASSERT_GT(cleaveHexValue, 0);
			if(!evaluateOnly)
				projectedActor->damage(cleaveHexDamage, false, battle::DamageProvenance::SPELL);
			const float ownHexValue = mainHexValue + cleaveHexValue;

			DamageCache cache;
			cache.buildDamageCache(branch, BattleSide::ATTACKER);
			BattleExchangeVariant exchange;
			const float score = exchange.trackAttack(source, victim, false, true, cache, branch, evaluateOnly, false);
			EXPECT_FLOAT_EQ(score, mainValue + cleaveValue - ownHexValue);
			EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, ownHexValue);
			EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, evaluateOnly ? 0 : mainValue + cleaveValue);
			if(evaluateOnly)
			{
				EXPECT_EQ(source->save(), sourceBefore);
				EXPECT_EQ(victim->save(), victimBefore);
				EXPECT_EQ(extraVictim->save(), extraBefore);
			}
			else
			{
				EXPECT_EQ(source->save(), expectedActor->save());
				EXPECT_EQ(victim->save(), expectedTarget->save());
				EXPECT_EQ(extraVictim->save(), expectedSecondary->save());
				EXPECT_TRUE(source->cleaveUsedThisActivation);
				EXPECT_FALSE(victim->alive());
				EXPECT_GT(extraVictim->getAvailableHealth(), 0);
				EXPECT_LT(extraVictim->getAvailableHealth(), secondary->getAvailableHealth());
			}
			const auto afterOwner = source->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
			ASSERT_EQ(afterOwner->size(), 1u);
			EXPECT_EQ(afterOwner->front()->toJsonNode(), markerBefore);
			EXPECT_EQ(randomizerSnapshot(), rngBefore);
			EXPECT_EQ(actor->acquireState()->save(), liveActor);
			EXPECT_EQ(target->acquireState()->save(), liveTarget);
			EXPECT_EQ(secondary->acquireState()->save(), liveSecondary);
			EXPECT_EQ(parent->getForUpdate(actor->unitId())->save(), parentActor);
			EXPECT_EQ(parent->getForUpdate(target->unitId())->save(), parentTarget);
			EXPECT_EQ(parent->getForUpdate(secondary->unitId())->save(), parentSecondary);
			EXPECT_EQ(sibling->getForUpdate(actor->unitId())->save(), siblingActor);
			EXPECT_EQ(sibling->getForUpdate(target->unitId())->save(), siblingTarget);
			EXPECT_EQ(sibling->getForUpdate(secondary->unitId())->save(), siblingSecondary);
			EXPECT_EQ(branch->getHeroActionAllowances(BattleSide::ATTACKER), attackerAllowance);
			EXPECT_EQ(branch->getHeroActionAllowances(BattleSide::DEFENDER), defenderAllowance);
			EXPECT_EQ(branch->getHeroOrderStates(BattleSide::ATTACKER), attackerOrders);
			EXPECT_EQ(branch->getHeroOrderStates(BattleSide::DEFENDER), defenderOrders);
			EXPECT_EQ(branch->getFocusFireState(BattleSide::ATTACKER), attackerFocus);
			EXPECT_EQ(branch->getFocusFireState(BattleSide::DEFENDER), defenderFocus);
		}
	}

	void checkMainAttack(bool withHex, bool evaluateOnly, bool shooting = false, bool withCleave = false)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		const auto spell = SpellID(SpellID::decode("new-horizons:hexOfPain"));
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(defenderSideHero, 1000);
		if(withCleave)
		{
			const SecondarySkill offense(SecondarySkill::decode(newHorizonsOffense::SKILL));
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, "new-horizons:offense.shockAssault"});
			attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		auto * actor = addStack(BattleSide::ATTACKER, creatureByName(shooting ? "core:archer" : "core:peasant"), BattleHex(7, 5), 1000);
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(shooting ? 12 : 8, 5), withCleave ? 1 : 10000);
		CStack * cleaveTarget = withCleave
			? addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(9, 5), 10000) : nullptr;
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		blockRetaliation(actor);
		forceMaximumDamage(actor);
		// Like the maintained Hex spell fixture, enter a real spell-enabled round.
		// setupBattle alone has not reset the Hero Action allowance for that round.
		battle()->nextRound();
		ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER).currentRound, battle()->getRound());
		if(withHex)
		{
			ASSERT_TRUE(castOn(defenderSideHero, spell, actor)); // Mechanics-validated real script setup.
		}
		ASSERT_EQ(newHorizonsHexOfPainAI::hasEffect(actor), withHex);

		HexSnapshotEnvironment environment(gameState());
		auto parent = std::make_shared<HypotheticBattle>(&environment,
			std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto child = std::make_shared<HypotheticBattle>(&environment, parent);
		auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
		auto ownedSource = child->getForUpdate(actor->unitId());
		auto countedSource = std::make_shared<SnapshotCountingStack>(child.get(), ownedSource.get());
		std::shared_ptr<StackWithBonuses> source = evaluateOnly ? countedSource : ownedSource;
		auto victim = child->getForUpdate(target->unitId());
		auto siblingSource = sibling->getForUpdate(actor->unitId());
		auto siblingTarget = sibling->getForUpdate(target->unitId());
		const auto actorHP = source->getAvailableHealth();
		const auto targetHP = victim->getAvailableHealth();
		const auto casts = source->castSpellThisTurn;
		const auto attacks = source->getTotalAttacks(false);
		const auto sourceState = source->save();
		const auto targetState = victim->save();
		DamageCache cache;
		cache.buildDamageCache(child, BattleSide::ATTACKER);
		const auto damage = cache.getDamage(source.get(), victim.get(), child);
		ASSERT_GT(damage, 0);
		if(withCleave)
			ASSERT_GE(damage, targetHP);
		else
			ASSERT_LT(damage, targetHP);
		const auto enemyValue = AttackPossibility::calculateDamageReduce(source.get(), victim.get(), damage, cache, child);
		float cleaveValue = 0;
		JsonNode cleaveState;
		if(withCleave)
		{
			ASSERT_TRUE(child->battleCanTriggerCleave(source.get()));
			const auto * selected = child->battleSelectCleaveTarget(source.get(), victim.get());
			ASSERT_NE(selected, nullptr);
			ASSERT_EQ(selected->unitId(), cleaveTarget->unitId());
			auto afterAttack = source->acquireState();
			afterAttack->afterAttack(false, false, true);
			BattleAttackInfo cleaveAttack(afterAttack.get(), cleaveTarget, 0, false);
			cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
			const auto cleaveDamage = child->battleExpectedLuckDamage(cleaveAttack);
			cleaveValue = AttackPossibility::calculateDamageReduce(afterAttack.get(), cleaveTarget, cleaveDamage, cache, child);
			ASSERT_GT(cleaveValue, 0);
			cleaveState = cleaveTarget->save();
		}
		int64_t hexDamage = 0;
		float ownValue = 0;
		if(withHex)
		{
			const auto markers = source->getBonuses(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)
				.And(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))));
			ASSERT_EQ(markers->size(), 1u);
			ASSERT_EQ(markers->front()->val, 15);
			ASSERT_FALSE(source->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION));
			hexDamage = 15 + damage / 10;
			ASSERT_EQ(defenderSideHero->getSpellBonus(spell.toSpell(), hexDamage, actor), hexDamage);
			ASSERT_LT(hexDamage, actorHP);
			// Independent pre-trigger scoring state: the ordinary strike has finished,
			// but reactive Hex damage has not reduced the attacker's health yet.
			auto beforeHex = source->acquireState();
			beforeHex->afterAttack(shooting, false, true);
			ownValue = AttackPossibility::calculateDamageReduce(nullptr, beforeHex.get(), hexDamage, cache, child);
			ASSERT_GT(ownValue, 0);
		}
		BattleExchangeVariant exchange;
		countedSource->snapshotCalls = 0;
		const auto score = exchange.trackAttack(source, victim, shooting, true, cache, child, evaluateOnly, false);
		EXPECT_FLOAT_EQ(score, enemyValue + cleaveValue - ownValue);
		EXPECT_EQ(countedSource->snapshotCalls, evaluateOnly && (withHex || withCleave) ? 1u : 0u);
		EXPECT_FLOAT_EQ(exchange.getScore().ourDamageReduce, ownValue);
		EXPECT_FLOAT_EQ(exchange.getScore().enemyDamageReduce, evaluateOnly ? 0 : enemyValue);
		EXPECT_EQ(source->getAvailableHealth(), actorHP - (evaluateOnly ? 0 : hexDamage));
		EXPECT_EQ(victim->getAvailableHealth(), targetHP - (evaluateOnly ? 0 : damage));
		EXPECT_EQ(source->castSpellThisTurn, casts);
		if(evaluateOnly)
		{
			EXPECT_EQ(source->getTotalAttacks(false), attacks);
			EXPECT_EQ(source->save(), sourceState);
			EXPECT_EQ(victim->save(), targetState);
		}
		EXPECT_EQ(newHorizonsHexOfPainAI::hasEffect(source.get()), withHex);
		EXPECT_EQ(actor->getAvailableHealth(), actorHP);
		EXPECT_EQ(target->getAvailableHealth(), targetHP);
		EXPECT_EQ(parent->battleGetUnitByID(actor->unitId())->getAvailableHealth(), actorHP);
		EXPECT_EQ(parent->battleGetUnitByID(target->unitId())->getAvailableHealth(), targetHP);
		EXPECT_EQ(siblingSource->getAvailableHealth(), actorHP);
		EXPECT_EQ(siblingTarget->getAvailableHealth(), targetHP);
		if(withCleave)
		{
			EXPECT_EQ(cleaveTarget->save(), cleaveState);
		}
	}
};
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexProbePreservesScoreAndActionState)
{
	checkMainAttack(false, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexAppliedAttackPreservesDamageAndScoring)
{
	checkMainAttack(false, false);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexProbeUsesPreTriggerSnapshotWithoutMutatingBranches)
{
	checkMainAttack(true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexAppliedAttackRetainsExactDamageAndPreTriggerScoring)
{
	checkMainAttack(true, false);
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexRangedProbePreservesExactScoreWithoutSnapshot)
{
	checkMainAttack(false, true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexRangedProbeKeepsPostShotSnapshotAndExactScore)
{
	checkMainAttack(true, true, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, MeleeCleaveProbeKeepsPostAttackSnapshotAndExactSecondaryScore)
{
	checkMainAttack(false, true, false, true);
}

TEST_F(NewHorizonsHexSnapshotAITest, NoHexAppliedRetaliationKeepsExactScoreAndBranchState)
{
	checkAppliedRetaliation(false);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexRetaliationKeepsPreTriggerScoringAndLethalSelfDamage)
{
	checkAppliedRetaliation(true);
}

TEST_F(NewHorizonsHexSnapshotAITest, ActiveHexCleaveProbeAndAppliedKeepExactSecondaryScore)
{
	checkActiveHexCleave();
}
