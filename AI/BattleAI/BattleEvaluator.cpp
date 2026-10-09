/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "BattleEvaluator.h"
#include "BattleExchangeVariant.h"

#include "StackWithBonuses.h"
#include "NewHorizonsHexOfPain.h"
#include "tbb/parallel_for.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/CStopWatch.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/spells/NewHorizonsBlink.h"
#include "../../lib/spells/NewHorizonsElementalTerrain.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"


// TODO: remove
// Eventually only IBattleInfoCallback and battle::Unit should be used,
// CUnitState should be private and CStack should be removed completely
#include "../../lib/CStack.h"

#define LOGL(text) print(text)
#define LOGFL(text, formattingEl) print(boost::str(boost::format(text) % formattingEl))

enum class SpellTypes
{
	ADVENTURE, BATTLE, OTHER
};

SpellTypes spellType(const CSpell * spell)
{
	if(!spell->isCombat() || spell->isCreatureAbility())
		return SpellTypes::OTHER;

	if(spell->isOffensive() || spell->hasEffects() || spell->hasBattleEffects())
		return SpellTypes::BATTLE;

	return SpellTypes::OTHER;
}

bool isDivineMandateLightSpell(const CBattleInfoCallback & battle, SpellID spell)
{
	const auto schools = battle.battleGetSpellSchools(spell);
	return std::any_of(schools.begin(), schools.end(), [](const SpellSchool & school)
	{
		return school.serializationKey() == "new-horizons:light";
	});
}

bool isTransfigureMatter(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:transfigureMatter";
}

bool isCanonicalSummonTrolls(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:summonTrolls";
}

bool isCanonicalElementalConvergence(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:elementalConvergence";
}

bool isCanonicalVerdantPrison(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:verdantPrison";
}

bool isCanonicalHandOfFate(const CBattleInfoCallback & battle, const CSpell * spell)
{
	if(!spell || spell->getJsonKey() != "new-horizons:handOfFate" || !battle.getBattle())
		return false;

	const auto & magicRules = battle.getBattle()->getMagicRules();
	return newHorizonsMagic::rulesActive(magicRules)
		&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spell->getId());
}

bool isCounterspell(const CSpell * spell)
{
	return newHorizonsMagic::isCounterspell(spell);
}

bool isHavocStructuralSpell(const CSpell * spell)
{
	return spell && (spell->getId() == SpellID::METEOR_SHOWER || spell->getId() == SpellID::ARMAGEDDON);
}

bool isCanonicalLandMine(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(spell->getId());
}

bool isSelectedQuicksand(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battle.getBattle()->getMagicRules(), spell->getId());
}

bool isCanonicalFireWall(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(spell->getId());
}

bool isCanonicalTimeStop(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

bool isCanonicalSpellLock(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL;
}

bool isCanonicalPurify(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsPurify::SPELL_ID
		&& spell->getId() == newHorizonsPurify::spellID();
}

float purifyingMandateAdditionalAfflictionValue(const SpellTargetEvaluator::PurifySelection & selection,
	const Environment * environment, const std::shared_ptr<CBattleInfoCallback> & battleCallback)
{
	std::map<int32_t, std::vector<SpellID>> groupsByUnit;
	std::set<int32_t> physicalPoisonUnits(selection.physicalPoisonStackIds.begin(),
		selection.physicalPoisonStackIds.end());
	for(const auto & [unitId, sourceSpell] : selection.spellEffectGroups)
		groupsByUnit[unitId].push_back(sourceSpell);

	if(groupsByUnit.empty())
		return 0.0f;
	const bool hasQualifyingGroup = std::any_of(groupsByUnit.begin(), groupsByUnit.end(),
		[&](const auto & candidate)
		{
			const auto * unit = battleCallback->battleGetUnitByID(static_cast<uint32_t>(candidate.first));
			return unit && std::any_of(candidate.second.begin(), candidate.second.end(), [&](const SpellID sourceSpell)
			{
				return newHorizonsPurify::isMagicalSpellEffectGroup(unit, sourceSpell);
			});
		});
	if(!hasQualifyingGroup)
		return 0.0f;

	auto projection = std::make_shared<HypotheticBattle>(environment, battleCallback);
	float result = 0.0f;
	for(const auto & [unitId, groups] : groupsByUnit)
	{
		auto projectedUnit = projection->getForUpdate(static_cast<uint32_t>(unitId));
		if(!projectedUnit || !projectedUnit->alive())
			continue;

		const bool removesMagicalSpellGroup = std::any_of(groups.begin(), groups.end(), [&](const SpellID sourceSpell)
		{
			return newHorizonsPurify::isMagicalSpellEffectGroup(projectedUnit.get(), sourceSpell);
		});
		projectedUnit->applyPurifySelection(groups, physicalPoisonUnits.contains(unitId));
		if(!removesMagicalSpellGroup)
			continue;

		const auto affliction = physicalAfflictions::first(*projectedUnit);
		if(!affliction)
			continue;

		// Keep the additional cleanse on the same health-value scale used by
		// Purify's spell-group evaluation; the projection applies the shared
		// Poison/Disease/Bleeding/oldest selection order.
		if(projectedUnit->removeFirstPhysicalAffliction())
			result += std::max(1.0f, static_cast<float>(projectedUnit->getAvailableHealth()) * 0.9f);
	}
	return result;
}

bool isCanonicalShadowGift(const CBattleInfoCallback & battle, const CSpell * spell)
{
	const auto & magicRules = battle.getBattle()->getMagicRules();
	return spell && spell->getJsonKey() == newHorizonsShadowGift::SPELL_ID
		&& newHorizonsMagic::shadowGiftEnabled(magicRules, spell->getId());
}

bool isPhantomArmy(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSorcery::PHANTOM_ARMY_SPELL;
}

bool isCanonicalRegeneration(const CSpell * spell, const JsonNode & rules)
{
	return spell && newHorizonsMagic::spellVariantBase(rules, spell->getId()).toSpell()->getJsonKey()
		== newHorizonsMagic::NATURE_REGENERATION_SPELL;
}

bool isCanonicalHydrasVitality(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:hydrasVitality";
}

bool isCanonicalChaosBlink(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsBlink::SPELL_ID;
}

bool isCanonicalVampirism(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_VAMPIRISM_SPELL
		&& newHorizonsMagic::vampirismEnabled(battle.getBattle()->getMagicRules(), spell->getId());
}

bool isCanonicalReanimate(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_REANIMATE_SPELL
		&& newHorizonsMagic::reanimateEnabled(battle.getBattle()->getMagicRules(), spell->getId());
}

bool isCanonicalSoulReaper(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL
		&& newHorizonsMagic::soulReaperEnabled(battle.getBattle()->getMagicRules(), spell->getId());
}

bool isCanonicalHexOfPain(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsHexOfPainAI::SPELL_ID;
}

bool isCanonicalFrailty(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:frailty";
}

bool isCanonicalDoom(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_DOOM_SPELL;
}

bool isCanonicalPuppetMaster(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:puppetMaster";
}

bool BattleEvaluator::canonicalDoomAvailableInSavedRules(const JsonNode & magicRules, const CSpell * spell)
{
	return isCanonicalDoom(spell)
		&& newHorizonsMagic::doomRulesEnabled(magicRules, spell->getId());
}

bool isHexOfPainTriggerBonus(const Bonus * bonus)
{
	if(!bonus || bonus->type != BonusType::COMBAT_EVENT_TRIGGER
		|| bonus->source != BonusSource::SPELL_EFFECT || bonus->sid.toString() != newHorizonsHexOfPainAI::SPELL_ID)
		return false;

	return bonus->subtype.toString() == newHorizonsHexOfPainAI::TRIGGER_ID;
}

float expectedMoraleActivationChange(int morale)
{
	if(morale == 0)
		return 0.0f;

	const auto settings = LIBRARY->engineSettings();
	const auto & chanceByMorale = settings->getVector(morale > 0
		? EGameSettings::COMBAT_GOOD_MORALE_CHANCE
		: EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const int diceSize = settings->getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	if(chanceByMorale.empty() || diceSize <= 0)
		return 0.0f;

	const auto chanceIndex = std::min<size_t>(static_cast<size_t>(std::abs(morale)), chanceByMorale.size()) - 1;
	const auto chance = std::max<int64_t>(0, chanceByMorale[chanceIndex]);
	const auto sign = morale > 0 ? 1.0f : -1.0f;
	return sign * static_cast<float>(chance) / static_cast<float>(diceSize);
}

float expectedMoraleActivationChange(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->unaffectedByMorale())
		return 0.0f;

	return expectedMoraleActivationChange(battle.battleGetMorale(unit));
}

int timedSpellEffectRounds(const battle::Unit * unit, SpellID spell, BonusType effectType)
{
	if(!unit)
		return 0;

	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(effectType)));
	if(!effects)
		return 0;

	int rounds = 0;
	for(const auto & effect : *effects)
		if(effect && Bonus::NTurns(effect.get()))
			rounds = std::max(rounds, static_cast<int>(effect->turnsRemain));
	return std::max(0, rounds);
}

int sorrowEffectRounds(const battle::Unit * unit)
{
	return timedSpellEffectRounds(unit, SpellID(SpellID::SORROW), BonusType::MORALE);
}

int curseEffectRounds(const battle::Unit * unit)
{
	return timedSpellEffectRounds(unit, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE);
}

int misfortuneEffectRounds(const battle::Unit * unit)
{
	return timedSpellEffectRounds(unit, SpellID(SpellID::MISFORTUNE),
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
}

float expectedTargetActivationValue(const battle::Unit * target,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || !target->alive() || target->getCount() <= 0
		|| target->isGhost() || target->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == target->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(target, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*target, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto perAttackDamage = damageCache.getDamage(target, friendly, projectedBattle);
		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto cappedPerAttackDamage = std::min(perAttackDamage, availableHealth);
		const auto attackDamage = std::min(availableHealth,
			cappedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(attackDamage <= 0)
			continue;

		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(attackDamage), damageCache, projectedBattle)));
	}

	// Forecast the expected value of one activation after the same source-aware
	// Fearless query used by the server's turn-start fear roll. This is a nominal
	// chance estimate; seeded combat-ability bias and Twist of Fate history are
	// not projected here.
	const int fearChance = target->hasBonusOfType(BonusType::FEARFUL)
		? std::clamp(projectedBattle->battleGetFearChance(target), 0, 100)
		: 0;
	return bestActionValue * static_cast<float>(100 - fearChance) / 100.0f;
}

float holdFastMoraleGrantValue(const CBattleInfoCallback & battle, const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !projectedBattle || !projected->alive()
		|| original->unaffectedByMorale() || projected->unaffectedByMorale())
		return 0.0f;

	const float before = expectedMoraleActivationChange(battle, original);
	if(before >= 0.0f)
		return 0.0f;

	// The shared floor bonus is already installed in the forecast copy. Value
	// only the next activation's expected bad-Morale loss; the saved-rules
	// trigger remains server-authoritative and no battle RNG is consumed here.
	const float after = expectedMoraleActivationChange(*projectedBattle, projected);
	const float recoveredActivationChance = std::max(0.0f, after - before);
	return recoveredActivationChance * expectedTargetActivationValue(projected, damageCache, projectedBattle);
}

void expireCompletedProjectedBerserkActivation(HypotheticBattle & projectedBattle,
	const battle::Unit * unit)
{
	// The caller invokes this only after a forced Berserk action was projected.
	// Unlike a successful attack, WALK and NO_ACTION do not consume UNTIL_OWN_ATTACK,
	// so remove only the shared helper's eligible saved-v3 Berserk spell markers.
	const auto bonuses = newHorizonsBerserk::completedForcedActivationBonuses(projectedBattle, unit);
	if(unit && !bonuses.empty())
		projectedBattle.removeUnitBonus(unit->unitId(), bonuses);
}

float expectedBerserkActivationValue(const battle::Unit * original, const battle::Unit * projected,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	const Environment * environment, PlayerColor valuedPlayer)
{
	if(!original || !projected || !projectedBattle || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| original->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		|| !projected->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
		return 0.0f;

	// Berserk resolves one forced action at the target's next activation. Score
	// every tied nearest target as an equally likely outcome, with no live RNG and
	// no optimistic choice of the candidate that happens to have the best damage.
	PotentialTargets candidates(projected, damageCache, projectedBattle);
	const auto forcedOwnerValue = candidates.expectedBerserkActionValue();

	// Price only the change in its next action. In particular, an archer normally
	// able to shoot our troops loses that productive shot when Berserk forces a
	// melee attack on a nearby creature or a move toward one.
	auto baseline = std::make_shared<HypotheticBattle>(environment, projectedBattle);
	auto baselineTarget = baseline->getForUpdate(projected->unitId());
	baselineTarget->removeUnitBonus(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE));
	DamageCache baselineDamage(&damageCache);
	baselineDamage.buildDamageCache(baseline, projected->unitSide());
	PotentialTargets ordinaryCandidates(baselineTarget.get(), baselineDamage, baseline);
	const float ordinaryOwnerValue = ordinaryCandidates.possibleAttacks.empty()
		? 0.0f : ordinaryCandidates.possibleAttacks.front().attackValue();
	const float ownerValueDelta = forcedOwnerValue - ordinaryOwnerValue;
	const auto signedValue = projectedBattle->battleGetOwner(projected) == valuedPlayer
		? ownerValueDelta : -ownerValueDelta;
	const int resistance = std::clamp(original->magicResistance(), 0, 100);
	const float applicationChance = 1.0f - static_cast<float>(resistance) / 100.0f;
	return signedValue * applicationChance;
}

float expectedPuppetMasterActivationSwing(const CSpell * spell, const spells::Target & target,
	const CGHeroInstance * caster, bool metamagicFollowup, bool metamagicGrand, int overcharge,
	const std::shared_ptr<HypotheticBattle> & currentBattle, DamageCache & damageCache,
	const Environment * environment, PlayerColor valuedPlayer, BattleSide valuedSide)
{
	if(!isCanonicalPuppetMaster(spell) || target.size() != 1 || !target.front().unitValue
		|| !caster || !currentBattle || !environment)
		return 0.0f;

	const auto targetId = target.front().unitValue->unitId();
	const auto * originalTarget = currentBattle->battleGetUnitByID(targetId);
	if(!originalTarget || !originalTarget->alive() || originalTarget->isGhost() || originalTarget->isTurret())
		return 0.0f;

	PotentialTargets ordinaryActions(originalTarget, damageCache, currentBattle);
	const float ordinaryActionValue = std::max(0.0f, static_cast<float>(ordinaryActions.bestActionValue()));

	auto projectedBattle = std::make_shared<HypotheticBattle>(environment, currentBattle);
	const auto * projectedTarget = projectedBattle->battleGetUnitByID(targetId);
	if(!projectedTarget)
		return 0.0f;

	spells::Target projectedAim{spells::Destination(projectedTarget)};
	spells::BattleCast projectedCast(projectedBattle.get(), caster, spells::Mode::HERO, spell);
	projectedCast.setMetamagicFollowup(metamagicFollowup);
	projectedCast.setMetamagicGrand(metamagicGrand);
	projectedCast.setOvercharge(overcharge);
	projectedCast.setMetamagicTargetUnitId(targetId);
	auto projectedMechanics = spell->battleMechanics(&projectedCast);
	spells::detail::ProblemImpl problem;
	if(!projectedMechanics->canBeCastAt(projectedAim, problem))
		return 0.0f;
	projectedMechanics->castEval(projectedBattle->getServerCallback(), projectedAim);
	if(projectedBattle->battleGetActionController(projectedTarget) != valuedPlayer
		|| projectedBattle->playerToSide(projectedBattle->battleGetActionController(projectedTarget)) != valuedSide)
		return 0.0f;

	DamageCache controlledDamage(&damageCache);
	controlledDamage.buildDamageCache(projectedBattle, valuedSide);
	PotentialTargets controlledActions(projectedTarget, controlledDamage, projectedBattle);
	const float controlledActionValue = std::max(0.0f, static_cast<float>(controlledActions.bestActionValue()));
	const float applicationChance = 1.0f - static_cast<float>(std::clamp(originalTarget->magicResistance(), 0, 100)) / 100.0f;

	// The target's ordinary activation would benefit its true owner; Puppet
	// Master replaces that value with a real action selected by its controller.
	return (ordinaryActionValue + controlledActionValue) * applicationChance;
}

float expectedCurseTargetActivationValue(const battle::Unit * target,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || !target->alive() || target->getCount() <= 0
		|| target->isGhost() || target->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == target->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(target, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*target, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto originalPerAttackDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(target, friendly, projectedBattle));
		const auto projectedPerAttackDamage = std::max<int64_t>(0,
			damageCache.getDamage(target, friendly, projectedBattle));
		const auto originalAttackDamage = std::min(availableHealth,
			originalPerAttackDamage * static_cast<int64_t>(attackCount));
		const auto projectedAttackDamage = std::min(availableHealth,
			projectedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(originalAttackDamage <= projectedAttackDamage)
			continue;

		const auto preventedDamage = originalAttackDamage - projectedAttackDamage;
		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(preventedDamage), damageCache, projectedBattle)));
	}
	return bestActionValue;
}

float expectedMisfortuneLuckTargetActivationValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == projected->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(projected, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*projected, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		// Both values use the shared expected-Luck damage path. The original stack
		// supplies the pre-cast Luck state; the detached projected stack supplies
		// Misfortune's final Luck cap. This is one bounded future action proxy, not
		// a roll from the server's seeded RandomizationBias stream.
		const BattleAttackInfo originalAttack(original, friendly, 0, shooting);
		const BattleAttackInfo projectedAttack(projected, friendly, 0, shooting);
		const auto originalDamage = std::max<int64_t>(0,
			projectedBattle->battleExpectedLuckDamage(originalAttack));
		const auto projectedDamage = std::max<int64_t>(0,
			projectedBattle->battleExpectedLuckDamage(projectedAttack));
		if(originalDamage <= projectedDamage)
			continue;

		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto perActionDamage = std::min(availableHealth,
			(originalDamage - projectedDamage) * static_cast<int64_t>(attackCount));
		if(perActionDamage <= 0)
			continue;

		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(perActionDamage), damageCache, projectedBattle)));
	}
	return bestActionValue;
}

float expectedMisfortuneDeathBlowTargetActivationValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret() || !projectedBattle)
		return 0.0f;

	const int baseChance = std::clamp(original->valOfBonuses(BonusType::DOUBLE_DAMAGE_CHANCE), 0, 100);
	const int originalChance = original->favorableCreatureAbilityChanceBasisPoints(baseChance);
	const int projectedChance = projected->favorableCreatureAbilityChanceBasisPoints(baseChance);
	const int chanceReduction = std::max(0, originalChance - projectedChance);
	if(chanceReduction <= 0)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == projected->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(projected, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*projected, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		// Isolate only the expected extra damage of a successful Death Blow.
		// Both estimates include the same projected Luck state, so this does not
		// re-count the positive-Luck reduction valued above.
		BattleAttackInfo ordinaryAttack(projected, friendly, 0, shooting);
		BattleAttackInfo deathBlowAttack = ordinaryAttack;
		deathBlowAttack.deathBlow = true;
		const auto ordinaryDamage = std::max<int64_t>(0,
			projectedBattle->battleExpectedLuckDamage(ordinaryAttack));
		const auto deathBlowDamage = std::max<int64_t>(0,
			projectedBattle->battleExpectedLuckDamage(deathBlowAttack));
		if(deathBlowDamage <= ordinaryDamage)
			continue;

		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto conditionalDamage = std::min(availableHealth,
			(deathBlowDamage - ordinaryDamage) * static_cast<int64_t>(attackCount));
		if(conditionalDamage <= 0)
			continue;

		const auto conditionalValue = AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(conditionalDamage), damageCache, projectedBattle);
		const auto expectedPreventedValue = conditionalValue * static_cast<float>(chanceReduction) / 10000.0f;
		bestActionValue = std::max(bestActionValue, expectedPreventedValue);
	}
	return bestActionValue;
}

float estimateProjectedMisfortuneTargetValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !projected->alive())
		return 0.0f;

	const auto remainingRounds = misfortuneEffectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	// This forecast is intentionally bounded to the implemented generic
	// positive-Luck damage path and DOUBLE_DAMAGE_CHANCE (Death Blow). Other
	// favorable proc families, including attack spell effects and script-driven
	// abilities, still need effect-specific expected-value models. Never consume
	// battle RNG or use the server's seeded rollCombatAbility threshold here.
	const auto preventedLuckValue = expectedMisfortuneLuckTargetActivationValue(
		original, projected, damageCache, projectedBattle);
	const auto preventedDeathBlowValue = expectedMisfortuneDeathBlowTargetActivationValue(
		original, projected, damageCache, projectedBattle);
	return (preventedLuckValue + preventedDeathBlowValue)
		* static_cast<float>(remainingRounds) * 0.5f;
}

float BattleEvaluator::estimateProjectedSorrowTargetValue(const battle::Unit * original, const battle::Unit * projected,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !projected->alive()
		|| original->unaffectedByMorale() || projected->unaffectedByMorale())
		return 0.0f;

	const auto remainingRounds = sorrowEffectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	// This is an expected-value estimate, not a deterministic roll forecast:
	// BattleAI cannot know the per-army seeded RandomizationBias history or all
	// future morale eligibility gates (e.g. waited/defended/fear/canMove/hadMorale).
	// Use the configured raw chance table and projected Morale delta instead of
	// recomputing Sorrow's saved-rules School-rank formula.
	const auto lostExpectedActivations = -projectedBattle->projectMoraleActivationDelta(original, projected,
		expectedMoraleActivationChange(*projectedBattle, original), expectedMoraleActivationChange(*projectedBattle, projected),
		static_cast<float>(remainingRounds) * 0.5f);
	if(lostExpectedActivations <= 0.0f)
		return 0.0f;

	// One best direct attack is a bounded proxy for one normal activation. The
	// spell's real timed bonus supplies the duration; exact activation timing,
	// eligibility gates, special non-damage actions, and per-army bias history
	// remain Phase-2 integration work.
	const auto activationValue = expectedTargetActivationValue(projected, damageCache, projectedBattle);
	return lostExpectedActivations * activationValue;
}

float BattleEvaluator::estimateProjectedCurseTargetValue(const battle::Unit * original, const battle::Unit * projected,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive())
		return 0.0f;

	const auto remainingRounds = curseEffectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	// The projected Curse bonus drives the shared expected-damage calculator;
	// this estimator does not reproduce Curse's damage-range rule. One best
	// hostile attack is a bounded proxy for each remaining round, matching the
	// Sorrow estimate's treatment of future activations.
	const auto preventedAttackValue = expectedCurseTargetActivationValue(projected, damageCache, projectedBattle);
	return preventedAttackValue * static_cast<float>(remainingRounds) * 0.5f;
}

float BattleEvaluator::estimateProjectedFrailtyTargetValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| !projectedBattle)
		return 0.0f;

	const auto availableHealth = std::max<int64_t>(0, projected->getAvailableHealth());
	if(availableHealth <= 0)
		return 0.0f;

	int64_t increasedDamage = 0;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == projected->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(friendly, projected->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*friendly, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		// The child cache contains projected post-Frailty damage, while its parent
		// retains the live pre-cast damage snapshot. Compare one activation from
		// every allied stack, then cap their combined gain at this target's current
		// available health. This deliberately uses no guessed effect duration.
		const auto originalDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(friendly, projected, projectedBattle));
		const auto projectedDamage = std::max<int64_t>(0,
			damageCache.getDamage(friendly, projected, projectedBattle));
		if(projectedDamage <= originalDamage)
			continue;

		const auto perAttackIncrease = projectedDamage - originalDamage;
		const auto remainingHealth = availableHealth - increasedDamage;
		const auto attacksNeededToFinish = remainingHealth / attackCount
			+ (remainingHealth % attackCount == 0 ? 0 : 1);
		if(perAttackIncrease >= attacksNeededToFinish)
		{
			increasedDamage = availableHealth;
			break;
		}

		// The comparison above guarantees this product is below remainingHealth,
		// so multiplying by the bounded attack count cannot overflow int64_t.
		increasedDamage += perAttackIncrease * static_cast<int64_t>(attackCount);
	}

	if(increasedDamage <= 0)
		return 0.0f;

	return AttackPossibility::calculateDamageReduce(nullptr, projected,
		static_cast<uint64_t>(increasedDamage), damageCache, projectedBattle);
}

float BattleEvaluator::estimateProjectedHexOfPainTargetValue(const battle::Unit * original,
	const battle::Unit * projected, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| !projectedBattle || !newHorizonsHexOfPainAI::hasEffect(projected))
		return 0.0f;

	const auto remainingRounds = newHorizonsHexOfPainAI::effectRounds(projected);
	if(remainingRounds <= 0)
		return 0.0f;

	const auto bestAttackValue = [&](DamageCache & cache)
	{
		const auto * actor = projectedBattle->battleGetUnitByID(projected->unitId());
		if(!actor || !actor->alive())
			return 0.0f;

		PotentialTargets targets(actor, cache, projectedBattle);
		if(targets.possibleAttacks.empty())
			return 0.0f;
		return std::max(0.0f, targets.possibleAttacks.front().attackValue());
	};

	// Compare the same projected position, health, and other spell effects with
	// just this registered trigger removed. The attack preview executes the Hex
	// script after each projected strike/retaliation, so this estimates the value
	// of the curse's reactive self-damage without inventing generic trigger rules.
	DamageCache hexedDamage;
	hexedDamage.buildDamageCache(projectedBattle, projected->unitSide());
	const auto hexedActionValue = bestAttackValue(hexedDamage);

	std::vector<Bonus> removedBonuses;
	const auto triggers = projected->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
		if(bonus && isHexOfPainTriggerBonus(bonus.get()))
			removedBonuses.emplace_back(*bonus);
	if(removedBonuses.empty())
		return 0.0f;

	const auto triggerSelector = CSelector([](const Bonus * bonus)
	{
		return isHexOfPainTriggerBonus(bonus);
	});
	const auto targetState = projectedBattle->getForUpdate(projected->unitId());
	targetState->removeUnitBonus(triggerSelector);

	DamageCache unhexedDamage;
	unhexedDamage.buildDamageCache(projectedBattle, projected->unitSide());
	const auto unhexedActionValue = bestAttackValue(unhexedDamage);

	// Forecasting is observational: restore the exact captured bonus objects so
	// later candidate scoring sees the accepted hypothetical cast unchanged.
	targetState->addUnitBonus(removedBonuses);

	const auto preventedActionValue = std::max(0.0f, unhexedActionValue - hexedActionValue);
	return preventedActionValue * static_cast<float>(remainingRounds) * 0.5f;
}

float BattleEvaluator::estimateProjectedDoomTargetValue(const battle::Unit * original,
	const battle::Unit * projected, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!original || !projected || !original->alive() || !projected->alive()
		|| projected->getCount() <= 0 || projected->isGhost() || projected->isTurret()
		|| !projectedBattle)
		return 0.0f;

	const SpellID doom(SpellID::decode(std::string(newHorizonsMagic::SHADOW_DOOM_SPELL)));
	const auto attackReductionBonuses = projected->getBonuses(
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(doom))
			.And(Selector::type()(BonusType::GENERAL_ATTACK_REDUCTION)));
	if(!attackReductionBonuses)
		return 0.0f;

	int damagePenaltyPercent = 0;
	int remainingRounds = 0;
	for(const auto & bonus : *attackReductionBonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;
		damagePenaltyPercent = std::max(damagePenaltyPercent, bonus->val);
		remainingRounds = std::max(remainingRounds, static_cast<int>(bonus->turnsRemain));
	}
	if(damagePenaltyPercent <= 0 || remainingRounds <= 0)
		return 0.0f;

	float bestPreventedAttackValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| friendly->unitSide() == projected->unitSide())
			continue;

		const bool shooting = projectedBattle->battleCanShoot(projected, friendly->getPosition());
		const int attackCount = AttackPossibility::getAttackCount(*projected, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto originalPerAttackDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(projected, friendly, projectedBattle));
		const auto projectedPerAttackDamage = std::max<int64_t>(0,
			damageCache.getDamage(projected, friendly, projectedBattle));
		const auto originalAttackDamage = std::min(availableHealth,
			originalPerAttackDamage * static_cast<int64_t>(attackCount));
		const auto projectedAttackDamage = std::min(availableHealth,
			projectedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(originalAttackDamage <= projectedAttackDamage)
			continue;

		const auto preventedDamage = originalAttackDamage - projectedAttackDamage;
		bestPreventedAttackValue = std::max(bestPreventedAttackValue,
			static_cast<float>(AttackPossibility::calculateDamageReduce(nullptr, friendly,
				static_cast<uint64_t>(preventedDamage), damageCache, projectedBattle)));
	}

	// The projected GENERAL_ATTACK_REDUCTION bonus is authoritative for both
	// normal and retaliation damage. This bounded forecast uses one ordinary
	// attack delta; do not multiply by Doom's percentage again or add another
	// copy of that same prospective hit as a retaliation estimate.
	const float expectedFutureAttackValue = bestPreventedAttackValue * static_cast<float>(remainingRounds) * 0.5f;
	const auto lostExpectedActivations = -projectedBattle->projectMoraleActivationDelta(original, projected,
		expectedMoraleActivationChange(*projectedBattle, original), expectedMoraleActivationChange(*projectedBattle, projected),
		static_cast<float>(remainingRounds) * 0.5f);
	const float moraleValue = lostExpectedActivations > 0.0f
		? lostExpectedActivations * expectedTargetActivationValue(projected, damageCache, projectedBattle)
		: 0.0f;

	// Projected spell mechanics intentionally do not roll resistance. Weight the
	// whole status forecast by application chance exactly once, using the live
	// target's authoritative resistance rather than modifying the projection.
	const int resistance = std::clamp(original->magicResistance(), 0, 100);
	const float applicationChance = 1.0f - static_cast<float>(resistance) / 100.0f;
	return (expectedFutureAttackValue + moraleValue) * applicationChance;
}

bool isCanonicalHolyArmor(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:holyArmor";
}

bool isCanonicalHeavenlyGale(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:heavenlyGale";
}

bool isCanonicalCrusade(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:crusade";
}

bool isCanonicalShieldOfChaos(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:shieldOfChaos";
}

bool isCanonicalSanctuary(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:sanctuary";
}

bool isCanonicalGuardianSpirit(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:guardianSpirit";
}

bool isCanonicalDivineRetribution(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:divineRetribution";
}

bool isCanonicalNatureEntangle(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:entangle";
}

bool guardianSpiritAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return isCanonicalGuardianSpirit(spell)
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle.getBattle()->getMagicRules(), spell->getId());
}

bool heavenlyGaleAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return isCanonicalHeavenlyGale(spell)
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle.getBattle()->getMagicRules(), spell->getId());
}

bool crusadeAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	const auto & magicRules = battle.getBattle()->getMagicRules();
	return isCanonicalCrusade(spell)
		&& newHorizonsMagic::rulesActive(magicRules)
		&& magicRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spell->getId());
}

bool shieldOfChaosAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	const auto & magicRules = battle.getBattle()->getMagicRules();
	return isCanonicalShieldOfChaos(spell)
		&& newHorizonsMagic::rulesActive(magicRules)
		&& magicRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spell->getId());
}

bool divineRetributionAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return isCanonicalDivineRetribution(spell)
		&& newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& battle.getBattle()->getMagicRules()["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle.getBattle()->getMagicRules(), spell->getId());
}

bool natureEntangleAvailableInSavedRules(const CBattleInfoCallback & battle, const CSpell * spell)
{
	const auto & magicRules = battle.getBattle()->getMagicRules();
	return isCanonicalNatureEntangle(spell)
		&& newHorizonsMagic::rulesActive(magicRules)
		&& magicRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spell->getId());
}

struct HolyArmorProtection
{
	int reductionPercent = 0;
	int rounds = 0;

	int exposurePercentRounds() const
	{
		return reductionPercent * rounds;
	}
};

HolyArmorProtection holyArmorProtection(const battle::Unit * unit, SpellID spell)
{
	HolyArmorProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION)));
	if(!bonuses)
		return result;
	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		const HolyArmorProtection current{
			std::clamp(bonus->val, 0, 100),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		if(current.exposurePercentRounds() > result.exposurePercentRounds())
			result = current;
	}
	return result;
}

void projectRegenerationRateSnapshot(HypotheticBattle & projectedBattle,
	const spells::Mechanics & mechanics, const CSpell * spell,
	const spells::Target & acceptedTarget)
{
	const auto & savedRules = projectedBattle.getBattle()->getMagicRules();
	if(!isCanonicalRegeneration(spell, savedRules) || !newHorizonsMagic::rulesActive(savedRules))
		return;

	const auto regenerationMarker = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(newHorizonsMagic::spellVariantBase(savedRules, spell->getId())))
		.And(Selector::type()(BonusType::HP_REGENERATION));

	const auto * hero = mechanics.getHeroCaster();
	const bool herbalist = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST));
	const auto rate = newHorizonsMagic::regenerationRateMillionthsBasisPoints(
		std::max<int32_t>(0, mechanics.getEffectPower()),
		mechanics.getSpellPowerCoefficientBasisPoints(), herbalist,
		mechanics.getWarcastingBonusPercent(), mechanics.getEmpowerSpellBonusPercent());
	for(const auto * unit : mechanics.getAffectedStacks(acceptedTarget))
	{
		if(!unit)
			continue;
		auto targetState = projectedBattle.getForUpdate(unit->unitId());
		if(targetState->alive() && targetState->hasBonus(regenerationMarker))
			targetState->regenerationRateMillionths = rate;
	}
}

int regenerationEffectRounds(const battle::Unit * unit, SpellID spell)
{
	if(!unit)
		return 0;

	int result = 0;
	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	for(const auto & effect : *effects)
		if(effect)
			result = std::max<int>(result, effect->turnsRemain);
	return std::max(0, result);
}

std::set<uint32_t> improvedRegenerationTargets(
	const HypotheticBattle & projectedBattle,
	const CBattleInfoCallback & originalBattle,
	SpellID spell)
{
	std::set<uint32_t> result;
	for(const auto * projected : projectedBattle.battleGetAllUnits())
	{
		const auto * projectedState = dynamic_cast<const battle::CUnitState *>(projected);
		if(!projectedState || !projectedState->alive() || projectedState->regenerationRateMillionths <= 0)
			continue;

		const auto * original = originalBattle.battleGetUnitByID(projected->unitId());
		const auto * originalState = dynamic_cast<const battle::CUnitState *>(original);
		const int originalRate = originalState ? originalState->regenerationRateMillionths : 0;
		const int originalRounds = regenerationEffectRounds(original, spell);
		if(projectedState->regenerationRateMillionths > originalRate
			|| regenerationEffectRounds(projected, spell) > originalRounds)
			result.insert(projected->unitId());
	}
	return result;
}

void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action)
{
	auto attackerState = state.getForUpdate(liveUnit->unitId());
	const bool attackerWasAlive = attackerState->alive();
	*attackerState = *action.attackerState;
	state.recordBloodrageTransition(attackerState, attackerWasAlive);

	if(action.defenderDamageReduce > 0)
	{
		attackerState->removeUnitBonus(Bonus::UntilAttack);
		attackerState->removeUnitBonus(Bonus::UntilOwnAttack);
	}
	if(action.attackerDamageReduce > 0)
		attackerState->removeUnitBonus(Bonus::UntilBeingAttacked);

	// Accepted movement/attack actions break Sanctuary before resolving. Keep
	// the detached actor used by later morale forecasts in the same state.
	const auto sanctuaryMarkers = attackerState->getBonusesOfType(BonusType::SANCTIFIED);
	if(sanctuaryMarkers && !sanctuaryMarkers->empty())
	{
		std::vector<BonusSourceID> sanctuarySources;
		for(const auto & marker : *sanctuaryMarkers)
		{
			if(!marker || marker->source != BonusSource::SPELL_EFFECT)
				continue;
			if(std::find(sanctuarySources.begin(), sanctuarySources.end(), marker->sid) == sanctuarySources.end())
				sanctuarySources.push_back(marker->sid);
		}
		for(const auto & sourceID : sanctuarySources)
			attackerState->removeUnitBonus(Selector::source(BonusSource::SPELL_EFFECT, sourceID));
		attackerState->removeUnitBonus(Selector::type()(BonusType::SANCTIFIED));
	}

	for(const auto & affected : action.affectedUnits)
	{
		if(!affected)
			continue;
		auto affectedState = state.getForUpdate(affected->unitId());
		const bool affectedWasAlive = affectedState->alive();
		*affectedState = *affected;
		state.recordBloodrageTransition(affectedState, affectedWasAlive);

		if(action.defenderDamageReduce > 0)
			affectedState->removeUnitBonus(Bonus::UntilBeingAttacked);
		if(action.attackerDamageReduce > 0 && action.attack.defender->unitId() == affected->unitId())
			affectedState->removeUnitBonus(Bonus::UntilAttack);
	}
}

float projectedRegenerationValue(
	const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder,
	const std::set<uint32_t> & targets,
	DamageCache & damageCache,
	BattleSide ourSide,
	PlayerColor ourPlayer,
	float positiveEffectMultiplier)
{
	if(targets.empty())
		return 0.0f;

	auto forecast = std::make_shared<HypotheticBattle>(environment, afterCast);
	auto baseline = std::make_shared<HypotheticBattle>(environment, beforeCast);
	DamageCache forecastDamage(&damageCache);
	DamageCache baselineDamage(&damageCache);
	forecastDamage.buildDamageCache(forecast, ourSide);
	baselineDamage.buildDamageCache(baseline, ourSide);
	float score = 0.0f;
	bool firstRound = true;

	for(const auto & round : turnOrder)
	{
		if(!firstRound)
		{
			forecast->nextRound();
			baseline->nextRound();
		}
		firstRound = false;

		for(const auto * queuedUnit : round)
		{
			const auto * unit = forecast->battleGetUnitByID(queuedUnit->unitId());
			const auto * baselineUnit = baseline->battleGetUnitByID(queuedUnit->unitId());
			const auto * unitState = dynamic_cast<const battle::CUnitState *>(unit);
			const auto * baselineState = dynamic_cast<const battle::CUnitState *>(baselineUnit);
			const bool startsActivation = unit
				&& forecast->battleBeginsActivation(unit, BattleUnitTurnReason::TURN_QUEUE);
			if(startsActivation && unit->alive() && targets.contains(unit->unitId())
				&& forecast->battleGetOwner(unit) == ourPlayer)
			{
				const auto projectedHeal = unitState ? unitState->regenerationProjectedHeal() : 0;
				const auto baselineHeal = baselineUnit && baselineUnit->alive() && baselineState
					? baselineState->regenerationProjectedHeal() : 0;
				const auto additionalHeal = std::max<int64_t>(0, projectedHeal - baselineHeal);
				if(additionalHeal > 0)
				{
					const auto healedValue = AttackPossibility::calculateDamageReduce(
						nullptr, unit, static_cast<uint64_t>(additionalHeal), forecastDamage, forecast);
					score += healedValue * positiveEffectMultiplier;
				}
			}

			const bool forecastForfeits = unit && newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
			const bool baselineForfeits = baselineUnit && newHorizonsFrozen::forfeitsNormalActivation(*baselineUnit, BattleUnitTurnReason::TURN_QUEUE);
			if(unit && unit->alive())
				forecast->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			if(baselineUnit && baselineUnit->alive())
				baseline->nextTurn(baselineUnit->unitId(), BattleUnitTurnReason::TURN_QUEUE);

			const auto * currentForecastUnit = unit
				? forecast->battleGetUnitByID(unit->unitId()) : nullptr;
			const auto * currentBaselineUnit = baselineUnit
				? baseline->battleGetUnitByID(baselineUnit->unitId()) : nullptr;
			if(!forecastForfeits && currentForecastUnit && currentForecastUnit->alive())
			{
				PotentialTargets potentialTargets(currentForecastUnit, forecastDamage, forecast);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*forecast, currentForecastUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*forecast,
						forecast->battleGetUnitByID(queuedUnit->unitId()));
			}
			if(currentForecastUnit)
				forecast->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			if(!baselineForfeits && currentBaselineUnit && currentBaselineUnit->alive())
			{
				PotentialTargets potentialTargets(currentBaselineUnit, baselineDamage, baseline);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*baseline, currentBaselineUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*baseline,
						baseline->battleGetUnitByID(queuedUnit->unitId()));
			}
			if(currentBaselineUnit)
				baseline->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
		}
	}

	return score;
}

float projectedCapacityRegenerationValue(
	const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder,
	uint32_t targetId,
	DamageCache & damageCache,
	BattleSide ourSide,
	PlayerColor ourPlayer,
	float positiveEffectMultiplier)
{
	if(!afterCast || targetId == std::numeric_limits<uint32_t>::max())
		return 0.0f;

	auto forecast = std::make_shared<HypotheticBattle>(environment, afterCast);
	auto baseline = std::make_shared<HypotheticBattle>(environment, beforeCast);
	DamageCache forecastDamage(&damageCache);
	DamageCache baselineDamage(&damageCache);
	forecastDamage.buildDamageCache(forecast, ourSide);
	baselineDamage.buildDamageCache(baseline, ourSide);
	float score = 0.0f;
	bool firstRound = true;
	int targetQueueOccurrences = 0;
	int targetActivationOccurrences = 0;
	int forecastRound = 0;

	for(const auto & round : turnOrder)
	{
		if(!firstRound)
		{
			forecast->nextRound();
			baseline->nextRound();
			++forecastRound;
		}
		firstRound = false;

		for(const auto * queuedUnit : round)
		{
			if(!queuedUnit)
				continue;

			const auto unitId = queuedUnit->unitId();
			auto * unit = forecast->getForUpdate(unitId).get();
			auto * baselineUnit = baseline->getForUpdate(unitId).get();
			const bool startsActivation = unit
				&& forecast->battleBeginsActivation(unit, BattleUnitTurnReason::TURN_QUEUE);
			if(unitId == targetId)
			{
				++targetQueueOccurrences;
				if(startsActivation)
					++targetActivationOccurrences;
			}
			const int64_t forecastHealthBefore = unit
				? static_cast<int64_t>(unit->getAvailableHealth()) : 0;
			const int64_t baselineHealthBefore = baselineUnit
				? static_cast<int64_t>(baselineUnit->getAvailableHealth()) : 0;

			const bool forecastForfeits = unit && newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
			const bool baselineForfeits = baselineUnit && newHorizonsFrozen::forfeitsNormalActivation(*baselineUnit, BattleUnitTurnReason::TURN_QUEUE);
			if(unit && unit->alive())
				forecast->nextTurn(unitId, BattleUnitTurnReason::TURN_QUEUE);
			if(baselineUnit && baselineUnit->alive())
				baseline->nextTurn(unitId, BattleUnitTurnReason::TURN_QUEUE);

			const auto * currentForecastUnit = forecast->battleGetUnitByID(unitId);
			const auto * currentBaselineUnit = baseline->battleGetUnitByID(unitId);
			if(startsActivation && unitId == targetId && currentForecastUnit
				&& forecast->battleGetOwner(currentForecastUnit) == ourPlayer)
			{
				const int64_t forecastHealthAfter = static_cast<int64_t>(currentForecastUnit->getAvailableHealth());
				const int64_t baselineHealthAfter = currentBaselineUnit
					? static_cast<int64_t>(currentBaselineUnit->getAvailableHealth()) : baselineHealthBefore;
				// Value only HP that the shared activation hook actually restored.
				// The capacity increase itself remains an empty health reserve until
				// this point and is never counted as a cast-time heal.
				const auto additionalHealth = std::max<int64_t>(0,
					(forecastHealthAfter - forecastHealthBefore)
					- (baselineHealthAfter - baselineHealthBefore));
				if(additionalHealth > 0)
				{
					const auto healedValue = AttackPossibility::calculateDamageReduce(
						nullptr, currentForecastUnit, static_cast<uint64_t>(additionalHealth),
						forecastDamage, forecast);
					const auto weightedValue = healedValue * positiveEffectMultiplier;
					score += weightedValue;
				}
			}

			if(!forecastForfeits && currentForecastUnit && currentForecastUnit->alive())
			{
				PotentialTargets potentialTargets(currentForecastUnit, forecastDamage, forecast);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*forecast, currentForecastUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*forecast,
						forecast->battleGetUnitByID(unitId));
			}
			if(currentForecastUnit)
				forecast->getForUpdate(unitId)->removeUnitBonus(Bonus::UntilActivationEnds);
			if(!baselineForfeits && currentBaselineUnit && currentBaselineUnit->alive())
			{
				PotentialTargets potentialTargets(currentBaselineUnit, baselineDamage, baseline);
				if(!potentialTargets.possibleAttacks.empty())
					applyProjectedBestAction(*baseline, currentBaselineUnit, potentialTargets.bestAction());
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*baseline,
						baseline->battleGetUnitByID(unitId));
			}
			if(currentBaselineUnit)
				baseline->getForUpdate(unitId)->removeUnitBonus(Bonus::UntilActivationEnds);
		}
	}
	return score;
}

float projectedVampirismValue(
	const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeCast,
	const std::shared_ptr<HypotheticBattle> & afterCast,
	const std::vector<battle::Units> & turnOrder,
	uint32_t targetId,
	DamageCache & damageCache,
	BattleSide ourSide,
	PlayerColor ourPlayer,
	float positiveEffectMultiplier)
{
	if(targetId == std::numeric_limits<uint32_t>::max())
		return 0.0f;

	const auto valueVampirismHealing = [&](const std::shared_ptr<HypotheticBattle> & forecast,
		DamageCache & forecastDamage)
	{
		float value = 0.0f;
		bool firstRound = true;
		for(const auto & round : turnOrder)
		{
			if(!firstRound)
				forecast->nextRound();
			firstRound = false;

			for(const auto * queuedUnit : round)
			{
				auto * unit = queuedUnit
					? forecast->getForUpdate(queuedUnit->unitId()).get() : nullptr;
				if(!unit || !unit->alive())
					continue;

				const bool beginsActivation = forecast->battleBeginsActivation(
					unit, BattleUnitTurnReason::TURN_QUEUE);
				const bool frozenForfeits = newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
				forecast->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
				unit = forecast->getForUpdate(queuedUnit->unitId()).get();
				if(!frozenForfeits && beginsActivation && unit->alive())
				{
					PotentialTargets potentialTargets(unit, forecastDamage, forecast);
					if(!potentialTargets.possibleAttacks.empty())
					{
						auto action = potentialTargets.bestAction();
						for(const auto & [healedUnitId, healedHealth] : action.vampirismHealingByUnit)
						{
							if(healedUnitId != targetId || healedHealth <= 0)
								continue;

							const auto * recipient = forecast->battleGetUnitByID(healedUnitId);
							if(!recipient || !recipient->alive()
								|| forecast->battleGetOwner(recipient) != ourPlayer)
								continue;
							value += AttackPossibility::calculateDamageReduce(nullptr, recipient,
								static_cast<uint64_t>(healedHealth), forecastDamage, forecast)
								* positiveEffectMultiplier;
						}
						applyProjectedBestAction(*forecast, unit, action);
					}
					if(potentialTargets.berserk)
						expireCompletedProjectedBerserkActivation(*forecast,
							forecast->battleGetUnitByID(queuedUnit->unitId()));
				}

				forecast->getForUpdate(queuedUnit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			}
		}
		return value;
	};

	auto forecast = std::make_shared<HypotheticBattle>(environment, afterCast);
	auto baseline = std::make_shared<HypotheticBattle>(environment, beforeCast);
	DamageCache forecastDamage(&damageCache);
	DamageCache baselineDamage(&damageCache);
	forecastDamage.buildDamageCache(forecast, ourSide);
	baselineDamage.buildDamageCache(baseline, ourSide);
	return std::max(0.0f,
		valueVampirismHealing(forecast, forecastDamage)
		- valueVampirismHealing(baseline, baselineDamage));
}

template<typename Unit>
int64_t phantomArmyMarkerIntegrity(const Unit * unit, SpellID spellId)
{
	const auto markers = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellId)));
	if(!markers)
		return 0;

	for(const auto & marker : *markers)
		if(marker && marker->type == BonusType::NONE)
			return std::max<int64_t>(0, marker->val);

	return 0;
}

template<typename Unit>
int64_t phantomArmyCurrentIntegrity(const Unit * unit, SpellID spellId)
{
	if constexpr(requires { unit->getPhantomIntegrity(); })
		return std::max<int64_t>(0, unit->getPhantomIntegrity());
	else
		return phantomArmyMarkerIntegrity(unit, spellId);
}

template<typename Unit>
int64_t phantomArmyInitialIntegrity(const Unit * unit, SpellID spellId)
{
	if constexpr(requires { unit->getPhantomInitialIntegrity(); })
		return std::max<int64_t>(0, unit->getPhantomInitialIntegrity());
	else
		return phantomArmyMarkerIntegrity(unit, spellId);
}

/// Estimate the temporary combat value of one newly projected Phantom Army
/// stack.  Its copied count provides normal offensive power, while the
/// integrity pool limits how much of that power is likely to survive; its
/// two-round lifetime caps the contribution.  This score deliberately uses
/// the Phantom profile instead of the stack's synthetic ordinary health.
float phantomArmyCombatValue(const battle::Unit * unit, SpellID spellId)
{
	const auto currentIntegrity = phantomArmyCurrentIntegrity(unit, spellId);
	const auto initialIntegrity = phantomArmyInitialIntegrity(unit, spellId);
	const auto totalHealth = std::max<int64_t>(1, unit->getTotalHealth());
	const auto creature = unit->unitType();
	if(currentIntegrity <= 0 || initialIntegrity <= 0 || !creature || unit->getCount() <= 0)
		return 0.0f;

	const auto integrityFraction = std::clamp(
		static_cast<float>(initialIntegrity) / static_cast<float>(totalHealth), 0.0f, 1.0f);
	const auto survivingFraction = std::clamp(
		static_cast<float>(currentIntegrity) / static_cast<float>(initialIntegrity), 0.0f, 1.0f);
	const auto expectedActiveRounds = static_cast<float>(newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS)
		* survivingFraction;
	const auto copiedArmyValue = static_cast<float>(unit->getCount())
		* static_cast<float>(std::max(0, creature->getAIValue()));

	return copiedArmyValue * integrityFraction * expectedActiveRounds;
}

BattleHex::EDir fireWallDirection(const spells::Target & target)
{
	if(target.size() < 2 || target.front().unitValue != nullptr || target.at(1).unitValue != nullptr)
		return BattleHex::NONE;

	for(const auto direction : BattleHex::hexagonalDirections())
		if(target.front().hexValue.cloneInDirection(direction, false) == target.at(1).hexValue)
			return direction;

	return BattleHex::NONE;
}

/// Estimate the deterministic value of arming Counterspell without mutating a
/// battle preview.  The authoritative battle snapshot is used for the enemy
/// hero, mana costs, and current ward state; the enemy spellbook is only used
/// as a read-only threat list.  A ward is useful only when the AI can afford
/// both the arming cast and at least one likely enemy spell's listed ward cost.
float counterspellThreatValue(const CBattleInfoCallback & battle, BattleSide side,
	const CGHeroInstance * caster, const CSpell * counterspell)
{
	if(!caster || !counterspell || battle.battleWasCounterspellArmed(side))
		return 0.0f;

	const auto enemySide = CBattleInfoEssentials::otherSide(side);
	const auto * enemy = battle.getBattle()->getSideHero(enemySide);
	if(!enemy || !enemy->hasSpellbook())
		return 0.0f;

	// Player callbacks intentionally hide enemy hero details.  Spell-level
	// blockers must nevertheless be evaluated from the all-knowing authoritative
	// battle callback, rather than from the caller's perspective-limited view.
	const auto * authoritativeBattle = dynamic_cast<const CBattleInfoCallback *>(battle.getBattle());
	if(!authoritativeBattle)
		return 0.0f;
	const auto minEnemySpellLevel = authoritativeBattle->battleMinSpellLevel(enemySide);
	const auto maxEnemySpellLevel = authoritativeBattle->battleMaxSpellLevel(enemySide);

	const int armCost = battle.battleGetSpellCost(counterspell, caster);
	const int64_t remainingMana = caster->getManaAvailable() - armCost;
	if(remainingMana < 0)
		return 0.0f;

	const bool countermage = caster->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.countermage");
	float bestValue = 0.0f;
	for(const auto spellID : enemy->getInscribedSpellsForCasting())
	{
		const auto * spell = spellID.toSpell();
		if(!spell || !spell->isCombat() || spell->isCreatureAbility() || isCounterspell(spell))
			continue;
		const int spellLevel = battle.battleGetSpellLevel(spell->getId());
		if(spellLevel < minEnemySpellLevel || spellLevel > maxEnemySpellLevel)
			continue;
		if(!enemy->canCastThisSpell(spell))
			continue;

		const int listedCost = enemy->getListedSpellCost(spell);
		const int enemyManaCost = battle.battleGetSpellCost(spell, enemy);
		if(listedCost < 0 || enemy->getManaAvailable() < enemyManaCost)
			continue;

		const int wardCost = newHorizonsMagic::counterspellCost(listedCost, countermage);
		if(remainingMana < wardCost)
			continue;

		// A high-cost offensive spell represents the largest deterministic
		// threat.  The small non-offensive component still lets the ward cover
		// decisive disables, buffs, and summons without making it automatic.
		const float value = static_cast<float>(listedCost) * 100.0f
			+ (spell->isOffensive() ? 250.0f : 0.0f)
			- static_cast<float>(armCost) * 10.0f;
		bestValue = std::max(bestValue, value);
	}

	return bestValue;
}

namespace
{
constexpr HeroCommand riposteCommand()
{
	return HeroCommand::RIPOSTE;
}

constexpr HeroCommand braceCommand()
{
	return HeroCommand::BRACE;
}

constexpr HeroCommand protectCommand()
{
	return HeroCommand::PROTECT;
}

constexpr HeroCommand flankCommand()
{
	return HeroCommand::FLANK;
}

constexpr HeroCommand secondWindCommand()
{
	return HeroCommand::SECOND_WIND;
}

bool isEligibleOrderUnit(const CBattleInfoCallback & battle, BattleSide side,
	const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && unit->isValidTarget()
		&& battle.battleGetOwner(unit) == battle.sideToPlayer(side)
		&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool adjacentForProtect(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second || !first->getPosition().isValid() || !second->getPosition().isValid())
		return false;
	for(const auto & firstHex : first->getHexes())
		for(const auto & secondHex : second->getHexes())
			if(BattleHex::getDistance(firstHex, secondHex) <= 1)
				return true;
	return false;
}

/// A value-only fallback for older snapshots and for runtime APIs that expose
/// only the ordinary one-target query.  It is intentionally conservative: the
/// authoritative callback remains the final arbiter before an action is sent.
template <typename Battle>
std::vector<std::vector<uint32_t>> orderTargetOptions(const Battle & battle,
	BattleSide side, HeroCommand command)
{
	std::vector<std::vector<uint32_t>> result;

	// Canonical runtime builds validate the complete target tuple (including the
	// Protector/Ward ordering and round eligibility) through this snapshot API.
	// Enumerate only the small candidate identity set exposed by the callback;
	// never infer authority from local geometry or activation flags.
	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, std::vector<uint32_t>{}); })
	{
		// The method is present on every current callback, including legacy
		// targeted-command snapshots.  Only the canonical ruleset gives it
		// authority; legacy Focus Fire still uses its scalar validator below.
		const bool canonicalRules = battle.getBattle()
			&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		if(canonicalRules)
		{
			const auto candidates = battle.battleGetHeroCommandTargets(side, command);
			if(command == protectCommand())
			{
				for(const auto protector : candidates)
					for(const auto ward : candidates)
						if(protector != ward && battle.battlePrepareHeroOrderState(side, command, {protector, ward}))
							result.push_back({protector, ward});
			}
			else
			{
				for(const auto targetId : candidates)
					if(battle.battlePrepareHeroOrderState(side, command, {targetId}))
						result.push_back({targetId});
			}
			return result;
		}
	}

	if(command == protectCommand())
	{
		const auto units = battle.battleGetAllUnits(false);
		for(size_t i = 0; i < units.size(); ++i)
		{
			if(!isEligibleOrderUnit(battle, side, units[i]))
				continue;
			for(size_t j = 0; j < units.size(); ++j)
			{
				if(i != j && isEligibleOrderUnit(battle, side, units[j]) && adjacentForProtect(units[i], units[j]))
					result.push_back({units[i]->unitId(), units[j]->unitId()});
			}
		}
		return result;
	}

	// The existing callback already returns authoritative Focus Fire targets;
	// the runtime extension uses the same shape for Flank and Second Wind.
	for(const auto targetId : battle.battleGetHeroCommandTargets(side, command))
		result.push_back({targetId});
	return result;
}

template <typename Battle>
bool commandTargetIsLegal(const Battle & battle, BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targetIds)
{
	if(targetIds.empty())
		return battle.battleCanUseHeroCommand(side, command);

	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, targetIds); })
	{
		if(battle.getBattle() && heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
			return battle.battlePrepareHeroOrderState(side, command, targetIds).has_value();
	}
	if constexpr(requires { battle.battleGetHeroCommandTargetOptions(side, command); })
	{
		for(const auto & option : battle.battleGetHeroCommandTargetOptions(side, command))
			if(std::vector<uint32_t>(option.begin(), option.end()) == targetIds)
				return true;
		return false;
	}
	else if constexpr(requires { battle.battleCanConfirmHeroCommand(side, command, targetIds); })
		return battle.battleCanConfirmHeroCommand(side, command, targetIds);
	else if(targetIds.size() == 1)
		return battle.battleCanConfirmHeroCommand(side, command, targetIds.front());
	else
		// Protect's pair legality is checked atomically by the new server API.
		// No pair must be sent from a legacy callback that cannot validate it.
		return false;
}

float averageOrderDamage(const DamageEstimation & damage)
{
	return static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
}

/// Values one immediately available direct attack from the chosen landing.
/// This intentionally omits full-battle rollouts and movement/path simulation;
/// it is only a bounded tactical signal for whether Blink changes direct reach.
float blinkDirectAttackValue(const battle::Unit * attacker, const battle::Unit * defender,
	const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & battleState,
	DamageCache & damageCache, bool includeShooting = true)
{
	if(!attacker || !defender || !attacker->alive() || !defender->alive()
		|| !battle.battleCanAttackUnitAction(attacker, defender))
		return 0.0f;

	const auto availableHealth = std::max<int64_t>(0, defender->getAvailableHealth());
	if(availableHealth <= 0)
		return 0.0f;

	float bestValue = 0.0f;
	const auto valueForAttackMode = [&](bool shooting)
	{
		const auto attackCount = std::max(0, AttackPossibility::getAttackCount(*attacker, shooting, battle));
		if(attackCount <= 0)
			return 0.0f;

		const BattleAttackInfo attack(attacker, defender, 0, shooting);
		const auto perAttackDamage = static_cast<int64_t>(averageOrderDamage(battle.battleEstimateDamage(attack)));
		if(perAttackDamage <= 0)
			return 0.0f;

		const auto totalDamage = std::min(availableHealth, perAttackDamage * static_cast<int64_t>(attackCount));
		return AttackPossibility::calculateDamageReduce(attacker, defender,
			static_cast<uint64_t>(totalDamage), damageCache, battleState);
	};

	if(includeShooting && battle.battleCanShootAction(attacker, defender->getPosition()))
		bestValue = std::max(bestValue, valueForAttackMode(true));
	if(battle.isMeleeAttackPossibleWithLongReach(attacker, defender) || battle.isLongWeaponAttack(attacker, defender))
		bestValue = std::max(bestValue, valueForAttackMode(false));
	return bestValue;
}

/// Signed direct-attack pressure involving a stack at one endpoint. Attacks by
/// the relocated stack are limited to its single best target; attacks by other
/// stacks are accumulated because they represent independent future activations.
float blinkLandingPositionValue(const battle::Unit * original, const BattleHex & landing,
	PlayerColor player, const CBattleInfoCallback & battle,
	const std::shared_ptr<CBattleInfoCallback> & battleState, DamageCache & damageCache)
{
	if(!original || !original->alive() || !landing.isValid())
		return 0.0f;

	auto relocated = original->acquireState();
	if(!relocated)
		return 0.0f;
	relocated->setPosition(landing);

	float bestOutgoing = 0.0f;
	float signedIncoming = 0.0f;
	for(const auto * other : battle.battleGetAllUnits(false))
	{
		if(!other || !other->alive() || other->unitId() == original->unitId()
			|| other->isGhost() || other->isTurret())
			continue;

		bestOutgoing = std::max(bestOutgoing,
			blinkDirectAttackValue(relocated.get(), other, battle, battleState, damageCache));

		// The battle callback resolves a shot's target by occupied hex; a detached
		// relocation is deliberately not installed into that live occupancy map.
		// Keep the positional forecast symmetric by valuing direct incoming melee
		// pressure here while outgoing shots still use the live enemy occupancy.
		const auto incoming = blinkDirectAttackValue(other, relocated.get(), battle, battleState,
			damageCache, false);
		if(incoming > 0.0f)
			signedIncoming += battle.battleGetOwner(other) == player ? incoming : -incoming;
	}

	const float outgoingSign = battle.battleGetOwner(original) == player ? 1.0f : -1.0f;
	return outgoingSign * bestOutgoing + signedIncoming;
}

/// Returns the exact expected marginal direct-attack pressure for one Blink
/// target. The shared distribution handles normal uniform destinations and
/// Blinkmaster's two independent draws with replacement; this code consumes no RNG.
float blinkExpectedPositionValue(const battle::Unit * target,
	const newHorizonsBlink::Preview & preview, PlayerColor player,
	const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & battleState,
	DamageCache & damageCache)
{
	if(!target || preview.legalDestinations.empty())
		return 0.0f;

	const auto origin = target->getPosition();
	const auto outcomes = newHorizonsBlink::outcomeDistribution(preview, origin);
	if(outcomes.empty() || outcomes.front().totalWeight == 0)
		return 0.0f;

	const auto before = blinkLandingPositionValue(target, origin, player, battle, battleState, damageCache);
	long double weightedDelta = 0.0L;
	for(const auto & outcome : outcomes)
	{
		const auto after = blinkLandingPositionValue(target, outcome.hex, player, battle, battleState, damageCache);
		weightedDelta += static_cast<long double>(after - before) * outcome.weight;
	}

	return static_cast<float>(weightedDelta / outcomes.front().totalWeight);
}

/// Values at most one immediately available melee action that a rooted unit
/// would otherwise make after walking to an allied target. This intentionally
/// ignores shooters and targets already adjacent to any ally: Entangle does not
/// stop their attack. The active stack is excluded so this forecast cannot
/// duplicate the exchange already used to score its current action.
float entangleMovementThreatValue(const battle::Unit * liveTarget,
	const battle::Unit * projectedTarget, uint32_t activeStackId, BattleSide ourSide,
	const CBattleInfoCallback & liveBattle,
	const std::shared_ptr<HypotheticBattle> & projectedBattle, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->unitSide() == ourSide || !liveTarget->willMove()
		|| !liveTarget->isMeleeAttacker() || liveTarget->isShooter()
		|| liveTarget->getMovementRange() <= 0 || projectedTarget->getMovementRange() != 0
		|| !projectedBattle)
		return 0.0f;

	const auto attackCount = std::max(0, AttackPossibility::getAttackCount(*liveTarget, false, liveBattle));
	if(attackCount <= 0)
		return 0.0f;

	const auto availableHexes = liveBattle.battleGetAvailableHexes(liveTarget, false);
	const auto reachability = liveBattle.getReachability(liveTarget);
	if(availableHexes.empty())
		return 0.0f;

	const auto friendlyUnits = liveBattle.battleGetAllUnits(false);
	// Rooting must not receive credit when the target could keep attacking
	// without moving. This conservative check also avoids estimating a target's
	// next choice between an adjacent attack and a movement-dependent one.
	BattleHexArray stationaryPosition;
	stationaryPosition.insert(liveTarget->getPosition());
	for(const auto * friendly : friendlyUnits)
	{
		if(!friendly || !friendly->alive() || friendly->unitSide() != ourSide
			|| !friendly->isValidTarget() || friendly->isGhost() || friendly->isTurret())
			continue;
		if(!liveBattle.battleCanAttackUnit(liveTarget, friendly))
			continue;
		for(const auto & defenderHex : friendly->getHexes())
			for(int direction = 0; direction < 8; ++direction)
				if(liveBattle.battleCanAttackHex(stationaryPosition, liveTarget,
					defenderHex, static_cast<BattleHex::EDir>(direction)))
					return 0.0f;
	}

	float bestThreatValue = 0.0f;
	for(const auto * friendly : friendlyUnits)
	{
		if(!friendly || friendly->unitId() == activeStackId || !friendly->alive()
			|| friendly->unitSide() != ourSide || !friendly->isValidTarget()
			|| friendly->isGhost() || friendly->isTurret()
			|| !liveBattle.battleCanAttackUnit(liveTarget, friendly))
			continue;

		int64_t bestDamage = 0;
		for(const auto & defenderHex : friendly->getHexes())
		{
			if(!defenderHex.isValid())
				continue;

			for(int direction = 0; direction < 8; ++direction)
			{
				const auto attackDirection = static_cast<BattleHex::EDir>(direction);
				if(!liveBattle.battleCanAttackHex(availableHexes, liveTarget,
					defenderHex, attackDirection))
					continue;

				const auto attackFrom = liveBattle.fromWhichHexAttack(liveTarget,
					defenderHex, attackDirection);
				if(!attackFrom.isValid() || attackFrom == liveTarget->getPosition())
					continue;
				const auto movementDistance = reachability.distances[attackFrom.toInt()];
				if(movementDistance >= ReachabilityInfo::INFINITE_DIST)
					continue;

				const auto travelDistance = SpellTargetEvaluator::physicalTravelDistance(reachability, attackFrom);
				BattleAttackInfo attack(liveTarget, friendly,
					std::max(0, travelDistance), false);
				attack.attackerPos = attackFrom;
				attack.defenderPos = defenderHex;
				bestDamage = std::max(bestDamage,
					static_cast<int64_t>(averageOrderDamage(liveBattle.battleEstimateDamage(attack))));
			}
		}

		if(bestDamage <= 0)
			continue;
		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto attackDamage = std::min(availableHealth,
			bestDamage * static_cast<int64_t>(attackCount));
		if(attackDamage <= 0)
			continue;

		const auto value = AttackPossibility::calculateDamageReduce(nullptr, friendly,
			static_cast<uint64_t>(attackDamage), damageCache, projectedBattle);
		bestThreatValue = std::max(bestThreatValue, value);
	}

	return bestThreatValue;
}

struct GuardianSpiritShield
{
	int64_t hitPoints = 0;
	int roundsRemaining = 0;
};

GuardianSpiritShield guardianSpiritShield(const battle::Unit * unit)
{
	if(!unit)
		return {};

	const auto state = unit->acquireState();
	if(!state)
		return {};

	return {
		std::max<int64_t>(0, state->guardianSpiritHitPoints),
		std::clamp(state->guardianSpiritRoundsRemaining, 0, 2)};
}

/// Estimate physical creature damage that can reach this one target during the
/// shield's remaining exposure. Only visible enemy stacks and direct melee or
/// ranged attacks are considered; spell-like shots, siege weapons, turrets, and
/// hidden enemy-hero spellbooks are deliberately outside Guardian Spirit's scope.
float guardianSpiritPhysicalThreat(const battle::Unit * target,
	const CBattleInfoCallback & battle, int roundsRemaining)
{
	if(!target || !target->alive() || target->isGhost() || target->isTurret()
		|| roundsRemaining <= 0)
		return 0.0f;

	const int rounds = std::clamp(roundsRemaining, 0, 2);
	float damagePerRound = 0.0f;
	for(const auto * attacker : battle.battleGetAllUnits(false))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget()
			|| attacker->isGhost() || attacker->isTurret()
			|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| !battle.battleMatchOwner(attacker, target))
			continue;

		bool canShootTarget = false;
		if(attacker->canShoot())
			for(const auto & targetHex : target->getHexes())
				if(battle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}

		bool canMeleeTarget = false;
		if(attacker->isMeleeAttacker())
		{
			canMeleeTarget = !battle.meleeAttackHexes(attacker, target,
				attacker->getPosition()).empty();
			if(!canMeleeTarget)
				for(const auto & position : battle.battleGetAvailableHexes(attacker, false))
					if(!battle.meleeAttackHexes(attacker, target, position).empty())
					{
						canMeleeTarget = true;
						break;
					}
		}

		float bestAttackThreat = 0.0f;
		for(const bool shooting : {false, true})
		{
			if((shooting && !canShootTarget) || (!shooting && !canMeleeTarget))
				continue;

			const BattleAttackInfo attack(attacker, target, 0, shooting);
			if(!attack.physicalDamage)
				continue;

			const float perAttackDamage = averageOrderDamage(battle.battleEstimateDamage(attack));
			if(perAttackDamage <= 0.0f)
				continue;

			const int availableAttacks = std::max(0,
				AttackPossibility::getAttackCount(*attacker, shooting, battle));
			int attacksPerFutureActivation = std::max(1, attacker->getTotalAttacks(shooting));
			if(shooting)
				if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
					attackerState && attackerState->shots.isLimited()
						&& attackerState->shots.available() <= availableAttacks)
					attacksPerFutureActivation = 0;
			const float attackEquivalents = static_cast<float>(availableAttacks)
				+ (rounds > 1 ? 0.5f * static_cast<float>(attacksPerFutureActivation) : 0.0f);
			bestAttackThreat = std::max(bestAttackThreat, perAttackDamage * attackEquivalents);
		}

		// A stack chooses either its shot or melee attack, not both. Keep its
		// strongest relevant mode once, then aggregate independent enemy stacks.
		damagePerRound += bestAttackThreat;
	}

	return damagePerRound;
}

float guardianSpiritMitigationValue(const battle::Unit * liveTarget,
	const battle::Unit * projectedTarget, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->getAvailableHealth() <= 0 || !projectedBattle)
		return 0.0f;

	const auto before = guardianSpiritShield(liveTarget);
	const auto after = guardianSpiritShield(projectedTarget);
	const auto incomingBefore = guardianSpiritPhysicalThreat(projectedTarget, *projectedBattle,
		before.roundsRemaining);
	const auto incomingAfter = guardianSpiritPhysicalThreat(projectedTarget, *projectedBattle,
		after.roundsRemaining);
	const auto absorbedBefore = std::min(static_cast<long double>(before.hitPoints),
		static_cast<long double>(incomingBefore));
	const auto absorbedAfter = std::min(static_cast<long double>(after.hitPoints),
		static_cast<long double>(incomingAfter));
	const auto newlyAbsorbed = absorbedAfter - absorbedBefore;
	if(!std::isfinite(newlyAbsorbed) || newlyAbsorbed <= 0.0L)
		return 0.0f;

	const auto savedDamage = static_cast<uint64_t>(std::floor(newlyAbsorbed));
	if(savedDamage == 0)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, projectedTarget, savedDamage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = liveTarget->getMaxHealth();
	const auto * creature = liveTarget->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	// A shield still preserves a stack that cannot currently attack. Bound its
	// fallback value by the target's ordinary creature-health value.
	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

struct HeavenlyGaleProtection
{
	int reductionBasisPoints = 0;
	int roundsRemaining = 0;

	int exposureBasisPoints() const
	{
		return reductionBasisPoints * roundsRemaining;
	}
};

HeavenlyGaleProtection heavenlyGaleProtection(const battle::Unit * unit, SpellID spell)
{
	HeavenlyGaleProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::HEAVENLY_GALE)));
	if(!bonuses)
		return result;

	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		const HeavenlyGaleProtection current{
			std::clamp(bonus->val, 0, 8000),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		if(current.exposureBasisPoints() > result.exposureBasisPoints())
			result = current;
	}
	return result;
}

float heavenlyGaleAttackEquivalents(const battle::Unit * attacker,
	const CBattleInfoCallback & battle, int roundsRemaining)
{
	if(!attacker || roundsRemaining <= 0)
		return 0.0f;

	const int availableAttacks = std::max(0, AttackPossibility::getAttackCount(*attacker, true, battle));
	int attacksPerFutureActivation = std::max(1, attacker->getTotalAttacks(true));
	if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
		attackerState && attackerState->shots.isLimited()
			&& attackerState->shots.available() <= availableAttacks)
		attacksPerFutureActivation = 0;

	return static_cast<float>(availableAttacks)
		+ (roundsRemaining > 1 ? 0.5f * static_cast<float>(attacksPerFutureActivation) : 0.0f);
}

float heavenlyGaleSavedDamageValue(const battle::Unit * target, uint64_t savedDamage,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || savedDamage == 0 || !projectedBattle)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, target, savedDamage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = target->getMaxHealth();
	const auto * creature = target->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

/// Estimate the army-wide value from the actual projected Heavenly Gale marker.
/// Each visible ranged attacker contributes only its best threatened ally, so a
/// single shooter's same volley is not counted once for every stack. Siege
/// weapons and towers are included; spell-like projectiles are filtered through
/// BattleAttackInfo::physicalDamage. No opposing hero book is inspected.
float heavenlyGaleMitigationValue(BattleSide side,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!projectedBattle)
		return 0.0f;

	float totalValue = 0.0f;
	const auto visibleUnits = liveBattle.battleGetAllUnits(true);
	for(const auto * attacker : visibleUnits)
	{
		if(!attacker || !attacker->alive() || attacker->isGhost() || attacker->unitSide() == side
			|| !attacker->canShoot())
			continue;

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		float bestAttackerValue = 0.0f;
		for(const auto * liveTarget : liveBattle.battleGetAllUnits(false))
		{
			if(!liveTarget || !liveTarget->alive() || !liveTarget->isValidTarget(false)
				|| liveTarget->isGhost() || liveTarget->unitSide() != side)
				continue;

			const auto * projectedTarget = projectedBattle->battleGetUnitByID(liveTarget->unitId());
			if(!projectedTarget || !projectedTarget->alive())
				continue;

			const auto before = heavenlyGaleProtection(liveTarget, spell);
			const auto after = heavenlyGaleProtection(projectedTarget, spell);
			if(after.roundsRemaining <= 0 || after.reductionBasisPoints <= 0
				|| after.exposureBasisPoints() <= before.exposureBasisPoints())
				continue;

			bool canShootTarget = false;
			for(const auto & targetHex : liveTarget->getHexes())
				if(liveBattle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}
			if(!canShootTarget)
				continue;

			const BattleAttackInfo beforeAttack(attacker, liveTarget, 0, true);
			if(!beforeAttack.physicalDamage)
				continue;

			const BattleAttackInfo afterAttack(projectedAttacker, projectedTarget, 0, true);
			if(!afterAttack.physicalDamage)
				continue;

			const auto beforePerAttackDamage = std::max(0.0f,
				averageOrderDamage(liveBattle.battleEstimateDamage(beforeAttack)));
			const auto afterPerAttackDamage = std::max(0.0f,
				averageOrderDamage(projectedBattle->battleEstimateDamage(afterAttack)));
			const long double beforeDamageScale = std::max<int>(1, 10000 - before.reductionBasisPoints);
			const long double afterDamageScale = std::max<int>(1, 10000 - after.reductionBasisPoints);
			const long double rawPerAttackDamage = std::max(
				static_cast<long double>(beforePerAttackDamage) * 10000.0L / beforeDamageScale,
				static_cast<long double>(afterPerAttackDamage) * 10000.0L / afterDamageScale);
			if(!std::isfinite(rawPerAttackDamage) || rawPerAttackDamage <= 0.0L)
				continue;

			const auto beforeExposure = static_cast<long double>(before.reductionBasisPoints)
				* heavenlyGaleAttackEquivalents(attacker, liveBattle, before.roundsRemaining);
			const auto afterExposure = static_cast<long double>(after.reductionBasisPoints)
				* heavenlyGaleAttackEquivalents(attacker, liveBattle, after.roundsRemaining);
			const auto targetHealth = std::max<int64_t>(0, liveTarget->getAvailableHealth());
			const auto beforePrevented = std::min(static_cast<long double>(targetHealth),
				rawPerAttackDamage * beforeExposure / 10000.0L);
			const auto afterPrevented = std::min(static_cast<long double>(targetHealth),
				rawPerAttackDamage * afterExposure / 10000.0L);
			const auto addedProtection = afterPrevented - beforePrevented;
			if(!std::isfinite(addedProtection) || addedProtection <= 0.0L)
				continue;

			const auto savedDamage = static_cast<uint64_t>(std::floor(addedProtection));
			const auto value = heavenlyGaleSavedDamageValue(projectedTarget, savedDamage,
				damageCache, projectedBattle);
			bestAttackerValue = std::max(bestAttackerValue, value);
		}
		totalValue += bestAttackerValue;
	}

	return std::isfinite(totalValue) ? totalValue : 0.0f;
}

struct CrusadeProjection
{
	int attack = 0;
	int defense = 0;
	int initiative = 0;
	int magicalDamageReductionBasisPoints = 0;
	bool preventsNegativeMorale = false;
	int roundsRemaining = 0;

	bool hasCombatEffect() const
	{
		return attack > 0 || defense > 0 || initiative > 0
			|| magicalDamageReductionBasisPoints > 0 || preventsNegativeMorale;
	}
};

CrusadeProjection crusadeProjection(const battle::Unit * unit, SpellID spell)
{
	CrusadeProjection result;
	if(!unit)
		return result;

	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	if(!effects)
		return result;

	for(const auto & effect : *effects)
	{
		if(!effect || !Bonus::NTurns(effect.get()) || effect->turnsRemain <= 0)
			continue;

		result.roundsRemaining = std::max(result.roundsRemaining, static_cast<int>(effect->turnsRemain));
		if(effect->type == BonusType::PRIMARY_SKILL
			&& effect->subtype == BonusSubtypeID(PrimarySkill::ATTACK))
			result.attack = std::max(result.attack, effect->val);
		else if(effect->type == BonusType::PRIMARY_SKILL
			&& effect->subtype == BonusSubtypeID(PrimarySkill::DEFENSE))
			result.defense = std::max(result.defense, effect->val);
		else if(effect->type == BonusType::STACKS_INITIATIVE_FLAT)
			result.initiative = std::max(result.initiative, effect->val);
		else if(effect->type == BonusType::MINIMUM_MORALE && effect->val >= 0)
			result.preventsNegativeMorale = true;
		else if(effect->type == BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS)
			result.magicalDamageReductionBasisPoints = std::max(
				result.magicalDamageReductionBasisPoints, effect->val);
	}
	return result;
}

std::optional<bool> crusadeAttackMode(const battle::Unit * attacker,
	const battle::Unit * target, const CBattleInfoCallback & battle)
{
	if(!attacker || !target || !attacker->alive() || !target->alive()
		|| attacker->isGhost() || target->isGhost() || attacker->isTurret() || target->isTurret())
		return std::nullopt;

	for(const auto & targetHex : target->getHexes())
		if(targetHex.isValid() && battle.battleCanShoot(attacker, targetHex))
			return true;

	for(const auto & attackerHex : attacker->getHexes())
	{
		if(!attackerHex.isValid())
			continue;
		for(const auto & targetHex : target->getHexes())
			if(targetHex.isValid() && BattleHex::getDistance(attackerHex, targetHex) == 1)
				return false;
	}

	return std::nullopt;
}

/// Value only the real projected Crusade bonuses. Damage estimates include
/// Attack, Defense, and magical reduction together; marker duration and
/// presence come from the detached timed effects, not a second copy of the
/// spell's Spell Power formula. Each visible attacker contributes its best
/// currently legal direct attack against the opposing army.
float crusadeArmyValue(BattleSide side, uint32_t activeUnitId,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!projectedBattle)
		return 0.0f;

	float totalValue = 0.0f;
	const auto visibleUnits = liveBattle.battleGetAllUnits(true);
	for(const auto * attacker : visibleUnits)
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget()
			|| attacker->isGhost() || attacker->isTurret())
			continue;

		const bool friendlyAttacker = attacker->unitSide() == side;
		if(friendlyAttacker && attacker->unitId() == activeUnitId)
			continue; // The active stack's projected exchange is already in stackActionScore.

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		float bestAttackValue = 0.0f;
		for(const auto * target : visibleUnits)
		{
			if(!target || !target->alive() || !target->isValidTarget()
				|| target->unitSide() == attacker->unitSide()
				|| target->isGhost() || target->isTurret())
				continue;

			const auto attackMode = crusadeAttackMode(attacker, target, liveBattle);
			if(!attackMode)
				continue;

			const auto * projectedTarget = projectedBattle->battleGetUnitByID(target->unitId());
			if(!projectedTarget || !projectedTarget->alive())
				continue;

			const auto * blessedUnit = friendlyAttacker ? projectedAttacker : projectedTarget;
			const auto effect = crusadeProjection(blessedUnit, spell);
			if(effect.roundsRemaining <= 0 || !effect.hasCombatEffect())
				continue;

			const auto attacksPerActivation = std::max(1, attacker->getTotalAttacks(*attackMode));
			const float activationExposure = static_cast<float>(effect.roundsRemaining) * 0.5f;
			if(activationExposure <= 0.0f)
				continue;

			const BattleAttackInfo beforeAttack(attacker, target, 0, *attackMode);
			const BattleAttackInfo afterAttack(projectedAttacker, projectedTarget, 0, *attackMode);
			const float beforeDamage = averageOrderDamage(liveBattle.battleEstimateDamage(beforeAttack));
			const float afterDamage = averageOrderDamage(projectedBattle->battleEstimateDamage(afterAttack));
			const float damageDeltaPerActivation = friendlyAttacker
				? afterDamage - beforeDamage : beforeDamage - afterDamage;
			if(damageDeltaPerActivation <= 0.0f)
				continue;

			const auto availableHealth = std::max<int64_t>(0, target->getAvailableHealth());
			const long double projectedDelta = std::min(static_cast<long double>(availableHealth),
				static_cast<long double>(damageDeltaPerActivation)
					* static_cast<long double>(attacksPerActivation)
					* static_cast<long double>(activationExposure));
			if(!std::isfinite(projectedDelta) || projectedDelta <= 0.0L)
				continue;

			const auto affectedDamage = static_cast<uint64_t>(std::floor(projectedDelta));
			const auto value = heavenlyGaleSavedDamageValue(projectedTarget,
				affectedDamage, damageCache, projectedBattle);
			bestAttackValue = std::max(bestAttackValue, value);
		}
		totalValue += bestAttackValue;
	}

	return std::isfinite(totalValue) ? totalValue : 0.0f;
}

int shieldOfChaosRounds(const battle::Unit * unit, SpellID spell)
{
	if(!unit)
		return 0;

	const auto effects = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	if(!effects)
		return 0;

	int rounds = 0;
	for(const auto & effect : *effects)
		if(effect && Bonus::NTurns(effect.get()) && effect->turnsRemain > 0)
			rounds = std::max(rounds, static_cast<int>(effect->turnsRemain));
	return rounds;
}

float shieldOfChaosAttackEquivalents(const battle::Unit * attacker,
	const CBattleInfoCallback & battle, bool shooting, int roundsRemaining, uint32_t activeUnitId)
{
	if(!attacker || roundsRemaining <= 0)
		return 0.0f;

	const int availableAttacks = std::max(0, AttackPossibility::getAttackCount(*attacker, shooting, battle));
	int attacksPerFutureActivation = std::max(1, attacker->getTotalAttacks(shooting));
	if(shooting)
		if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
			attackerState && attackerState->shots.isLimited()
				&& attackerState->shots.available() <= availableAttacks)
			attacksPerFutureActivation = 0;

	// The active stack's immediate exchange is already included in stackActionScore.
	const int immediateAttacks = attacker->unitId() == activeUnitId ? 0 : availableAttacks;
	return static_cast<float>(immediateAttacks)
		+ (roundsRemaining > 1 ? 0.5f * static_cast<float>(attacksPerFutureActivation) : 0.0f);
}

float expectedVisibleCreatureSpellDamage(const CBattleInfoCallback & battle,
	const battle::Unit * caster, const battle::Unit * target)
{
	if(!caster || !target || !caster->canCast())
		return 0.0f;

	const auto spellcasters = caster->getBonuses(Selector::type()(BonusType::SPELLCASTER));
	if(!spellcasters)
		return 0.0f;

	float bestSpellDamage = 0.0f;
	for(const auto & bonus : *spellcasters)
	{
		if(!bonus || bonus->parameters || !bonus->subtype.as<SpellID>().hasValue())
			continue;

		const auto * spell = bonus->subtype.as<SpellID>().toSpell();
		if(!spell || !spell->isCombat() || (!spell->isOffensive() && !spell->isDamage())
			|| !spell->isMagical() || spell->isCreatureAbility())
			continue;

		spells::BattleCast cast(&battle, caster, spells::Mode::CREATURE_ACTIVE, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		if(!mechanics || !mechanics->isReceptive(target))
			continue;

		spells::Target aim{spells::Destination(target)};
		spells::detail::ProblemImpl problem;
		if(!mechanics->canBeCastAt(aim, problem))
			continue;

		float applicationChance = 1.0f;
		if(mechanics->isNegativeSpell() && mechanics->isMagicalEffect())
			applicationChance -= static_cast<float>(std::clamp(target->magicResistance(), 0, 100)) / 100.0f;

		const auto adjustedDamage = std::max<int64_t>(0, mechanics->adjustEffectValue(target));
		bestSpellDamage = std::max(bestSpellDamage,
			static_cast<float>(adjustedDamage) * applicationChance);
	}
	return bestSpellDamage;
}

float shieldOfChaosIncomingDamageValue(uint32_t activeUnitId,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->isGhost() || liveTarget->isTurret() || !projectedBattle)
		return 0.0f;

	const int rounds = shieldOfChaosRounds(projectedTarget, spell);
	if(rounds <= 0)
		return 0.0f;

	long double savedDamage = 0.0L;
	for(const auto * attacker : liveBattle.battleGetAllUnits(false))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget()
			|| attacker->isGhost() || attacker->isTurret()
			|| attacker->unitSide() == liveTarget->unitSide())
			continue;

		const auto attackMode = crusadeAttackMode(attacker, liveTarget, liveBattle);
		if(!attackMode)
			continue;

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		const float attackEquivalents = shieldOfChaosAttackEquivalents(
			attacker, liveBattle, *attackMode, rounds, activeUnitId);
		if(attackEquivalents <= 0.0f)
			continue;

		const auto beforeDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(attacker, liveTarget, projectedBattle));
		const auto afterDamage = std::max<int64_t>(0,
			damageCache.getDamage(projectedAttacker, projectedTarget, projectedBattle));
		if(beforeDamage > afterDamage)
			savedDamage += static_cast<long double>(beforeDamage - afterDamage) * attackEquivalents;
	}

	// Price visible creature spellcasters through the actual projected spell
	// damage path. Their hidden or randomized spell choices are never inspected.
	for(const auto * caster : liveBattle.battleGetAllUnits(false))
	{
		if(!caster || !caster->alive() || caster->isGhost() || caster->isTurret()
			|| caster->unitSide() == liveTarget->unitSide())
			continue;

		const auto * projectedCaster = projectedBattle->battleGetUnitByID(caster->unitId());
		if(!projectedCaster || !projectedCaster->alive())
			continue;

		const auto beforeDamage = expectedVisibleCreatureSpellDamage(liveBattle, caster, liveTarget);
		const auto afterDamage = expectedVisibleCreatureSpellDamage(*projectedBattle,
			projectedCaster, projectedTarget);
		if(beforeDamage > afterDamage)
			savedDamage += static_cast<long double>(beforeDamage - afterDamage)
				* static_cast<long double>(rounds) * 0.5L;
	}

	const auto availableHealth = std::max<int64_t>(0, liveTarget->getAvailableHealth());
	savedDamage = std::clamp(savedDamage, 0.0L, static_cast<long double>(availableHealth));
	if(!std::isfinite(savedDamage) || savedDamage < 1.0L)
		return 0.0f;

	return heavenlyGaleSavedDamageValue(projectedTarget,
		static_cast<uint64_t>(std::floor(savedDamage)), damageCache, projectedBattle);
}

float shieldOfChaosLuckOutputDelta(uint32_t activeUnitId,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	int roundsRemaining, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->isGhost() || liveTarget->isTurret() || !projectedBattle
		|| roundsRemaining <= 0)
		return 0.0f;

	float bestBeforeValue = 0.0f;
	float bestAfterValue = 0.0f;
	for(const auto * victim : liveBattle.battleGetAllUnits(false))
	{
		if(!victim || !victim->alive() || !victim->isValidTarget(true)
			|| victim->isGhost() || victim->isTurret()
			|| victim->unitSide() == liveTarget->unitSide())
			continue;

		const auto attackMode = crusadeAttackMode(liveTarget, victim, liveBattle);
		if(!attackMode)
			continue;

		const auto * projectedVictim = projectedBattle->battleGetUnitByID(victim->unitId());
		if(!projectedVictim || !projectedVictim->alive())
			continue;

		const float attackEquivalents = shieldOfChaosAttackEquivalents(
			liveTarget, liveBattle, *attackMode, roundsRemaining, activeUnitId);
		if(attackEquivalents <= 0.0f)
			continue;

		const auto availableHealth = std::max<int64_t>(0, victim->getAvailableHealth());
		if(availableHealth <= 0)
			continue;

		const BattleAttackInfo beforeAttack(liveTarget, victim, 0, *attackMode);
		const BattleAttackInfo afterAttack(projectedTarget, projectedVictim, 0, *attackMode);
		const auto beforeDamage = std::min<long double>(availableHealth,
			static_cast<long double>(std::max<int64_t>(0, liveBattle.battleExpectedLuckDamage(beforeAttack)))
				* attackEquivalents);
		const auto afterDamage = std::min<long double>(availableHealth,
			static_cast<long double>(std::max<int64_t>(0, projectedBattle->battleExpectedLuckDamage(afterAttack)))
				* attackEquivalents);

		if(beforeDamage > 0.0L)
			bestBeforeValue = std::max(bestBeforeValue, heavenlyGaleSavedDamageValue(
				victim, static_cast<uint64_t>(std::floor(beforeDamage)), damageCache, projectedBattle));
		if(afterDamage > 0.0L)
			bestAfterValue = std::max(bestAfterValue, heavenlyGaleSavedDamageValue(
				projectedVictim, static_cast<uint64_t>(std::floor(afterDamage)), damageCache, projectedBattle));
	}

	return bestAfterValue - bestBeforeValue;
}

float shieldOfChaosTargetValue(uint32_t activeUnitId, BattleSide scoringSide,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive())
		return 0.0f;

	const int rounds = shieldOfChaosRounds(projectedTarget, spell);
	if(rounds <= 0)
		return 0.0f;

	const float targetSign = liveTarget->unitSide() == scoringSide ? 1.0f : -1.0f;
	const auto protection = shieldOfChaosIncomingDamageValue(activeUnitId, liveTarget,
		projectedTarget, liveBattle, projectedBattle, spell, damageCache);
	const auto luckOutputDelta = shieldOfChaosLuckOutputDelta(activeUnitId, liveTarget,
		projectedTarget, liveBattle, projectedBattle, rounds, damageCache);

	// This is an expected activation delta from the configured chance table, not
	// the battlefield's seeded morale roll. A bounded best-attack value converts
	// that probability to the same stack-value scale as the damage terms above.
	const float moraleActivationDelta = projectedBattle->projectMoraleActivationDelta(liveTarget, projectedTarget,
		expectedMoraleActivationChange(liveBattle, liveTarget),
		expectedMoraleActivationChange(*projectedBattle, projectedTarget),
		static_cast<float>(rounds) * 0.5f);
	const float moraleOutputDelta = moraleActivationDelta
		* expectedTargetActivationValue(projectedTarget, damageCache, projectedBattle);

	const float value = targetSign * (protection + luckOutputDelta + moraleOutputDelta);
	return std::isfinite(value) ? value : 0.0f;
}

struct DivineRetributionProtection
{
	int rawCap = 0;
	int retributionistPercent = 100;
	int roundsRemaining = 0;
};

DivineRetributionProtection divineRetributionProtection(const battle::Unit * unit, SpellID spell)
{
	DivineRetributionProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	if(!bonuses)
		return result;

	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		int retributionistPercent = 100;
		if(bonus->parameters)
		{
			try
			{
				const auto & parameters = bonus->parameters->toCustom<JsonNode>();
				const auto percent = parameters["retributionistPercent"];
				if(percent.isNumber())
					retributionistPercent = static_cast<int>(percent.Integer());
			}
			catch(const std::runtime_error &)
			{
				// Older or unrelated marker payloads keep the neutral 100% behavior.
			}
		}

		const DivineRetributionProtection current{
			std::max(0, bonus->val),
			std::clamp(retributionistPercent, 100, 120),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		const int64_t currentExposure = static_cast<int64_t>(current.rawCap)
			* current.retributionistPercent * current.roundsRemaining;
		const int64_t resultExposure = static_cast<int64_t>(result.rawCap)
			* result.retributionistPercent * result.roundsRemaining;
		if(currentExposure > resultExposure)
			result = current;
	}
	return result;
}

int divineRetributionDamage(const DivineRetributionProtection & protection,
	long double actualDamage, int roundOffset)
{
	if(protection.roundsRemaining <= roundOffset || protection.rawCap <= 0
		|| !std::isfinite(actualDamage) || actualDamage <= 0.0L)
		return 0;

	const auto cappedBaseDamage = std::min(static_cast<long double>(protection.rawCap),
		std::floor(actualDamage * 0.30L));
	const auto finalDamage = std::floor(cappedBaseDamage
		* static_cast<long double>(protection.retributionistPercent) / 100.0L);
	if(finalDamage <= 0.0L)
		return 0;
	return static_cast<int>(std::min(finalDamage,
		static_cast<long double>(std::numeric_limits<int>::max())));
}

float divineRetributionDamageValue(const battle::Unit * target, uint64_t damage,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || damage == 0 || !projectedBattle)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, target, damage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = target->getMaxHealth();
	const auto * creature = target->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	const long double healthValue = static_cast<long double>(damage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

/// Estimate the marginal delayed Holy response from visible physical creature
/// attackers. Siege weapons, towers, spell-like shots, and hidden hero spellbooks
/// are deliberately excluded. Each stack contributes its strongest legal
/// melee or ranged attack once per round, with a bounded second-round forecast.
float divineRetributionThreatValue(const battle::Unit * liveTarget,
	const battle::Unit * projectedTarget, BattleSide side,
	const CBattleInfoCallback & liveBattle,
	const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->getAvailableHealth() <= 0 || !projectedBattle)
		return 0.0f;

	const auto before = divineRetributionProtection(liveTarget, spell);
	const auto after = divineRetributionProtection(projectedTarget, spell);
	if(after.roundsRemaining <= 0 || after.rawCap <= 0)
		return 0.0f;

	float totalValue = 0.0f;
	for(const auto * attacker : liveBattle.battleGetAllUnits(true))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget() || attacker->isGhost() || attacker->isTurret()
			|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| attacker->unitSide() == side || !liveBattle.battleMatchOwner(attacker, liveTarget))
			continue;

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		bool canShootTarget = false;
		if(attacker->canShoot())
			for(const auto & targetHex : liveTarget->getHexes())
				if(liveBattle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}

		bool canMeleeTarget = false;
		if(attacker->isMeleeAttacker())
		{
			canMeleeTarget = !liveBattle.meleeAttackHexes(attacker, liveTarget,
				attacker->getPosition()).empty();
			if(!canMeleeTarget)
				for(const auto & position : liveBattle.battleGetAvailableHexes(attacker, false))
					if(!liveBattle.meleeAttackHexes(attacker, liveTarget, position).empty())
					{
						canMeleeTarget = true;
						break;
					}
		}

		float attackerValue = 0.0f;
		for(const int roundOffset : {0, 1})
		{
			const float roundWeight = roundOffset == 0 ? 1.0f : 0.5f;
			int afterAttacks = roundOffset == 0
				? std::max(0, AttackPossibility::getAttackCount(*attacker, true, liveBattle))
				: std::max(1, attacker->getTotalAttacks(true));
			if(canShootTarget && roundOffset == 1)
				if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
					attackerState && attackerState->shots.isLimited())
				{
					const int currentShots = std::max(0,
						AttackPossibility::getAttackCount(*attacker, true, liveBattle));
					afterAttacks = std::min(afterAttacks,
						std::max(0, attackerState->shots.available() - currentShots));
				}

			int beforeResponse = 0;
			int afterResponse = 0;
			for(const bool shooting : {false, true})
			{
				if((shooting && !canShootTarget) || (!shooting && !canMeleeTarget))
					continue;

				const BattleAttackInfo attack(attacker, liveTarget, 0, shooting);
				if(!attack.physicalDamage)
					continue;

				const auto estimate = liveBattle.battleEstimateDamage(attack);
				const auto perAttackDamage = static_cast<long double>(std::max<int64_t>(0,
					estimate.damage.min + estimate.damage.max)) / 2.0L;
				if(perAttackDamage <= 0.0L)
					continue;

				const int attackCount = shooting ? afterAttacks : (roundOffset == 0
					? std::max(0, AttackPossibility::getAttackCount(*attacker, false, liveBattle))
					: std::max(1, attacker->getTotalAttacks(false)));
				if(attackCount <= 0)
					continue;

				const auto actualDamage = std::min(
					static_cast<long double>(liveTarget->getAvailableHealth()),
					perAttackDamage * static_cast<long double>(attackCount));
				beforeResponse = std::max(beforeResponse,
					divineRetributionDamage(before, actualDamage, roundOffset));
				afterResponse = std::max(afterResponse,
					divineRetributionDamage(after, actualDamage, roundOffset));
			}

			if(afterResponse <= beforeResponse)
				continue;

			attackerValue += divineRetributionDamageValue(projectedAttacker,
				static_cast<uint64_t>(afterResponse - beforeResponse), damageCache, projectedBattle)
				* roundWeight;
		}
		totalValue += attackerValue;
	}

	return std::isfinite(totalValue) ? totalValue : 0.0f;
}

/// Estimate visible creature spell pressure without querying a concealed enemy
/// hero or asking a random-spellcaster to choose an ability. Spellcaster
/// bonuses are part of the visible creature stack; each stack contributes its
/// strongest available direct-damage spell once. `magicalOnly` is for defenses
/// that reduce magical damage but do not affect physical attacks. Do not call
/// `canBeCast` here: a player-specific callback may refuse to answer for an
/// opposing side, which is not evidence that its visible spellcaster is inert.
/// `canCast()` and explicit visible SPELLCASTER bonuses bound this estimate.
float visibleCreatureSpellThreat(
	const std::vector<const battle::Unit *> & enemyUnits, bool magicalOnly = false)
{
	float totalThreat = 0.0f;
	for(const auto * enemy : enemyUnits)
	{
		if(!enemy || !enemy->canCast())
			continue;
		float bestSpellDamage = 0.0f;
		const auto spellcasters = enemy->getBonuses(Selector::type()(BonusType::SPELLCASTER));
		if(!spellcasters)
			continue;
		for(const auto & bonus : *spellcasters)
		{
			if(!bonus || bonus->parameters || !bonus->subtype.as<SpellID>().hasValue())
				continue;
			const auto * spell = bonus->subtype.as<SpellID>().toSpell();
			if(!spell || !spell->isCombat()
				|| (!spell->isOffensive() && !spell->isDamage())
				|| spell->isCreatureAbility()
				|| (magicalOnly && !spell->isMagical()))
				continue;
			const auto spellDamage = static_cast<float>(std::max<int64_t>(0, spell->calculateDamage(enemy)));
			bestSpellDamage = std::max(bestSpellDamage, spellDamage);
		}
		totalThreat += bestSpellDamage;
	}
	return totalThreat;
}

float holyArmorMitigationValue(BattleSide side,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget, SpellID spell,
	int64_t friendlyHealth, float visibleMagicalThreat, DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> projectedBattle)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->unitSide() != side || visibleMagicalThreat <= 0.0f || friendlyHealth <= 0)
		return 0.0f;

	const auto targetHealth = liveTarget->getAvailableHealth();
	if(targetHealth <= 0)
		return 0.0f;

	const auto before = holyArmorProtection(liveTarget, spell);
	const auto after = holyArmorProtection(projectedTarget, spell);
	const int addedExposure = after.exposurePercentRounds() - before.exposurePercentRounds();
	if(addedExposure <= 0)
		return 0.0f;

	// Spread the visible creature-caster volley across allied health as a bounded
	// target-priority estimate. The detached cast supplies the actual Holy Armor
	// reduction and duration; the threat side deliberately uses one best spell
	// per visible caster and does not infer anything from an enemy hero's book.
	const long double targetShare = static_cast<long double>(targetHealth)
		/ static_cast<long double>(friendlyHealth);
	const long double estimatedSavedDamage = std::min(
		static_cast<long double>(targetHealth),
		static_cast<long double>(visibleMagicalThreat) * targetShare
			* static_cast<long double>(addedExposure) / 100.0L);
	if(!std::isfinite(estimatedSavedDamage) || estimatedSavedDamage <= 0.0L)
		return 0.0f;

	const auto savedDamage = static_cast<uint64_t>(std::floor(estimatedSavedDamage));
	if(savedDamage == 0)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, liveTarget, savedDamage, damageCache, std::move(projectedBattle));
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;
	const auto maxHealth = liveTarget->getMaxHealth();
	const auto creatureValue = liveTarget->unitType()->getAIValue();
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	// The usual damage-reduction value reflects the attacks the saved stack can
	// still make. That can be zero for a distant or temporarily immobilized unit,
	// even though preserving its health is useful. Fall back to a bounded share
	// of the stack's creature value so defensive spells are not treated as no-ops.
	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	const auto boundedHealthValue = static_cast<float>(std::min(
		healthValue, static_cast<long double>(std::numeric_limits<float>::max())));
	return boundedHealthValue;
}

namespace
{
/// Values only enemy attacks that deliberately name the Sanctified stack as
/// their primary target. If it is merely collateral from an attack on another
/// stack, Sanctuary does not prevent that damage and must not receive credit.
float sanctuaryDirectAttackValue(uint32_t targetUnitId, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & battleState)
{
	if(!battleState)
		return 0.0f;

	const auto * target = battleState->battleGetUnitByID(targetUnitId);
	if(!target || !target->alive() || target->getAvailableHealth() <= 0
		|| target->hasBonusOfType(BonusType::SANCTIFIED))
		return 0.0f;

	int64_t incomingDirectDamage = 0;
	for(const auto * attacker : battleState->battleGetAllUnits(false))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget()
			|| attacker->isGhost() || attacker->isTurret()
			|| !battleState->battleMatchOwner(attacker, target))
			continue;

		PotentialTargets potentialTargets(attacker, damageCache, battleState);
		int64_t bestDirectDamage = 0;
		for(const auto & attack : potentialTargets.possibleAttacks)
		{
			if(!attack.attack.defender || attack.attack.defender->unitId() != targetUnitId)
				continue;

			for(const auto & affected : attack.affectedUnits)
			{
				if(!affected || affected->unitId() != targetUnitId)
					continue;

				const auto damage = std::max<int64_t>(0,
					target->getAvailableHealth() - affected->getAvailableHealth());
				bestDirectDamage = std::max(bestDirectDamage, damage);
			}
		}

		incomingDirectDamage += bestDirectDamage;
		if(incomingDirectDamage >= target->getAvailableHealth())
		{
			incomingDirectDamage = target->getAvailableHealth();
			break;
		}
	}

	if(incomingDirectDamage <= 0)
		return 0.0f;

	return AttackPossibility::calculateDamageReduce(nullptr, target,
		static_cast<uint64_t>(incomingDirectDamage), damageCache, battleState);
}

float sanctuaryKeeperMoraleValue(uint32_t targetUnitId, BattleSide casterSide,
	const CBattleInfoCallback & liveBattle, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!projectedBattle || !liveBattle.getBattle())
		return 0.0f;

	const auto & magicRules = liveBattle.getBattle()->getMagicRules();
	const auto * hero = liveBattle.getBattle()->getSideHero(casterSide);
	if(!newHorizonsMagic::rulesActive(magicRules)
		|| magicRules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !hero || !hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.sanctuaryKeeper"))
		return 0.0f;

	const auto * original = liveBattle.battleGetUnitByID(targetUnitId);
	const auto * projected = projectedBattle->battleGetUnitByID(targetUnitId);
	if(!original || !projected || !original->alive() || !projected->alive()
		|| original->unitSide() != casterSide || projected->unitSide() != casterSide
		|| original->unaffectedByMorale() || projected->unaffectedByMorale())
		return 0.0f;

	const auto sanctuaryMarkers = projected->getBonusesOfType(BonusType::SANCTIFIED);
	const bool hasCurrentSanctuaryMarker = sanctuaryMarkers
		&& std::any_of(sanctuaryMarkers->begin(), sanctuaryMarkers->end(), [](const auto & bonus)
		{
			return bonus && bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid.toString() == "new-horizons:sanctuary";
		});
	const auto existingMorale = projected->getBonuses(Selector::type()(BonusType::MORALE));
	const bool alreadyHasKeeperMorale = hasCurrentSanctuaryMarker && existingMorale
		&& std::any_of(existingMorale->begin(), existingMorale->end(), [](const auto & bonus)
		{
			return bonus && bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid.toString() == "new-horizons:sanctuary";
		});
	if(alreadyHasKeeperMorale)
		return 0.0f;

	const int morale = liveBattle.battleGetMorale(original);
	if(morale >= 0)
		return 0.0f;

	// Sanctuary breaks before the protected stack resolves its action, so this
	// perk can only prevent a bad-Morale penalty before the next activation.
	// In particular, do not price a good-Morale bonus after that action.
	const float before = expectedMoraleActivationChange(morale);
	const float after = expectedMoraleActivationChange(std::min(0, morale + 2));
	const float preventedBadMorale = projectedBattle->projectMoraleActivationDelta(
		original, projected, before, after, 1.0f);
	if(preventedBadMorale <= 0.0f)
		return 0.0f;

	return preventedBadMorale * expectedTargetActivationValue(projected, damageCache, projectedBattle);
}
}

/// Hero presence is exposed through InfoAboutHero even when the opposing
/// hero's detailed state is concealed.  Use only that public presence bit and
/// the allied army's visible health to represent a modest possible hero spell;
/// never inspect the hidden hero's book, mana, attributes, or perks.
float publicEnemyHeroSpellThreat(const CBattleInfoCallback & battle, BattleSide side,
	const std::vector<const battle::Unit *> & ownUnits)
{
	const auto enemySide = side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	const auto enemyHero = battle.battleGetHeroInfo(enemySide);
	if(enemyHero.owner == PlayerColor::NEUTRAL)
		return 0.0f;

	int64_t alliedHealth = 0;
	for(const auto * own : ownUnits)
		if(own)
			alliedHealth += own->getAvailableHealth();

	// Estimate at most 4% of current allied health and cap it so the public
	// presence signal remains a modest prior rather than overwhelming real
	// spell or attack valuations.
	return std::min(static_cast<float>(alliedHealth) * 0.04f, 500.0f);
}

float canonicalOrderBaseHeuristic(const CBattleInfoCallback & battle, BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetIds,
	std::optional<int> focusFireSnapshotPercent, const Environment * environment,
	DamageCache & damageCache, std::shared_ptr<CBattleInfoCallback> realBattle)
{
	const auto perspective = battle.battleGetMySide();
	const bool heroKnown = perspective == BattleSide::ALL_KNOWING || perspective == side;
	const auto * hero = heroKnown ? battle.battleGetFightingHero(side) : nullptr;
	const auto units = battle.battleGetAllUnits(false);
	const auto ownPlayer = battle.sideToPlayer(side);
	std::vector<const battle::Unit *> ownUnits;
	std::vector<const battle::Unit *> enemyUnits;
	for(const auto * unit : units)
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
			continue;
		if(battle.battleGetOwner(unit) == ownPlayer)
			ownUnits.push_back(unit);
		else
			enemyUnits.push_back(unit);
	}

	const auto & commandRules = battle.getBattle()->getHeroCommandRules()["commands"];
	const bool combinedArmsEnabled = hero
		&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		&& heroCommands::hasCombinedArms(hero);
	const auto round = battle.battleGetRound();
	const auto orderAllowance = battle.battleGetOrderActionAllowance(side);
	const auto preparedOrder = hero
		? battle.battlePrepareHeroOrderState(side, command, targetIds)
		: std::optional<HeroOrderState>();
	const int warcastingBonus = hero && orderAllowance
		&& orderAllowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO
		&& newHorizonsWarcasting::enabled(battle.getBattle()->getMagicRules())
		? newHorizonsWarcasting::orderBonus(hero, battle.getBattle()->getWarcastingState(side), round) : 0;
	const int divineMandateEfficiencyBonusPercent = preparedOrder
		? preparedOrder->divineMandateEfficiencyBonusPercent() : 0;
	const auto coefficient = [&](const char * commandKey, const char * effectKey)
	{
		const auto & formula = commandRules[commandKey]["effects"][effectKey];
		return static_cast<float>(hero ? heroCommands::coefficient(formula, *hero, warcastingBonus,
			divineMandateEfficiencyBonusPercent)
			: heroCommands::coefficient(formula, 0, 0));
	};
	const auto meleeDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit || !attackerUnit->isMeleeAttacker())
			return 0.0f;
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, false)));
	};
	const auto anyDamage = [&](const battle::Unit * attackerUnit, const battle::Unit * defenderUnit)
	{
		if(!attackerUnit || !defenderUnit)
			return 0.0f;
		const bool shooting = battle.battleCanShoot(attackerUnit, defenderUnit->getPosition());
		return averageOrderDamage(battle.battleEstimateDamage(
			BattleAttackInfo(attackerUnit, defenderUnit, 0, shooting)));
	};
	const auto bestOwnMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : ownUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto bestEnemyMeleeDamage = [&](const battle::Unit * defenderUnit)
	{
		float best = 0.0f;
		for(const auto * unit : enemyUnits)
			best = std::max(best, meleeDamage(unit, defenderUnit));
		return best;
	};
	const auto sumOwnMeleeDamage = [&]
	{
		float total = 0.0f;
		for(const auto * own : ownUnits)
		{
			float best = 0.0f;
			for(const auto * enemy : enemyUnits)
				best = std::max(best, meleeDamage(own, enemy));
			total += best;
		}
		return total;
	};
	const auto ownMeleePotential = sumOwnMeleeDamage();
	if(command == HeroCommand::FOCUS_FIRE && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		// The prepared snapshot is shared with the runtime, so Combined Arms uses
		// precisely half of the Order's ranged coefficient (not a recomputed
		// shooter-only extension such as Target Caller).
		const auto rangedPercent = focusFireSnapshotPercent.value_or(
			static_cast<int>(coefficient("focusFire", "rangedDamagePercent")));
		const auto combinedArmsMeleePercent = combinedArmsEnabled
			? static_cast<float>(heroCommands::combinedArmsFocusFirePercent(rangedPercent, *hero)) : 0.0f;
		float potential = 0.0f;
		for(const auto * unit : ownUnits)
		{
			if(!unit->willMove(0))
				continue;
			const bool canReceiveRangedBonus = unit->isShooter() && !unit->isTurret()
				&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& battle.battleCanShoot(unit, target->getPosition());
			const float rangedValue = canReceiveRangedBonus
				? anyDamage(unit, target) * static_cast<float>(rangedPercent) / 100.0f : 0.0f;
			const bool canReceiveMeleeBonus = combinedArmsMeleePercent > 0.0f
				&& isEligibleOrderUnit(battle, side, unit)
				&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
				&& unit->isMeleeAttacker();
			const float meleeValue = canReceiveMeleeBonus
				? meleeDamage(unit, target) * combinedArmsMeleePercent / 100.0f : 0.0f;
			// A stack chooses one attack mode during its activation.  If a shooter
			// can also make a melee attack, value the better Focus Fire benefit rather
			// than counting the same stack twice.
			potential += std::max(rangedValue, meleeValue);
		}
		return potential;
	}
	const auto chargePercent = coefficient("charge", "meleeDamagePercent");
	const auto holdPercent = coefficient("holdTheLine", "damageReductionPercent");
	const auto riposteReduction = coefficient("riposte", "meleeDamageReductionPercent");
	const auto riposteDamage = coefficient("riposte", "retaliationDamagePercent");
	const auto braceDamage = coefficient("brace", "preemptiveDamagePercent");
	const auto protectReduction = coefficient("protect", "interceptedDamageReductionPercent");
	const auto flankDamage = coefficient("flank", "meleeDamagePercent");
	const auto & flankFormula = commandRules["flank"]["effects"]["meleeDamagePercent"];
	const auto combinedArmsRangedPercent = combinedArmsEnabled
		? static_cast<float>(heroCommands::combinedArmsFlankPercent(flankFormula, *hero, warcastingBonus,
			divineMandateEfficiencyBonusPercent))
		: 0.0f;

	if(command == HeroCommand::CHARGE)
	{
		const bool canCharge = std::any_of(ownUnits.begin(), ownUnits.end(), [](const auto * unit)
		{
			return unit->isMeleeAttacker() && unit->getMovementRange(0) >= 3;
		});
		return canCharge ? ownMeleePotential * chargePercent / 100.0f : 0.0f;
	}

	if(command == HeroCommand::HOLD_THE_LINE)
	{
		float incoming = 0.0f;
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
				if(enemy->isMeleeAttacker())
					incoming = std::max(incoming, meleeDamage(enemy, own));
		float value = incoming * holdPercent / 100.0f;
		const bool hasIronDiscipline = hero && hero->hasActivePerk(newHorizonsIronDiscipline::SKILL,
			newHorizonsIronDiscipline::PERK);
		const bool hasHoldFast = newHorizonsDiscipline::hasHoldFast(hero);
		const auto prepared = (hasIronDiscipline || hasHoldFast)
			? battle.battlePrepareHeroOrderState(side, command, {}) : std::optional<HeroOrderState>();
		if(hasIronDiscipline)
		{
			// Use the exact saved reduction that issuing Hold would snapshot.  The
			// prepare query is read-only.  Threat estimation uses only public basic
			// enemy-hero presence, visible allied health and visible creature spells.
			if(prepared && prepared->holdMagicalReductionBasisPoints > 0)
			{
				const auto visibleCreatureThreat = visibleCreatureSpellThreat(enemyUnits, true);
				const auto publicHeroThreat = publicEnemyHeroSpellThreat(battle, side, ownUnits);
				const auto magicalThreat = std::max(visibleCreatureThreat, publicHeroThreat);
				value += magicalThreat * static_cast<float>(prepared->holdMagicalReductionBasisPoints) / 10000.0f;
			}
		}
		if(hasHoldFast && prepared && environment && realBattle)
		{
			auto holdFastPreview = std::make_shared<HypotheticBattle>(environment, realBattle);
			for(const auto & anchor : prepared->anchors)
			{
				const auto * unit = battle.battleGetUnitByID(anchor.unitId);
				if(!unit || !unit->alive() || unit->isGhost()
					|| battle.battleGetOwner(unit) != ownPlayer
					|| unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
					|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
					|| unit->unaffectedByMorale() || battle.battleGetMorale(unit) >= 0)
					continue;
				auto projected = holdFastPreview->getForUpdate(unit->unitId());
				projected->addUnitBonus(std::vector<Bonus>{
					newHorizonsDiscipline::holdFastMoraleFloorBonus()});
				value += holdFastMoraleGrantValue(battle, unit, projected.get(), damageCache, holdFastPreview);
			}
		}
		return value;
	}

	if(command == riposteCommand())
	{
		float incoming = 0.0f;
		float retaliation = 0.0f;
		float vengeanceRetaliation = 0.0f;
		static const auto firstStrikeSelector = Selector::typeSubtype(
			BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
			.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
		for(const auto * own : ownUnits)
			for(const auto * enemy : enemyUnits)
			{
				if(!enemy->isMeleeAttacker())
					continue;
				const BattleAttackInfo incomingAttack(enemy, own, 0, false);
				DamageEstimation retaliationEstimate;
				const auto incomingEstimate = battle.battleEstimateDamage(incomingAttack, &retaliationEstimate);
				incoming = std::max(incoming, averageOrderDamage(incomingEstimate));
				retaliation = std::max(retaliation, averageOrderDamage(retaliationEstimate));
				if(hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE)
					&& isEligibleOrderUnit(battle, side, own)
					&& own->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
					&& !own->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS)
					&& !own->isTimeStopped()
					&& !own->hasBonusOfType(BonusType::NO_RETALIATION)
					&& !enemy->hasBonusOfType(BonusType::BLOCKS_RETALIATION)
					&& !enemy->isInvincible()
					&& !battle.isLongWeaponAttack(enemy, own)
					&& battle.isMeleeAttackPossible(enemy, own)
					&& (!battle.battleShroudDeniesRetaliation(incomingAttack)
						|| own->hasBonus(firstStrikeSelector))
					&& !newHorizonsOffense::hasVengeanceRetaliationBonus(own))
				{
					// Vengeance supplies the charge that ableToRetaliate() would otherwise
					// reject after a spent counter. Mirror the engine's non-ammunition
					// restrictions above, then estimate from detached post-hit states.
					const auto estimateExtraCounter = [&](int64_t projectedDamage)
					{
						auto projectedOwn = own->acquireState();
						projectedOwn->damage(projectedDamage);
						if(!projectedOwn->alive())
							return DamageEstimation();
						BattleAttackInfo extraRetaliation(projectedOwn.get(), enemy, 0, false);
						extraRetaliation.retaliation = true;
						return battle.battleEstimateDamage(extraRetaliation);
					};
					const auto counterAfterMinimumDamage = estimateExtraCounter(incomingEstimate.damage.min);
					const auto counterAfterMaximumDamage = estimateExtraCounter(incomingEstimate.damage.max);
					DamageEstimation projectedRetaliation;
					projectedRetaliation.damage.min = std::min(counterAfterMinimumDamage.damage.min,
						counterAfterMaximumDamage.damage.min);
					projectedRetaliation.damage.max = std::max(counterAfterMinimumDamage.damage.max,
						counterAfterMaximumDamage.damage.max);
					projectedRetaliation.kills.min = std::min(counterAfterMinimumDamage.kills.min,
						counterAfterMaximumDamage.kills.min);
					projectedRetaliation.kills.max = std::max(counterAfterMinimumDamage.kills.max,
						counterAfterMaximumDamage.kills.max);
					vengeanceRetaliation = std::max(vengeanceRetaliation,
						averageOrderDamage(projectedRetaliation));
				}
			}
		const float extraRetaliation = vengeanceRetaliation * (100.0f + riposteDamage) / 100.0f;
		return incoming * riposteReduction / 100.0f
			+ retaliation * riposteDamage / 100.0f + extraRetaliation;
	}

	if(command == braceCommand())
	{
		float advancingDamage = 0.0f;
		for(const auto * enemy : enemyUnits)
			if(enemy->isMeleeAttacker() && enemy->getMovementRange(0) >= 3)
				for(const auto * own : ownUnits)
					// Brace is a pre-emptive blow by the defending friendly stack,
					// not a multiplier on the advancing enemy's own attack.
					advancingDamage = std::max(advancingDamage, meleeDamage(own, enemy));
		const auto resolvedBraceDamage = newHorizonsCombatSkills::bracePreemptivePercent(braceDamage, hero);
		return advancingDamage * resolvedBraceDamage / 100.0f;
	}

	if(command == protectCommand() && targetIds.size() == 2)
	{
		const auto * protector = battle.battleGetUnitByID(targetIds[0]);
		const auto * ward = battle.battleGetUnitByID(targetIds[1]);
		if(!isEligibleOrderUnit(battle, side, protector) || !isEligibleOrderUnit(battle, side, ward))
			return 0.0f;
		return bestEnemyMeleeDamage(ward) * protectReduction / 100.0f;
	}

	if(command == flankCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		if(!target || !target->alive() || battle.battleGetOwner(target) == battle.sideToPlayer(side))
			return 0.0f;
		const bool targetCanBeFlanked = !battle.battleHasFormationFightingProtection(target);
		// Estimate the immediate opportunity from allied melee stacks that have not
		// acted yet. The authoritative attack path records exact sides as attacks
		// resolve; this projection reads current contacts only and never mutates them.
		uint8_t availableSides = 0;
		for(const auto * unit : ownUnits)
		{
			if(!targetCanBeFlanked || !unit->isMeleeAttacker() || !unit->willMove(0))
				continue;
			availableSides |= battle.battleHeroOrderFlankSide(unit, target);
		}
		int distinctSides = 0;
		for(auto bits = availableSides; bits; bits &= static_cast<uint8_t>(bits - 1))
			++distinctSides;
		const int additionalSides = std::max(0, distinctSides - 1);
		const int additionalSidePercent = battle.battleHeroOrderFlankAdditionalSidePercent(
			side, warcastingBonus, divineMandateEfficiencyBonusPercent);
		const auto meleeOrderValue = targetCanBeFlanked
			? bestOwnMeleeDamage(target) * (flankDamage + additionalSides * additionalSidePercent) / 100.0f
			: 0.0f;
		if(combinedArmsRangedPercent <= 0.0f)
			return meleeOrderValue;

		const battle::Unit * bestMeleeAttacker = nullptr;
		float bestMeleeDamage = 0.0f;
		for(const auto * unit : ownUnits)
		{
			const auto value = meleeDamage(unit, target);
			if(value > bestMeleeDamage)
			{
				bestMeleeDamage = value;
				bestMeleeAttacker = unit;
			}
		}
		float rangedOpportunity = 0.0f;
		float overlappingMeleeAttackerRangedOpportunity = 0.0f;
		for(const auto * unit : ownUnits)
		{
			if(!isEligibleOrderUnit(battle, side, unit)
				|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
				|| !unit->willMove(0) || !newHorizonsArchery::isOrdinaryPhysicalShooter(unit)
				|| !battle.battleCanShoot(unit, target->getPosition()))
				continue;
			const auto opportunity = anyDamage(unit, target) * combinedArmsRangedPercent / 100.0f;
			if(unit == bestMeleeAttacker)
				overlappingMeleeAttackerRangedOpportunity = opportunity;
			else
				rangedOpportunity += opportunity;
		}
		// The leading melee attacker cannot both shoot and strike in the same
		// activation. Preserve its existing melee valuation and only add ranged
		// value where that alternative is better; other shooters remain separate
		// future attacks.
		rangedOpportunity += std::max(0.0f,
			overlappingMeleeAttackerRangedOpportunity - meleeOrderValue);
		return meleeOrderValue + rangedOpportunity;
	}

	if(command == secondWindCommand() && targetIds.size() == 1)
	{
		const auto * target = battle.battleGetUnitByID(targetIds.front());
		const bool canonicalRules = heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		const bool hasSpentActivation = target
			&& (target->moved(0) || (canonicalRules && target->defended()));
		if(!isEligibleOrderUnit(battle, side, target) || !hasSpentActivation)
			return 0.0f;
		float extraAttack = 0.0f;
		for(const auto * enemy : enemyUnits)
			extraAttack = std::max(extraAttack, anyDamage(target, enemy));
		const auto directDamagePercent = hero
			? static_cast<float>(heroCommands::secondWindPercent(*hero, warcastingBonus,
				divineMandateEfficiencyBonusPercent)) : 50.0f;
		return extraAttack * directDamagePercent / 100.0f;
	}

	return 0.0f;
}

float canonicalOrderHeuristic(const CBattleInfoCallback & battle, BattleSide side,
	HeroCommand command, const std::vector<uint32_t> & targetIds,
	std::optional<int> focusFireSnapshotPercent, const Environment * environment,
	DamageCache & damageCache, std::shared_ptr<CBattleInfoCallback> realBattle)
{
	const auto baseValue = canonicalOrderBaseHeuristic(battle, side, command, targetIds,
		focusFireSnapshotPercent, environment, damageCache, realBattle);
	// Second Wind starts its extra activation immediately, expiring the grant
	// without another Morale roll. Preserve that existing action economy.
	if(command == HeroCommand::SECOND_WIND || !environment || !realBattle || !battle.getBattle())
		return baseValue;
	const auto prepared = battle.battlePrepareHeroOrderState(side, command, targetIds);
	const auto * hero = battle.battleGetFightingHero(side);
	if(!prepared || !newHorizonsEnchantedCommand::eligible(battle.getBattle()->getMagicRules(), hero, *prepared))
		return baseValue;
	auto preview = std::make_shared<HypotheticBattle>(environment, realBattle);
	float moraleValue = 0.0f;
	for(const auto id : newHorizonsEnchantedCommand::recipientIds(battle, side, *prepared))
	{
		const auto * original = battle.battleGetUnitByID(id);
		if(original->unaffectedByMorale() || original->isTimeStopped()
			|| original->hasBonus(newHorizonsEnchantedCommand::moraleBonusSelector()))
			continue;
		auto projected = preview->getForUpdate(id);
		projected->addUnitBonus(std::vector<Bonus>{newHorizonsEnchantedCommand::moraleBonus()});
		auto before = expectedMoraleActivationChange(battle, original);
		auto after = expectedMoraleActivationChange(*preview, projected.get());
		const auto * active = battle.battleActiveUnit();
		if(!active || active->unitId() != id)
		{
			// On queued stacks the grant protects the pre-activation bad-Morale
			// roll, then expires before the post-action good-Morale roll.
			before = std::min(0.0f, before);
			after = std::min(0.0f, after);
		}
		else
		{
			const auto state = original->acquireState();
			if(state->hadMorale || state->fear || original->waited() || original->defended() || !original->canMove())
				continue;
			before = std::max(0.0f, before);
			after = std::max(0.0f, after);
		}
		const auto delta = preview->projectMoraleActivationDelta(original, projected.get(), before, after, 1.0f);
		moraleValue += std::max(0.0f, delta)
			* expectedTargetActivationValue(projected.get(), damageCache, preview);
	}
	return baseValue + moraleValue;
}

bool defensiveStanceMakesDefendWorthwhile(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & battle, const battle::Unit * stack,
	DamageCache & damageCache)
{
	if(!stack || stack->defended()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack))
		return false;
	const auto * hero = battle->battleGetOwnerHero(stack);
	const int bulwarkRank = newHorizonsBulwark::rank(hero);
	const int paviseReduction = newHorizonsCombatSkills::paviseReductionPercent(hero);
	const bool hasBattlecraftPreemptiveStrike = newHorizonsBattlecraft::hasPreemptiveStrike(hero);
	const bool hasHoldFast = newHorizonsDiscipline::hasHoldFast(hero);
	const bool hasBastion = hero && hero->hasActivePerk(
		std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID),
		std::string(newHorizonsCombatSkills::BASTION_PERK_ID));
	const bool hasBattlefieldMastery = newHorizonsBattlecraft::hasBattlefieldMastery(hero);
	const bool hasExistingDefendValue = bulwarkRank > 0 || paviseReduction > 0
		|| hasBattlecraftPreemptiveStrike || hasHoldFast || hasBastion;
	if(!hasExistingDefendValue && !hasBattlefieldMastery)
		return false;

	auto defendedPreview = std::make_shared<HypotheticBattle>(environment, battle);
	auto projectedTarget = defendedPreview->getForUpdate(stack->unitId());
	if(!projectedTarget)
		return false;
	projectedTarget->defending = true;
	projectedTarget->addUnitBonus(std::vector<Bonus>{
		Bonus(BonusDuration::STACK_GETS_TURN, BonusType::UNIT_DEFENDING, BonusSource::OTHER, 0, BonusSourceID())});
	const auto masterySide = defendedPreview->playerToSide(defendedPreview->battleGetOwner(projectedTarget.get()));
	const auto masteryRound = defendedPreview->getRound();
	const bool awardsBattlefieldMastery = newHorizonsBattlecraft::canAwardBattlefieldMastery(hero,
		projectedTarget.get(), masteryRound, defendedPreview->getBattlecraftMasteryAwardRound(masterySide),
		BattlecraftMasteryAction::DEFEND);
	if(awardsBattlefieldMastery)
		defendedPreview->awardBattlecraftMastery(masterySide, projectedTarget->unitId(), masteryRound,
			BattlecraftMasteryAction::DEFEND);
	if(!hasExistingDefendValue && !awardsBattlefieldMastery)
		return false;

	bool bastionForecastAvailable = defendedPreview->battleHasBastionProtection(projectedTarget.get());
	float holdFastMoraleValue = 0.0f;
	if(hasHoldFast && !stack->unaffectedByMorale() && battle->battleGetMorale(stack) < 0)
	{
		projectedTarget->addUnitBonus(std::vector<Bonus>{
			newHorizonsDiscipline::holdFastMoraleFloorBonus()});
		holdFastMoraleValue = holdFastMoraleGrantValue(
			*battle, stack, projectedTarget.get(), damageCache, defendedPreview);
	}
	struct IncomingThreat
	{
		const battle::Unit * enemy = nullptr;
		bool shooting = false;
		bool physicalDamage = true;
		int attackCount = 1;
		float beforePerAttack = 0.0f;
		float afterFirstPerAttack = 0.0f;
		float afterPerAttack = 0.0f;
		std::optional<int32_t> immovableRoundBefore;
		std::optional<int32_t> bastionRoundBefore;
		bool bastionPhysicalDamage = false;
	};
	std::vector<IncomingThreat> threats;
	struct SharedCoverThreat
	{
		const battle::Unit * ally = nullptr;
		float beforeTotal = 0.0f;
		float afterTotal = 0.0f;
		bool immovableAvailable = false;
		bool bastionAvailable = false;
	};
	std::vector<SharedCoverThreat> sharedCoverThreats;
	if(newHorizonsBulwark::hasSharedCover(hero))
	{
		for(const auto * ally : battle->battleAdjacentUnits(stack))
			if(ally && ally->unitSide() == stack->unitSide() && ally->defended()
				&& ally->alive() && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(ally))
			{
				const auto * allyHero = battle->battleGetOwnerHero(ally);
				const auto allyState = ally->acquireState();
				const bool immovableAvailable = newHorizonsBulwark::hasImmovable(allyHero) && allyState
					&& allyState->bulwarkImmovableRound != battle->battleGetRound();
				const bool bastionAvailable = battle->battleHasBastionProtection(ally);
				sharedCoverThreats.push_back({ally, 0.0f, 0.0f, immovableAvailable, bastionAvailable});
			}
	}
	const auto enemies = battle->battleGetUnitsIf([&](const battle::Unit * enemy)
	{
		return enemy && enemy->alive()
			&& battle->battleGetOwner(enemy) != battle->battleGetOwner(stack)
			&& !enemy->isGhost()
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(enemy);
	});
	bool immovableForecastAvailable = newHorizonsBulwark::hasImmovable(hero)
		&& projectedTarget->bulwarkImmovableRound != battle->battleGetRound();
	for(const auto * enemy : enemies)
	{
		bool canShoot = enemy->canShoot()
			&& battle->battleCanShoot(enemy, stack->getPosition());
		bool canMelee = false;
		if(enemy->isMeleeAttacker())
		{
			canMelee = !battle->meleeAttackHexes(enemy, stack, enemy->getPosition()).empty();
			if(!canMelee)
				for(const auto & position : battle->battleGetAvailableHexes(enemy, false))
					if(!battle->meleeAttackHexes(enemy, stack, position).empty())
					{
						canMelee = true;
						break;
					}
		}
		// An enemy that can either shoot or close to melee chooses one attack
		// mode. Forecast the more damaging immediate option instead of counting
		// the same stack as two separate threats.
		const auto estimate = [&](bool shooting)
		{
			const BattleAttackInfo beforeAttack(enemy, stack, 0, shooting);
			const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
			const BattleAttackInfo afterAttack(projectedEnemy, projectedTarget.get(), 0, shooting);
			return std::pair{averageOrderDamage(battle->battleEstimateDamage(beforeAttack)),
				averageOrderDamage(defendedPreview->battleEstimateDamage(afterAttack))};
		};
		std::optional<IncomingThreat> selected;
		for(const bool shooting : {false, true})
		{
			if((shooting && !canShoot) || (!shooting && !canMelee))
				continue;
			const auto [before, after] = estimate(shooting);
			const int count = std::max(0, AttackPossibility::getAttackCount(*enemy, shooting, *battle));
			if(count <= 0)
				continue;
			IncomingThreat candidate{enemy, shooting,
				!shooting || !enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK), count,
				before, after, after, std::nullopt, std::nullopt, false};
			const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
			const BattleAttackInfo bastionAttack(projectedEnemy, projectedTarget.get(), 0, shooting);
			candidate.bastionPhysicalDamage = newHorizonsCombatSkills::isPhysicalCreatureAttack(
				projectedEnemy, bastionAttack.physicalDamage);
			if(!selected || candidate.beforePerAttack * candidate.attackCount
				> selected->beforePerAttack * selected->attackCount)
				selected = candidate;
		}
		if(selected)
		{
			threats.push_back(*selected);
			auto & threat = threats.back();
			const bool forecastsImmovable = immovableForecastAvailable && threat.physicalDamage;
			const bool forecastsBastion = bastionForecastAvailable && threat.bastionPhysicalDamage;
			if(forecastsImmovable || forecastsBastion)
			{
				// The first physical hit receives each available per-round reduction;
				// estimate the remaining attacks after spending those allowances.
				if(forecastsImmovable)
				{
					threat.immovableRoundBefore = projectedTarget->bulwarkImmovableRound;
					projectedTarget->bulwarkImmovableRound = battle->battleGetRound();
					immovableForecastAvailable = false;
				}
				if(forecastsBastion)
				{
					threat.bastionRoundBefore = projectedTarget->armorerBastionRound;
					projectedTarget->armorerBastionRound = battle->battleGetRound();
					bastionForecastAvailable = false;
				}
				const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
				const BattleAttackInfo subsequentAttack(projectedEnemy, projectedTarget.get(), 0, threat.shooting);
				threat.afterPerAttack = averageOrderDamage(
					defendedPreview->battleEstimateDamage(subsequentAttack));
			}
		}

		// Shared Cover only protects a neighboring stack that is already
		// Defending. Forecast immediate shots and adjacent melee attacks here;
		// avoid another reachable-hex search for every neighbor and enemy.
		for(auto & sharedThreat : sharedCoverThreats)
		{
			bool allyCanShoot = enemy->canShoot();
			if(allyCanShoot)
			{
				allyCanShoot = false;
				for(const auto hex : sharedThreat.ally->getHexes())
					if(battle->battleCanShoot(enemy, hex))
					{
						allyCanShoot = true;
						break;
					}
			}
			const bool allyCanMelee = enemy->isMeleeAttacker()
				&& !battle->meleeAttackHexes(enemy, sharedThreat.ally, enemy->getPosition()).empty();
			if(!allyCanShoot && !allyCanMelee)
				continue;

			std::optional<IncomingThreat> allyThreat;
			for(const bool shooting : {false, true})
			{
				if((shooting && !allyCanShoot) || (!shooting && !allyCanMelee))
					continue;
				const BattleAttackInfo beforeAttack(enemy, sharedThreat.ally, 0, shooting);
				const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
				const auto * projectedAlly = defendedPreview->battleGetUnitByID(sharedThreat.ally->unitId());
				const BattleAttackInfo afterAttack(projectedEnemy, projectedAlly, 0, shooting);
				const int count = std::max(0, AttackPossibility::getAttackCount(*enemy, shooting, *battle));
				if(count <= 0)
					continue;
				const float after = averageOrderDamage(defendedPreview->battleEstimateDamage(afterAttack));
				IncomingThreat candidate{enemy, shooting,
					!shooting || !enemy->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK), count,
					averageOrderDamage(battle->battleEstimateDamage(beforeAttack)),
					after, after, std::nullopt, std::nullopt, false};
				candidate.bastionPhysicalDamage = newHorizonsCombatSkills::isPhysicalCreatureAttack(
					projectedEnemy, afterAttack.physicalDamage);
				if(!allyThreat || candidate.beforePerAttack * candidate.attackCount
					> allyThreat->beforePerAttack * allyThreat->attackCount)
					allyThreat = candidate;
			}
			if(allyThreat)
			{
				sharedThreat.beforeTotal += allyThreat->beforePerAttack * allyThreat->attackCount;
				const bool forecastsImmovable = sharedThreat.immovableAvailable && allyThreat->physicalDamage;
				const bool forecastsBastion = sharedThreat.bastionAvailable && allyThreat->bastionPhysicalDamage;
				if(forecastsImmovable || forecastsBastion)
				{
					auto projectedAlly = defendedPreview->getForUpdate(sharedThreat.ally->unitId());
					if(forecastsImmovable)
					{
						projectedAlly->bulwarkImmovableRound = battle->battleGetRound();
						sharedThreat.immovableAvailable = false;
					}
					if(forecastsBastion)
					{
						projectedAlly->armorerBastionRound = battle->battleGetRound();
						sharedThreat.bastionAvailable = false;
					}
					const auto * projectedEnemy = defendedPreview->battleGetUnitByID(enemy->unitId());
					const BattleAttackInfo subsequentAttack(projectedEnemy, projectedAlly.get(), 0,
						allyThreat->shooting);
					const float afterPerAttack = averageOrderDamage(
						defendedPreview->battleEstimateDamage(subsequentAttack));
					sharedThreat.afterTotal += allyThreat->afterFirstPerAttack
						+ afterPerAttack * std::max(0, allyThreat->attackCount - 1);
				}
				else
					sharedThreat.afterTotal += allyThreat->afterFirstPerAttack
						+ allyThreat->afterPerAttack * std::max(0, allyThreat->attackCount - 1);
			}
		}
	}

	float preemptiveValue = 0.0f;
	const int battlecraftPreemptivePercent = newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		hero, projectedTarget.get(), battle->battleGetRound());
	const auto applyPreemptiveReaction = [&](int percent)
	{
		const IncomingThreat * bestMeleeThreat = nullptr;
		float bestReaction = -1.0f;
		for(const auto & threat : threats)
		{
			if(threat.shooting || !threat.physicalDamage)
				continue;
			auto projectedEnemy = defendedPreview->getForUpdate(threat.enemy->unitId());
			if(!projectedEnemy || !projectedEnemy->alive())
				continue;
			BattleAttackInfo reaction(projectedTarget.get(), projectedEnemy.get(), 0, false);
			reaction.retaliation = true;
			reaction.preemptiveDamagePercent = percent;
			const float value = averageOrderDamage(defendedPreview->battleEstimateDamage(reaction));
			if(value > bestReaction)
			{
				bestReaction = value;
				bestMeleeThreat = &threat;
			}
		}
		if(!bestMeleeThreat)
			return 0.0f;

		const IncomingThreat & threat = *bestMeleeThreat;
		auto projectedEnemy = defendedPreview->getForUpdate(threat.enemy->unitId());
		BattleAttackInfo reaction(projectedTarget.get(), projectedEnemy.get(), 0, false);
		reaction.retaliation = true;
		reaction.preemptiveDamagePercent = percent;
		auto reactionDamage = defendedPreview->battleExpectedLuckDamage(reaction);
		vstd::amin(reactionDamage, projectedEnemy->getAvailableHealth());
		projectedEnemy->damage(reactionDamage);
		if(!projectedEnemy->alive())
		{
			for(auto & projectedThreat : threats)
				if(projectedThreat.enemy->unitId() == threat.enemy->unitId()
					&& !projectedThreat.shooting)
				{
					projectedThreat.afterFirstPerAttack = 0.0f;
					projectedThreat.afterPerAttack = 0.0f;
				}
		}
		else
		{
			BattleAttackInfo afterReaction(projectedEnemy.get(), projectedTarget.get(), 0, false);
			const float afterPerAttack = averageOrderDamage(
				defendedPreview->battleEstimateDamage(afterReaction));
			float afterFirstPerAttack = afterPerAttack;
			if(threat.immovableRoundBefore || threat.bastionRoundBefore)
			{
				const auto consumedRound = projectedTarget->bulwarkImmovableRound;
				const auto consumedBastionRound = projectedTarget->armorerBastionRound;
				if(threat.immovableRoundBefore)
					projectedTarget->bulwarkImmovableRound = *threat.immovableRoundBefore;
				if(threat.bastionRoundBefore)
					projectedTarget->armorerBastionRound = *threat.bastionRoundBefore;
				BattleAttackInfo firstAfterReaction(projectedEnemy.get(), projectedTarget.get(), 0, false);
				afterFirstPerAttack = averageOrderDamage(
					defendedPreview->battleEstimateDamage(firstAfterReaction));
				projectedTarget->bulwarkImmovableRound = consumedRound;
				projectedTarget->armorerBastionRound = consumedBastionRound;
			}
			for(auto & projectedThreat : threats)
				if(projectedThreat.enemy->unitId() == threat.enemy->unitId()
					&& !projectedThreat.shooting)
				{
					projectedThreat.afterFirstPerAttack = afterFirstPerAttack;
					projectedThreat.afterPerAttack = afterPerAttack;
				}
		}
		return static_cast<float>(reactionDamage);
	};
	if(battlecraftPreemptivePercent > 0)
	{
		const float value = applyPreemptiveReaction(battlecraftPreemptivePercent);
		if(value > 0.0f)
		{
			projectedTarget->battlecraftPreemptiveStrikeRound = battle->battleGetRound();
			preemptiveValue += value;
		}
	}
	const int bulwarkPreemptivePercent = newHorizonsBulwark::preemptivePercent(
		bulwarkRank, newHorizonsBulwark::hasBogAmbush(hero));
	if(bulwarkPreemptivePercent > 0 && !projectedTarget->bulwarkPreemptiveUsed)
	{
		const float value = applyPreemptiveReaction(bulwarkPreemptivePercent);
		if(value > 0.0f)
		{
			projectedTarget->bulwarkPreemptiveUsed = true;
			preemptiveValue += value;
		}
	}
	float incomingBeforeDefend = 0.0f;
	float incomingAfterDefend = 0.0f;
	for(const auto & threat : threats)
	{
		incomingBeforeDefend += threat.beforePerAttack * threat.attackCount;
		incomingAfterDefend += threat.afterFirstPerAttack
			+ threat.afterPerAttack * std::max(0, threat.attackCount - 1);
	}
	const float currentHealth = static_cast<float>(stack->getAvailableHealth());
	const float damagePrevented = std::max(0.0f,
		std::min(currentHealth, incomingBeforeDefend) - std::min(currentHealth, incomingAfterDefend));

	int64_t remainingHealth = stack->getAvailableHealth();
	int64_t reflectedDamage = 0;
	int64_t swampRenewalDamage = 0;
	for(const auto & threat : threats)
	{
		if(remainingHealth <= 0)
			break;
		const float proposed = threat.afterFirstPerAttack
			+ threat.afterPerAttack * std::max(0, threat.attackCount - 1);
		const int64_t received = std::min<int64_t>(remainingHealth,
			static_cast<int64_t>(std::max(0.0f, proposed)));
		remainingHealth -= received;
		if(threat.physicalDamage)
			swampRenewalDamage += received;
		if(received <= 0 || !threat.physicalDamage)
			continue;
		const int basisPoints = newHorizonsBulwark::reflectionBasisPoints(bulwarkRank,
			threat.shooting, newHorizonsBulwark::hasThickHide(hero),
			newHorizonsBulwark::hasVengefulMire(hero));
		if(basisPoints <= 0)
			continue;
		const auto * projectedEnemy = defendedPreview->battleGetUnitByID(threat.enemy->unitId());
		if(!projectedEnemy || !projectedEnemy->alive())
			continue;
		const int64_t reflected = newHorizonsBulwark::reflectedDamage(received, basisPoints);
		reflectedDamage += std::min<int64_t>(projectedEnemy->getAvailableHealth(), reflected);
	}

	const float defendValue = damagePrevented + preemptiveValue + reflectedDamage + holdFastMoraleValue;
	int64_t swampRenewalValue = 0;
	if(newHorizonsBulwark::hasSwampRenewal(hero) && remainingHealth > 0 && swampRenewalDamage > 0)
	{
		const int64_t availableHealing = std::max<int64_t>(0,
			stack->getTotalHealth() - remainingHealth);
		swampRenewalValue = std::min(availableHealing, swampRenewalDamage / 10);
	}
	float sharedCoverDamagePrevented = 0.0f;
	for(const auto & threat : sharedCoverThreats)
		sharedCoverDamagePrevented += std::max(0.0f,
			std::min<float>(threat.ally->getAvailableHealth(), threat.beforeTotal)
			- std::min<float>(threat.ally->getAvailableHealth(), threat.afterTotal));
	const float defendValueWithSharedCover = defendValue + sharedCoverDamagePrevented + swampRenewalValue;
	// Require a visible benefit to beat the existing Wait fallback.  Damage
	// prevented, the one available pre-emptive strike and reflected damage all
	// use the same health-value scale.
	return defendValueWithSharedCover >= std::max<int64_t>(1, stack->getAvailableHealth() / 20);
}
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env),
	cb(cb), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	hb = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	damageCache.buildDamageCache(hb, side);

	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	:scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cachedAttack(), playerID(playerID), side(side), env(env), cb(cb), hb(hb),
	damageCache(damageCache), strengthRatio(strengthRatio), battleID(battleID), simulationTurnsCount(simulationTurnsCount)
{
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

std::vector<BattleHex> BattleEvaluator::getBrokenWallMoatHexes() const
{
	std::vector<BattleHex> result;

	for(EWallPart wallPart : { EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL })
	{
		auto state = cb->getBattle(battleID)->battleGetWallState(wallPart);

		if(state != EWallState::DESTROYED)
			continue;

		auto wallHex = cb->getBattle(battleID)->wallPartToBattleHex(wallPart);
		auto moatHex = wallHex.cloneInDirection(BattleHex::LEFT);

		result.push_back(moatHex);

		moatHex = moatHex.cloneInDirection(BattleHex::LEFT);
		auto obstaclesSecondRow = cb->getBattle(battleID)->battleGetAllObstaclesOnPos(moatHex, false);

		for(auto obstacle : obstaclesSecondRow)
		{
			if(obstacle->obstacleType == CObstacleInstance::EObstacleType::MOAT)
			{
				result.push_back(moatHex);
				break;
			}
		}
	}

	return result;
}

bool BattleEvaluator::hasWorkingTowers() const
{
	bool keepIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::KEEP) != EWallState::DESTROYED;
	bool upperIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::UPPER_TOWER) != EWallState::DESTROYED;
	bool bottomIntact = cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::NONE && cb->getBattle(battleID)->battleGetWallState(EWallPart::BOTTOM_TOWER) != EWallState::DESTROYED;
	return keepIntact || upperIntact || bottomIntact;
}

std::optional<PossibleSpellcast> BattleEvaluator::findBestCreatureSpell(const CStack * stack)
{
	if(!stack->canCast())
		return std::nullopt;
	if(const auto randomAbility = stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER)))
	{
		const auto battle = cb->getBattle(battleID);
		auto baseline = std::make_shared<HypotheticBattle>(env.get(), battle);
		DamageCache baselineCache;
		const float baselinePressure = beneficialCreaturePressure(baseline, stack->unitId(), baselineCache);
		std::optional<PossibleSpellcast> best;
		for(const auto * recipient : battle->battleAliveUnits())
		{
			if(!battle->battleMatchActionController(stack, recipient, true))
				continue;
			const auto pool = battle->getAvailableBeneficialSpells(stack, recipient);
			if(pool.empty())
				continue;
			double total = 0;
			for(const auto spell : pool)
				total += beneficialCreatureOutcomeValue(stack, recipient, spell.toSpell(),
					creatureAbilitySpellLevel(stack, spell.toSpell(), randomAbility->val), baselinePressure);
			const float value = static_cast<float>(total / pool.size());
			if(std::isfinite(value) && value > 0 && (!best || value > best->value))
			{
				PossibleSpellcast candidate;
				// The server replaces this representative ID with its own random
				// outcome from the same recipient-specific pool upon acceptance.
				candidate.spell = pool.front().toSpell();
				candidate.dest = {spells::Destination(recipient)};
				candidate.value = value;
				best = std::move(candidate);
			}
		}
		return best;
	}

	std::vector<SpellID> spellsToCast;
	TConstBonusListPtr bl = stack->getBonusesOfType(BonusType::SPELLCASTER);

	//TODO: faerie dragon type spell should be selected by server
	SpellID creatureSpellToCast = cb->getBattle(battleID)->getRandomCastedSpell(CRandomGenerator::getDefault(), stack);

	for(const auto & bonus : *bl)
		if(!bonus->parameters && bonus->subtype.as<SpellID>().hasValue())
			spellsToCast.push_back(bonus->subtype.as<SpellID>());

	if(creatureSpellToCast.hasValue())
		spellsToCast.push_back(creatureSpellToCast);

	std::vector<PossibleSpellcast> possibleCasts;
	std::optional<float> beneficialBaseline;

	for(const auto spellID : spellsToCast)
	{
		const CSpell * spell = spellID.toSpell();

		if(!spell->canBeCast(cb->getBattle(battleID).get(), spells::Mode::CREATURE_ACTIVE, stack))
			continue;

		spells::BattleCast temp(cb->getBattle(battleID).get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
		{
			PossibleSpellcast ps;
			ps.dest = target;
			ps.spell = spell;
			evaluateCreatureSpellcast(stack, ps);
			const auto ability = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER,
				BonusSubtypeID(spellID)));
			// Only fill the existing zero-HP blind spot for an ordinary friendly
			// single-target buff. Damage, healing, summons and weighted abilities
			// retain their existing scoring and selection policies.
			if(ps.value == 0 && spell->isPositive() && ability && !ability->parameters
				&& target.size() == 1 && target.front().unitValue
				&& cb->getBattle(battleID)->battleMatchActionController(stack, target.front().unitValue, true))
			{
				if(!beneficialBaseline)
				{
					auto baseline = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
					DamageCache cache;
					beneficialBaseline = beneficialCreaturePressure(baseline, stack->unitId(), cache);
				}
				ps.value = beneficialCreatureOutcomeValue(stack, target.front().unitValue, spell,
					creatureAbilitySpellLevel(stack, spell, ability->val), *beneficialBaseline);
			}
			possibleCasts.push_back(ps);
		}
	}

	std::sort(
		possibleCasts.begin(), possibleCasts.end(),
		[&](const PossibleSpellcast & lhs, const PossibleSpellcast & rhs)
		{
			return lhs.value > rhs.value;
		}
	);

	if(!possibleCasts.empty() && possibleCasts.front().value > 0)
		return possibleCasts.front();

	return std::nullopt;
}

float BattleEvaluator::beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
	uint32_t spentCasterId, DamageCache & cache) const
{
	float value = 0;
	cache.buildDamageCache(state, side);
	for(const auto * unit : state->getUnitsIf([&](const battle::Unit * unit)
	{
		return unit->alive() && unit->unitId() != spentCasterId && !unit->isTimeStopped();
	}))
	{
		// Reuse ordinary spell evaluation's physical-attack valuation, without
		// advancing turns or running a battle queue for each random outcome.
		PotentialTargets targets(unit, cache, state);
		if(targets.possibleAttacks.empty())
			continue;
		const float pressure = static_cast<float>(targets.bestActionValue());
		value += state->battleGetActionController(unit) == playerID ? pressure : -pressure;
	}
	return value;
}

int BattleEvaluator::creatureAbilitySpellLevel(const CStack * caster, const CSpell * spell, int abilityLevel)
{
	int spellLevel = std::max(0, abilityLevel);
	// Match BattleActionProcessor: only the ANY-school battlefield raise is
	// applied to creature casts, not school-specific hero proficiency.
	if(spell->getLevel() > 0)
		vstd::amax(spellLevel, caster->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	return spellLevel;
}

float BattleEvaluator::beneficialCreatureOutcomeValue(const CStack * caster,
	const battle::Unit * recipient, const CSpell * spell, int spellLevel, float baselinePressure) const
{
	const auto battle = cb->getBattle(battleID);
	if(!recipient || !spell)
		return 0;
	auto state = std::make_shared<HypotheticBattle>(env.get(), battle);
	const auto * projectedCaster = state->battleGetUnitByID(caster->unitId());
	const auto * projectedRecipient = state->battleGetUnitByID(recipient->unitId());
	newHorizonsPuppetMaster::ActionControllerCaster actionCaster(projectedCaster,
		state->battleGetActionController(projectedCaster));
	spells::BattleCast cast(state.get(), &actionCaster, spells::Mode::CREATURE_ACTIVE, spell);
	cast.setSpellLevel(spellLevel);
	cast.castEval(state->getServerCallback(), {spells::Destination(projectedRecipient)});
	DamageCache cache;
	const float pressure = beneficialCreaturePressure(state, caster->unitId(), cache) - baselinePressure;
	const auto * restored = state->battleGetUnitByID(recipient->unitId());
	const int64_t healed = restored->getAvailableHealth() - recipient->getAvailableHealth();
	const float healingValue = healed > 0
		? AttackPossibility::calculateDamageReduce(nullptr, recipient, healed, cache, state) : 0;
	return (pressure + healingValue) * scoreEvaluator.getPositiveEffectMultiplier();
}

float BattleEvaluator::expectedBeneficialCreatureSpellValue(const CStack * caster,
	const battle::Unit * recipient) const
{
	const auto battle = cb->getBattle(battleID);
	if(!caster || !caster->canCast() || !recipient
		|| !battle->battleMatchActionController(caster, recipient, true))
		return 0;
	const auto pool = battle->getAvailableBeneficialSpells(caster, recipient);
	const auto ability = caster->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	if(pool.empty() || !ability)
		return 0;
	auto baseline = std::make_shared<HypotheticBattle>(env.get(), battle);
	DamageCache cache;
	const float baselinePressure = beneficialCreaturePressure(baseline, caster->unitId(), cache);
	double total = 0;
	for(const auto spell : pool)
		total += beneficialCreatureOutcomeValue(caster, recipient, spell.toSpell(),
			creatureAbilitySpellLevel(caster, spell.toSpell(), ability->val), baselinePressure);
	return static_cast<float>(total / pool.size());
}

float BattleEvaluator::beneficialCreatureSpellOutcomeValue(const CStack * caster,
	const battle::Unit * recipient, const CSpell * spell) const
{
	if(!caster || !recipient || !spell)
		return 0;
	const auto randomAbility = caster->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	const auto ability = randomAbility ? randomAbility : caster->getBonus(Selector::typeSubtype(
		BonusType::SPELLCASTER, BonusSubtypeID(spell->getId())));
	if(!ability)
		return 0;
	auto baseline = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	DamageCache cache;
	return beneficialCreatureOutcomeValue(caster, recipient, spell,
		creatureAbilitySpellLevel(caster, spell, ability->val),
		beneficialCreaturePressure(baseline, caster->unitId(), cache));
}

BattleAction BattleEvaluator::selectStackAction(const CStack * stack)
{
#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace("Select stack action");
#endif
	//evaluate casting spell for spellcasting stack
	std::optional<PossibleSpellcast> bestSpellcast = findBestCreatureSpell(stack);

	auto moveTarget = scoreEvaluator.findMoveTowardsUnreachable(stack, *targets, damageCache, hb);
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});
	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& !stack->canShoot()
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if(targets->possibleAttacks.empty() && bestSpellcast.has_value())
	{
		activeActionMade = true;
		return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
	}

	if(!targets->possibleAttacks.empty())
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating attack for %s", stack->getDescription());
#endif

		auto evaluationResult = scoreEvaluator.findBestTarget(stack, *targets, damageCache, hb, siegeDefense);
		auto & bestAttack = evaluationResult.bestAttack;

		cachedAttack.ap = bestAttack;
		cachedAttack.score = evaluationResult.score;
		cachedAttack.turn = 0;
		cachedAttack.waited = evaluationResult.wait;

		//TODO: consider more complex spellcast evaluation, f.e. because "re-retaliation" during enemy move in same turn for melee attack etc.
		if(bestSpellcast.has_value() && bestSpellcast->value > bestAttack.damageDiff())
		{
			// return because spellcast value is damage dealt and score is dps reduce
			activeActionMade = true;
			return BattleAction::makeCreatureSpellcast(stack, bestSpellcast->dest, bestSpellcast->spell->id);
		}

		if(evaluationResult.score > score)
		{
			score = evaluationResult.score;

			logAi->debug("BattleAI: %s -> %s x %d, from %d curpos %d dist %d speed %d: +%2f -%2f = %2f",
				bestAttack.attackerState->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->unitType()->getJsonKey(),
				bestAttack.affectedUnits[0]->getCount(),
				bestAttack.from.toInt(),
				bestAttack.attack.attacker->getPosition().toInt(),
				bestAttack.attack.chargeDistance,
				bestAttack.attack.attacker->getMovementRange(0),
				bestAttack.defenderDamageReduce,
				bestAttack.attackerDamageReduce,
				score
			);

			if (moveTarget.score <= score)
			{
				if(evaluationResult.wait && !stack->acquireState()->waitedThisTurn)
				{
					return BattleAction::makeWait(stack);
				}
				else if(bestAttack.attack.shooting)
				{
					activeActionMade = true;
					const auto * hero = hb->battleGetFightingHero(stack->unitSide());
					if(bestAttack.from.isValid() && bestAttack.from != stack->getPosition()
						&& newHorizonsArchery::canUseSkirmisher(hero, stack)
						&& hb->battleCanSkirmisherAttackFromHex(stack,
							bestAttack.attack.defender->getPosition(), bestAttack.from))
					{
						// Skirmisher encodes its chosen legal move-and-fire destination through
						// the existing WALK_AND_ATTACK packet.
						auto action = BattleAction::makeMeleeAttack(stack,
							bestAttack.attack.defender->getPosition(), bestAttack.from, false);
						action.archerySkirmisherAttack = true;
						action.perfectMoment = bestAttack.perfectMoment;
						return action;
					}
					auto action = BattleAction::makeShotAttack(stack, bestAttack.attack.defender);
					action.perfectMoment = bestAttack.perfectMoment;
					return action;
				}
				else
				{
					if(bestAttack.collateralDamageReduce
						&& bestAttack.collateralDamageReduce >= bestAttack.defenderDamageReduce / 2
						&& score < 0)
					{
						return BattleAction::makeDefend(stack);
					}

					bool isTargetOutsideFort = !hb->battleIsInsideWalls(bestAttack.from);
					bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
						&& !bestAttack.attack.shooting
						&& hasWorkingTowers()
						&& !enemyMellee.empty()
						&& isTargetOutsideFort;

					if(siegeDefense)
					{
						logAi->trace("Evaluating exchange at %d self-defense", stack->getPosition());

						BattleAttackInfo bai(stack, stack, 0, false);
						AttackPossibility apDefend(stack->getPosition(), stack->getPosition(), bai);

						float defenseValue = scoreEvaluator.evaluateExchange(apDefend, 0, *targets, damageCache, hb);

						if((defenseValue > score && score <= 0) || (defenseValue > 2 * score && score > 0))
						{
							return BattleAction::makeDefend(stack);
						}
					}
					
					activeActionMade = true;
					auto action = BattleAction::makeMeleeAttack(stack, bestAttack.attack.defenderPos, bestAttack.from);
					action.perfectMoment = bestAttack.perfectMoment;
					return action;
				}
			}
		}
	}

	//ThreatMap threatsToUs(stack); // These lines may be useful but they are't used in the code.
	if(moveTarget.score > score)
	{
		score = moveTarget.score;
		cachedAttack.ap = moveTarget.cachedAttack;
		cachedAttack.score = score;
		cachedAttack.turn = moveTarget.turnsToReach;

		if(stack->acquireState()->waitedThisTurn)
		{
			logAi->debug(
				"Moving %s towards hex %s[%d], score: %2f",
				stack->getDescription(),
				moveTarget.cachedAttack->attack.defender->getDescription(),
				moveTarget.cachedAttack->attack.defender->getPosition(),
				moveTarget.score);

			return goTowardsNearest(stack, moveTarget.positions, *targets);
		}
		else
		{
			cachedAttack.waited = true;
			return BattleAction::makeWait(stack);
		}
	}

	if(score <= EvaluationResult::INEFFECTIVE_SCORE
		&& !stack->hasBonusOfType(BonusType::FLYING)
		&& stack->getMovementRange(0) != 0
		&& stack->unitSide() == BattleSide::ATTACKER
	   && cb->getBattle(battleID)->battleGetFortifications().hasMoat)
	{
		auto brokenWallMoat = getBrokenWallMoatHexes();

		if(brokenWallMoat.size())
		{
			activeActionMade = true;

			if(stack->doubleWide() && vstd::contains(brokenWallMoat, stack->getPosition()))
				return BattleAction::makeMove(stack, stack->getPosition().cloneInDirection(BattleHex::RIGHT));
			else
				return goTowardsNearest(stack, brokenWallMoat, *targets);
		}
	}

	if(stack->acquireState()->waitedThisTurn)
		return BattleAction::makeDefend(stack);

	if(defensiveStanceMakesDefendWorthwhile(env.get(), hb, stack, damageCache))
		return BattleAction::makeDefend(stack);
	return BattleAction::makeWait(stack);
}

uint64_t timeElapsed(std::chrono::time_point<std::chrono::steady_clock> start)
{
	auto end = std::chrono::steady_clock::now();

	return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

BattleAction BattleEvaluator::moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets)
{
	auto additionalScore = 0;
	std::optional<AttackPossibility> attackOnTheWay;

	for(auto & target : targets.possibleAttacks)
	{
		if(!target.attack.shooting && target.from == hex && target.attackValue() > additionalScore)
		{
			additionalScore = target.attackValue();
			attackOnTheWay = target;
		}
	}

	if(attackOnTheWay)
	{
		activeActionMade = true;
		auto action = BattleAction::makeMeleeAttack(stack, attackOnTheWay->attack.defender->getPosition(), attackOnTheWay->from);
		action.perfectMoment = attackOnTheWay->perfectMoment;
		return action;
	}
	else
	{
		if(stack->position == hex)
			return BattleAction::makeDefend(stack);
		else
		{
			// Pure movement has no attack preview to carry an Overwatch death.
			// Reject a deterministically lethal route without spending live ammo/RNG.
			auto movement = std::make_shared<HypotheticBattle>(env.get(), hb);
			movement->projectVoluntaryMovement(stack->unitId(), hex);
			if(!movement->battleGetUnitByID(stack->unitId())->alive())
				return BattleAction::makeDefend(stack);
			return BattleAction::makeMove(stack, hex);
		}
	}
}

BattleAction BattleEvaluator::goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets)
{
	auto reachability = cb->getBattle(battleID)->getReachability(stack);
	auto avHexes = cb->getBattle(battleID)->battleGetAvailableHexes(reachability, stack, false);

	auto enemyMellee = hb->getUnitsIf([this](const battle::Unit* u) -> bool
		{
			return u->unitSide() == BattleSide::ATTACKER && !hb->battleCanShoot(u);
		});

	bool siegeDefense = stack->unitSide() == BattleSide::DEFENDER
		&& hasWorkingTowers()
		&& !enemyMellee.empty();

	if (siegeDefense)
	{
		avHexes.eraseIf([&](const BattleHex & hex)
		{
			return !cb->getBattle(battleID)->battleIsInsideWalls(hex);
		});
	}

	if(avHexes.empty() || hexes.empty()) //we are blocked or dest is blocked
	{
		return BattleAction::makeDefend(stack);
	}

	BattleHexArray targetHexes = hexes;

	targetHexes.sort([&reachability](const BattleHex & h1, const BattleHex & h2) -> bool
		{
			return reachability.distances[h1.toInt()] < reachability.distances[h2.toInt()];
		});

	BattleHex bestNeighbour = targetHexes.front();

	if(reachability.distances[bestNeighbour.toInt()] > GameConstants::BFIELD_SIZE)
	{
		logAi->trace("No reachable hexes.");
		return BattleAction::makeDefend(stack);
	}

	// this turn
	for(const auto & hex : targetHexes)
	{
		if(avHexes.contains(hex))
		{
			return moveOrAttack(stack, hex, targets);
		}

		if(stack->coversPos(hex))
		{
			logAi->warn("Warning: already standing on neighbouring hex!");
			//We shouldn't even be here...
			return BattleAction::makeDefend(stack);
		}
	}

	// not this turn
	scoreEvaluator.updateReachabilityMap(hb);

	if(stack->hasBonusOfType(BonusType::FLYING))
	{
		BattleHexArray obstacleHexes;

		const auto & obstacles = hb->battleGetAllObstacles();

		for (const auto & obst : obstacles) 
		{
			if(obst->triggersEffects())
			{
				auto triggerAbility =  LIBRARY->spells()->getById(obst->getTrigger());
				auto triggerIsNegative = triggerAbility->isNegative() || triggerAbility->isDamage();

				if(triggerIsNegative)
					obstacleHexes.insert(obst->getAffectedTiles());
			}
		}
		// Flying stack doesn't go hex by hex, so we can't backtrack using predecessors.
		// We just check all available hexes and pick the one closest to the target.
		auto nearestAvailableHex = vstd::minElementByFun(avHexes, [this, &bestNeighbour, &stack, &obstacleHexes](const BattleHex & hex) -> int
		{
			const int NEGATIVE_OBSTACLE_PENALTY = 100; // avoid landing on negative obstacle (moat, fire wall, etc)
			const int BLOCKED_STACK_PENALTY = 100; // avoid landing on moat

			auto distance = BattleHex::getDistance(bestNeighbour, hex);

			if(obstacleHexes.contains(hex))
				distance += NEGATIVE_OBSTACLE_PENALTY;

			return scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, hex) ? BLOCKED_STACK_PENALTY + distance : distance;
		});

		return moveOrAttack(stack, *nearestAvailableHex, targets);
	}
	else
	{
		BattleHex currentDest = bestNeighbour;

		while(true)
		{
			if(!currentDest.isValid())
			{
				return BattleAction::makeDefend(stack);
			}

			if(avHexes.contains(currentDest)
				&& !scoreEvaluator.checkPositionBlocksOurStacks(*hb, stack, currentDest))
			{
				return moveOrAttack(stack, currentDest, targets);
			}

			currentDest = reachability.predecessors[currentDest.toInt()];
		}
	}
	
	logAi->error("We should either detect that hexes are unreachable or make a move!");
	return BattleAction::makeDefend(stack);
}

bool BattleEvaluator::canCastSpell()
{
	const auto battleView = cb->getBattle(battleID);
	if(battleView->battleHasPendingDoubleCommand(side)
		|| battleView->battleHasPendingPreCombatOrder(side))
		return true;
	const auto mandate = battleView->battleGetDivineMandateStatus(side);
	if(mandate.active)
	{
		const auto * hero = battleView->battleGetMyHero();
		if(hero && hero->hasSpellbook())
			for(const auto & spell : LIBRARY->spellh->objects)
			{
				const auto selection = battleView->battleGetSpellActionAllowance(side, spell->getId());
				const bool metamagicFollowup = selection
					&& battleView->battleCanUseMetamagicFollowup(side, spell->getId());
				const bool grand = HeroSpellAllowanceTransition::activatesGrand(metamagicFollowup,
					battleView->battleMetamagicPendingCount(side),
					battleView->battleMetamagicSequenceSpells(side).size(),
					battleView->battleMetamagicUsesConsumed(side),
					newHorizonsMagic::metamagicRank(hero),
					newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
					battleView->battleMetamagicGrandUsed(side));
				if(selection && (spellType(spell.get()) == SpellTypes::BATTLE || isCounterspell(spell.get()))
					&& spell->canBeCast(battleView.get(), spells::Mode::HERO, hero, grand))
					return true;
			}
	}
	auto hero = battleView->battleGetMyHero();
	if(!hero)
		return false;

	if(!mandate.active && battleView->battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK)
		return true;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		if(battleView->battleCanUseHeroCommand(side, command))
			return true;
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
		if(battleView->battleCanBeginHeroCommand(side, command))
			return true;
	return false;
}

bool BattleEvaluator::attemptCastingSpell(const CStack * activeStack, bool allowSpells)
{
	const auto battleView = cb->getBattle(battleID);
	const bool mandatoryDoubleCommand = battleView->battleHasPendingDoubleCommand(side);
	const bool mandatoryPreCombatOrder = battleView->battleHasPendingPreCombatOrder(side);
	const bool mandatoryOrder = mandatoryDoubleCommand || mandatoryPreCombatOrder;
	auto hero = battleView->battleGetMyHero();
	if(!hero)
		return false;

	LOGL("Casting spells sounds like fun. Let's see...");
	// Double Command and Battle Plan both require an Order before any ordinary
	// spell or creature action. Battle Plan's typed grant is not a HERO action.
	const bool legacyMetamagicFollowup = !mandatoryOrder
		&& battleView->battleCanUseMetamagicFollowup(side);
	const auto divineMandateStatus = battleView->battleGetDivineMandateStatus(side);
	// Grand is an automatic outcome of the third used sequence, never a
	// selectable alternative. Project exactly the transition the server applies.
	//Get all spells we can cast
	struct SpellOption
	{
		const CSpell * spell = nullptr;
		bool metamagicFollowup = false;
		bool divineMandateFollowup = false;
		bool metamagicGrand = false;
	};
	std::vector<SpellOption> possibleSpells;

	for(auto const & s : LIBRARY->spellh->objects)
	{
		if(!allowSpells || mandatoryOrder)
			continue;

		bool candidateMetamagicFollowup = legacyMetamagicFollowup;
		bool divineMandateFollowup = false;
		if(divineMandateStatus.active)
		{
			const auto selection = battleView->battleGetSpellActionAllowance(side, s->getId());
			if(!selection)
				continue;
			candidateMetamagicFollowup = battleView->battleCanUseMetamagicFollowup(side, s->getId());
			divineMandateFollowup = divineMandateStatus.pendingFollowup
				&& selection->grantId == divineMandateStatus.pendingFollowup->id;
		}

		const bool activatesGrand = HeroSpellAllowanceTransition::activatesGrand(candidateMetamagicFollowup,
			cb->getBattle(battleID)->battleMetamagicPendingCount(side),
			cb->getBattle(battleID)->battleMetamagicSequenceSpells(side).size(),
			cb->getBattle(battleID)->battleMetamagicUsesConsumed(side),
			newHorizonsMagic::metamagicRank(hero),
			newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
			cb->getBattle(battleID)->battleMetamagicGrandUsed(side));
		if(s->canBeCast(battleView.get(), spells::Mode::HERO, hero, activatesGrand))
			possibleSpells.push_back({s.get(), candidateMetamagicFollowup,
				divineMandateFollowup, activatesGrand});
	}
	const bool hasFollowupCandidate = !mandatoryOrder && (divineMandateStatus.active
		? std::any_of(possibleSpells.begin(), possibleSpells.end(), [](const SpellOption & option)
		{
			return option.metamagicFollowup || option.divineMandateFollowup;
		})
		: legacyMetamagicFollowup);

	LOGFL("I can cast %d spells.", possibleSpells.size());

	const auto battleCallback = battleView;
	vstd::erase_if(possibleSpells, [&](const SpellOption & option)
	{
		if(spellType(option.spell) != SpellTypes::BATTLE && !isCounterspell(option.spell))
			return true;

		// New Horizons spells may be present in installed content when evaluating
		// a legacy battle. Do not let that content leak into the saved roster's AI
		// decisions; the authoritative cast path uses the same saved-v3 identity gate.
		const bool unavailableReanimate = option.spell
			&& option.spell->getJsonKey() == newHorizonsMagic::SHADOW_REANIMATE_SPELL
			&& !isCanonicalReanimate(*battleCallback, option.spell);
		const bool unavailableSoulReaper = option.spell
			&& option.spell->getJsonKey() == newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL
			&& !isCanonicalSoulReaper(*battleCallback, option.spell);
		const bool unavailableDoom = isCanonicalDoom(option.spell)
			&& !canonicalDoomAvailableInSavedRules(battleCallback->getBattle()->getMagicRules(), option.spell);
		const bool unavailableGuardianSpirit = isCanonicalGuardianSpirit(option.spell)
			&& !guardianSpiritAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailableHeavenlyGale = isCanonicalHeavenlyGale(option.spell)
			&& !heavenlyGaleAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailableCrusade = isCanonicalCrusade(option.spell)
			&& !crusadeAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailableShieldOfChaos = isCanonicalShieldOfChaos(option.spell)
			&& !shieldOfChaosAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailableDivineRetribution = isCanonicalDivineRetribution(option.spell)
			&& !divineRetributionAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailableEntangle = isCanonicalNatureEntangle(option.spell)
			&& !natureEntangleAvailableInSavedRules(*battleCallback, option.spell);
		const bool unavailablePurify = isCanonicalPurify(option.spell)
			&& !newHorizonsPurify::enabled(battleCallback->getBattle()->getMagicRules(), option.spell->getId());
		return unavailableReanimate || unavailableSoulReaper || unavailableDoom
			|| unavailableGuardianSpirit || unavailableHeavenlyGale || unavailableCrusade
			|| unavailableShieldOfChaos || unavailableDivineRetribution
			|| unavailableEntangle || unavailablePurify;
	});

	LOGFL("I know how %d of them works.", possibleSpells.size());

	const bool hasHolyArmor = std::any_of(possibleSpells.begin(), possibleSpells.end(), [](const SpellOption & option)
	{
		return isCanonicalHolyArmor(option.spell);
	});
	std::vector<const battle::Unit *> visibleEnemyUnits;
	int64_t friendlyAvailableHealth = 0;
	float visibleMagicalSpellThreat = 0.0f;
	if(hasHolyArmor)
	{
		for(const auto * unit : battleCallback->battleGetAllUnits(false))
		{
			if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
				continue;

			if(unit->unitSide() == side)
				friendlyAvailableHealth += std::max<int64_t>(0, unit->getAvailableHealth());
			else
				visibleEnemyUnits.push_back(unit);
		}
		visibleMagicalSpellThreat = visibleCreatureSpellThreat(visibleEnemyUnits, true);
	}

	//Get viable spell-target pairs
	std::vector<battle::Units> regenerationTurnOrder;
	bool regenerationTurnOrderPrepared = false;
	auto getRegenerationTurnOrder = [&]() -> const std::vector<battle::Units> &
	{
		if(!regenerationTurnOrderPrepared)
		{
			cb->getBattle(battleID)->battleGetTurnOrder(regenerationTurnOrder, 0, 4);
			regenerationTurnOrderPrepared = true;
		}
		return regenerationTurnOrder;
	};
	std::vector<PossibleSpellcast> possibleCasts;
	for(const auto & spellOption : possibleSpells)
	{
		const auto * spell = spellOption.spell;
		const bool metamagicFollowup = spellOption.metamagicFollowup;
		const bool divineMandateFollowup = spellOption.divineMandateFollowup;
		const bool metamagicGrandChoice = spellOption.metamagicGrand;
		if(isCounterspell(spell))
		{
			// Counterspell has a no-target cast and no ordinary spell effect to
			// project.  Give it a dedicated threat score instead of allowing the
			// generic effect evaluator to treat the ward as a zero-value cast.
			const auto value = counterspellThreatValue(*cb->getBattle(battleID), side, hero, spell);
			if(value <= 0.0f)
				continue;

			PossibleSpellcast ps;
			ps.spell = spell;
			ps.dest = {spells::Destination()};
			ps.metamagicFollowup = metamagicFollowup;
			ps.divineMandateFollowup = divineMandateFollowup;
			ps.metamagicGrand = metamagicGrandChoice;
			ps.value = value;
			possibleCasts.push_back(std::move(ps));
			continue;
		}

		const int maxOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
			cb->getBattle(battleID)->getBattle()->getMagicRules(), spell->getId(), hero->getEffectPower(spell),
			newHorizonsMagic::magicArrowOverchargeModifiers(hero));
		const bool canUseSelectiveDispel = spell->getId() == SpellID::DISPEL
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel");
		const bool canUseTemporalField = spell->getId() == SpellID::SLOW
			&& !newHorizonsMagic::hasDistinctMassSlow(cb->getBattle(battleID)->getBattle()->getMagicRules())
			&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField")
			&& !cb->getBattle(battleID)->battleWasTemporalFieldUsed(side);
		std::vector<std::pair<SpellID, std::string>> cureAfflictionChoices{{SpellID::NONE, {}}};
		const auto & magicRules = cb->getBattle(battleID)->getBattle()->getMagicRules();
		if(newHorizonsMagic::cureEnabled(magicRules, spell->getId()))
		{
			for(const auto * unit : cb->getBattle(battleID)->battleGetAllUnits(false))
			{
				for(const auto affliction : newHorizonsMagic::cureAfflictions(magicRules, unit))
					if(!vstd::contains(cureAfflictionChoices, std::make_pair(affliction, std::string{})))
						cureAfflictionChoices.emplace_back(affliction, std::string{});
				const auto frozenChoice = std::make_pair(SpellID(SpellID::NONE), std::string("frozen"));
				if(newHorizonsFrozen::isFrozen(*unit) && !vstd::contains(cureAfflictionChoices, frozenChoice))
					cureAfflictionChoices.push_back(frozenChoice);
			}
			std::sort(cureAfflictionChoices.begin(), cureAfflictionChoices.end(), [](const auto & lhs, const auto & rhs)
			{
				return std::make_pair(lhs.first.getNum(), lhs.second) < std::make_pair(rhs.first.getNum(), rhs.second);
			});
		}

		const std::vector<int32_t> shadowGiftChoices = isCanonicalShadowGift(*cb->getBattle(battleID), spell)
			? std::vector<int32_t>{10, 20, 30} : std::vector<int32_t>{0};
		for(const auto shadowGiftSacrificePercent : shadowGiftChoices)
		for(const auto & [cureAffliction, curePhysicalAffliction] : cureAfflictionChoices)
		for(const bool massSlow : {false, true})
		{
			if(massSlow && !canUseTemporalField)
				continue;
			for(const bool selectiveDispel : {false, true})
			{
				if(selectiveDispel && !canUseSelectiveDispel)
					continue;
				spells::BattleCast temp(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
				temp.setMetamagicFollowup(metamagicFollowup);
				temp.setMetamagicGrand(metamagicGrandChoice);
				temp.setCureAffliction(cureAffliction);
				temp.setCurePhysicalAffliction(curePhysicalAffliction);
			temp.setMassSlow(massSlow);
			temp.setShadowGiftSacrificePercent(shadowGiftSacrificePercent);
			temp.setSelectiveDispel(selectiveDispel);
			for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
				{
					for(int overcharge = 0; overcharge <= maxOvercharge; ++overcharge)
					{
						spells::BattleCast candidateCast(cb->getBattle(battleID).get(), hero, spells::Mode::HERO, spell);
						candidateCast.setMetamagicFollowup(metamagicFollowup);
						candidateCast.setMetamagicGrand(metamagicGrandChoice);
						candidateCast.setCureAffliction(cureAffliction);
						candidateCast.setCurePhysicalAffliction(curePhysicalAffliction);
						if(!target.empty() && target.front().unitValue)
							candidateCast.setMetamagicTargetUnitId(target.front().unitValue->unitId());
						candidateCast.setOvercharge(overcharge);
						candidateCast.setMassSlow(massSlow);
						candidateCast.setShadowGiftSacrificePercent(shadowGiftSacrificePercent);
						candidateCast.setSelectiveDispel(selectiveDispel);
						auto candidateMechanics = spell->battleMechanics(&candidateCast);
						spells::detail::ProblemImpl problem;
						const bool stormOfDaggers = candidateMechanics->isNewHorizonsStormOfDaggers();
						if(stormOfDaggers
							&& (!candidateMechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(target.size()))
								|| !candidateMechanics->canBeCastAt(target, problem)))
							continue;
						if(!candidateMechanics->canBeCast(problem))
							continue;

						PossibleSpellcast ps;
						ps.dest = target;
						// NO_LOCATION is represented on the wire by one invalid
						// destination.  Keep a concrete sentinel in the hypothetical
						// cast too: BattleSpellMechanics::castEval intentionally rejects
						// an entirely empty aim, while mass effects use the invalid
						// destination to collect every eligible unit.
						if(candidateMechanics->isMassive() && ps.dest.empty())
							ps.dest.emplace_back(BattleHex::INVALID);
						ps.spell = spell;
						ps.metamagicFollowup = metamagicFollowup;
						ps.divineMandateFollowup = divineMandateFollowup;
						ps.metamagicGrand = metamagicGrandChoice;
						ps.spellOvercharge = overcharge;
						ps.spellSelectiveDispel = selectiveDispel;
						ps.spellCureAffliction = cureAffliction;
						ps.spellCurePhysicalAffliction = curePhysicalAffliction;
						ps.spellMassSlow = massSlow;
						ps.spellStormOfDaggers = stormOfDaggers;
						ps.spellShadowGiftSacrificePercent = shadowGiftSacrificePercent;
						if(isCanonicalPurify(spell))
						{
							const auto selection = SpellTargetEvaluator::purifySelection(
								candidateMechanics.get(), ps.dest);
							if(selection.value <= 0.0f)
								continue;
							ps.spellPurifyChoices = selection.spellEffectGroups;
							ps.spellPurifyPhysicalTargets = selection.physicalPoisonStackIds;
							ps.spellPurifyHeuristicValue = selection.value;
							if(divineMandateFollowup
								&& newHorizonsDivineMandate::hasPurifyingMandatePerk(hero))
							{
								ps.spellPurifyHeuristicValue += purifyingMandateAdditionalAfflictionValue(
									selection, env.get(), battleCallback);
							}
						}
						if(isCanonicalShadowGift(*cb->getBattle(battleID), spell))
						{
							ps.spellShadowGiftHeuristicValue = SpellTargetEvaluator::shadowGiftTradeValue(
								candidateMechanics.get(), ps.dest, shadowGiftSacrificePercent, cb->getBattle(battleID));
							if(ps.spellShadowGiftHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalHandOfFate(*cb->getBattle(battleID), spell))
						{
							const auto expectedDamage = SpellTargetEvaluator::handOfFateExpectedDamageValue(
								candidateMechanics.get(), ps.dest, playerID, cb->getBattle(battleID));
							if(!expectedDamage)
								continue;
							ps.spellHandOfFateExpectedValue = expectedDamage->hostileDamageValue
								* scoreEvaluator.getPositiveEffectMultiplier()
								- 4.0f * expectedDamage->friendlyDamageValue
									* scoreEvaluator.getNegativeEffectMultiplier();
						}
						if(const auto * battleFormEffect = candidateMechanics->findEffect<spells::effects::BattleFormEffect>())
						{
							const auto expectedValue = SpellTargetEvaluator::battleFormExpectedOffensiveValue(
								candidateMechanics.get(), battleFormEffect, ps.dest, env.get(), cb->getBattle(battleID));
							if(!expectedValue)
								continue;
							ps.spellBattleFormExpectedValue = *expectedValue
								* scoreEvaluator.getPositiveEffectMultiplier();
						}
						if(spell->getJsonKey() == "new-horizons:confusion")
						{
							const auto value = SpellTargetEvaluator::confusionExpectedActivationValue(
								candidateMechanics.get(), ps.dest, env.get(), cb->getBattle(battleID));
							if(!value || *value <= 0.0f)
								continue;
							ps.spellConfusionExpectedValue = *value * scoreEvaluator.getPositiveEffectMultiplier();
						}
						if(isCanonicalPuppetMaster(spell))
						{
							const auto expectedValue = expectedPuppetMasterActivationSwing(
								spell, ps.dest, hero, metamagicFollowup, metamagicGrandChoice, overcharge,
								hb, damageCache, env.get(), playerID, side);
							if(expectedValue <= 0.0f)
								continue;
							ps.spellPuppetMasterExpectedValue = expectedValue
								* scoreEvaluator.getPositiveEffectMultiplier();
						}
						if(const auto structuralValue = SpellTargetEvaluator::earthquakeStructuralHPValue(
							candidateMechanics.get(), ps.dest, env.get(), cb->getBattle(battleID)))
						{
							if(*structuralValue <= 0.0f)
								continue;
							ps.spellPlacementHeuristicValue = *structuralValue;
						}
						if(const auto structuralValue = SpellTargetEvaluator::havocStructuralHPValue(
							candidateMechanics.get(), ps.dest))
							ps.spellPlacementHeuristicValue = *structuralValue;
						if(isCanonicalLandMine(*cb->getBattle(battleID), spell))
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::landMinePlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
						if(isSelectedQuicksand(*cb->getBattle(battleID), spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::quicksandPlacementValue(
								candidateMechanics.get(), ps.dest);
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalFireWall(*cb->getBattle(battleID), spell))
						{
							ps.spellFireWallDirection = fireWallDirection(ps.dest);
							if(ps.spellFireWallDirection == BattleHex::NONE)
								continue;
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::fireWallPlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							// A zero-value line is either exposed to friendly ground or has no
							// reachable hostile pressure. Do not let the generic hypothetical
							// cast evaluator turn such a delayed placement into an accidental
							// positive action.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalTimeStop(spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::timeStopPlacementValue(
								candidateMechanics.get(), ps.dest);
							// Time Stop is neutral by content definition, so the generic
							// target comparer cannot tell a helpful enemy footprint from
							// a harmful healthy-ally footprint.  Require a strictly
							// beneficial placement before exposing it to action ranking.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalSpellLock(spell))
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::spellLockPlacementValue(
								candidateMechanics.get(), ps.dest);
							// Spell Lock is indifferent at the content layer. Do not leave
							// generic evaluation free to value a no-op seal as a cast.
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(newHorizonsMagic::physicalPoisonEnabled(
							cb->getBattle(battleID)->getBattle()->getMagicRules(), spell->getId()))
						{
							ps.spellNaturePoisonValue = SpellTargetEvaluator::naturePoisonPlacementValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							if(ps.spellNaturePoisonValue <= 0.0f)
								continue;
						}
						if(spell->getJsonKey() == newHorizonsPlague::SPELL_ID)
						{
							ps.spellPlacementHeuristicValue = SpellTargetEvaluator::plagueDelayedDamageValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(spell->getJsonKey() == newHorizonsSoulChain::SPELL_ID)
						{
							ps.spellSoulChainDelayedValue = SpellTargetEvaluator::soulChainDelayedDamageValue(
								candidateMechanics.get(), ps.dest, cb->getBattle(battleID));
							if(ps.spellSoulChainDelayedValue <= 0.0f)
								continue;
						}
						if(isCanonicalHydrasVitality(spell))
						{
							if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
								continue;

							const auto targetId = ps.dest.front().unitValue->unitId();
							auto projected = std::make_shared<HypotheticBattle>(env.get(), battleCallback);
							const auto * projectedUnit = projected->battleGetUnitByID(targetId);
							if(!projectedUnit)
								continue;

							spells::Target projectedTarget{spells::Destination(projectedUnit)};
							spells::BattleCast projectedCast(projected.get(), hero, spells::Mode::HERO, spell);
							projectedCast.setMetamagicFollowup(metamagicFollowup);
							projectedCast.setMetamagicGrand(metamagicGrandChoice);
							projectedCast.setOvercharge(overcharge);
							projectedCast.setMetamagicTargetUnitId(targetId);
							auto projectedMechanics = spell->battleMechanics(&projectedCast);
							spells::detail::ProblemImpl projectedProblem;
							if(!projectedMechanics->canBeCastAt(projectedTarget, projectedProblem))
								continue;
							projectedMechanics->castEval(projected->getServerCallback(), projectedTarget);

							ps.spellPlacementHeuristicValue = projectedCapacityRegenerationValue(
								env.get(), battleCallback, projected, getRegenerationTurnOrder(), targetId,
								damageCache, side, playerID, scoreEvaluator.getPositiveEffectMultiplier());
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalChaosBlink(spell))
						{
							if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
								continue;

							const auto blinkPreview = newHorizonsBlink::preview(
								*candidateMechanics, ps.dest.front().unitValue);
							if(!blinkPreview)
								continue;

							ps.spellPlacementHeuristicValue = blinkExpectedPositionValue(
								ps.dest.front().unitValue, *blinkPreview, playerID,
								*battleCallback, battleCallback, damageCache)
								* scoreEvaluator.getPositiveEffectMultiplier();
							if(ps.spellPlacementHeuristicValue <= 0.0f)
								continue;
						}
						if(isCanonicalHolyArmor(spell))
						{
							if(visibleMagicalSpellThreat <= 0.0f || friendlyAvailableHealth <= 0
								|| ps.dest.size() != 1 || !ps.dest.front().unitValue
								|| ps.dest.front().unitValue->unitSide() != side)
								continue;
						}
						if(isCanonicalHeavenlyGale(spell) && !target.empty())
							continue;
						if(isCanonicalGuardianSpirit(spell)
							&& (ps.dest.size() != 1 || !ps.dest.front().unitValue
								|| ps.dest.front().unitValue->unitSide() != side))
							continue;
						if(isCanonicalDivineRetribution(spell)
							&& (ps.dest.size() != 1 || !ps.dest.front().unitValue
								|| ps.dest.front().unitValue->unitSide() != side))
							continue;
						if(isCanonicalNatureEntangle(spell)
							&& (ps.dest.size() != 1 || !ps.dest.front().unitValue
								|| ps.dest.front().unitValue->unitSide() == side))
							continue;
						possibleCasts.push_back(ps);
					}
				}
			}
		}
	}
	// Authoritative queries decide whether an Order allowance is available.
	// Possessing a Spell Action must not hide a separate legal Order Action.
	// Side-wide Orders use the authoritative availability query.  Targeted Orders
	// are enumerated through the callback's legal target-set query, with no local
	// guess about action budget, ownership, or current-round state.
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		const auto battleView = cb->getBattle(battleID);
		if(battleView->battleCanUseHeroCommand(side, command))
		{
			PossibleSpellcast candidate;
			candidate.command = command;
			if(heroCommands::isCanonicalRules(battleView->getBattle()->getHeroCommandRules()))
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*battleView, side, command, {}, std::nullopt, env.get(), damageCache, battleView);
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(candidate);
		}
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
	{
		for(const auto & targetIds : orderTargetOptions(*cb->getBattle(battleID), side, command))
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, command, targetIds))
				continue;
			const auto battleView = cb->getBattle(battleID);

			PossibleSpellcast candidate;
			candidate.command = command;
			candidate.commandTargets = targetIds;
			if(command == HeroCommand::FOCUS_FIRE)
			{
				if(targetIds.size() != 1)
					continue;
				candidate.focusFire = cb->getBattle(battleID)->battlePrepareFocusFireState(side, targetIds.front());
				if(!candidate.focusFire)
					continue;
				const auto & recipients = candidate.focusFire->recipientUnitIds;
				const bool hasRemainingShooter = std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
				{
					const auto * unit = battleView->battleGetUnitByID(id);
					return unit && unit->willMove(0) && unit->canShoot();
				});
				const bool canonicalRules = heroCommands::isCanonicalRules(
					battleView->getBattle()->getHeroCommandRules());
				// This is the evaluator's own side. Combined Arms may admit a living
				// melee recipient even when no ordinary shooter remains; legacy
				// snapshots retain their historical shooter-only candidate filter.
				const auto * ownHero = canonicalRules ? battleView->battleGetFightingHero(side) : nullptr;
				const bool hasRemainingCombinedArmsMelee = canonicalRules
					&& heroCommands::hasCombinedArms(ownHero)
					&& std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
					{
						const auto * unit = battleView->battleGetUnitByID(id);
						return isEligibleOrderUnit(*battleView, side, unit)
							&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
							&& unit->willMove(0) && unit->isMeleeAttacker();
					});
				if(!hasRemainingShooter && !hasRemainingCombinedArmsMelee)
					continue;
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*battleView, side, command, targetIds, candidate.focusFire->rangedDamagePercent,
					env.get(), damageCache, battleView);
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			else
			{
				candidate.commandHeuristicValue = canonicalOrderHeuristic(
					*battleView, side, command, targetIds, std::nullopt, env.get(), damageCache, battleView);
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(std::move(candidate));
		}
	}
	LOGFL("Found %d spell-target combinations.", possibleCasts.size());
	if(possibleCasts.empty())
		return false;
	if(!regenerationTurnOrderPrepared && std::any_of(possibleCasts.begin(), possibleCasts.end(),
		[&](const PossibleSpellcast & candidate)
		{
			return isCanonicalRegeneration(candidate.spell, battleCallback->getBattle()->getMagicRules());
		}))
		getRegenerationTurnOrder();

	using ValueMap = PossibleSpellcast::ValueMap;

	auto evaluateQueue = [&](ValueMap & values, const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state, size_t minTurnSpan, bool * enemyHadTurnOut) -> bool
	{
		bool firstRound = true;
		bool enemyHadTurn = false;
		size_t ourTurnSpan = 0;

		bool stop = false;

		for(auto & round : queue)
		{
			if(!firstRound)
				state->nextRound();
			for(auto queuedUnit : round)
			{
				const auto * unit = state->battleGetUnitByID(queuedUnit->unitId());
				if(!unit)
					continue;
				if(!vstd::contains(values, unit->unitId()))
					values[unit->unitId()] = 0;

				if(!unit->alive())
					continue;

				if(state->battleGetActionController(unit) != playerID)
				{
					enemyHadTurn = true;

					const auto enemySide = state->playerToSide(state->battleGetActionController(unit));
					const auto & enemyAllowances = state->getHeroActionAllowances(enemySide);
					const bool enemyCanPayForSpell = enemyAllowances.currentRound >= 0
						? enemyAllowances.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL,
							state->getRound()).has_value()
						: state->battleCastSpells(enemySide) == 0;
					if(!firstRound || enemyCanPayForSpell)
					{
						//enemy could counter our spell at this point
						//anyway, we do not know what enemy will do
						//just stop evaluation
						stop = true;
						break;
					}
				}
				else if(!enemyHadTurn)
				{
					ourTurnSpan++;
				}

				const bool frozenForfeits = newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE);
				state->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
				if(frozenForfeits)
					continue;

				PotentialTargets potentialTargets(unit, damageCache, state);

				if(!potentialTargets.possibleAttacks.empty())
				{
					AttackPossibility attackPossibility = potentialTargets.bestAction();

					auto stackWithBonuses = state->getForUpdate(unit->unitId());
					const bool attackerWasAlive = stackWithBonuses->alive();
					*stackWithBonuses = *attackPossibility.attackerState;
					state->recordBloodrageTransition(stackWithBonuses, attackerWasAlive);

					if(attackPossibility.defenderDamageReduce > 0)
					{
						stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
						stackWithBonuses->removeUnitBonus(Bonus::UntilOwnAttack);
					}
					if(attackPossibility.attackerDamageReduce > 0)
						stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);

					for(auto affected : attackPossibility.affectedUnits)
					{
						stackWithBonuses = state->getForUpdate(affected->unitId());
						const bool affectedWasAlive = stackWithBonuses->alive();
						*stackWithBonuses = *affected;
						state->recordBloodrageTransition(stackWithBonuses, affectedWasAlive);

						if(attackPossibility.defenderDamageReduce > 0)
							stackWithBonuses->removeUnitBonus(Bonus::UntilBeingAttacked);
						if(attackPossibility.attackerDamageReduce > 0 && attackPossibility.attack.defender->unitId() == affected->unitId())
							stackWithBonuses->removeUnitBonus(Bonus::UntilAttack);
					}
				}

				float bav = potentialTargets.berserk
					? potentialTargets.expectedBerserkActionValue()
					: static_cast<float>(potentialTargets.bestActionValue());

				//best action is from effective owner`s point if view, we need to convert to our point if view
				if(state->battleGetActionController(unit) != playerID)
					bav = -bav;
				values[unit->unitId()] += bav;
				if(potentialTargets.berserk)
					expireCompletedProjectedBerserkActivation(*state,
						state->battleGetUnitByID(unit->unitId()));
				state->getForUpdate(unit->unitId())->removeUnitBonus(Bonus::UntilActivationEnds);
			}

			firstRound = false;

			if(stop)
				break;
		}

		if(enemyHadTurnOut)
			*enemyHadTurnOut = enemyHadTurn;

		return ourTurnSpan >= minTurnSpan;
	};

	ValueMap valueOfStack;
	ValueMap healthOfStack;

	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	size_t ourRemainingTurns = 0;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
		valueOfStack[unit->unitId()] = 0;

		if(cb->getBattle(battleID)->battleGetActionController(unit) == playerID && unit->canMove() && !unit->moved())
			ourRemainingTurns++;
	}

	LOGFL("I have %d turns left in this round", ourRemainingTurns);

	const bool castNow = ourRemainingTurns <= 1;

	if(castNow)
		print("I should try to cast a spell now");
	else
		print("I could wait better moment to cast a spell");

	auto amount = all.size();

	std::vector<battle::Units> turnOrder;

	cb->getBattle(battleID)->battleGetTurnOrder(turnOrder, amount, 2); //no more than 1 turn after current, each unit at least once
	std::vector<battle::Units> vampirismTurnOrder;
	cb->getBattle(battleID)->battleGetTurnOrder(vampirismTurnOrder, 0,
		newHorizonsMagic::VAMPIRISM_BASE_DURATION_ROUNDS);

	const auto * battleState = battleView->getBattle();
	const bool unresolvedPreCombatOrder = battleState->getPreCombatOrderState(BattleSide::ATTACKER).isUnresolved()
		|| battleState->getPreCombatOrderState(BattleSide::DEFENDER).isUnresolved();
	if(!mandatoryOrder && !unresolvedPreCombatOrder)
	{
		bool enemyHadTurn = false;

		auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));

		evaluateQueue(valueOfStack, turnOrder, state, 0, &enemyHadTurn);

		if(!enemyHadTurn)
		{
			auto battleIsFinishedOpt = state->battleIsFinished();

			if(battleIsFinishedOpt && !hasFollowupCandidate)
			{
				print("No need to cast a spell. Battle will finish soon.");
				return false;
			}
		}
	}

	CStopWatch timer;

#if BATTLE_TRACE_LEVEL >= 1
	tbb::blocked_range<size_t> r(0, possibleCasts.size());
#else
	tbb::parallel_for(tbb::blocked_range<size_t>(0, possibleCasts.size()), [&](const tbb::blocked_range<size_t> & r)
		{
#endif
			for(auto i = r.begin(); i != r.end(); i++)
			{
				auto & ps = possibleCasts[i];
				auto state = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
				const auto baseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
					? cachedAttack.score : 0.0f;
				std::optional<HypotheticBattle::ProjectedSpellAllowance> spellAllowance;
				std::optional<HypotheticBattle::ProjectedOrderAllowance> orderAllowance;
				HypotheticBattle::ProjectedCounterspellOutcome counterspell;
				bool counterspellNegated = false;
				uint32_t targetId = std::numeric_limits<uint32_t>::max();
				size_t verdantPrisonDendroidStackCount = 0;

				if(ps.command == HeroCommand::NONE)
				{
					spellAllowance = state->prepareHeroSpellAllowance(side, ps.spell->getId(),
						ps.metamagicFollowup, ps.metamagicGrand);
					if(!spellAllowance)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					if(!state->beginProjectedHeroAction(side, *spellAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					counterspell = state->resolveProjectedCounterspell(side, ps.spell);
					if(!counterspell.resolutionKnown || !counterspell.negated.has_value())
					{
						// The armed ward is public, but the opposing hero's mana and
						// Countermage perk may be hidden from this player's callback.
						// Do not invent either a successful or failed Counterspell result.
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					counterspellNegated = *counterspell.negated;
				}
				else
				{
					orderAllowance = state->prepareHeroOrderAllowance(side);
					if(!orderAllowance)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					if(!state->beginProjectedHeroAction(side, *orderAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
				}
				if(isCounterspell(ps.spell))
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					continue;
				}
				// Hand of Fate's secondary recipient is random by design. Its signed
				// expected value was computed across the complete pool during target
				// enumeration; never replace that expectation with castEval's one
				// RNGStub-selected recipient.
				if(ps.command == HeroCommand::NONE && ps.spellHandOfFateExpectedValue.has_value())
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + *ps.spellHandOfFateExpectedValue;
					continue;
				}
				// Battle-form identity is an effect mechanic, not a registered spell
				// ID. Its signed expectation covers the full shared form pool; skip the
				// generic castEval projection so RNGStub cannot replace it with one form.
				if(ps.command == HeroCommand::NONE && ps.spellBattleFormExpectedValue.has_value())
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + *ps.spellBattleFormExpectedValue;
					continue;
				}
				if(ps.command == HeroCommand::NONE && ps.spellConfusionExpectedValue.has_value())
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + *ps.spellConfusionExpectedValue;
					continue;
				}
				// Puppet Master has no immediate HP delta. Value the target's
				// projected controlled activation from target enumeration, then keep
				// that candidate-specific score instead of letting generic castEval
				// collapse the control marker to zero.
				if(ps.command == HeroCommand::NONE && ps.spellPuppetMasterExpectedValue.has_value())
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + *ps.spellPuppetMasterExpectedValue;
					continue;
				}
				// Sanctuary has no immediate health delta. Price direct attacks on
				// this primary target and, for an active Keeper, only the bad-Morale
				// penalty avoided before Sanctuary breaks on the next action.
				if(ps.command == HeroCommand::NONE && isCanonicalSanctuary(ps.spell))
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}

					targetId = ps.dest.front().unitValue->unitId();
					DamageCache sanctuaryDamageCache(&damageCache);
					const auto protectionValue = sanctuaryDirectAttackValue(
						targetId, sanctuaryDamageCache, state);
					const auto moraleValue = activeStack && activeStack->unitId() == targetId
						? 0.0f
						: sanctuaryKeeperMoraleValue(targetId, side,
							*battleCallback, sanctuaryDamageCache, state);
					const auto sanctuaryValue = protectionValue + moraleValue;
					if(sanctuaryValue <= 0.0f
						|| counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + sanctuaryValue * scoreEvaluator.getPositiveEffectMultiplier();
					continue;
				}
				// Purify's selected source groups are action metadata rather than a
				// BattleCast variant. Apply those exact groups to the detached unit
				// snapshots before valuing the accepted Hero Action; never mutate the
				// live callback state during candidate evaluation.
				if(ps.command == HeroCommand::NONE && isCanonicalPurify(ps.spell)
					&& ps.spellPurifyHeuristicValue > 0.0f)
				{
					std::map<uint32_t, std::vector<SpellID>> groupsByUnit;
					std::set<uint32_t> physicalPoisonUnits;
					std::set<uint32_t> selectedUnits;
					for(const auto & [unitId, sourceSpell] : ps.spellPurifyChoices)
					{
						const auto id = static_cast<uint32_t>(unitId);
						groupsByUnit[id].push_back(sourceSpell);
						selectedUnits.insert(id);
					}
					for(const auto unitId : ps.spellPurifyPhysicalTargets)
					{
						const auto id = static_cast<uint32_t>(unitId);
						physicalPoisonUnits.insert(id);
						selectedUnits.insert(id);
					}
					const bool applyPurifyingMandate = ps.divineMandateFollowup
						&& newHorizonsDivineMandate::hasPurifyingMandatePerk(hero);

					bool selectionProjected = !selectedUnits.empty();
					for(const auto id : selectedUnits)
					{
						auto projectedUnit = state->getForUpdate(id);
						if(!projectedUnit || !projectedUnit->alive())
						{
							selectionProjected = false;
							break;
						}
						const auto foundGroups = groupsByUnit.find(id);
						bool removesMagicalSpellGroup = false;
						if(foundGroups != groupsByUnit.end())
							for(const auto sourceSpell : foundGroups->second)
							{
								if(sourceSpell == newHorizonsPurify::physicalPoisonChoiceID())
								{
									if(!newHorizonsPurify::hasPhysicalPoison(projectedUnit.get()))
										selectionProjected = false;
									continue;
								}
								if(newHorizonsPurify::spellEffectGroupBonuses(projectedUnit.get(), sourceSpell).empty())
								{
									selectionProjected = false;
									continue;
								}
								removesMagicalSpellGroup = newHorizonsPurify::isMagicalSpellEffectGroup(
									projectedUnit.get(), sourceSpell) || removesMagicalSpellGroup;
							}
						if(physicalPoisonUnits.contains(id)
							&& !newHorizonsPurify::hasPhysicalPoison(projectedUnit.get()))
							selectionProjected = false;
						if(!selectionProjected)
							break;

						const auto groups = foundGroups == groupsByUnit.end()
							? std::vector<SpellID>{} : foundGroups->second;
						if(!projectedUnit->applyPurifySelection(groups, physicalPoisonUnits.contains(id),
							applyPurifyingMandate && removesMagicalSpellGroup))
						{
							selectionProjected = false;
							break;
						}
					}

					if(!selectionProjected
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance)
						|| counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellPurifyHeuristicValue;
					continue;
				}
				// Shadow Gift's immediate sacrifice is a real cost, but its value is in
				// a bounded three-round attack projection. Use the signed snapshot score
				// instead of generic cast evaluation, which cannot price future attacks.
				if(ps.command == HeroCommand::NONE && ps.spellShadowGiftHeuristicValue > 0.0f)
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellShadowGiftHeuristicValue;
					continue;
				}
				// Hydra's Vitality has no cast-time HP delta: its value comes only
				// from health restored by the shared projected activation lifecycle.
				// Preserve that detached forecast while still rejecting a countered cast.
				if(ps.command == HeroCommand::NONE && isCanonicalHydrasVitality(ps.spell)
					&& ps.spellPlacementHeuristicValue > 0.0f)
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellPlacementHeuristicValue;
					continue;
				}
				// Blink has no deterministic landing at cast time. Preserve its exact
				// shared-helper expectation (including the Blinkmaster distribution)
				// instead of projecting RNGStub's midpoint as if it were authoritative.
				if(ps.command == HeroCommand::NONE && isCanonicalChaosBlink(ps.spell)
					&& ps.spellPlacementHeuristicValue > 0.0f)
				{
					if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					targetId = ps.dest.front().unitValue->unitId();
					if(counterspellNegated
						|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
							ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
							counterspellNegated, *spellAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellPlacementHeuristicValue;
					continue;
				}
				// Canonical delayed spells such as Land Mine, Fire Wall, and Plague may
				// have no immediate unit-health delta. Keep their deterministic
				// live-snapshot value instead of allowing the generic hypothetical cast
				// path to collapse a legal delayed effect to zero before it triggers.
				if(ps.command == HeroCommand::NONE && ps.spellPlacementHeuristicValue > 0.0f
					&& !isHavocStructuralSpell(ps.spell))
				{
					// A delayed spell still consumes the hero exchange; preserve the
					// same best-attack baseline used by contextual Orders so its
					// placement heuristic is compared on the shared BattleAI scale.
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellPlacementHeuristicValue;
					continue;
				}
				// Canonical Nature Poison changes only the target's physical
				// Poison state. Its detached, three-activation marginal value was
				// forecast during target enumeration, so avoid a second generic
				// effect projection that has no immediate health delta.
				if(ps.command == HeroCommand::NONE && ps.spellNaturePoisonValue > 0.0f)
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellNaturePoisonValue;
					continue;
				}
				// Soul Chain changes future damage relationships without an immediate
				// health delta. Its detached forecast was valued during target generation,
				// so do not collapse the cast to zero through generic castEval scoring.
				if(ps.command == HeroCommand::NONE && ps.spellSoulChainDelayedValue > 0.0f)
				{
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance) || counterspellNegated)
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.spellSoulChainDelayedValue;
					continue;
				}
				// Contextual Orders have no faithful projection in the old
				// hypothetical-battle model: targeted Orders and trigger/relationship
				// state carry more information than ordinary unit bonuses.  Their
				// deterministic read-only value was computed while enumerating the
				// authoritative legal target set.  Keep that score intact so they
				// still compete with spells and normal attacks.
				if(ps.command != HeroCommand::NONE
					&& (ps.commandHeuristicValue > 0.0f || mandatoryOrder))
				{
					// An Order consumes the hero's exchange but does not replace the
					// unit action that follows it.  Keep the normal best-action score
					// in the candidate value so contextual Orders compete on the same
					// scale as spells and ordinary attacks rather than being treated as
					// a small, standalone bonus.
					if(!state->projectAcceptedHeroOrder(side, ps.command, ps.commandTargets, *orderAllowance))
						ps.value = std::numeric_limits<float>::lowest();
					else
						ps.value = baseline + ps.commandHeuristicValue;
					continue;
				}

#if BATTLE_TRACE_LEVEL >= 1
				if(ps.dest.empty())
					logAi->trace("Evaluating %s", ps.name());
				else
				{
					auto psFirst = ps.dest.front();
					auto strWhere = psFirst.unitValue ? psFirst.unitValue->getDescription() : std::to_string(psFirst.hexValue.toInt());

					logAi->trace("Evaluating %s at %s", ps.name(), strWhere);
				}
#endif

				if(ps.command == HeroCommand::NONE)
				{
					auto candidateTarget = ps.dest;
					bool missingProjectedTarget = false;
					if(!candidateTarget.empty() && candidateTarget.front().unitValue)
						targetId = candidateTarget.front().unitValue->unitId();
					for(auto & destination : candidateTarget)
					{
						if(!destination.unitValue)
							continue;
						const auto destinationId = destination.unitValue->unitId();
						destination.unitValue = state->battleGetUnitByID(destinationId);
						missingProjectedTarget |= destination.unitValue == nullptr;
					}
					if(missingProjectedTarget)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, ps.spell);
					cast.setMetamagicFollowup(ps.metamagicFollowup);
					cast.setMetamagicGrand(ps.metamagicGrand);
					if(targetId != std::numeric_limits<uint32_t>::max())
						cast.setMetamagicTargetUnitId(targetId);
					cast.setOvercharge(ps.spellOvercharge);
					cast.setSelectiveDispel(ps.spellSelectiveDispel);
					cast.setCureAffliction(ps.spellCureAffliction);
					cast.setCurePhysicalAffliction(ps.spellCurePhysicalAffliction);
					cast.setMassSlow(ps.spellMassSlow);
					if(counterspell.wardActive)
						cast.setCounterspell(counterspell.wardSide, counterspellNegated);
					if(!counterspellNegated)
					{
						auto mechanics = ps.spell->battleMechanics(&cast);
						if(isCanonicalVerdantPrison(ps.spell))
						{
							const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
							if(!liveTarget || !liveTarget->alive() || !liveTarget->unitType())
							{
								ps.value = std::numeric_limits<float>::lowest();
								continue;
							}
						}
						if(mechanics->isNewHorizonsStormOfDaggers())
						{
							// The selected subset size is part of this cast's shared damage
							// context. Use the ordinary mechanics forecast after setting it so
							// school scaling and target resistance follow the authoritative path.
							if(!mechanics->setStormOfDaggersTargetCount(
								static_cast<int32_t>(candidateTarget.size())))
							{
								ps.value = std::numeric_limits<float>::lowest();
								continue;
							}
							mechanics->castEval(state->getServerCallback(), candidateTarget);
						}
						else
						{
							mechanics->castEval(state->getServerCallback(), candidateTarget);
							// Authoritative BattleSpellMechanics::cast captures this fixed-point
							// snapshot after applying the timed marker. castEval deliberately
							// projects effect packets only, so mirror that snapshot onto the
							// cloned accepted target for future-wound valuation.
							projectRegenerationRateSnapshot(*state, *mechanics, ps.spell, candidateTarget);
						}
					}
					if(!state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
						ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
						counterspellNegated, *spellAllowance))
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					if(isCanonicalVampirism(*cb->getBattle(battleID), ps.spell))
					{
						ps.value = baseline + projectedVampirismValue(env.get(), cb->getBattle(battleID),
							state, vampirismTurnOrder, targetId, damageCache, side, playerID,
							scoreEvaluator.getPositiveEffectMultiplier());
						continue;
					}
				}
				else if(ps.command == HeroCommand::FOCUS_FIRE)
				{
					state->setFocusFireState(side, ps.focusFire.value());
				}
				else
				{
					const auto effects = heroCommands::bonuses(state->getHeroCommandRules(), ps.command, *hero);
					for(const auto * unit : state->battleGetAllStacks(true))
					{
						if(state->battleGetOwner(unit) != playerID)
							continue;
						if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
							state->addUnitBonus(unit->unitId(), effects);
					}
				}

				if(ps.command != HeroCommand::NONE
					&& !state->projectAcceptedHeroOrder(side, ps.command, ps.commandTargets, *orderAllowance))
				{
					ps.value = std::numeric_limits<float>::lowest();
					continue;
				}

				// Removed sacrifice victims must remain in the health accounting below.
				if(ps.command == HeroCommand::NONE && ps.spell
					&& ps.spell->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY)
				{
					// castEval above applied the real atomic exchange and the accepted
					// spell allowance was projected normally. Unchanged HP is not a no-op:
					// compare the actual before/after bonus-aware tactical branches.
					const float value = counterspellNegated ? 0.0f
						: SpellTargetEvaluator::realityWarpExchangeValue(env.get(), battleCallback, state,
							playerID, ps.dest, hero->getCasterOwner());
					ps.value = value > 0.0f
						? baseline + value * scoreEvaluator.getPositiveEffectMultiplier()
						: std::numeric_limits<float>::lowest();
					continue;
				}
				auto allUnits = state->battleGetUnitsIf([](const battle::Unit * u) -> bool { return !u->isTurret(); });
				const bool transfigureMatter = isTransfigureMatter(ps.spell);
				const bool phantomArmy = isPhantomArmy(ps.spell);
				const bool summonTrolls = isCanonicalSummonTrolls(ps.spell);
				const auto convergenceElemental = isCanonicalElementalConvergence(ps.spell)
					? newHorizonsElementalTerrain::primaryElemental(*state) : std::nullopt;
				const bool verdantPrison = isCanonicalVerdantPrison(ps.spell);
				const bool slowFamily = ps.spell
					&& newHorizonsMagic::spellVariantBase(state->getMagicRules(), ps.spell->getId()) == SpellID::SLOW;

				auto needFullEval = ps.command == HeroCommand::FOCUS_FIRE
					|| state->hasObstacleChanges() || state->hasWallChanges()
					|| vstd::contains_if(allUnits, [&](const battle::Unit * u) -> bool
					{
						auto original = cb->getBattle(battleID)->battleGetUnitByID(u->unitId());
						return !original || u->getMovementRange() != original->getMovementRange()
							|| newHorizonsFrozen::isFrozen(*u) != newHorizonsFrozen::isFrozen(*original)
							|| (u->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
								!= original->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE))
							|| (slowFamily
								&& u->getInitiative() != original->getInitiative())
							|| u->getPosition() != original->getPosition()
							|| u->alive() != original->alive() || u->isGhost() != original->isGhost();
					});

				DamageCache safeCopy = damageCache;
				DamageCache innerCache(&safeCopy);

				innerCache.buildDamageCache(state, side);

				float entangleMovementValue = 0.0f;
				if(isCanonicalNatureEntangle(ps.spell))
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto movementThreat = counterspellNegated ? 0.0f
						: entangleMovementThreatValue(liveTarget, projectedTarget,
							activeStack->unitId(), side, *battleCallback, state, innerCache);
					const int resistance = liveTarget
						? std::clamp(liveTarget->magicResistance(), 0, 100) : 100;
					const auto applicationChance = 1.0f - static_cast<float>(resistance) / 100.0f;
					if(movementThreat <= 0.0f || applicationChance <= 0.0f)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					// Entangle's status is projected as applied; fold the target's
					// authoritative resistance into this one bounded forecast once.
					entangleMovementValue = movementThreat * applicationChance;
				}

				if(cachedAttack.ap && cachedAttack.waited)
				{
					state->makeWait(activeStack);
				}

				float stackActionScore = 0;
				float damageToHostilesScore = 0;
				float damageToFriendliesScore = 0;
				float initiativeEffectScore = 0;
				float projectedDebuffScore = 0;
				float projectedCrusadeBonusScore = 0;
				if(isCanonicalRegeneration(ps.spell, state->getBattle()->getMagicRules()))
				{
					const auto targets = improvedRegenerationTargets(
						*state, *cb->getBattle(battleID),
						newHorizonsMagic::spellVariantBase(state->getBattle()->getMagicRules(), ps.spell->getId()));
					const auto regenerationValue = projectedRegenerationValue(env.get(), cb->getBattle(battleID), state,
						regenerationTurnOrder, targets, innerCache, side, playerID,
						scoreEvaluator.getPositiveEffectMultiplier());
					damageToHostilesScore += regenerationValue;
				}
				if(isCanonicalHolyArmor(ps.spell) && targetId != std::numeric_limits<uint32_t>::max())
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto mitigationValue = holyArmorMitigationValue(side, liveTarget, projectedTarget,
						ps.spell->getId(), friendlyAvailableHealth, visibleMagicalSpellThreat, innerCache, state);
					damageToHostilesScore += mitigationValue * scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalGuardianSpirit(ps.spell) && targetId != std::numeric_limits<uint32_t>::max())
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto mitigationValue = guardianSpiritMitigationValue(liveTarget,
						projectedTarget, innerCache, state);
					if(counterspellNegated || mitigationValue <= 0.0f)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					damageToHostilesScore += mitigationValue * scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalHeavenlyGale(ps.spell))
				{
					const auto mitigationValue = heavenlyGaleMitigationValue(side, *battleCallback,
						state, ps.spell->getId(), innerCache);
					if(counterspellNegated || mitigationValue <= 0.0f)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					damageToHostilesScore += mitigationValue * scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalCrusade(ps.spell))
				{
					const auto armyValue = crusadeArmyValue(side, activeStack->unitId(),
						*battleCallback, state, ps.spell->getId(), innerCache);
					if(counterspellNegated)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					damageToHostilesScore += armyValue * scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalShieldOfChaos(ps.spell))
				{
					if(counterspellNegated || targetId == std::numeric_limits<uint32_t>::max())
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto signedTargetValue = shieldOfChaosTargetValue(activeStack->unitId(), side,
						liveTarget, projectedTarget, *battleCallback, state, ps.spell->getId(), innerCache);
					if(signedTargetValue <= 0.0f)
					{
						// Shield is neutral-polarity: the target's real damage protection
						// must outweigh its Morale/Luck output loss from our perspective.
						// A zero or harmful net effect should not spend the Hero Action.
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					damageToHostilesScore += signedTargetValue
						* scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalDivineRetribution(ps.spell) && targetId != std::numeric_limits<uint32_t>::max())
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					const auto * projectedTarget = state->battleGetUnitByID(targetId);
					const auto responseValue = divineRetributionThreatValue(liveTarget, projectedTarget,
						side, *battleCallback, state, ps.spell->getId(), innerCache);
					if(counterspellNegated || responseValue <= 0.0f)
					{
						ps.value = std::numeric_limits<float>::lowest();
						continue;
					}
					damageToHostilesScore += responseValue * scoreEvaluator.getPositiveEffectMultiplier();
				}
				if(isCanonicalNatureEntangle(ps.spell))
					damageToHostilesScore += entangleMovementValue
						* scoreEvaluator.getPositiveEffectMultiplier();
				const auto modelActive = state->getForUpdate(activeStack->unitId());
				if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
				{
#if BATTLE_TRACE_LEVEL >= 1
					logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

					PotentialTargets innerTargets(modelActive.get(), innerCache, state);
					BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

					innerEvaluator.updateReachabilityMap(state);

					auto moveTarget = innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state);

					if(!innerTargets.possibleAttacks.empty())
					{
						auto newStackAction = innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state);

						stackActionScore = std::max(moveTarget.score, newStackAction.score);
					}
					else
					{
						stackActionScore = moveTarget.score;
					}
				}
				else if(modelActive->alive())
				{
					auto updatedAttacker = state->getForUpdate(cachedAttack.ap->attack.attacker->unitId());
					auto updatedDefender = state->getForUpdate(cachedAttack.ap->attack.defender->unitId());
					auto updatedBai = BattleAttackInfo(
						updatedAttacker.get(),
						updatedDefender.get(),
						cachedAttack.ap->attack.chargeDistance,
						cachedAttack.ap->attack.shooting);

					auto updatedAttack = AttackPossibility::evaluate(updatedBai, cachedAttack.ap->from, innerCache, state, cachedAttack.ap->perfectMoment);

					BattleExchangeEvaluator innerEvaluator(scoreEvaluator);

					stackActionScore = innerEvaluator.evaluateExchange(updatedAttack, cachedAttack.turn, *targets, innerCache, state);
				}
				for(const auto & unit : allUnits)
				{
					if(!unit->isValidTarget(true) && !vstd::contains(healthOfStack, unit->unitId()))
						continue;

					auto newHealth = unit->getAvailableHealth();
					auto oldHealth = vstd::find_or(healthOfStack, unit->unitId(), 0); // old health value may not exist for newly summoned units
					auto original = cb->getBattle(battleID)->battleGetUnitByID(unit->unitId());
					if(ps.spellCurePhysicalAffliction == "frozen" && original
						&& newHorizonsFrozen::isFrozen(*original) && !newHorizonsFrozen::isFrozen(*unit)
						&& state->battleGetActionController(unit) == playerID
						&& unit->alive() && unit->canMove() && unit->willMove(0))
					{
						// A full-HP Cure has no health/stat delta. Recovering another
						// stack's lost normal action can still be valuable when the active
						// stack cannot seed an exchange. Use a real legal action's value,
						// as a lower bound, not an extra sum over the same queued benefit.
						PotentialTargets recoveredActions(unit, innerCache, state);
						const float recoveredValue = std::max(0.0f,
							static_cast<float>(recoveredActions.bestActionValue()));
						stackActionScore = std::max(stackActionScore, baseline
							+ recoveredValue * scoreEvaluator.getPositiveEffectMultiplier());
					}
					if(ps.spell && original && state->battleGetOwner(unit) != playerID)
					{
						if(ps.spell->getId() == SpellID::BERSERK && unit->unitId() == targetId)
							projectedDebuffScore += expectedBerserkActivationValue(
								original, unit, innerCache, state, env.get(), playerID);
						else if(ps.spell->getId() == SpellID::SORROW)
							projectedDebuffScore += estimateProjectedSorrowTargetValue(original, unit, innerCache, state);
						else if(ps.spell->getId() == SpellID::CURSE)
							projectedDebuffScore += estimateProjectedCurseTargetValue(original, unit, innerCache, state);
						else if(ps.spell->getId() == SpellID::MISFORTUNE && unit->unitId() == targetId)
							projectedDebuffScore += estimateProjectedMisfortuneTargetValue(original, unit, innerCache, state);
						else if(isCanonicalHexOfPain(ps.spell))
							projectedDebuffScore += estimateProjectedHexOfPainTargetValue(original, unit, state);
						else if(isCanonicalFrailty(ps.spell) && unit->unitId() == targetId)
							projectedDebuffScore += estimateProjectedFrailtyTargetValue(original, unit, innerCache, state);
						else if(isCanonicalDoom(ps.spell) && unit->unitId() == targetId)
							projectedDebuffScore += estimateProjectedDoomTargetValue(original, unit, innerCache, state);
					}
					const bool phantomArmyStack = phantomArmy && !original
						&& state->battleGetOwner(unit) == playerID
						&& phantomArmyInitialIntegrity(unit, ps.spell->getId()) > 0;
					if(phantomArmyStack)
					{
						// Summoned stacks are excluded from the ordinary health-delta score
						// below.  Score this copied army once from its full count, its
						// dedicated integrity pool, and the spell's two-round duration.
						damageToHostilesScore += phantomArmyCombatValue(unit, ps.spell->getId());
					}
					if(transfigureMatter && !original && unit->unitType()
						&& unit->unitType()->getJsonKey() == "core:diamondGolem"
						&& state->battleGetOwner(unit) == playerID && newHealth > 0)
					{
						// A Transfigure Matter cast creates temporary Diamond Golems. Use
						// their projected partial-stack health and normal creature AI
						// value, rather than dropping all magical summons from scoring.
						const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
						damageToHostilesScore += static_cast<float>(newHealth)
							* static_cast<float>(unit->unitType()->getAIValue())
							/ static_cast<float>(maxHealth);
					}
					if(!original && unit->unitType()
						&& ((summonTrolls && unit->unitType()->getJsonKey() == "core:troll")
							|| (convergenceElemental && unit->creatureId() == *convergenceElemental))
						&& unit->isSummoned()
						&& state->battleGetOwner(unit) == playerID && newHealth > 0)
					{
						const auto unitState = unit->acquireState();
						if(unitState && unitState->natureSummoned)
						{
							// These Nature stacks are magical summons and skipped by the generic
							// health-delta score below.  Value their exact projected HP here,
							// including the partial final creature and its native template value.
							const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
							damageToHostilesScore += static_cast<float>(newHealth)
								* static_cast<float>(unit->unitType()->getAIValue())
								/ static_cast<float>(maxHealth);
						}
					}
					if(verdantPrison && !original && unit->unitType()
						&& unit->unitType()->getJsonKey() == "core:dendroidGuard"
						&& unit->isSummoned()
						&& state->battleGetOwner(unit) == playerID && newHealth > 0)
					{
						const auto unitState = unit->acquireState();
						if(unitState && unitState->natureSummoned)
						{
							++verdantPrisonDendroidStackCount;
							// Verdant Prison's temporary Dendroids are excluded from the
							// generic magical-summon health delta. Value only the exact HP
							// created by this cast, including the wounded final creatures.
							const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
							damageToHostilesScore += static_cast<float>(newHealth)
								* static_cast<float>(unit->unitType()->getAIValue())
								/ static_cast<float>(maxHealth);
						}
					}
					if(slowFamily
						&& original && original->alive() && unit->alive())
					{
						const int oldInitiative = std::max(1, original->getInitiative());
						const int initiativeDelta = unit->getInitiative() - original->getInitiative();
						const int signedDelta = state->battleGetOwner(unit) == playerID
							? initiativeDelta : -initiativeDelta;
						const float stackValue = static_cast<float>(unit->getCount()) * unit->unitType()->getAIValue();
						initiativeEffectScore += stackValue * static_cast<float>(signedDelta)
							/ static_cast<float>(oldInitiative) * 0.01f;
					}
					if(isCanonicalCrusade(ps.spell) && original && original->alive() && unit->alive()
						&& state->battleGetOwner(unit) == playerID && unit->unitType())
					{
						const auto effect = crusadeProjection(unit, ps.spell->getId());
						const auto rounds = static_cast<float>(effect.roundsRemaining);
						if(rounds > 0.0f && effect.initiative > 0)
						{
							const int oldInitiative = std::max(1, original->getInitiative());
							const int initiativeDelta = unit->getInitiative() - original->getInitiative();
							const float stackValue = static_cast<float>(unit->getCount())
								* unit->unitType()->getAIValue();
							initiativeEffectScore += stackValue * static_cast<float>(initiativeDelta)
								/ static_cast<float>(oldInitiative) * 0.01f * rounds * 0.5f;
						}
						if(rounds > 0.0f && effect.preventsNegativeMorale)
						{
							const float recoveredMoraleActivations = state->projectMoraleActivationDelta(original, unit,
								expectedMoraleActivationChange(*battleCallback, original),
								expectedMoraleActivationChange(*state, unit), rounds * 0.5f);
							if(recoveredMoraleActivations > 0.0f)
								projectedCrusadeBonusScore += recoveredMoraleActivations
								* expectedTargetActivationValue(unit, innerCache, state);
						}
					}

					if(oldHealth != newHealth)
					{
						if(state->isElementalRebirthSpawn(unit->unitId()) && oldHealth == 0
							&& newHealth > 0 && unit->unitType())
						{
							// Elemental Rebirth is a newly-created temporary stack, but unlike
							// generic magical summons it replaces destroyed army strength. Keep
							// its score in the existing creature-value / exact-HP scale.
							const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
							const float spawnValue = static_cast<float>(newHealth)
								* static_cast<float>(unit->unitType()->getAIValue())
								/ static_cast<float>(maxHealth);
							const bool ourUnit = state->battleGetOwner(unit) == playerID;
							damageToHostilesScore += ourUnit ? spawnValue : -spawnValue;
							continue;
						}

						auto damage = std::abs(oldHealth - newHealth);
						auto originalDefender = original;

						auto dpsReduce = AttackPossibility::calculateDamageReduce(
							nullptr,
							originalDefender && originalDefender->alive() ? originalDefender : unit,
							damage,
							innerCache,
							state);
						const bool ourUnit = state->battleGetOwner(unit) == playerID;
						const bool goodEffect = newHealth > oldHealth;
						const bool naturesWrath = ps.spell
							&& ps.spell->getJsonKey() == newHorizonsNaturesWrath::SPELL_KEY;
						const bool pandemonium = ps.spell
							&& ps.spell->getJsonKey() == newHorizonsPandemonium::SPELL_KEY;
						if(original && !goodEffect && (pandemonium
							|| ((ps.spellStormOfDaggers || naturesWrath) && !ourUnit)))
						{
							// Pandemonium also permits friendly resistance. castEval uses
							// shared damage per recipient but leaves
							// that random roll out, so value its expected hit probability here.
							const int resistance = std::clamp(original->magicResistance(), 0, 100);
							dpsReduce *= 1.0f - static_cast<float>(resistance) / 100.0f;
						}

						if(ourUnit == goodEffect)
						{
							auto isMagical = state->getForUpdate(unit->unitId())->summoned
								|| unit->isClone()
								|| unit->isGhost()
								|| phantomArmyStack;

							if(ourUnit && goodEffect && isMagical && !naturesWrath)
								continue;

							damageToHostilesScore += dpsReduce * scoreEvaluator.getPositiveEffectMultiplier();
						}
						else
							// discourage AI making collateral damage with spells
							damageToFriendliesScore -= 4 * dpsReduce * scoreEvaluator.getNegativeEffectMultiplier();

#if BATTLE_TRACE_LEVEL >= 1
						// Ensure ps.dest is not empty before accessing the first element
						if (!ps.dest.empty()) 
						{
							logAi->trace(
								"Spell %s to %d affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								ps.dest.at(0).hexValue.toInt(),  // Safe to access .at(0) now
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
						else 
						{
							// Handle the case where ps.dest is empty
							logAi->trace(
								"Spell %s has no destination, affects %s (%d), dps: %2f oldHealth: %d newHealth: %d",
								ps.name(),
								unit->creatureId().toCreature()->getNameSingularTranslated(),
								unit->getCount(),
								dpsReduce,
								oldHealth,
								newHealth);
						}
#endif
					}
				}
				if(verdantPrison && verdantPrisonDendroidStackCount > 0)
				{
					const auto * liveTarget = battleCallback->battleGetUnitByID(targetId);
					if(liveTarget && liveTarget->alive() && liveTarget->unitType())
					{
						// More actual Dendroid stacks mean fewer open ground routes
						// around this enemy. Keep this small beside the value of the
						// exact summoned HP, and discount flying targets.
						const float enemyStackValue = static_cast<float>(liveTarget->getCount())
							* static_cast<float>(liveTarget->unitType()->getAIValue());
						const float ringCoverageBonus = std::min(0.24f,
							0.04f * static_cast<float>(verdantPrisonDendroidStackCount));
						const float escapeFactor = liveTarget->hasBonusOfType(BonusType::FLYING) ? 0.15f : 1.0f;
						damageToHostilesScore += enemyStackValue * ringCoverageBonus * escapeFactor
							* scoreEvaluator.getPositiveEffectMultiplier();
					}
				}
				damageToHostilesScore += projectedDebuffScore * scoreEvaluator.getPositiveEffectMultiplier();
				damageToHostilesScore += projectedCrusadeBonusScore
					* scoreEvaluator.getPositiveEffectMultiplier();

				if (vstd::isAlmostEqual(stackActionScore, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)))
				{
					ps.value = damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}
				else
				{
				ps.value = stackActionScore + damageToFriendliesScore + damageToHostilesScore + initiativeEffectScore;
				}
				if(!counterspellNegated && ps.command == HeroCommand::NONE && isHavocStructuralSpell(ps.spell))
					ps.value += ps.spellPlacementHeuristicValue;
#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("Total score for %s: %2f (action: %2f, friedly damage: %2f, hostile damage: %2f)", ps.name(), ps.value, stackActionScore, damageToFriendliesScore, damageToHostilesScore);
#endif
			}
#if BATTLE_TRACE_LEVEL == 0
		});
#endif

	// No effective ordinary action still permits declining a harmful spell.
	// Use this same baseline for casts and their projected continuations.
	const auto noCastBaseline = cachedAttack.score > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
		? cachedAttack.score : 0.0f;
	// Re-evaluate a granted continuation against the actual post-cast battlefield
	// on the next decision. Do not add a second copy of a pre-cast damage score:
	// the first spell may remove its target or spend the mana the second needs.

	LOGFL("Evaluation took %d ms", timer.getDiff());

	auto castToPerform = *vstd::maxElementByFun(possibleCasts, [](const PossibleSpellcast & ps) -> float
		{
			return ps.value;
		});
	if(!mandatoryOrder && (castToPerform.metamagicFollowup || castToPerform.divineMandateFollowup)
		&& (castToPerform.value < noCastBaseline
			|| vstd::isAlmostEqual(castToPerform.value, noCastBaseline)))
	{
		// Keeping the allowance is the no-cast baseline, not literal score zero: hypothetical
		// spell values include the active exchange's projected attack. A legal
		// follow-up can therefore be positive in absolute terms while still
		// being harmful relative to leaving the active exchange untouched.
		LOGL("No beneficial hero action; retaining the Spell Action for this round.");
		return false;
	}
	if(mandatoryOrder || castToPerform.metamagicFollowup || castToPerform.divineMandateFollowup
		|| (castToPerform.value > noCastBaseline && !vstd::isAlmostEqual(castToPerform.value, noCastBaseline)))
	{
		LOGFL("Best hero action is %s (value %d). Will perform.", castToPerform.name() % castToPerform.value);
		if(castToPerform.command != HeroCommand::NONE)
		{
			if(!commandTargetIsLegal(*cb->getBattle(battleID), side, castToPerform.command,
				castToPerform.commandTargets))
				return false;

			if(castToPerform.command == HeroCommand::FOCUS_FIRE)
			{
				const auto targetId = castToPerform.focusFire.value().targetUnitId;
				if(!cb->getBattle(battleID)->battleCanConfirmHeroCommand(side, castToPerform.command, targetId))
					return false;
				cb->battleMakeSpellAction(battleID,
					BattleAction::makeTargetedHeroCommand(side, castToPerform.command, targetId));
			}
			else
			{
				BattleAction action;
				if(castToPerform.commandTargets.size() == 1)
					action = BattleAction::makeTargetedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front());
				else if(castToPerform.commandTargets.size() == 2)
					action = BattleAction::makePairedHeroCommand(side, castToPerform.command,
						castToPerform.commandTargets.front(), castToPerform.commandTargets.back());
				else
					action = BattleAction::makeHeroCommand(side, castToPerform.command);
				cb->battleMakeSpellAction(battleID, action);
			}
			activeActionMade = true;
			return true;
		}
		BattleAction spellcast;
		spellcast.actionType = EActionType::HERO_SPELL;
		spellcast.spell = castToPerform.spell->id;
		spellcast.spellOvercharge = castToPerform.spellOvercharge;
		spellcast.spellSelectiveDispel = castToPerform.spellSelectiveDispel;
		spellcast.spellCureAffliction = castToPerform.spellCureAffliction;
		spellcast.spellCurePhysicalAffliction = castToPerform.spellCurePhysicalAffliction;
		spellcast.spellPurifyChoices = castToPerform.spellPurifyChoices;
		spellcast.spellMassSlow = castToPerform.spellMassSlow;
		spellcast.spellShadowGiftSacrificePercent = castToPerform.spellShadowGiftSacrificePercent;
		spellcast.metamagicFollowup = castToPerform.metamagicFollowup;
		if(isCanonicalFireWall(*cb->getBattle(battleID), castToPerform.spell)
			&& castToPerform.spellFireWallDirection != BattleHex::NONE
			&& !castToPerform.dest.empty())
		{
			// Evaluation keeps the complete footprint so delayed damage and
			// friendly exposure can be scored.  The wire action intentionally
			// carries only the start hex plus the direction; the server expands
			// and validates the line authoritatively.
			spellcast.aimToHex(castToPerform.dest.front().hexValue);
			spellcast.spellFireWallDirection = castToPerform.spellFireWallDirection;
		}
		else if(isCanonicalHeavenlyGale(castToPerform.spell)
			|| isCanonicalCrusade(castToPerform.spell) || castToPerform.dest.empty())
			// Mass spells use AimType::NOTHING in mechanics and are enumerated as
			// an empty candidate. The action protocol still requires one destination
			// entry; INVALID is the shared NO_LOCATION sentinel, not a unit target.
			spellcast.aimToHex(BattleHex::INVALID);
		else
			spellcast.setTarget(castToPerform.dest);
		spellcast.side = side;
		spellcast.stackNumber = -1;
		cb->battleMakeSpellAction(battleID, spellcast);
		activeActionMade = true;

		return true;
	}

	LOGFL("Best hero action is %s. But it is actually useless (value %d).", castToPerform.name() % castToPerform.value);

	return false;
}

//Below method works only for offensive spells
void BattleEvaluator::evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps)
{
	using ValueMap = PossibleSpellcast::ValueMap;

	RNGStub rngStub;
	HypotheticBattle state(env.get(), cb->getBattle(battleID));
	TStacks all = cb->getBattle(battleID)->battleGetAllStacks(false);

	ValueMap healthOfStack;
	ValueMap newHealthOfStack;

	for(auto unit : all)
	{
		healthOfStack[unit->unitId()] = unit->getAvailableHealth();
	}


	spells::BattleCast cast(&state, stack, spells::Mode::CREATURE_ACTIVE, ps.spell);
	cast.castEval(state.getServerCallback(), ps.dest);

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);
		newHealthOfStack[unitId] = localUnit->getAvailableHealth();
	}

	int64_t totalGain = 0;

	for(auto unit : all)
	{
		auto unitId = unit->unitId();
		auto localUnit = state.battleGetUnitByID(unitId);

		auto healthDiff = newHealthOfStack[unitId] - healthOfStack[unitId];

		if(state.battleGetOwner(localUnit) != playerID)
			healthDiff = -healthDiff;

		if(healthDiff < 0)
		{
			ps.value = -1;
			return; //do not damage own units at all
		}

		totalGain += healthDiff;
	}

	// consider the case in which spell summons units
	auto newUnits = state.getUnitsIf([&](const battle::Unit * u) -> bool
		{
			return !u->isGhost() && !u->isTurret() && !vstd::contains(healthOfStack, u->unitId());
		});

	for(auto unit : newUnits)
	{
		const auto health = unit->getAvailableHealth();
		totalGain += state.battleGetOwner(unit) == playerID ? health : -health;
	}

	ps.value = totalGain;
}

void BattleEvaluator::print(const std::string & text) const
{
	logAi->trace("%s Battle AI[%p]: %s", playerID.toString(), this, text);
}
