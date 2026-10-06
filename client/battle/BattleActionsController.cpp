/*
 * BattleActionsController.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleActionsController.h"

#include "BattleFieldController.h"
#include "BattleHero.h"
#include "BattleInterface.h"
#include "MagicArrowOverchargeWindow.h"
#include "NewHorizonsBattleStatus.h"
#include "BattleSiegeController.h"
#include "BattleStacksController.h"
#include "BattleWindow.h"
#include "TemporalFieldWindow.h"
#include "PurifyWindow.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/CIntObject.h"
#include "../gui/CursorHandler.h"
#include "../gui/WindowHandler.h"
#include "../windows/CCreatureWindow.h"
#include "../windows/InfoWindows.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "lib/spells/NewHorizonsBlink.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../lib/spells/OrientedSpellPattern.h"
#include "../../lib/spells/effects/Effect.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsFriendlyFire.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/texts/CGeneralTextHandler.h"

#include <set>

struct TextReplacement
{
	std::string placeholder;
	std::string replacement;
};

using TextReplacementList = std::vector<TextReplacement>;

constexpr std::string_view transfigureMatterJsonKey = "new-horizons:transfigureMatter";
constexpr std::string_view summonTrollsJsonKey = "new-horizons:summonTrolls";
constexpr std::string_view verdantPrisonJsonKey = "new-horizons:verdantPrison";
constexpr std::string_view hydrasVitalityJsonKey = "new-horizons:hydrasVitality";
constexpr std::string_view stormOfDaggersJsonKey = "new-horizons:stormOfDaggers";
constexpr std::string_view shadowGiftJsonKey = "new-horizons:shadowGift";
constexpr std::string_view chainLightningJsonKey = "core:chainLightning";
constexpr std::string_view masterChainLightningJsonKey = "new-horizons:masterChainLightning";
constexpr int32_t stormOfDaggersMaximumTargets = 5;
constexpr int32_t vengefulVinesFootprintHexCount = 3;

struct FriendlyFirePreview
{
	newHorizonsFriendlyFire::ConfirmationSnapshot snapshot;
	std::vector<const CStack *> recipients;
};

std::optional<FriendlyFirePreview> buildFriendlyFirePreview(BattleInterface & owner,
	const BattleAction & action, uint64_t castingSession)
{
	if(action.actionType != EActionType::HERO_SPELL || !owner.curInt || !owner.curInt->cb
		|| owner.curInt->isAutoFightOn || !owner.makingTurn() || owner.isInTacticsMode())
		return std::nullopt;

	const auto battle = owner.getBattle();
	const auto * battleState = battle ? battle->getBattle() : nullptr;
	const auto * hero = owner.currentHero();
	const auto * spell = action.spell.toSpell();
	if(!battle || !battleState || !hero || !spell || !spell->isDamage())
		return std::nullopt;

	const auto side = battle->battleGetMySide();
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| action.side != side || battleState->getSideHero(side) != hero
		|| battleState->getSidePlayer(side) != owner.curInt->cb->getPlayerID())
		return std::nullopt;

	const auto target = action.getTarget(battle.get());
	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setOvercharge(action.spellOvercharge);
	cast.setSelectiveDispel(action.spellSelectiveDispel);
	cast.setCureAffliction(action.spellCureAffliction);
	cast.setMassSlow(action.spellMassSlow);
	cast.setShadowGiftSacrificePercent(action.spellShadowGiftSacrificePercent);
	cast.setMetamagicFollowup(action.metamagicFollowup);
	cast.setMetamagicGrand(action.metamagicGrand);
	cast.setMetamagicManaRefund(action.metamagicManaRefund);
	for(const auto & destination : target)
	{
		if(destination.unitValue)
		{
			cast.setMetamagicTargetUnitId(destination.unitValue->unitId());
			break;
		}
	}

	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics || !mechanics->usesNewHorizonsMagicV3())
		return std::nullopt;
	mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(action.target.size()));

	spells::detail::ProblemImpl targetProblem;
	if(!mechanics->canBeCastAt(target, targetProblem))
		return std::nullopt;

	FriendlyFirePreview result;
	result.snapshot.battleID = battleState->getBattleID();
	result.snapshot.spellID = action.spell;
	result.snapshot.heroID = hero->id;
	result.snapshot.casterSide = side;
	result.snapshot.round = battleState->getRound();
	result.snapshot.castingSession = castingSession;
	result.recipients = newHorizonsFriendlyFire::potentialFriendlyDamageTargets(*mechanics, target);
	result.snapshot.friendlyUnitIDs.reserve(result.recipients.size());
	for(const auto * stack : result.recipients)
		if(stack)
			result.snapshot.friendlyUnitIDs.push_back(stack->unitId());
	return result;
}

std::string friendlyFireConfirmationText(const CSpell * spell, const std::vector<const CStack *> & recipients)
{
	std::string result = spell ? spell->getNameTranslated() : "This spell";
	result += " may also affect these friendly stacks:\n";
	for(const auto * stack : recipients)
	{
		if(stack)
			result += "\n  " + std::to_string(stack->getCount()) + " " + stack->getName();
	}
	result += "\n\nCast anyway?";
	return result;
}

bool isStormOfDaggersSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == stormOfDaggersJsonKey;
}

bool isSoulChainSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsSoulChain::SPELL_ID;
}

bool isLifeDrainSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL;
}

bool isShadowGiftSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == shadowGiftJsonKey;
}

bool isChainLightningPreviewSpell(const CSpell * spell)
{
	return spell && (spell->getJsonKey() == chainLightningJsonKey
		|| spell->getJsonKey() == masterChainLightningJsonKey);
}

bool isCanonicalLandMine(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isLandMine(spell->id);
}

bool isCanonicalFireWall(const CBattleInfoCallback & battle, const CSpell * spell)
{
	return spell && newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		&& newHorizonsMagic::isFireWall(spell->id);
}

BattleHex::EDir fireWallDirectionBetween(const BattleHex & start, const BattleHex & endpoint)
{
	for(const auto direction : BattleHex::hexagonalDirections())
		if(start.cloneInDirection(direction, false) == endpoint)
			return direction;
	return BattleHex::NONE;
}

bool canonicalLandMineHexIsEmpty(const CBattleInfoCallback & battle, const BattleHex & hex)
{
	if(!hex.isAvailable()
		|| battle.battleGetUnitByPos(hex, true)
		|| !battle.battleGetAllObstaclesOnPos(hex, false).empty())
		return false;
	return battle.getAccessibility()[hex.toInt()] == EAccessibility::ACCESSIBLE;
}

bool canonicalFireWallHexIsEmpty(const CBattleInfoCallback & battle, const BattleHex & hex)
{
	if(!canonicalLandMineHexIsEmpty(battle, hex))
		return false;
	if(!battle.hasFortifications())
		return true;

	const auto wallPart = battle.battleHexToWallPart(hex);
	if(wallPart == EWallPart::INVALID)
		return true;
	if(wallPart == EWallPart::INDESTRUCTIBLE_PART
		|| wallPart == EWallPart::INDESTRUCTIBLE_PART_OF_GATE
		|| wallPart == EWallPart::BOTTOM_TOWER
		|| wallPart == EWallPart::UPPER_TOWER)
		return false;

	const auto wallState = battle.battleGetWallState(wallPart);
	return wallState == EWallState::NONE || wallState == EWallState::DESTROYED;
}

bool isTransfigureMatterObstacle(const CObstacleInstance * obstacle)
{
	if(!obstacle || obstacle->obstacleType != CObstacleInstance::USUAL)
		return false;

	// The effect converts the whole ordinary scenery footprint. Reading the
	// affected tiles also keeps malformed/empty obstacle entries out of the
	// overlay and click path.
	return !obstacle->getAffectedTiles().empty();
}

static std::string replacePlaceholders(const std::string & input, const TextReplacementList & format )
{
	MetaString result = MetaString::createFromRawString(input);
	for(const auto & entry : format)
		result.replaceTokenRawString(entry.placeholder, entry.replacement);

	return result.toString(&GAME->translator());
}

static std::string formatWithStackName(const std::string & textID, const CStack * stack)
{
	MetaString result = MetaString::createFromTextID(textID);
	result.replaceName(stack->unitType()->getId(), stack->getCount());
	return result.toString(&GAME->translator());
}

static std::string translatePlural(int amount, const std::string& baseTextID)
{
	if(amount == 1)
		return LIBRARY->generaltexth->translate(baseTextID + ".1");
	return LIBRARY->generaltexth->translate(baseTextID);
}

static std::string formatPluralImpl(int amount, const std::string & amountString, const std::string & baseTextID)
{
	std::string baseString = translatePlural(amount, baseTextID);
	TextReplacementList replacements {
		{ "%d", amountString }
	};

	return replacePlaceholders(baseString, replacements);
}

static std::string formatPlural(int amount, const std::string & baseTextID)
{
	return formatPluralImpl(amount, std::to_string(amount), baseTextID);
}

static std::string formatPlural(DamageRange range, const std::string & baseTextID)
{
	if (range.min == range.max)
		return formatPlural(range.min, baseTextID);

	std::string rangeString = std::to_string(range.min) + " - " + std::to_string(range.max);

	return formatPluralImpl(range.max, rangeString, baseTextID);
}

static std::string formatAttack(const DamageEstimation & estimation, const std::string & creatureName, const std::string & baseTextID, int shotsLeft)
{
	TextReplacementList replacements = {
		{ "%CREATURE", creatureName },
		{ "%DAMAGE", formatPlural(estimation.damage, "vcmi.battleWindow.damageEstimation.damage") },
		{ "%SHOTS", formatPlural(shotsLeft, "vcmi.battleWindow.damageEstimation.shots") },
		{ "%KILLS", formatPlural(estimation.kills, "vcmi.battleWindow.damageEstimation.kills") },
	};

	return replacePlaceholders(LIBRARY->generaltexth->translate(baseTextID), replacements);
}

static std::string formatMeleeAttack(const DamageEstimation & estimation, const std::string & creatureName)
{
	std::string baseTextID = estimation.kills.max == 0 ?
		"vcmi.battleWindow.damageEstimation.melee" :
		"vcmi.battleWindow.damageEstimation.meleeKills";

	return formatAttack(estimation, creatureName, baseTextID, 0);
}

static std::string formatRangedAttack(const DamageEstimation & estimation, const std::string & creatureName, int shotsLeft)
{
	std::string baseTextID = estimation.kills.max == 0 ?
		"vcmi.battleWindow.damageEstimation.ranged" :
		"vcmi.battleWindow.damageEstimation.rangedKills";

	return formatAttack(estimation, creatureName, baseTextID, shotsLeft);
}

static std::string formatRetaliation(const DamageEstimation & estimation, bool mayBeKilled)
{
	if (estimation.damage.max == 0)
		return LIBRARY->generaltexth->translate("vcmi.battleWindow.damageRetaliation.never");

	std::string baseTextID = estimation.kills.max == 0 ?
								 "vcmi.battleWindow.damageRetaliation.damage" :
								 "vcmi.battleWindow.damageRetaliation.damageKills";

	std::string prefixTextID = mayBeKilled ?
		"vcmi.battleWindow.damageRetaliation.may" :
		"vcmi.battleWindow.damageRetaliation.will";

	return LIBRARY->generaltexth->translate(prefixTextID) + formatAttack(estimation, "", baseTextID, 0);
}

static std::string prepareSpellEffectText(int gnrlTextID, const spells::effects::SpellEffectValue & value,
										  const std::string & spellName, const std::string & targetName)
{
	auto templateText = MetaString::createFromTextID("core.genrltxt", gnrlTextID);
	if (!spellName.empty())
		templateText.replaceRawString(spellName);
	if (!targetName.empty())
		templateText.replaceRawString(targetName);

	std::string baseText = templateText.toString(&GAME->translator());

	if(value.unitsDelta > 0)
	{
		auto unitTypeName = value.unitsDelta == 1 ? value.unitType->getNameSingularTranslated()
												  : value.unitType->getNamePluralTranslated();
		return baseText +" (+ "+ std::to_string(value.unitsDelta) +" "+ unitTypeName +")";
	}

	if(value.hpDelta == 0)
		return baseText;

	std::string outputString;
	if(value.hpDelta > 0)
	{
		auto val = value.hpDelta;
		TextReplacementList replacements {{ "%d", std::to_string(val) }};
		int64_t correctPluralIndex = val > 3 ? 0 : std::clamp(val, int64_t(1), int64_t(2));
		std::string textTemplateKey;
		if(gnrlTextID == 549) //sacrifice spell
			textTemplateKey = "vcmi.battleWindow.sacrificeAcquiredHealth.";
		else
			textTemplateKey = "vcmi.battleWindow.healValuePreview.";
		outputString = LIBRARY->generaltexth->translate(textTemplateKey + std::to_string(correctPluralIndex));
		outputString = replacePlaceholders(outputString, replacements);
	}
	else
	{
		outputString = formatPlural(value.hpDelta * -1, "vcmi.battleWindow.damageEstimation.damage");
		if(value.unitsDelta < 0)
			outputString += ", "+ formatPlural(value.unitsDelta * -1, "vcmi.battleWindow.damageEstimation.kills");
	}

	return baseText +" ("+ outputString +")";
}

static std::string prepareTransfigureMatterText(const CSpell * spell, const spells::effects::SpellEffectValue & value)
{
	if(!spell)
		return {};

	auto templateText = MetaString::createFromTextID("core.genrltxt", 26);
	templateText.replaceRawString(spell->getNameTranslated());
	std::string result = templateText.toString(&GAME->translator());
	std::vector<std::string> details;

	if(value.unitsDelta > 0 && value.unitType)
	{
		const auto unitName = value.unitsDelta == 1
			? value.unitType->getNameSingularTranslated()
			: value.unitType->getNamePluralTranslated();
		details.push_back("+ " + std::to_string(value.unitsDelta) + " " + unitName);
	}

	if(value.hpDelta > 0)
		details.push_back("total HP: " + std::to_string(value.hpDelta));

	if(!details.empty())
	{
		result += " (";
		for(size_t index = 0; index < details.size(); ++index)
		{
			if(index != 0)
				result += ", ";
			result += details[index];
		}
		result += ")";
	}

	return result;
}

static std::string prepareSummonTrollsText(const CSpell * spell, const spells::effects::SpellEffectValue & value)
{
	if(!spell)
		return {};

	auto templateText = MetaString::createFromTextID("core.genrltxt", 26);
	templateText.replaceRawString(spell->getNameTranslated());
	std::string result = templateText.toString(&GAME->translator());
	std::vector<std::string> details;

	if(value.unitsDelta > 0 && value.unitType)
	{
		const auto unitName = value.unitsDelta == 1
			? value.unitType->getNameSingularTranslated()
			: value.unitType->getNamePluralTranslated();
		details.push_back("temporary Troll stack: + " + std::to_string(value.unitsDelta) + " " + unitName);
	}

	if(value.hpDelta > 0)
		details.push_back("total HP: " + std::to_string(value.hpDelta));

	details.push_back("footprint: 1 hex");

	if(!details.empty())
	{
		result += " (";
		for(size_t index = 0; index < details.size(); ++index)
		{
			if(index != 0)
				result += ", ";
			result += details[index];
		}
		result += ")";
	}

	return result;
}

static std::string prepareVerdantPrisonText(
	const CSpell * spell,
	const spells::effects::SpellEffectValue & value,
	const std::string & targetName,
	size_t legalRingHexCount)
{
	if(!spell)
		return {};

	auto templateText = MetaString::createFromTextID("core.genrltxt", 27);
	templateText.replaceRawString(spell->getNameTranslated());
	if(!targetName.empty())
		templateText.replaceRawString(targetName);
	std::string result = templateText.toString(&GAME->translator());
	std::vector<std::string> details;

	if(value.unitsDelta > 0)
	{
		const auto unitName = value.unitType ? value.unitType->getNamePluralTranslated() : "Dendroids";
		details.push_back("temporary " + unitName + " count: " + std::to_string(value.unitsDelta));
	}

	if(value.hpDelta > 0)
		details.push_back("aggregate HP: " + std::to_string(value.hpDelta));

	details.push_back("legal ring footprint: " + std::to_string(legalRingHexCount) + " hexes");

	if(!details.empty())
	{
		result += " (";
		for(size_t index = 0; index < details.size(); ++index)
		{
			if(index != 0)
				result += ", ";
			result += details[index];
		}
		result += ")";
	}

	return result;
}

static std::string formatPercentMillionths(int64_t percentMillionths)
{
	constexpr int64_t percentMillionthsPerPercent = 1'000'000;
	const auto wholePercent = percentMillionths / percentMillionthsPerPercent;
	const auto fractionalPercentMillionths = percentMillionths % percentMillionthsPerPercent;
	if(fractionalPercentMillionths == 0)
		return std::to_string(wholePercent) + "%";

	std::string fraction = std::to_string(fractionalPercentMillionths);
	const auto fractionWidth = std::to_string(percentMillionthsPerPercent - 1).size();
	fraction.insert(fraction.begin(), fractionWidth - fraction.size(), '0');
	while(!fraction.empty() && fraction.back() == '0')
		fraction.pop_back();
	return std::to_string(wholePercent) + "." + fraction + "%";
}

static std::string prepareHydrasVitalityText(
	const CSpell * spell,
	const std::string & targetName,
	int64_t capacityIncreasePercentMillionths,
	int32_t durationRounds)
{
	if(!spell)
		return {};

	auto templateText = MetaString::createFromTextID("core.genrltxt", 27);
	templateText.replaceRawString(spell->getNameTranslated());
	if(!targetName.empty())
		templateText.replaceRawString(targetName);
	std::string result = templateText.toString(&GAME->translator());
	result += " (maximum HP +" + formatPercentMillionths(capacityIncreasePercentMillionths) + " for "
		+ std::to_string(durationRounds) + " rounds; "
		"no immediate healing or casualty restoration; at each genuine activation start, regenerates 10% of "
		"enhanced maximum HP per surviving creature)";
	return result;
}

static BattleHex findAttackFromHex(const BattleInterface & owner, const CStack * attacker, const BattleHex & targetHex, bool allowLongWeapon)
{
	if(!attacker || !targetHex.isValid())
		return BattleHex::INVALID;

	const auto preferredDirection = owner.fieldController->selectAttackDirection(targetHex);
	BattleHex attackFromHex = owner.getBattle()->fromWhichHexAttack(attacker, targetHex, preferredDirection, allowLongWeapon);

	if(attackFromHex.isValid())
		return attackFromHex;

	for(int direction = 0; direction < 8; ++direction)
	{
		attackFromHex = owner.getBattle()->fromWhichHexAttack(attacker, targetHex, static_cast<BattleHex::EDir>(direction), allowLongWeapon);
		if(attackFromHex.isValid())
			return attackFromHex;
	}

	return BattleHex::INVALID;
}

BattleActionsController::BattleActionsController(BattleInterface & owner):
	owner(owner),
	selectedStack(nullptr),
	heroSpellToCast(nullptr)
{
}

BattleActionsController::~BattleActionsController()
{
	friendlyFireCallbackLifetime.reset();
}

bool BattleActionsController::submitHeroSpellAction(const BattleAction & action)
{
	if(!owner.curInt || !owner.curInt->cb)
		return false;

	const auto preview = buildFriendlyFirePreview(owner, action, castingSession);
	if(!preview || preview->recipients.empty())
	{
		owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), action);
		return true;
	}

	const auto snapshot = preview->snapshot;
	const auto message = friendlyFireConfirmationText(action.spell.toSpell(), preview->recipients);
	auto gate = std::make_shared<newHorizonsFriendlyFire::ConfirmationGate>(snapshot);
	std::weak_ptr<int> weakLifetime = friendlyFireCallbackLifetime;

	auto closeCapturedSession = [this, weakLifetime, expectedSession = snapshot.castingSession]()
	{
		const auto lifetime = weakLifetime.lock();
		if(!lifetime || castingSession != expectedSession)
			return;
		endCastingSpell();
	};

	owner.curInt->showYesNoDialog(message,
		[this, weakLifetime, gate, action, snapshot]()
		{
			const auto lifetime = weakLifetime.lock();
			if(!lifetime || !gate->isPending())
				return;

			if(castingSession != snapshot.castingSession || !heroSpellToCast
				|| heroSpellToCast->spell != snapshot.spellID || heroSpellToCast->side != snapshot.casterSide)
			{
				gate->cancel();
				if(castingSession == snapshot.castingSession)
					endCastingSpell();
				return;
			}

			const auto current = buildFriendlyFirePreview(owner, action, castingSession);
			if(!current || current->recipients.empty() || !gate->confirm(current->snapshot))
			{
				gate->cancel();
				if(castingSession == snapshot.castingSession)
					endCastingSpell();
				return;
			}

			if(!owner.curInt || !owner.curInt->cb)
			{
				endCastingSpell();
				return;
			}
			owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), action);
			endCastingSpell();
		},
		[this, weakLifetime, gate, closeCapturedSession]()
		{
			const auto lifetime = weakLifetime.lock();
			if(!lifetime || !gate->cancel())
				return;
			closeCapturedSession();
		});
	return false;
}

namespace
{
const char * heroOrderTargetName(HeroCommand command)
{
	switch(command)
	{
	case HeroCommand::FOCUS_FIRE: return "Focus Fire";
	case HeroCommand::PROTECT: return "Protect";
	case HeroCommand::FLANK: return "Flank";
	case HeroCommand::SECOND_WIND: return "Second Wind";
	default: return "Order";
	}
}

bool isHeroOrderPair(HeroCommand command)
{
	return command == HeroCommand::PROTECT;
}
}

bool BattleActionsController::heroOrderTargetingContextIsCurrent() const
{
	if(!selectedHeroOrderCommand || !owner.curInt || !owner.curInt->cb || !owner.getBattle()
		|| !owner.currentHero() || !owner.makingTurn() || owner.curInt->isAutoFightOn
		|| owner.isInTacticsMode() || heroSpellcastingModeActive())
		return false;

	const auto side = owner.getBattle()->battleGetMySide();
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
}

std::vector<uint32_t> BattleActionsController::heroOrderTargetIds() const
{
	if(!heroOrderTargetingContextIsCurrent())
		return {};

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto candidates = owner.getBattle()->battleGetHeroCommandTargets(side, command);
	if(!isHeroOrderPair(command))
		return candidates;

	std::vector<uint32_t> result;
	if(!heroOrderTargetingFirst)
	{
		for(const auto firstId : candidates)
		{
			const auto hasWard = std::ranges::any_of(candidates, [this, side, command, firstId](const auto secondId)
			{
				return firstId != secondId && owner.getBattle()->battlePrepareHeroOrderState(
					side, command, {firstId, secondId}).has_value();
			});
			if(hasWard)
				result.push_back(firstId);
		}
		return result;
	}

	for(const auto id : candidates)
	{
		if(id != *heroOrderTargetingFirst && heroOrderTargetIdIsLegal(id))
			result.push_back(id);
	}
	return result;
}

bool BattleActionsController::heroOrderTargetIdIsLegal(uint32_t unitId) const
{
	if(!heroOrderTargetingContextIsCurrent())
		return false;

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto * target = owner.getBattle()->battleGetUnitByID(unitId);
	if(!target || !target->alive() || target->isGhost())
		return false;

	if(isHeroOrderPair(command))
	{
		if(!heroOrderTargetingFirst || *heroOrderTargetingFirst == unitId)
		{
			const auto candidates = heroOrderTargetIds();
			return !heroOrderTargetingFirst && std::ranges::find(candidates, unitId) != candidates.end();
		}
		return owner.getBattle()->battlePrepareHeroOrderState(side, command,
			{*heroOrderTargetingFirst, unitId}).has_value();
	}

	if(command == HeroCommand::FOCUS_FIRE
		&& !heroCommands::isCanonicalRules(owner.getBattle()->getBattle()->getHeroCommandRules()))
		return owner.getBattle()->battlePrepareFocusFireState(side, unitId).has_value();

	return owner.getBattle()->battlePrepareHeroOrderState(side, command, {unitId}).has_value();
}

bool BattleActionsController::heroOrderTargetingHexIsLegal(const BattleHex & hex) const
{
	if(!hex.isValid())
		return false;
	const auto * stack = owner.getBattle()->battleGetStackByPos(hex, true);
	const auto targetIds = heroOrderTargetIds();
	return stack && std::ranges::find(targetIds, stack->unitId()) != targetIds.end();
}

BattleHexArray BattleActionsController::getHeroOrderTargetingLegalHexes() const
{
	BattleHexArray result;
	for(const auto id : heroOrderTargetIds())
	{
		const auto * stack = owner.getBattle()->battleGetStackByID(id, true);
		if(!stack)
			continue;
		result.insert(stack->getPosition());
		if(stack->doubleWide())
			result.insert(stack->occupiedHex());
	}
	return result;
}

BattleHexArray BattleActionsController::getHeroOrderTargetingSelectedHexes() const
{
	BattleHexArray result;
	if(!heroOrderTargetingFirst || !owner.getBattle())
		return result;

	const auto * stack = owner.getBattle()->battleGetStackByID(*heroOrderTargetingFirst, true);
	if(!stack)
		return result;
	result.insert(stack->getPosition());
	if(stack->doubleWide())
		result.insert(stack->occupiedHex());
	return result;
}

void BattleActionsController::updateHeroOrderTargetingStatus(const BattleHex & hoveredHex)
{
	if(!selectedHeroOrderCommand)
		return;

	if(!heroOrderTargetingContextIsCurrent())
	{
		cancelHeroOrderTargeting();
		return;
	}

	std::string message = std::string(heroOrderTargetName(*selectedHeroOrderCommand)) + ": ";
	if(isHeroOrderPair(*selectedHeroOrderCommand) && heroOrderTargetingFirst)
		message += "select an adjacent Ward on the battlefield.";
	else
		message += "select a legal stack on the battlefield.";

	if(hoveredHex.isValid())
	{
		const auto * stack = owner.getBattle()->battleGetStackByPos(hoveredHex, true);
		if(stack && heroOrderTargetIdIsLegal(stack->unitId()))
			message += " Click to confirm.";
		else
			message += " This stack is not a legal target.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

bool BattleActionsController::beginHeroOrderTargeting(HeroCommand command)
{
	invalidateChainLightningPreview();
	cancelHeroOrderTargeting();
	if(command != HeroCommand::FOCUS_FIRE && command != HeroCommand::PROTECT
		&& command != HeroCommand::FLANK && command != HeroCommand::SECOND_WIND)
		return false;

	selectedHeroOrderCommand = command;
	if(!heroOrderTargetingContextIsCurrent()
		|| !owner.getBattle()->battleCanBeginHeroCommand(owner.getBattle()->battleGetMySide(), command))
	{
		cancelHeroOrderTargeting();
		return false;
	}

	ENGINE->fakeMouseMove();
	updateHeroOrderTargetingStatus(BattleHex::INVALID);
	ENGINE->windows().totalRedraw();
	return true;
}

bool BattleActionsController::heroOrderTargetingModeActive() const
{
	return selectedHeroOrderCommand.has_value();
}

HeroCommand BattleActionsController::heroOrderTargetingCommand() const
{
	return selectedHeroOrderCommand.value_or(HeroCommand::NONE);
}

bool BattleActionsController::heroOrderTargetingFirstSelected() const
{
	return heroOrderTargetingFirst.has_value();
}

void BattleActionsController::cancelHeroOrderTargeting()
{
	if(!selectedHeroOrderCommand)
		return;
	selectedHeroOrderCommand.reset();
	heroOrderTargetingFirst.reset();
	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	currentConsoleMsg.clear();
	ENGINE->cursor().set(Cursor::Combat::POINTER);
	ENGINE->windows().totalRedraw();
}

void BattleActionsController::selectHeroOrderTarget(const BattleHex & clickedHex)
{
	if(!heroOrderTargetingModeActive())
		return;

	if(!heroOrderTargetingContextIsCurrent())
	{
		cancelHeroOrderTargeting();
		return;
	}

	const auto * target = owner.getBattle()->battleGetStackByPos(clickedHex, true);
	if(!target || !heroOrderTargetIdIsLegal(target->unitId()))
	{
		updateHeroOrderTargetingStatus(clickedHex);
		return;
	}

	if(isHeroOrderPair(*selectedHeroOrderCommand) && !heroOrderTargetingFirst)
	{
		heroOrderTargetingFirst = target->unitId();
		updateHeroOrderTargetingStatus(clickedHex);
		ENGINE->windows().totalRedraw();
		return;
	}

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto first = heroOrderTargetingFirst;
	const auto prepared = command == HeroCommand::FOCUS_FIRE
		&& !heroCommands::isCanonicalRules(owner.getBattle()->getBattle()->getHeroCommandRules())
		? owner.getBattle()->battlePrepareFocusFireState(side, target->unitId()).has_value()
		: owner.getBattle()->battlePrepareHeroOrderState(side, command,
			isHeroOrderPair(command) ? std::vector<uint32_t>{*first, target->unitId()}
				: std::vector<uint32_t>{target->unitId()}).has_value();
	if(!prepared || (isHeroOrderPair(command) && !first))
	{
		updateHeroOrderTargetingStatus(clickedHex);
		return;
	}

	const auto battleID = owner.getBattleID();
	auto playerCallback = owner.curInt->cb;
	const auto action = isHeroOrderPair(command)
		? BattleAction::makePairedHeroCommand(side, command, *first, target->unitId())
		: BattleAction::makeTargetedHeroCommand(side, command, target->unitId());
	cancelHeroOrderTargeting();
	playerCallback->battleMakeSpellAction(battleID, action);
}

bool BattleActionsController::landMinePlacementModeActive() const
{
	if(!heroSpellToCast || !owner.getBattle() || !owner.currentHero())
		return false;

	return isCanonicalLandMine(*owner.getBattle(), heroSpellToCast->spell.toSpell());
}

bool BattleActionsController::quicksandPlacementModeActive() const
{
	if(!heroSpellToCast || !owner.getBattle() || !owner.getBattle()->getBattle() || !owner.currentHero())
		return false;

	return newHorizonsMagic::quicksandSelectedPlacementEnabled(
		owner.getBattle()->getBattle()->getMagicRules(), heroSpellToCast->spell);
}

bool BattleActionsController::repeatedPlacementModeActive() const
{
	return landMinePlacementModeActive() || quicksandPlacementModeActive();
}

int BattleActionsController::repeatedPlacementRequiredHexes() const
{
	if(!repeatedPlacementModeActive())
		return 0;

	const auto * spell = heroSpellToCast->spell.toSpell();
	const auto hero = owner.currentHero();
	spells::BattleCast cast(owner.getBattle().get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return 0;

	if(quicksandPlacementModeActive())
		return mechanics->getNewHorizonsQuicksandPatchCount();

	return mechanics->getNewHorizonsLandMinePatchCount();
}

bool BattleActionsController::repeatedPlacementReady() const
{
	const int required = repeatedPlacementRequiredHexes();
	return required > 0 && static_cast<int>(repeatedPlacementSelectedHexes.size()) == required;
}

const std::vector<BattleHex> & BattleActionsController::getRepeatedPlacementSelectedHexes() const
{
	return repeatedPlacementSelectedHexes;
}

bool BattleActionsController::repeatedPlacementHexIsLegal(const BattleHex & hex) const
{
	if(!repeatedPlacementModeActive() || !owner.getBattle())
		return false;

	if(quicksandPlacementModeActive())
		return newHorizonsMagic::quicksandPlacementHexIsLegal(*owner.getBattle(), hex);

	return canonicalLandMineHexIsEmpty(*owner.getBattle(), hex);
}

bool BattleActionsController::repeatedPlacementHexIsSelected(const BattleHex & hex) const
{
	return std::find(repeatedPlacementSelectedHexes.begin(), repeatedPlacementSelectedHexes.end(), hex)
		!= repeatedPlacementSelectedHexes.end();
}

BattleHexArray BattleActionsController::getRepeatedPlacementLegalHexes() const
{
	BattleHexArray result;
	if(!repeatedPlacementModeActive())
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(repeatedPlacementHexIsLegal(hex))
			result.insert(hex);
	}
	return result;
}

bool BattleActionsController::stormOfDaggersTargetSelectionModeActive() const
{
	return heroSpellToCast && isStormOfDaggersSpell(heroSpellToCast->spell.toSpell());
}

const std::vector<uint32_t> & BattleActionsController::stormOfDaggersSelectedTargetIds() const
{
	return stormOfDaggersSelectedUnitIds;
}

int BattleActionsController::stormOfDaggersSelectionOrder(uint32_t unitId) const
{
	const auto found = std::ranges::find(stormOfDaggersSelectedUnitIds, unitId);
	return found == stormOfDaggersSelectedUnitIds.end()
		? 0 : static_cast<int>(std::distance(stormOfDaggersSelectedUnitIds.begin(), found)) + 1;
}

bool BattleActionsController::stormOfDaggersSelectionContextIsCurrent() const
{
	if(!stormOfDaggersTargetSelectionModeActive() || !owner.curInt || !owner.curInt->cb
		|| CPlayerInterface::battleInt.get() != &owner || owner.getBattleID() != stormOfDaggersBattleID
		|| !stormOfDaggersPlayer || owner.curInt->cb->getPlayerID() != *stormOfDaggersPlayer
		|| !owner.getBattle() || !owner.getBattle()->getBattle()
		|| owner.getBattle()->battleGetMySide() != stormOfDaggersSide
		|| owner.getBattle()->battleGetRound() != stormOfDaggersRound
		|| !owner.makingTurn() || owner.curInt->isAutoFightOn || owner.isInTacticsMode())
		return false;

	const auto * hero = owner.currentHero();
	return hero && hero->id == stormOfDaggersHeroID;
}

bool BattleActionsController::stormOfDaggersTargetsAreLegal(const std::vector<uint32_t> & unitIds) const
{
	if(unitIds.empty() || unitIds.size() > stormOfDaggersMaximumTargets
		|| !stormOfDaggersSelectionContextIsCurrent())
		return false;

	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!battle || !battle->getBattle() || !hero || !isStormOfDaggersSpell(spell)
		|| stormOfDaggersSide == BattleSide::NONE)
		return false;

	std::set<uint32_t> distinct;
	spells::Target target;
	const auto enemySide = battle->otherSide(stormOfDaggersSide);
	for(const auto unitId : unitIds)
	{
		if(!distinct.insert(unitId).second)
			return false;

		const auto * unit = battle->battleGetUnitByID(unitId);
		if(!unit || !unit->alive() || unit->unitSide() != enemySide)
			return false;
		target.emplace_back(unit, unit->getPosition());
	}

	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics || !mechanics->isNewHorizonsStormOfDaggers()
		|| !mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(unitIds.size())))
		return false;

	spells::detail::ProblemImpl problem;
	return mechanics->canBeCast(problem) && mechanics->canBeCastAt(target, problem);
}

bool BattleActionsController::stormOfDaggersTargetIsLegal(uint32_t unitId) const
{
	if(stormOfDaggersSelectedUnitIds.size() >= stormOfDaggersMaximumTargets
		|| std::ranges::find(stormOfDaggersSelectedUnitIds, unitId) != stormOfDaggersSelectedUnitIds.end())
		return false;

	auto candidate = stormOfDaggersSelectedUnitIds;
	candidate.push_back(unitId);
	return stormOfDaggersTargetsAreLegal(candidate);
}

bool BattleActionsController::stormOfDaggersTargetHexIsLegal(const BattleHex & hex) const
{
	if(!hex.isValid() || !stormOfDaggersSelectionContextIsCurrent())
		return false;
	const auto battle = owner.getBattle();
	const CStack * target = battle ? battle->battleGetStackByPos(hex, true) : nullptr;
	if(!target && battle)
		target = battle->battleGetStackByPos(hex, false);
	return target && stormOfDaggersTargetIsLegal(target->unitId());
}

StormOfDaggersSelectionPreview BattleActionsController::getStormOfDaggersSelectionPreview() const
{
	StormOfDaggersSelectionPreview result;
	result.selectedTargetCount = static_cast<int32_t>(stormOfDaggersSelectedUnitIds.size());
	result.maximumTargetCount = stormOfDaggersMaximumTargets;
	if(!stormOfDaggersTargetSelectionModeActive())
		return result;

	if(!stormOfDaggersSelectionContextIsCurrent())
	{
		result.status = "Battle context changed. Cancel this spell and reopen it.";
		return result;
	}

	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!battle || !battle->getBattle() || !hero || !isStormOfDaggersSpell(spell))
	{
		result.status = "Battle context changed. Cancel this spell and reopen it.";
		return result;
	}

	std::vector<const battle::Unit *> selectedUnits;
	spells::Target fullAim;
	bool allSelectedUnitsPresent = true;
	for(const auto unitId : stormOfDaggersSelectedUnitIds)
	{
		const auto * unit = battle->battleGetUnitByID(unitId);
		if(!unit)
		{
			allSelectedUnitsPresent = false;
			continue;
		}
		selectedUnits.push_back(unit);
		fullAim.emplace_back(unit, unit->getPosition());

		StormOfDaggersTargetPreview targetPreview;
		targetPreview.unitId = unitId;
		targetPreview.name = unit->unitType()->getNamePluralTranslated();
		result.targets.push_back(std::move(targetPreview));
	}

	result.canConfirm = stormOfDaggersTargetsAreLegal(stormOfDaggersSelectedUnitIds);
	if(allSelectedUnitsPresent && !selectedUnits.empty() && result.canConfirm)
	{
		const int32_t targetCount = static_cast<int32_t>(selectedUnits.size());
		spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
		cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
		auto mechanics = spell->battleMechanics(&cast);
		if(mechanics && mechanics->isNewHorizonsStormOfDaggers()
			&& mechanics->setStormOfDaggersTargetCount(targetCount))
		{
			result.totalDamagePool = mechanics->getStormOfDaggersTotalDamage(targetCount);
			result.rawDamagePerTarget = mechanics->getStormOfDaggersDamagePerTarget(targetCount);
			result.poolAvailable = true;

			const auto canonicalTarget = mechanics->canonicalizeTarget(fullAim);
			mechanics->forEachEffect([&](const spells::effects::Effect & effect)
			{
				if(effect.name != "directDamage")
					return false;

				const auto effectTarget = effect.transformTarget(mechanics.get(), fullAim, canonicalTarget);
				for(const auto & destination : effectTarget)
				{
					if(!destination.unitValue)
						continue;

					auto targetPreview = std::ranges::find(result.targets, destination.unitValue->unitId(),
						&StormOfDaggersTargetPreview::unitId);
					if(targetPreview == result.targets.end())
						continue;

					spells::Target oneTarget;
					oneTarget.emplace_back(destination);
					const auto value = effect.getHealthChange(mechanics.get(), oneTarget);
					targetPreview->projectedDamage = std::max<int64_t>(0, -value.hpDelta);
				}
				return true;
			});
		}
	}

	if(stormOfDaggersSelectedUnitIds.empty())
		result.status = "Click one to five distinct enemy stacks. Backspace undoes; Esc cancels.";
	else if(result.canConfirm)
		result.status = "Selection is ready. Confirm to cast or add another enemy stack.";
	else
		result.status = "A selected stack is no longer legal. Undo or cancel.";
	return result;
}

void BattleActionsController::updateStormOfDaggersSelectionStatus(const BattleHex & hoveredHex)
{
	if(!stormOfDaggersTargetSelectionModeActive())
		return;

	const auto preview = getStormOfDaggersSelectionPreview();
	std::string message = "Storm of Daggers: " + std::to_string(preview.selectedTargetCount)
		+ "/" + std::to_string(preview.maximumTargetCount) + " targets";
	if(preview.poolAvailable)
		message += ", pool " + std::to_string(preview.totalDamagePool)
			+ " (" + std::to_string(preview.rawDamagePerTarget) + " each before resistance)";

	if(!preview.status.empty())
		message += ". " + preview.status;
	if(hoveredHex.isValid())
	{
		const auto * target = getStackForHex(hoveredHex);
		if(target && stormOfDaggersSelectionOrder(target->unitId()) != 0)
			message += " Already selected; use Undo to remove the last target.";
		else if(stormOfDaggersTargetHexIsLegal(hoveredHex))
			message += " Click to select this enemy stack.";
		else if(target && owner.getBattle() && target->unitSide() == owner.getBattle()->battleGetMySide())
			message += " Friendly stacks cannot be targeted.";
		else if(preview.selectedTargetCount >= preview.maximumTargetCount)
			message += " Maximum target count reached.";
		else
			message += " Select a living enemy stack.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectStormOfDaggersTarget(const BattleHex & clickedHex)
{
	if(!stormOfDaggersTargetSelectionModeActive())
		return;
	if(!stormOfDaggersSelectionContextIsCurrent())
	{
		updateStormOfDaggersSelectionStatus(clickedHex);
		return;
	}

	const auto * target = getStackForHex(clickedHex);
	if(target && stormOfDaggersTargetIsLegal(target->unitId()))
		stormOfDaggersSelectedUnitIds.push_back(target->unitId());

	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateStormOfDaggersSelectionStatus(clickedHex);
	ENGINE->windows().totalRedraw();
}

void BattleActionsController::confirmStormOfDaggersTargets()
{
	if(!stormOfDaggersTargetSelectionModeActive())
		return;

	if(!stormOfDaggersTargetsAreLegal(stormOfDaggersSelectedUnitIds))
	{
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateStormOfDaggersSelectionStatus(BattleHex::INVALID);
		return;
	}

	BattleAction action = *heroSpellToCast;
	action.target.clear();
	for(const auto unitId : stormOfDaggersSelectedUnitIds)
	{
		const auto * target = owner.getBattle()->battleGetUnitByID(unitId);
		if(!target)
		{
			updateStormOfDaggersSelectionStatus(BattleHex::INVALID);
			return;
		}
		action.aimToUnit(target);
	}

	if(submitHeroSpellAction(action))
		endCastingSpell();
}

void BattleActionsController::undoStormOfDaggersTarget()
{
	if(!stormOfDaggersTargetSelectionModeActive() || stormOfDaggersSelectedUnitIds.empty())
		return;

	stormOfDaggersSelectedUnitIds.pop_back();
	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateStormOfDaggersSelectionStatus(BattleHex::INVALID);
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::soulChainTargetSelectionModeActive() const
{
	return heroSpellToCast && isSoulChainSpell(heroSpellToCast->spell.toSpell());
}

const std::vector<uint32_t> & BattleActionsController::soulChainSelectedTargetIds() const
{
	return soulChainSelectedUnitIds;
}

int BattleActionsController::soulChainSelectionOrder(uint32_t unitId) const
{
	const auto found = std::ranges::find(soulChainSelectedUnitIds, unitId);
	return found == soulChainSelectedUnitIds.end()
		? 0 : static_cast<int>(std::distance(soulChainSelectedUnitIds.begin(), found)) + 1;
}

bool BattleActionsController::soulChainSelectionContextIsCurrent() const
{
	if(!soulChainTargetSelectionModeActive() || !owner.curInt || !owner.curInt->cb
		|| CPlayerInterface::battleInt.get() != &owner || owner.getBattleID() != soulChainBattleID
		|| !soulChainPlayer || owner.curInt->cb->getPlayerID() != *soulChainPlayer
		|| !owner.getBattle() || !owner.getBattle()->getBattle()
		|| owner.getBattle()->battleGetMySide() != soulChainSide
		|| owner.getBattle()->battleGetRound() != soulChainRound
		|| !owner.makingTurn() || owner.curInt->isAutoFightOn || owner.isInTacticsMode())
		return false;

	const auto * hero = owner.currentHero();
	return hero && hero->id == soulChainHeroID;
}

bool BattleActionsController::soulChainTargetsAreLegal(const std::vector<uint32_t> & unitIds) const
{
	if(unitIds.empty() || unitIds.size() > newHorizonsSoulChain::MAX_TARGETS || !soulChainSelectionContextIsCurrent())
		return false;

	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!battle || !battle->getBattle() || !hero || !isSoulChainSpell(spell) || soulChainSide == BattleSide::NONE)
		return false;

	std::set<uint32_t> distinct;
	spells::Target target;
	for(const auto unitId : unitIds)
	{
		if(!distinct.insert(unitId).second)
			return false;

		const auto * unit = battle->battleGetUnitByID(unitId);
		if(!unit)
			return false;
		target.emplace_back(unit, unit->getPosition());
	}
	if(!newHorizonsSoulChain::validEnemyTargetSet(*battle, soulChainSide, target))
		return false;

	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	return mechanics->canBeCast(problem) && mechanics->canBeCastAt(target, problem);
}

bool BattleActionsController::soulChainTargetIsLegal(uint32_t unitId) const
{
	if(soulChainSelectedUnitIds.size() >= newHorizonsSoulChain::MAX_TARGETS
		|| std::ranges::find(soulChainSelectedUnitIds, unitId) != soulChainSelectedUnitIds.end())
		return false;

	auto candidate = soulChainSelectedUnitIds;
	candidate.push_back(unitId);
	return soulChainTargetsAreLegal(candidate);
}

bool BattleActionsController::soulChainTargetHexIsLegal(const BattleHex & hex) const
{
	if(!hex.isValid() || !soulChainSelectionContextIsCurrent())
		return false;

	const auto battle = owner.getBattle();
	const CStack * target = battle ? battle->battleGetStackByPos(hex, true) : nullptr;
	if(!target && battle)
		target = battle->battleGetStackByPos(hex, false);
	return target && soulChainTargetIsLegal(target->unitId());
}

SoulChainSelectionPreview BattleActionsController::getSoulChainSelectionPreview() const
{
	SoulChainSelectionPreview result;
	result.selectedTargetCount = static_cast<int32_t>(soulChainSelectedUnitIds.size());
	result.maximumTargetCount = newHorizonsSoulChain::MAX_TARGETS;
	if(!soulChainTargetSelectionModeActive())
		return result;

	if(!soulChainSelectionContextIsCurrent())
	{
		result.status = "Battle context changed. Cancel this spell and reopen it.";
		return result;
	}

	const auto battle = owner.getBattle();
	for(const auto unitId : soulChainSelectedUnitIds)
	{
		const auto * unit = battle ? battle->battleGetUnitByID(unitId) : nullptr;
		if(!unit)
			continue;
		result.targets.push_back({unitId, unit->unitType()->getNamePluralTranslated()});
	}

	result.canConfirm = soulChainTargetsAreLegal(soulChainSelectedUnitIds);
	if(soulChainSelectedUnitIds.empty())
		result.status = "Select one primary enemy. You may add up to two secondary enemies; Confirm after the primary to cast. Backspace undoes; Esc cancels.";
	else if(result.canConfirm && result.selectedTargetCount == 1)
		result.status = "Primary selected. Add up to two distinct secondary enemies, or Confirm to cast now. Backspace undoes; Esc cancels.";
	else if(result.canConfirm && result.selectedTargetCount < result.maximumTargetCount)
		result.status = "Primary and secondary targets are ready. Add another enemy, or Confirm. Backspace undoes; Esc cancels.";
	else if(result.canConfirm)
		result.status = "Primary and two secondary targets selected. Confirm to cast, or Undo to revise.";
	else
		result.status = "A selected stack is no longer legal. Undo or cancel.";
	return result;
}

void BattleActionsController::updateSoulChainSelectionStatus(const BattleHex & hoveredHex)
{
	if(!soulChainTargetSelectionModeActive())
		return;

	const auto preview = getSoulChainSelectionPreview();
	std::string message = "Soul Chain: ";
	if(preview.selectedTargetCount == 0)
		message += "select one primary enemy";
	else
		message += std::to_string(preview.selectedTargetCount) + "/" + std::to_string(preview.maximumTargetCount)
			+ " selected; #1 is the primary, later targets are secondary";
	if(!preview.status.empty())
		message += ". " + preview.status;

	if(hoveredHex.isValid())
	{
		const auto * target = getStackForHex(hoveredHex);
		if(target && soulChainSelectionOrder(target->unitId()) != 0)
			message += " Already selected as #" + std::to_string(soulChainSelectionOrder(target->unitId()))
				+ "; use Undo to remove the last target.";
		else if(soulChainTargetHexIsLegal(hoveredHex))
			message += preview.selectedTargetCount == 0
				? " Click to select this primary enemy."
				: " Click to add this secondary enemy.";
		else if(target && owner.getBattle() && target->unitSide() == owner.getBattle()->battleGetMySide())
			message += " Friendly stacks cannot be selected.";
		else if(preview.selectedTargetCount >= preview.maximumTargetCount)
			message += " Maximum target count reached.";
		else
			message += " Select a living enemy stack.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectSoulChainTarget(const BattleHex & clickedHex)
{
	if(!soulChainTargetSelectionModeActive())
		return;
	if(!soulChainSelectionContextIsCurrent())
	{
		updateSoulChainSelectionStatus(clickedHex);
		return;
	}

	const auto * target = getStackForHex(clickedHex);
	if(target && soulChainTargetIsLegal(target->unitId()))
		soulChainSelectedUnitIds.push_back(target->unitId());

	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateSoulChainSelectionStatus(clickedHex);
	ENGINE->windows().totalRedraw();
}

void BattleActionsController::confirmSoulChainTargets()
{
	if(!soulChainTargetSelectionModeActive())
		return;

	if(!soulChainTargetsAreLegal(soulChainSelectedUnitIds))
	{
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateSoulChainSelectionStatus(BattleHex::INVALID);
		return;
	}

	BattleAction action = *heroSpellToCast;
	action.target.clear();
	for(const auto unitId : soulChainSelectedUnitIds)
	{
		const auto * target = owner.getBattle()->battleGetUnitByID(unitId);
		if(!target)
		{
			updateSoulChainSelectionStatus(BattleHex::INVALID);
			return;
		}
		action.aimToUnit(target);
	}

	if(submitHeroSpellAction(action))
		endCastingSpell();
}

void BattleActionsController::undoSoulChainTarget()
{
	if(!soulChainTargetSelectionModeActive() || soulChainSelectedUnitIds.empty())
		return;

	soulChainSelectedUnitIds.pop_back();
	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateSoulChainSelectionStatus(BattleHex::INVALID);
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::lifeDrainTargetSelectionModeActive() const
{
	return heroSpellToCast && isLifeDrainSpell(heroSpellToCast->spell.toSpell());
}

bool BattleActionsController::lifeDrainSelectionContextIsCurrent() const
{
	if(!lifeDrainTargetSelectionModeActive() || !owner.curInt || !owner.curInt->cb
		|| CPlayerInterface::battleInt.get() != &owner || owner.getBattleID() != lifeDrainBattleID
		|| !lifeDrainPlayer || owner.curInt->cb->getPlayerID() != *lifeDrainPlayer
		|| !owner.getBattle() || !owner.getBattle()->getBattle()
		|| owner.getBattle()->battleGetMySide() != lifeDrainSide
		|| owner.getBattle()->battleGetRound() != lifeDrainRound
		|| !owner.makingTurn() || owner.curInt->isAutoFightOn || owner.isInTacticsMode())
		return false;

	const auto * hero = owner.currentHero();
	return hero && hero->id == lifeDrainHeroID;
}

bool BattleActionsController::lifeDrainTargetsAreLegal(const std::vector<uint32_t> & unitIds) const
{
	if(unitIds.empty() || unitIds.size() > 2 || !lifeDrainSelectionContextIsCurrent())
		return false;

	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!battle || !battle->getBattle() || !hero || !isLifeDrainSpell(spell)
		|| lifeDrainSide == BattleSide::NONE)
		return false;

	const auto * enemy = battle->battleGetUnitByID(unitIds.front());
	if(!enemy || !enemy->alive() || !enemy->isValidTarget(false) || enemy->isInvincible()
		|| enemy->unitSide() != battle->otherSide(lifeDrainSide))
		return false;

	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	if(!mechanics->canBeCast(problem))
		return false;

	// The server requires an exact pair, so do not send a one-target prefix
	// through canBeCastAt. Locally validate the first hostile target only; the
	// complete pair goes through the ordinary mechanics legality check below.
	if(unitIds.size() == 1)
		return mechanics->isReceptive(enemy);

	const auto * friendly = battle->battleGetUnitByID(unitIds[1]);
	if(!friendly || !friendly->alive() || !friendly->isValidTarget(false)
		|| friendly->unitSide() != lifeDrainSide)
		return false;

	spells::Target pair;
	pair.emplace_back(enemy, enemy->getPosition());
	pair.emplace_back(friendly, friendly->getPosition());
	return mechanics->canBeCastAt(pair, problem);
}

bool BattleActionsController::lifeDrainTargetIsLegal(uint32_t unitId) const
{
	if(lifeDrainSelectedUnitIds.size() >= 2
		|| std::ranges::find(lifeDrainSelectedUnitIds, unitId) != lifeDrainSelectedUnitIds.end())
		return false;

	auto candidate = lifeDrainSelectedUnitIds;
	candidate.push_back(unitId);
	return lifeDrainTargetsAreLegal(candidate);
}

bool BattleActionsController::lifeDrainTargetHexIsLegal(const BattleHex & hex) const
{
	if(!hex.isValid() || !lifeDrainSelectionContextIsCurrent())
		return false;
	const auto * target = owner.getBattle()->battleGetUnitByPos(hex, true);
	return target && lifeDrainTargetIsLegal(target->unitId());
}

void BattleActionsController::updateLifeDrainSelectionStatus(const BattleHex & hoveredHex)
{
	if(!lifeDrainTargetSelectionModeActive())
		return;

	std::string message = "Life Drain: ";
	if(!lifeDrainSelectionContextIsCurrent())
		message += "battle context changed. Cancel and reopen the spell.";
	else if(lifeDrainSelectedUnitIds.empty())
		message += "choose a living enemy stack first. Esc cancels.";
	else
	{
		const auto battle = owner.getBattle();
		const auto * enemy = battle ? battle->battleGetUnitByID(lifeDrainSelectedUnitIds.front()) : nullptr;
		if(!enemy || !enemy->alive())
			message += "the selected enemy is no longer available. Esc cancels.";
		else
			message += "enemy selected. Choose a living friendly stack; any creature type can receive healing. Esc cancels.";
	}

	if(hoveredHex.isValid() && lifeDrainSelectionContextIsCurrent())
	{
		const auto * target = getStackForHex(hoveredHex);
		if(!target)
			message += " Hover a living stack.";
		else if(std::ranges::find(lifeDrainSelectedUnitIds, target->unitId()) != lifeDrainSelectedUnitIds.end())
			message += " Enemy already selected; choose a friendly stack.";
		else if(lifeDrainTargetHexIsLegal(hoveredHex))
			message += lifeDrainSelectedUnitIds.empty()
				? " Click to select this enemy stack."
				: " Click to complete the enemy/friendly pair and cast.";
		else if(!target->alive() || !target->isValidTarget(false))
			message += " Dead or invalid stacks cannot be selected.";
		else
		{
			const auto battle = owner.getBattle();
			const auto expectedSide = lifeDrainSelectedUnitIds.empty()
				? battle->otherSide(lifeDrainSide)
				: lifeDrainSide;
			if(target->unitSide() != expectedSide)
				message += lifeDrainSelectedUnitIds.empty()
					? " The first target must be an enemy stack."
					: " The second target must be a friendly stack.";
			else
				message += lifeDrainSelectedUnitIds.empty()
					? " This enemy cannot be targeted by Life Drain."
					: " This friendly stack cannot receive Life Drain healing.";
		}
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectLifeDrainTarget(const BattleHex & clickedHex)
{
	if(!lifeDrainTargetSelectionModeActive())
		return;
	if(!lifeDrainSelectionContextIsCurrent())
	{
		updateLifeDrainSelectionStatus(clickedHex);
		return;
	}

	const auto * target = getStackForHex(clickedHex);
	if(!target || !lifeDrainTargetHexIsLegal(clickedHex))
	{
		updateLifeDrainSelectionStatus(clickedHex);
		return;
	}

	if(lifeDrainSelectedUnitIds.empty())
	{
		lifeDrainSelectedUnitIds.push_back(target->unitId());
		updateLifeDrainSelectionStatus(clickedHex);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	auto selected = lifeDrainSelectedUnitIds;
	selected.push_back(target->unitId());
	if(!lifeDrainTargetsAreLegal(selected))
	{
		updateLifeDrainSelectionStatus(clickedHex);
		return;
	}

	BattleAction action = *heroSpellToCast;
	action.target.clear();
	for(const auto unitId : selected)
	{
		const auto * chosen = owner.getBattle()->battleGetUnitByID(unitId);
		if(!chosen)
		{
			updateLifeDrainSelectionStatus(BattleHex::INVALID);
			return;
		}
		action.aimToUnit(chosen);
	}

	if(!owner.curInt || !owner.curInt->cb)
		return;
	if(submitHeroSpellAction(action))
		endCastingSpell();
}

bool BattleActionsController::fireWallPlacementModeActive() const
{
	if(!heroSpellToCast || !owner.getBattle() || !owner.currentHero())
		return false;

	return isCanonicalFireWall(*owner.getBattle(), heroSpellToCast->spell.toSpell());
}

bool BattleActionsController::fireWallPlacementStartSelected() const
{
	return fireWallPlacementModeActive() && fireWallSelectedStart.isValid();
}

BattleHex BattleActionsController::fireWallPlacementStart() const
{
	return fireWallSelectedStart;
}

bool BattleActionsController::fireWallPlacementLineIsLegal(const BattleHex & start, BattleHex::EDir direction) const
{
	if(!fireWallPlacementModeActive()
		|| direction < BattleHex::TOP_LEFT || direction > BattleHex::LEFT
		|| !owner.getBattle() || !owner.currentHero())
		return false;

	BattleHex current = start;
	const auto & battle = *owner.getBattle();
	for(int index = 0; index < 3; ++index)
	{
		if(!canonicalFireWallHexIsEmpty(battle, current))
			return false;
		if(index != 2)
			current = current.cloneInDirection(direction, false);
	}

	const auto * spell = heroSpellToCast->spell.toSpell();
	spells::BattleCast cast(owner.getBattle().get(), owner.currentHero(), spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	if(!mechanics->canBeCast(problem))
		return false;

	battle::Target line;
	current = start;
	for(int index = 0; index < 3; ++index)
	{
		line.emplace_back(current);
		if(index != 2)
			current = current.cloneInDirection(direction, false);
	}
	return mechanics->canBeCastAt(line, problem);
}

bool BattleActionsController::fireWallPlacementStartIsLegal(const BattleHex & hex) const
{
	if(!fireWallPlacementModeActive() || !hex.isValid())
		return false;

	for(const auto direction : BattleHex::hexagonalDirections())
		if(fireWallPlacementLineIsLegal(hex, direction))
			return true;
	return false;
}

bool BattleActionsController::fireWallPlacementEndpointIsLegal(const BattleHex & hex) const
{
	if(!fireWallPlacementStartSelected() || !hex.isValid())
		return false;

	const auto direction = fireWallDirectionBetween(fireWallSelectedStart, hex);
	return direction != BattleHex::NONE && fireWallPlacementLineIsLegal(fireWallSelectedStart, direction);
}

BattleHexArray BattleActionsController::getFireWallPlacementLegalStartHexes() const
{
	BattleHexArray result;
	if(!fireWallPlacementModeActive() || fireWallPlacementStartSelected())
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(fireWallPlacementStartIsLegal(hex))
			result.insert(hex);
	}
	return result;
}

BattleHexArray BattleActionsController::getFireWallPlacementLegalEndpoints() const
{
	BattleHexArray result;
	if(!fireWallPlacementStartSelected())
		return result;

	for(const auto direction : BattleHex::hexagonalDirections())
	{
		if(!fireWallPlacementLineIsLegal(fireWallSelectedStart, direction))
			continue;
		result.insert(fireWallSelectedStart.cloneInDirection(direction, false));
	}
	return result;
}

void BattleActionsController::updateFireWallPlacementStatus(const BattleHex & hoveredHex)
{
	if(!fireWallPlacementModeActive())
		return;

	std::string message;
	if(!fireWallPlacementStartSelected())
	{
		message = "Fire Wall: select an empty start hex.";
		if(hoveredHex.isValid() && !fireWallPlacementStartIsLegal(hoveredHex))
			message += " This hex cannot fit a three-hex line.";
	}
	else
	{
		message = "Fire Wall: select an adjacent endpoint for the line from hex "
			+ std::to_string(fireWallSelectedStart.toInt()) + ".";
		if(hoveredHex.isValid() && fireWallPlacementEndpointIsLegal(hoveredHex))
			message += " Click to cast.";
		else if(hoveredHex.isValid())
			message += " Choose a legal adjacent direction.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectFireWallStartOrDirection(const BattleHex & clickedHex)
{
	if(!fireWallPlacementModeActive())
		return;

	if(!fireWallPlacementStartSelected())
	{
		if(fireWallPlacementStartIsLegal(clickedHex))
			fireWallSelectedStart = clickedHex;
		updateFireWallPlacementStatus(clickedHex);
		ENGINE->windows().totalRedraw();
		return;
	}

	if(clickedHex == fireWallSelectedStart)
	{
		fireWallSelectedStart = BattleHex::INVALID;
		updateFireWallPlacementStatus(clickedHex);
		ENGINE->windows().totalRedraw();
		return;
	}

	if(!fireWallPlacementEndpointIsLegal(clickedHex))
	{
		updateFireWallPlacementStatus(clickedHex);
		return;
	}

	const auto direction = fireWallDirectionBetween(fireWallSelectedStart, clickedHex);
	BattleAction action = *heroSpellToCast;
	action.target.clear();
	action.aimToHex(fireWallSelectedStart);
	action.spellFireWallDirection = direction;
	if(owner.curInt && owner.curInt->cb)
	{
		if(submitHeroSpellAction(action))
			endCastingSpell();
	}
}

bool BattleActionsController::vengefulVinesTargetSelectionModeActive() const
{
	const auto battle = owner.getBattle();
	return heroSpellToCast && battle && battle->getBattle()
		&& newHorizonsVengefulVines::enabled(battle->getBattle()->getMagicRules(), heroSpellToCast->spell);
}

bool BattleActionsController::vengefulVinesSelectionContextIsCurrent() const
{
	if(!vengefulVinesTargetSelectionModeActive() || !owner.curInt || !owner.curInt->cb
		|| CPlayerInterface::battleInt.get() != &owner || owner.getBattleID() != vengefulVinesBattleID
		|| !vengefulVinesPlayer || owner.curInt->cb->getPlayerID() != *vengefulVinesPlayer
		|| !owner.getBattle() || !owner.getBattle()->getBattle()
		|| owner.getBattle()->battleGetMySide() != vengefulVinesSide
		|| owner.getBattle()->battleGetRound() != vengefulVinesRound
		|| !owner.makingTurn() || owner.curInt->isAutoFightOn || owner.isInTacticsMode())
		return false;

	const auto * hero = owner.currentHero();
	return hero && hero->id == vengefulVinesHeroID;
}

const std::vector<BattleHex> & BattleActionsController::getVengefulVinesSelectedHexes() const
{
	return vengefulVinesSelectedHexes;
}

bool BattleActionsController::vengefulVinesHexIsLegalCandidate(const BattleHex & hex) const
{
	if(!vengefulVinesTargetSelectionModeActive() || !vengefulVinesSelectionContextIsCurrent()
		|| !hex.isAvailable() || vengefulVinesSelectedHexes.size() >= vengefulVinesFootprintHexCount
		|| std::ranges::find(vengefulVinesSelectedHexes, hex) != vengefulVinesSelectedHexes.end())
		return false;

	if(!vengefulVinesSelectedHexes.empty())
	{
		const bool touchesSelection = std::ranges::any_of(vengefulVinesSelectedHexes, [&hex](const BattleHex & selected)
		{
			return BattleHex::mutualPosition(selected, hex) != BattleHex::NONE;
		});
		if(!touchesSelection)
			return false;
	}

	if(vengefulVinesSelectedHexes.size() == vengefulVinesFootprintHexCount - 1)
		return vengefulVinesHexCompletesLegalCast(hex);

	return true;
}

bool BattleActionsController::vengefulVinesHexCompletesLegalCast(const BattleHex & hex) const
{
	if(vengefulVinesSelectedHexes.size() != vengefulVinesFootprintHexCount - 1
		|| !hex.isAvailable()
		|| std::ranges::find(vengefulVinesSelectedHexes, hex) != vengefulVinesSelectedHexes.end())
		return false;

	const bool touchesSelection = std::ranges::any_of(vengefulVinesSelectedHexes, [&hex](const BattleHex & selected)
	{
		return BattleHex::mutualPosition(selected, hex) != BattleHex::NONE;
	});
	if(!touchesSelection)
		return false;

	auto candidate = vengefulVinesSelectedHexes;
	candidate.push_back(hex);
	return vengefulVinesTargetsAreLegal(candidate);
}

int32_t BattleActionsController::vengefulVinesEnemyTargetCount(const BattleHexArray & footprint) const
{
	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast ? heroSpellToCast->spell.toSpell() : nullptr;
	if(!battle || !hero || !spell || vengefulVinesSide == BattleSide::NONE)
		return 0;

	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	const auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return 0;

	std::set<uint32_t> distinctEnemies;
	const auto enemySide = battle->otherSide(vengefulVinesSide);
	for(const auto & hex : footprint)
	{
		const auto * unit = battle->battleGetUnitByPos(hex, true);
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isInvincible()
			|| unit->unitSide() != enemySide || !mechanics->isReceptive(unit))
			continue;

		distinctEnemies.insert(unit->unitId());
	}
	return static_cast<int32_t>(distinctEnemies.size());
}

bool BattleActionsController::vengefulVinesTargetsAreLegal(const std::vector<BattleHex> & selectedHexes) const
{
	if(!vengefulVinesSelectionContextIsCurrent() || selectedHexes.size() != vengefulVinesFootprintHexCount)
		return false;

	spells::Target target;
	for(const auto & hex : selectedHexes)
		target.emplace_back(hex);

	const auto footprint = newHorizonsVengefulVines::footprint(target);
	if(footprint.size() != vengefulVinesFootprintHexCount)
		return false;
	if(vengefulVinesEnemyTargetCount(footprint) == 0)
		return false;

	const auto battle = owner.getBattle();
	const auto * hero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!battle || !hero || !spell)
		return false;

	spells::BattleCast cast(battle.get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	if(!mechanics->canBeCast(problem))
		return false;

	return mechanics->canBeCastAt(target, problem);
}

void BattleActionsController::undoVengefulVinesSelection()
{
	if(!vengefulVinesTargetSelectionModeActive())
		return;
	if(!vengefulVinesSelectionContextIsCurrent())
	{
		endCastingSpell();
		return;
	}
	if(vengefulVinesSelectedHexes.empty())
		return;

	vengefulVinesSelectedHexes.pop_back();
	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateVengefulVinesStatus(BattleHex::INVALID);
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

void BattleActionsController::updateVengefulVinesStatus(const BattleHex & hoveredHex)
{
	if(!vengefulVinesTargetSelectionModeActive())
		return;

	std::string message;
	if(!vengefulVinesSelectionContextIsCurrent())
	{
		message = "Battle context changed. Vengeful Vines selection cancelled.";
	}
	else
	{
		message = "Vengeful Vines: " + std::to_string(vengefulVinesSelectedHexes.size())
			+ "/" + std::to_string(vengefulVinesFootprintHexCount) + " locations selected.";
		if(vengefulVinesSelectedHexes.size() < vengefulVinesFootprintHexCount)
			message += " Click a distinct connected playable hex; Backspace removes the last; Esc cancels.";
		if(hoveredHex.isValid())
		{
			if(vengefulVinesHexIsLegalCandidate(hoveredHex))
			{
				if(vengefulVinesSelectedHexes.size() == vengefulVinesFootprintHexCount - 1)
				{
					auto candidate = vengefulVinesSelectedHexes;
					candidate.push_back(hoveredHex);
					spells::Target target;
					for(const auto & hex : candidate)
						target.emplace_back(hex);
					const auto footprint = newHorizonsVengefulVines::footprint(target);
					const auto enemies = vengefulVinesEnemyTargetCount(footprint);
					message += " This completes a legal pattern affecting " + std::to_string(enemies)
						+ (enemies == 1 ? " enemy stack; click to cast." : " enemy stacks; click to cast.");
				}
				else
					message += " This is a legal next location.";
			}
			else if(vengefulVinesSelectedHexes.size() == vengefulVinesFootprintHexCount - 1)
				message += " That third hex is not a legal cast; the first two remain selected.";
		}
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectVengefulVinesHex(const BattleHex & clickedHex)
{
	if(!vengefulVinesTargetSelectionModeActive())
		return;
	if(!vengefulVinesSelectionContextIsCurrent())
	{
		endCastingSpell();
		return;
	}

	if(!vengefulVinesHexIsLegalCandidate(clickedHex))
	{
		updateVengefulVinesStatus(clickedHex);
		return;
	}

	vengefulVinesSelectedHexes.push_back(clickedHex);
	if(vengefulVinesSelectedHexes.size() == vengefulVinesFootprintHexCount)
	{
		// The third-click candidate check above revalidated both mechanics gates.
		// Preserve the ordered selections as three LOCATION destinations.
		BattleAction action = *heroSpellToCast;
		action.target.clear();
		for(const auto & hex : vengefulVinesSelectedHexes)
			action.aimToHex(hex);
		if(owner.curInt && owner.curInt->cb && submitHeroSpellAction(action))
		{
			endCastingSpell();
			return;
		}
	}

	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateVengefulVinesStatus(clickedHex);
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

void BattleActionsController::updateRepeatedPlacementStatus(const BattleHex & hoveredHex)
{
	if(!repeatedPlacementModeActive())
		return;

	const int required = repeatedPlacementRequiredHexes();
	const std::string placementName = quicksandPlacementModeActive() ? "Quicksand patches" : "Land Mine mines";
	const int remaining = std::max(0, required - static_cast<int>(repeatedPlacementSelectedHexes.size()));
	std::string message = placementName + " " + std::to_string(repeatedPlacementSelectedHexes.size())
		+ "/" + std::to_string(required) + " selected; " + std::to_string(remaining) + " remaining.";

	if(!repeatedPlacementSelectedHexes.empty())
	{
		message += " ";
		for(size_t index = 0; index < repeatedPlacementSelectedHexes.size(); ++index)
		{
			if(index != 0)
				message += " ";
			message += "#" + std::to_string(index + 1) + "="
				+ std::to_string(repeatedPlacementSelectedHexes[index].toInt());
		}
	}

	if(hoveredHex.isValid())
	{
		const bool selected = std::find(repeatedPlacementSelectedHexes.begin(), repeatedPlacementSelectedHexes.end(), hoveredHex)
			!= repeatedPlacementSelectedHexes.end();
		if(selected)
		{
			if(repeatedPlacementHexIsLegal(hoveredHex))
				message += " Click selected hex to undo.";
			else
				message += " Selection changed; undo it.";
		}
		else if(!repeatedPlacementHexIsLegal(hoveredHex))
		{
			message += " Hex unavailable.";
		}
		else if(repeatedPlacementReady())
		{
			message += " Click Confirm or press Enter.";
		}
		else
		{
			message += " Click an empty legal hex.";
		}
	}
	else if(repeatedPlacementReady())
	{
		message += " Click Confirm or press Enter.";
	}
	else
	{
		message += " Select legal hexes in order. Backspace undoes the last; Esc cancels.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectOrUndoRepeatedPlacementHex(const BattleHex & clickedHex)
{
	if(!repeatedPlacementModeActive())
		return;

	const auto selected = std::find(repeatedPlacementSelectedHexes.begin(), repeatedPlacementSelectedHexes.end(), clickedHex);
	if(selected != repeatedPlacementSelectedHexes.end())
	{
		repeatedPlacementSelectedHexes.erase(selected);
	}
	else if(repeatedPlacementHexIsLegal(clickedHex)
		&& static_cast<int>(repeatedPlacementSelectedHexes.size()) < repeatedPlacementRequiredHexes())
	{
		repeatedPlacementSelectedHexes.push_back(clickedHex);
	}

	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	updateRepeatedPlacementStatus(clickedHex);
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::repeatedPlacementTargetsValid() const
{
	if(!repeatedPlacementReady() || !owner.getBattle() || !owner.currentHero())
		return false;

	std::set<int> selected;
	for(const auto & hex : repeatedPlacementSelectedHexes)
	{
		if(!selected.insert(hex.toInt()).second || !repeatedPlacementHexIsLegal(hex))
			return false;
	}

	const auto * spell = heroSpellToCast->spell.toSpell();
	spells::BattleCast cast(owner.getBattle().get(), owner.currentHero(), spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	if(!mechanics->canBeCast(problem))
		return false;

	battle::Target target;
	for(const auto & hex : repeatedPlacementSelectedHexes)
		target.emplace_back(hex);
	return mechanics->canBeCastAt(target, problem);
}

void BattleActionsController::confirmRepeatedPlacement()
{
	if(!repeatedPlacementModeActive())
		return;

	if(!repeatedPlacementReady())
	{
		updateRepeatedPlacementStatus(BattleHex::INVALID);
		return;
	}

	// The battlefield may have changed while the player was choosing.  Re-run
	// the same live-snapshot checks as the generic mechanics before creating a
	// request; the server remains the final authority on this packet.
	if(!repeatedPlacementTargetsValid())
	{
		updateRepeatedPlacementStatus(BattleHex::INVALID);
		currentConsoleMsg += ". Selection is no longer legal; undo or cancel.";
		ENGINE->statusbar()->write(currentConsoleMsg);
		return;
	}

	BattleAction action = *heroSpellToCast;
	action.target.clear();
	for(const auto & hex : repeatedPlacementSelectedHexes)
		action.aimToHex(hex);

	if(!owner.curInt || !owner.curInt->cb)
		return;
	if(submitHeroSpellAction(action))
		endCastingSpell();
}

void BattleActionsController::undoRepeatedPlacement()
{
	if(!repeatedPlacementModeActive() || repeatedPlacementSelectedHexes.empty())
		return;

	repeatedPlacementSelectedHexes.pop_back();
	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::isTransfigureMatterSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == transfigureMatterJsonKey;
}

bool BattleActionsController::isSummonTrollsSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == summonTrollsJsonKey;
}

bool BattleActionsController::isVerdantPrisonSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == verdantPrisonJsonKey;
}

bool BattleActionsController::isHydrasVitalitySpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == hydrasVitalityJsonKey;
}

bool BattleActionsController::isBlinkSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsBlink::SPELL_ID;
}

std::optional<newHorizonsBlink::Preview> BattleActionsController::getBlinkDestinationPreview(
	const CSpell * spell, const BattleHex & targetHex)
{
	if(!isBlinkSpell(spell) || !heroSpellcastingModeActive() || !targetHex.isValid())
		return std::nullopt;

	const auto battle = owner.getBattle();
	const auto * target = battle ? battle->battleGetStackByPos(targetHex, true) : nullptr;
	if(!target && battle)
		target = battle->battleGetStackByPos(targetHex, false);
	if(!battle || !target || !isCastingPossibleHere(spell, nullptr, targetHex))
		return std::nullopt;

	const auto * caster = getCurrentSpellcaster();
	if(!caster || !heroSpellToCast)
		return std::nullopt;

	spells::BattleCast cast(battle.get(), caster, getCurrentCastMode(), spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	const auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return std::nullopt;

	return newHorizonsBlink::preview(*mechanics, target);
}

bool BattleActionsController::isValidTransfigureMatterTarget(const BattleHex & targetHex) const
{
	if(!targetHex.isValid())
		return false;

	const auto battle = owner.getBattle();
	if(!battle)
		return false;

	// Do not use battleGetAllObstaclesOnPos here: moat instances have no
	// affected-tile footprint and querying one would assert. Filter the category
	// before asking an ordinary obstacle for its occupied tiles.
	for(const auto & obstacle : battle->battleGetAllObstacles())
		if(isTransfigureMatterObstacle(obstacle.get()) && obstacle->getAffectedTiles().contains(targetHex))
			return true;

	return false;
}

BattleHexArray BattleActionsController::getTransfigureMatterTargetHexes(const CSpell * spell)
{
	BattleHexArray result;
	const auto battle = owner.getBattle();
	if(!battle)
		return result;

	for(const auto & obstacle : battle->battleGetAllObstacles())
	{
		if(!isTransfigureMatterObstacle(obstacle.get()))
			continue;

		const auto footprint = obstacle->getAffectedTiles();
		const auto legalTarget = std::ranges::find_if(footprint, [this, spell](const BattleHex & hex)
		{
			return hex.isValid() && isCastingPossibleHere(spell, nullptr, hex);
		});
		if(legalTarget == footprint.end())
			continue;

		for(const auto & hex : footprint)
			if(hex.isValid())
				result.insert(hex);
	}

	return result;
}

BattleHexArray BattleActionsController::getSummonTrollsTargetHexes(const CSpell * spell)
{
	BattleHexArray result;
	if(!isSummonTrollsSpell(spell) || !owner.getBattle() || !heroSpellcastingModeActive())
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(hex.isAvailable() && isCastingPossibleHere(spell, nullptr, hex))
			result.insert(hex);
	}
	return result;
}

BattleHexArray BattleActionsController::getVerdantPrisonTargetHexes(const CSpell * spell, const BattleHex & targetHex)
{
	if(!isVerdantPrisonSpell(spell) || !owner.getBattle() || !heroSpellcastingModeActive()
		|| !targetHex.isValid() || !isCastingPossibleHere(spell, nullptr, targetHex))
		return {};

	spells::BattleCast cast(owner.getBattle().get(), getCurrentSpellcaster(), getCurrentCastMode(), spell);
	const auto * battle = owner.getBattle().get();
	const auto side = battle->battleGetMySide();
	const bool followup = side != BattleSide::NONE
		&& battle->battleCanUseMetamagicFollowup(side, spell->getId());
	cast.setMetamagicFollowup(followup);

	const auto mechanics = spell->battleMechanics(&cast);
	return mechanics ? mechanics->rangeInHexes(targetHex) : BattleHexArray{};
}

void BattleActionsController::setMagicArrowOverchargeFactory(MagicArrowOverchargeFactory factory)
{
	magicArrowOverchargeFactory = std::move(factory);
}

void BattleActionsController::setSelectiveDispelFactory(SelectiveDispelFactory factory)
{
	selectiveDispelFactory = std::move(factory);
}

void BattleActionsController::setPurifyPicker(PurifyPicker picker)
{
	purifyPicker = std::move(picker);
}

void BattleActionsController::setCureAfflictionPicker(std::function<bool(const BattleAction &, const CStack *)> picker)
{
	cureAfflictionPicker = std::move(picker);
}

void BattleActionsController::setShadowGiftFactory(ShadowGiftFactory factory)
{
	shadowGiftFactory = std::move(factory);
}

void BattleActionsController::setTemporalFieldFactory(TemporalFieldFactory factory)
{
	temporalFieldFactory = std::move(factory);
}

void BattleActionsController::endCastingSpell()
{
	invalidateChainLightningPreview();
	++castingSession;
	// The battle's Escape shortcut also reaches this method outside spell mode.
	cancelHeroOrderTargeting();
	const bool wasRepeatedPlacement = repeatedPlacementModeActive();
	const bool wasFireWallPlacement = fireWallPlacementModeActive();
	const bool wasVengefulVinesSelection = vengefulVinesTargetSelectionModeActive();
	const bool wasStormOfDaggersSelection = stormOfDaggersTargetSelectionModeActive();
	const bool wasSoulChainSelection = soulChainTargetSelectionModeActive();
	const bool wasLifeDrainSelection = lifeDrainTargetSelectionModeActive();
	if(heroSpellToCast)
	{
		heroSpellToCast.reset();
		owner.windowObject->blockUI(false);
	}

	if(monsterCaster)
	{
		monsterCaster = nullptr;
		owner.stacksController->activateStack();
	}
	monsterSpellTargets.clear();
	repeatedPlacementSelectedHexes.clear();
	stormOfDaggersSelectedUnitIds.clear();
	stormOfDaggersPlayer.reset();
	stormOfDaggersSide = BattleSide::NONE;
	stormOfDaggersRound = -1;
	stormOfDaggersHeroID = ObjectInstanceID::NONE;
	soulChainSelectedUnitIds.clear();
	soulChainPlayer.reset();
	soulChainSide = BattleSide::NONE;
	soulChainRound = -1;
	soulChainHeroID = ObjectInstanceID::NONE;
	lifeDrainSelectedUnitIds.clear();
	lifeDrainPlayer.reset();
	lifeDrainSide = BattleSide::NONE;
	lifeDrainRound = -1;
	lifeDrainHeroID = ObjectInstanceID::NONE;
	fireWallSelectedStart = BattleHex::INVALID;
	vengefulVinesSelectedHexes.clear();
	vengefulVinesBattleID = BattleID();
	vengefulVinesPlayer.reset();
	vengefulVinesSide = BattleSide::NONE;
	vengefulVinesRound = -1;
	vengefulVinesHeroID = ObjectInstanceID::NONE;
	if((wasRepeatedPlacement || wasFireWallPlacement || wasVengefulVinesSelection
		|| wasStormOfDaggersSelection || wasSoulChainSelection || wasLifeDrainSelection)
		&& !currentConsoleMsg.empty())
	{
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
		currentConsoleMsg.clear();
	}

	if(owner.stacksController->getActiveStack())
	{
		possibleActions = getPossibleActionsForStack(owner.stacksController->getActiveStack()); //restore actions after they were cleared
		owner.windowObject->setPossibleActions(possibleActions);
	}
	if(owner.windowObject)
		owner.windowObject->updateBattleTargetSelectionControls();

	selectedStack = nullptr;
	ENGINE->fakeMouseMove();
}

bool BattleActionsController::isActiveStackSpellcaster() const
{
	const CStack * casterStack = owner.stacksController->getActiveStack();
	if (!casterStack)
		return false;

	bool spellcaster = casterStack->hasBonusOfType(BonusType::SPELLCASTER);
	return (spellcaster && casterStack->canCast());
}

void BattleActionsController::enterCreatureCastingMode()
{
	invalidateChainLightningPreview();
	//silently check for possible errors
	if (owner.isInTacticsMode())
		return;

	//hero is casting a spell
	if (heroSpellToCast)
		return;

	if (!owner.stacksController->getActiveStack())
		return;

	if(owner.getBattle()->battleCanTargetEmptyHex(owner.stacksController->getActiveStack()))
	{
		auto actionFilterPredicate = [](const PossiblePlayerBattleAction x)
		{
			return x.get() != PossiblePlayerBattleAction::SHOOT;
		};

		vstd::erase_if(possibleActions, actionFilterPredicate);
		ENGINE->fakeMouseMove();
		return;
	}

	if (!isActiveStackSpellcaster())
		return;

	for(const auto & action : possibleActions)
	{
		if (action.get() != PossiblePlayerBattleAction::NO_LOCATION)
			continue;

		const spells::Caster * caster = getCurrentSpellcaster();
		const CSpell * spell = action.spell().toSpell();

		spells::Target target;
		target.emplace_back();

		spells::BattleCast cast(owner.getBattle().get(), caster, spells::Mode::CREATURE_ACTIVE, spell);

		auto m = spell->battleMechanics(&cast);
		const bool isCastingPossible = m->canBeCastAt(target);

		if (isCastingPossible)
		{
			owner.giveCommand(EActionType::MONSTER_SPELL, BattleHex::INVALID, spell->getId());
			selectedStack = nullptr;

			ENGINE->cursor().set(Cursor::Combat::POINTER);
		}
		return;
	}

	possibleActions = getPossibleActionsForStack(owner.stacksController->getActiveStack());

	auto actionFilterPredicate = [](const PossiblePlayerBattleAction x)
	{
		return !x.spellcast();
	};

	vstd::erase_if(possibleActions, actionFilterPredicate);
	ENGINE->fakeMouseMove();
}

std::vector<PossiblePlayerBattleAction> BattleActionsController::getPossibleActionsForStack(const CStack *stack) const
{
	BattleClientInterfaceData data; //hard to get rid of these things so for now they're required data to pass

	for(const auto & spell : creatureSpells)
		data.creatureSpellsToCast.push_back(spell->id);

	data.tacticsMode = owner.isInTacticsMode();
	auto allActions = owner.getBattle()->getClientActionsForStack(stack, data);

	allActions.push_back(PossiblePlayerBattleAction::HERO_INFO);
	allActions.push_back(PossiblePlayerBattleAction::CREATURE_INFO);

	// A pending ranged continuation cannot be replaced with movement, Wait,
	// or a creature action. Keep Shoot only when the authority recognizes the
	// saved allowance; target-specific legality remains in actionIsLegal.
	// Information actions remain available, and Defend is the existing safe
	// NO_ACTION close control.
	const auto battle = owner.getBattle();
	const auto unitState = stack ? stack->acquireState() : nullptr;
	if(battle && unitState && unitState->rangedFollowUpDamagePercent > 0)
	{
		const bool pendingFollowUp = battle->battleHasPendingRangedFollowUp(stack);
		vstd::erase_if(allActions, [pendingFollowUp](const PossiblePlayerBattleAction & action)
			{
				if(action.get() == PossiblePlayerBattleAction::HERO_INFO
					|| action.get() == PossiblePlayerBattleAction::CREATURE_INFO)
					return false;
				return !pendingFollowUp || action.get() != PossiblePlayerBattleAction::SHOOT;
			});
	}

	return std::vector<PossiblePlayerBattleAction>(allActions);
}

void BattleActionsController::reorderPossibleActionsPriority(const CStack * stack, const CStack * targetStack)
{
	if(owner.getBattle()->battleTacticDist() > 0 || possibleActions.empty()) return; //this function is not supposed to be called in tactics mode or before getPossibleActionsForStack

	auto assignPriority = [&](const PossiblePlayerBattleAction & item
						  ) -> uint8_t //large lambda assigning priority which would have to be part of possibleActions without it
	{
		switch(item.get())
		{
			case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
			case PossiblePlayerBattleAction::ANY_LOCATION:
			case PossiblePlayerBattleAction::NO_LOCATION:
			case PossiblePlayerBattleAction::FREE_LOCATION:
			case PossiblePlayerBattleAction::OBSTACLE:
			case PossiblePlayerBattleAction::SACRIFICE:
			case PossiblePlayerBattleAction::LIFE_DRAIN:
				if(!stack->hasBonusOfType(BonusType::NO_SPELLCAST_BY_DEFAULT) && targetStack != nullptr)
				{
					PlayerColor stackOwner = owner.getBattle()->battleGetOwner(targetStack);
					bool enemyTargetingPositiveSpellcast = item.spell().toSpell()->isPositive() && stackOwner != owner.curInt->playerID;
					bool friendTargetingNegativeSpellcast = item.spell().toSpell()->isNegative() && stackOwner == owner.curInt->playerID;

					if(!enemyTargetingPositiveSpellcast && !friendTargetingNegativeSpellcast)
						return 1;
				}
				return 100; //bottom priority

				break;
			case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
				return 2;
				break;
			case PossiblePlayerBattleAction::SHOOT:
				if(targetStack == nullptr || !targetStack->alive()
					|| owner.getBattle()->battleMatchActionController(stack, targetStack, true))
					return 100; //bottom priority

				return 4;
				break;
			case PossiblePlayerBattleAction::ATTACK_AND_RETURN:
				return 5;
				break;
			case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
				return 6;
				break;
			case PossiblePlayerBattleAction::ATTACK:
				return 7;
				break;
			case PossiblePlayerBattleAction::WALK_AND_ATTACK:
				return 8;
			case PossiblePlayerBattleAction::SKIRMISHER_ATTACK:
				return 0;
				break;
			case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
				return 9;
				break;
			case PossiblePlayerBattleAction::MOVE_STACK:
				return 10;
				break;
			case PossiblePlayerBattleAction::DEMONIC_GATE:
				return 10;
				break;
			case PossiblePlayerBattleAction::CATAPULT:
				return 11;
				break;
			case PossiblePlayerBattleAction::HEAL:
				return 12;
				break;
			case PossiblePlayerBattleAction::CREATURE_INFO:
				return 13;
				break;
			case PossiblePlayerBattleAction::HERO_INFO:
				return 14;
				break;
			case PossiblePlayerBattleAction::TELEPORT:
				return 15;
				break;
			default:
				assert(0);
				return 200;
				break;
		}
	};

	auto comparer = [&](const PossiblePlayerBattleAction & lhs, const PossiblePlayerBattleAction & rhs)
	{
		return assignPriority(lhs) < assignPriority(rhs);
	};

	std::sort(possibleActions.begin(), possibleActions.end(), comparer);
}

void BattleActionsController::castThisSpell(SpellID spellID)
{
	invalidateChainLightningPreview();
	cancelHeroOrderTargeting();
	if(!owner.curInt)
		return;
	const auto * castingHero = owner.currentHero();
	if(!castingHero)
		return;
	const auto battle = owner.getBattle();
	if(!battle)
		return;

	heroSpellToCast = std::make_shared<BattleAction>();
	++castingSession;
	heroSpellToCast->actionType = EActionType::HERO_SPELL;
	heroSpellToCast->spell = spellID;
	heroSpellToCast->stackNumber = -1;
	heroSpellToCast->side = battle->battleGetMySide();
	heroSpellToCast->metamagicFollowup = heroSpellToCast->side != BattleSide::NONE
		&& battle->battleCanUseMetamagicFollowup(heroSpellToCast->side, spellID);
	vengefulVinesSelectedHexes.clear();
	vengefulVinesBattleID = BattleID();
	vengefulVinesPlayer.reset();
	vengefulVinesSide = BattleSide::NONE;
	vengefulVinesRound = -1;
	vengefulVinesHeroID = ObjectInstanceID::NONE;

	// New Horizons Storm of Daggers selects ordered enemy unit identities. Keep
	// this separate from the generic one-stack spell selector so every target is
	// validated together and the exact IDs survive into BattleAction::target.
	if(isStormOfDaggersSpell(heroSpellToCast->spell.toSpell()))
	{
		stormOfDaggersSelectedUnitIds.clear();
		stormOfDaggersBattleID = owner.getBattleID();
		stormOfDaggersPlayer = owner.curInt->cb->getPlayerID();
		stormOfDaggersSide = battle->battleGetMySide();
		stormOfDaggersRound = battle->battleGetRound();
		stormOfDaggersHeroID = castingHero->id;
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateStormOfDaggersSelectionStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Soul Chain keeps the primary first and optional secondaries after it. The
	// exact ordered stack IDs remain in the normal spell request until Confirm.
	if(isSoulChainSpell(heroSpellToCast->spell.toSpell()))
	{
		soulChainSelectedUnitIds.clear();
		soulChainBattleID = owner.getBattleID();
		soulChainPlayer = owner.curInt->cb->getPlayerID();
		soulChainSide = battle->battleGetMySide();
		soulChainRound = battle->battleGetRound();
		soulChainHeroID = castingHero->id;
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateSoulChainSelectionStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Life Drain requires one enemy stack followed by one allied stack. Keep
	// both explicit identities together and avoid the generic single-target
	// path (or Sacrifice's corpse-first interaction).
	if(isLifeDrainSpell(heroSpellToCast->spell.toSpell())
		&& battle->getCasterAction(heroSpellToCast->spell.toSpell(), castingHero, spells::Mode::HERO).get()
			== PossiblePlayerBattleAction::LIFE_DRAIN)
	{
		lifeDrainSelectedUnitIds.clear();
		lifeDrainBattleID = owner.getBattleID();
		lifeDrainPlayer = owner.curInt->cb ? std::optional<PlayerColor>(owner.curInt->cb->getPlayerID()) : std::nullopt;
		lifeDrainSide = battle->battleGetMySide();
		lifeDrainRound = battle->battleGetRound();
		lifeDrainHeroID = castingHero->id;
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		updateLifeDrainSelectionStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Canonical Land Mine and saved-marker Quicksand are ordered multi-hex
	// actions. They must not enter the generic NO_TARGET path, which would
	// immediately submit the legacy/random obstacle action. Markerless Quicksand
	// snapshots keep that existing path.
	if(landMinePlacementModeActive() || quicksandPlacementModeActive())
	{
		repeatedPlacementSelectedHexes.clear();
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateRepeatedPlacementStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Canonical New Horizons Fire Wall is selected as a start hex followed by
	// an adjacent endpoint. The second click is converted to the compact
	// start+direction protocol only after the live three-hex line is checked.
	if(fireWallPlacementModeActive())
	{
		fireWallSelectedStart = BattleHex::INVALID;
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		updateFireWallPlacementStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Saved-v3 Vengeful Vines uses its own orientation selector. In particular,
	// do not let the generic LOCATION dispatch interpret the selected endpoint
	// as an ordinary single-hex teleport-style target.
	if(vengefulVinesTargetSelectionModeActive())
	{
		vengefulVinesBattleID = owner.getBattleID();
		vengefulVinesPlayer = owner.curInt->cb ? std::optional<PlayerColor>(owner.curInt->cb->getPlayerID()) : std::nullopt;
		vengefulVinesSide = battle->battleGetMySide();
		vengefulVinesRound = battle->battleGetRound();
		vengefulVinesHeroID = castingHero->id;
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		if(owner.windowObject)
			owner.windowObject->updateBattleTargetSelectionControls();
		updateVengefulVinesStatus(BattleHex::INVALID);
		ENGINE->fakeMouseMove();
		ENGINE->windows().totalRedraw();
		return;
	}

	// Temporal Field is an explicit pre-target choice. The ordinary branch is
	// resumed through continueOrdinarySpellcast(), while Mass submits a single
	// NO_LOCATION hero spell action from the installed BattleInterface adapter.
	if(spellID == SpellID::SLOW && temporalFieldFactory)
	{
		if(const auto context = temporalFieldFactory(*heroSpellToCast))
		{
			ENGINE->windows().createAndPushWindow<TemporalFieldWindow>(*context);
			owner.windowObject->blockUI(true);
			return;
		}
	}

	//choosing possible targets
	PossiblePlayerBattleAction spellSelMode = owner.getBattle()->getCasterAction(spellID.toSpell(), castingHero, spells::Mode::HERO);

	if (spellSelMode.get() == PossiblePlayerBattleAction::NO_LOCATION) //user does not have to select location
	{
		heroSpellToCast->aimToHex(BattleHex::INVALID);
		if(spellID == SpellID::DISPEL && selectiveDispelFactory)
		{
			if(const auto context = selectiveDispelFactory(*heroSpellToCast, nullptr))
			{
				ENGINE->windows().createAndPushWindow<SelectiveDispelWindow>(*context);
				owner.windowObject->blockUI(true);
				return;
			}
		}
		if(submitHeroSpellAction(*heroSpellToCast))
			endCastingSpell();
	}
	else
	{
		possibleActions.clear();
		possibleActions.push_back (spellSelMode); //only this one action can be performed at the moment
		ENGINE->fakeMouseMove();//update cursor
	}

	owner.windowObject->blockUI(true);
}

bool BattleActionsController::continueOrdinarySpellcast()
{
	if(!owner.curInt || !heroSpellToCast)
		return false;

	const auto * castingHero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!castingHero || !spell)
		return false;
	const auto battle = owner.getBattle();
	if(!battle)
		return false;
	heroSpellToCast->side = battle->battleGetMySide();
	heroSpellToCast->metamagicFollowup = heroSpellToCast->side != BattleSide::NONE
		&& battle->battleCanUseMetamagicFollowup(heroSpellToCast->side, spell->getId());

	const auto spellSelMode = owner.getBattle()->getCasterAction(spell, castingHero, spells::Mode::HERO);
	if(spellSelMode.get() == PossiblePlayerBattleAction::INVALID)
		return false;

	if(spellSelMode.get() == PossiblePlayerBattleAction::NO_LOCATION)
	{
		heroSpellToCast->aimToHex(BattleHex::INVALID);
		if(submitHeroSpellAction(*heroSpellToCast))
			endCastingSpell();
		return true;
	}

	possibleActions.clear();
	possibleActions.push_back(spellSelMode);
	ENGINE->fakeMouseMove();
	owner.windowObject->blockUI(true);
	return true;
}

const CSpell * BattleActionsController::getHeroSpellToCast( ) const
{
	if (heroSpellToCast)
		return heroSpellToCast->spell.toSpell();
	return nullptr;
}

const CSpell * BattleActionsController::getStackSpellToCast(const BattleHex & hoveredHex)
{
	if (heroSpellToCast)
		return nullptr;

	if (!owner.stacksController->getActiveStack())
		return nullptr;

	if (!hoveredHex.isValid())
		return nullptr;

	auto action = selectAction(hoveredHex);

	if(owner.stacksController->getActiveStack()->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
	{
		auto bonus = owner.stacksController->getActiveStack()->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
		return bonus->subtype.as<SpellID>().toSpell();
	}

	if(action.get() == PossiblePlayerBattleAction::WALK_AND_SPELLCAST)
	{
		auto bonus = owner.stacksController->getActiveStack()->getBonus(Selector::type()(BonusType::ADJACENT_SPELLCASTER));
		return bonus->subtype.as<SpellID>().toSpell();
	}

	if (action.spell() == SpellID::NONE)
		return nullptr;

	return action.spell().toSpell();
}

const CSpell * BattleActionsController::getCurrentSpell(const BattleHex & hoveredHex)
{
	if (getHeroSpellToCast())
		return getHeroSpellToCast();
	return getStackSpellToCast(hoveredHex);
}

const ChainLightningPreview & BattleActionsController::getChainLightningPreview() const
{
	return chainLightningPreview;
}

void BattleActionsController::invalidateChainLightningPreview()
{
	chainLightningPreview = {};
	chainLightningPreviewCacheKey.reset();
}

void BattleActionsController::updateChainLightningPreview(PossiblePlayerBattleAction action, const BattleHex & hoveredHex)
{
	if(action.get() != PossiblePlayerBattleAction::AIMED_SPELL_CREATURE || !hoveredHex.isValid())
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto battle = owner.getBattle();
	const auto * battleState = battle ? battle->getBattle() : nullptr;
	const auto * spell = action.spell().toSpell();
	if(!battle || !battleState || !spell || !isChainLightningPreviewSpell(spell))
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto & savedRules = battleState->getMagicRules();
	if(!newHorizonsMagic::rulesActive(savedRules)
		|| savedRules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(savedRules, action.spell()))
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto mode = getCurrentCastMode();
	const auto * caster = getCurrentSpellcaster();
	const auto * heroCaster = mode == spells::Mode::HERO ? owner.currentHero() : nullptr;
	const CStack * creatureCaster = mode == spells::Mode::CREATURE_ACTIVE
		? (monsterCaster ? monsterCaster : owner.stacksController->getActiveStack()) : nullptr;
	if(!caster || (mode == spells::Mode::HERO && !heroCaster)
		|| (mode == spells::Mode::CREATURE_ACTIVE && !creatureCaster))
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto * targetUnit = battle->battleGetUnitByPos(hoveredHex, false);
	if(!targetUnit)
	{
		invalidateChainLightningPreview();
		return;
	}

	ChainLightningPreviewCacheKey cacheKey;
	cacheKey.aimedHex = hoveredHex;
	cacheKey.spell = action.spell();
	cacheKey.session = castingSession;
	cacheKey.casterUnitId = creatureCaster ? creatureCaster->unitId() : 0;
	cacheKey.casterHeroId = heroCaster ? heroCaster->id : ObjectInstanceID::NONE;
	cacheKey.side = heroCaster ? battle->battleGetMySide() : creatureCaster->unitSide();
	cacheKey.round = battleState->getRound();
	cacheKey.mode = static_cast<int32_t>(mode);
	if(heroSpellToCast)
	{
		cacheKey.metamagicFollowup = heroSpellToCast->metamagicFollowup;
		cacheKey.metamagicGrand = heroSpellToCast->metamagicGrand;
		cacheKey.metamagicManaRefund = heroSpellToCast->metamagicManaRefund;
		cacheKey.spellOvercharge = heroSpellToCast->spellOvercharge;
		cacheKey.spellSelectiveDispel = heroSpellToCast->spellSelectiveDispel;
		cacheKey.spellCureAffliction = heroSpellToCast->spellCureAffliction;
		cacheKey.spellMassSlow = heroSpellToCast->spellMassSlow;
		cacheKey.spellShadowGiftSacrificePercent = heroSpellToCast->spellShadowGiftSacrificePercent;
	}

	if(chainLightningPreview.active && chainLightningPreviewCacheKey
		&& *chainLightningPreviewCacheKey == cacheKey)
		return;

	spells::Target aim;
	aim.emplace_back(hoveredHex);
	aim.emplace_back(targetUnit);

	spells::BattleCast cast(battle.get(), caster, mode, spell);
	if(heroSpellToCast && mode == spells::Mode::HERO)
	{
		cast.setOvercharge(heroSpellToCast->spellOvercharge);
		cast.setSelectiveDispel(heroSpellToCast->spellSelectiveDispel);
		cast.setCureAffliction(heroSpellToCast->spellCureAffliction);
		cast.setMassSlow(heroSpellToCast->spellMassSlow);
		cast.setShadowGiftSacrificePercent(heroSpellToCast->spellShadowGiftSacrificePercent);
		cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
		cast.setMetamagicGrand(heroSpellToCast->metamagicGrand);
		cast.setMetamagicManaRefund(heroSpellToCast->metamagicManaRefund);
		cast.setMetamagicTargetUnitId(targetUnit->unitId());
	}

	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto canonicalTarget = mechanics->canonicalizeTarget(aim);
	ChainLightningPreview preview;
	preview.aimedHex = hoveredHex;
	preview.spell = action.spell();
	preview.castingSession = castingSession;
	preview.assumesNoResistance = true;
	bool foundDirectDamage = false;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		if(effect.name != "directDamage" || effect.indirect)
			return false;

		foundDirectDamage = true;
		const auto effectTarget = effect.transformTarget(mechanics.get(), aim, canonicalTarget);
		spells::Target prefix;
		prefix.reserve(effectTarget.size());
		int64_t previousCumulativeDamage = 0;
		int64_t previousCumulativeKills = 0;
		for(size_t index = 0; index < effectTarget.size(); ++index)
		{
			const auto & destination = effectTarget[index];
			prefix.push_back(destination);
			const auto value = effect.getHealthChange(mechanics.get(), prefix);
			const int64_t cumulativeDamage = std::max<int64_t>(0, -value.hpDelta);
			const int64_t cumulativeKills = std::max<int64_t>(0, -value.unitsDelta);
			if(destination.unitValue && destination.unitValue->alive())
			{
				ChainLightningRecipientPreview recipient;
				recipient.hopNumber = static_cast<int32_t>(index + 1);
				recipient.unitId = destination.unitValue->unitId();
				recipient.position = destination.unitValue->getPosition();
				recipient.occupiedHex = destination.unitValue->doubleWide()
					? destination.unitValue->occupiedHex() : BattleHex::INVALID;
				recipient.cumulativeDamage = cumulativeDamage;
				recipient.projectedDamage = std::max<int64_t>(0, cumulativeDamage - previousCumulativeDamage);
				recipient.estimatedKills = std::max<int64_t>(0, cumulativeKills - previousCumulativeKills);
				preview.recipients.push_back(std::move(recipient));
			}
			previousCumulativeDamage = cumulativeDamage;
			previousCumulativeKills = cumulativeKills;
		}
		return true;
	});

	if(!foundDirectDamage || preview.recipients.empty())
	{
		invalidateChainLightningPreview();
		return;
	}

	const auto headerTemplate = LIBRARY->generaltexth->translate(
		"new-horizons.combat.chainLightning.previewHeader");
	preview.consoleText = replacePlaceholders(headerTemplate,
		{{"%SPELL", spell->getNameTranslated()}});
	preview.consoleText += "\n";
	for(size_t index = 0; index < preview.recipients.size(); ++index)
	{
		if(index > 0)
			preview.consoleText += " ";
		const auto & recipient = preview.recipients[index];
		preview.consoleText += std::to_string(recipient.hopNumber) + ":"
			+ std::to_string(recipient.projectedDamage) + "/"
			+ std::to_string(recipient.estimatedKills);
	}
	preview.active = true;
	chainLightningPreview = std::move(preview);
	chainLightningPreviewCacheKey = cacheKey;
}

const CStack * BattleActionsController::getStackForHex(const BattleHex & hoveredHex)
{
	const CStack * shere = owner.getBattle()->battleGetStackByPos(hoveredHex, true);
	if(shere)
		return shere;
	return owner.getBattle()->battleGetStackByPos(hoveredHex, false);
}

void BattleActionsController::actionSetCursor(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	if(action.get() == PossiblePlayerBattleAction::SKIRMISHER_ATTACK)
	{
		ENGINE->cursor().set(Cursor::Combat::SHOOT);
		return;
	}
	switch (action.get())
	{
		case PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK:
			ENGINE->cursor().set(Cursor::Combat::POINTER);
			return;

		case PossiblePlayerBattleAction::MOVE_TACTICS:
		case PossiblePlayerBattleAction::MOVE_STACK:
			if (owner.stacksController->getActiveStack()->hasBonusOfType(BonusType::FLYING))
				ENGINE->cursor().set(Cursor::Combat::FLY);
			else
				ENGINE->cursor().set(Cursor::Combat::MOVE);
			return;

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN:
		{
			static const std::map<BattleHex::EDir, Cursor::Combat> sectorCursor = {
				{BattleHex::TOP_LEFT,     Cursor::Combat::HIT_SOUTHEAST},
				{BattleHex::TOP_RIGHT,    Cursor::Combat::HIT_SOUTHWEST},
				{BattleHex::RIGHT,        Cursor::Combat::HIT_WEST     },
				{BattleHex::BOTTOM_RIGHT, Cursor::Combat::HIT_NORTHWEST},
				{BattleHex::BOTTOM_LEFT,  Cursor::Combat::HIT_NORTHEAST},
				{BattleHex::LEFT,         Cursor::Combat::HIT_EAST     },
				{BattleHex::TOP,          Cursor::Combat::HIT_SOUTH    },
				{BattleHex::BOTTOM,       Cursor::Combat::HIT_NORTH    }
			};

			auto direction = owner.fieldController->selectAttackDirection(targetHex);

			assert(sectorCursor.count(direction) > 0);
			if (sectorCursor.count(direction))
				ENGINE->cursor().set(sectorCursor.at(direction));

			return;
		}

		case PossiblePlayerBattleAction::SHOOT:
			if (owner.getBattle()->battleHasShootingPenalty(owner.stacksController->getActiveStack(), targetHex))
				ENGINE->cursor().set(Cursor::Combat::SHOOT_PENALTY);
			else
				ENGINE->cursor().set(Cursor::Combat::SHOOT);
			return;

		case PossiblePlayerBattleAction::DEMONIC_GATE:
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			return;

		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::ANY_LOCATION:
		case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
		case PossiblePlayerBattleAction::FREE_LOCATION:
		case PossiblePlayerBattleAction::OBSTACLE:
		case PossiblePlayerBattleAction::LIFE_DRAIN:
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			return;

		case PossiblePlayerBattleAction::TELEPORT:
			if(!selectedStack)
				ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			else
				ENGINE->cursor().set(Cursor::Combat::TELEPORT);
			return;

		case PossiblePlayerBattleAction::SACRIFICE:
			if(!selectedStack)
				ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			else
				ENGINE->cursor().set(Cursor::Combat::SACRIFICE);
			return;

		case PossiblePlayerBattleAction::HEAL:
			ENGINE->cursor().set(Cursor::Combat::HEAL);
			return;

		case PossiblePlayerBattleAction::CATAPULT:
			ENGINE->cursor().set(Cursor::Combat::SHOOT_CATAPULT);
			return;

		case PossiblePlayerBattleAction::CREATURE_INFO:
			ENGINE->cursor().set(Cursor::Combat::QUERY);
			return;
		case PossiblePlayerBattleAction::HERO_INFO:
			ENGINE->cursor().set(Cursor::Combat::HERO);
			return;
	}
	assert(0);
}

void BattleActionsController::actionSetCursorBlocked(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	switch (action.get())
	{
		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
		case PossiblePlayerBattleAction::TELEPORT:
		case PossiblePlayerBattleAction::SACRIFICE:
		case PossiblePlayerBattleAction::LIFE_DRAIN:
		case PossiblePlayerBattleAction::FREE_LOCATION:
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
			return;
		default:
			if (targetHex == -1)
				ENGINE->cursor().set(Cursor::Combat::POINTER);
			else
				ENGINE->cursor().set(Cursor::Combat::BLOCKED);
			return;
	}
	assert(0);
}

std::string BattleActionsController::actionGetStatusMessage(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	const CStack * targetStack = getStackForHex(targetHex);
	if(action.get() == PossiblePlayerBattleAction::SKIRMISHER_ATTACK)
	{
		const auto * attacker = owner.stacksController->getActiveStack();
		if(!skirmisherTargetHex.isValid())
			return "Skirmisher: choose an enemy stack. It will move up to half Speed and shoot at 75% damage.";
		const auto * selectedTarget = owner.getBattle()->battleGetStackByPos(skirmisherTargetHex);
		if(!attacker || !selectedTarget)
			return "Skirmisher: choose a highlighted firing destination.";
		const auto & legalFiringHexes = getSkirmisherLegalFiringHexes();
		if(!vstd::contains(legalFiringHexes, targetHex))
			return "Skirmisher: choose a highlighted firing destination.";
		const auto distance = skirmisherFiringDistancesCache[targetHex.toInt()];
		BattleAttackInfo estimate(attacker, selectedTarget, static_cast<int>(distance), true);
		estimate.attackerPos = targetHex;
		estimate.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
		int rangedAttacks = attacker->getTotalAttacks(true);
		if(const auto * hero = owner.getBattle()->battleGetFightingHero(attacker->unitSide()))
			rangedAttacks += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(attacker->creatureId()));
		if(attacker->shots.isLimited())
			rangedAttacks = std::min(rangedAttacks, attacker->shots.available());
		const int shotsAfterAction = std::max(0, attacker->shots.available() - rangedAttacks);
		const std::string attackCount = rangedAttacks == 1 ? "one ranged attack" : std::to_string(rangedAttacks) + " ranged attacks";
		return "Skirmisher: move " + std::to_string(distance) + " hexes, then make " + attackCount
			+ " at 75% damage. Estimate per attack: "
			+ formatRangedAttack(owner.getBattle()->battleEstimateDamage(estimate), selectedTarget->getName(), shotsAfterAction);
	}

	switch (action.get()) //display console message, realize selected action
	{
		case PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK:
			return formatWithStackName("core.genrltxt.481", targetStack); //Select %s

		case PossiblePlayerBattleAction::MOVE_TACTICS:
		case PossiblePlayerBattleAction::MOVE_STACK:
		{
			const CStack * activeStack = owner.stacksController->getActiveStack();
			if (activeStack->hasBonusOfType(BonusType::FLYING))
				return formatWithStackName("core.genrltxt.295", activeStack); //Fly %s here
			else
				return formatWithStackName("core.genrltxt.294", activeStack); //Move %s here
		}

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN: //TODO: allow to disable return
			{
				const auto * attacker = owner.stacksController->getActiveStack();
				bool allowLongWeapon = action.get() == PossiblePlayerBattleAction::LONG_WEAPON_ATTACK;
				BattleHex attackFromHex = findAttackFromHex(owner, attacker, targetHex, allowLongWeapon);
				assert(attackFromHex.isValid());
				if(!attackFromHex.isValid())
					return "";
				int distance = attacker->position.isValid() ? owner.getBattle()->battleGetDistances(attacker, attacker->getPosition())[attackFromHex.toInt()] : 0;
				DamageEstimation retaliation;
				BattleAttackInfo attackInfo(attacker, targetStack, distance, false);
				attackInfo.attackerPos = attackFromHex;
				DamageEstimation estimation = owner.getBattle()->battleEstimateDamage(attackInfo, &retaliation);
				estimation.kills.max = std::min<int64_t>(estimation.kills.max, targetStack->getCount());
				estimation.kills.min = std::min<int64_t>(estimation.kills.min, targetStack->getCount());
				bool enemyMayBeKilled = estimation.kills.max == targetStack->getCount();

				// breath and other multi-hex attacks also strike extra units - add their kills to the prediction
				// (getAttackedBattleUnits excludes the directly-attacked hex, so the main target is handled above)
				for(const auto * splashTarget : owner.getBattle()->getAttackedCreatures(attacker, targetHex, false, attackFromHex).first)
				{
					if(splashTarget == targetStack || splashTarget == attacker)
						continue;
					BattleAttackInfo splashInfo(attacker, splashTarget, distance, false);
					splashInfo.attackerPos = attackFromHex;
					DamageEstimation splash = owner.getBattle()->battleEstimateDamage(splashInfo, nullptr);
					estimation.kills.min += std::min<int64_t>(splash.kills.min, splashTarget->getCount());
					estimation.kills.max += std::min<int64_t>(splash.kills.max, splashTarget->getCount());
				}

				std::string flankPrefix;
				const auto battle = owner.getBattle();
				const auto side = battle->battleGetMySide();
				const auto activeFlank = battle->battleGetHeroOrderState(side, HeroCommand::FLANK);
				if(activeFlank && activeFlank->issuedRound == owner.getBattle()->battleGetRound()
					&& activeFlank->primaryTargetUnitId == targetStack->unitId()
					&& attacker && attacker->alive() && !attacker->isGhost()
					&& battle->playerToSide(battle->battleGetOwner(attacker)) == side
					&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker)
					&& targetStack->alive() && !targetStack->isGhost())
				{
					const int flankPercent = owner.getBattle()->battleHeroOrderFlankMeleeDamagePercent(attackInfo);
					flankPrefix = "Flank +" + std::to_string(flankPercent) + "% here (included): ";
				}
				return flankPrefix + formatMeleeAttack(estimation, targetStack->getName())
					+ "\n" + formatRetaliation(retaliation, enemyMayBeKilled);
			}

		case PossiblePlayerBattleAction::SHOOT:
		{
			const auto * shooter = owner.stacksController->getActiveStack();

			if(targetStack == nullptr) //should be true only for spell-like attack
			{
				auto spellLikeAttackBonus = shooter->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
				assert(spellLikeAttackBonus != nullptr);
				const CSpell * spell = spellLikeAttackBonus->subtype.as<SpellID>().toSpell();

				DamageEstimation est = owner.getBattle()->estimateSpellLikeAttackDamage(shooter, spell, targetHex);
				return formatRangedAttack(est, spell->getNameTranslated(), shooter->shots.available());
			}

			DamageEstimation retaliation;
			BattleAttackInfo attackInfo(shooter, targetStack, 0, true );
			DamageEstimation estimation = owner.getBattle()->battleEstimateDamage(attackInfo, &retaliation);
			estimation.kills.max = std::min<int64_t>(estimation.kills.max, targetStack->getCount());
			estimation.kills.min = std::min<int64_t>(estimation.kills.min, targetStack->getCount());
			auto result = formatRangedAttack(estimation, targetStack->getName(), shooter->shots.available());
			const auto & battle = *owner.getBattle();
			if(battle.battleHasPendingRangedFollowUp(shooter))
				result += "\nMaster Gunner follow-up: "
					+ std::to_string(battle.battleGetRangedFollowUpDamagePercent(shooter))
					+ "% damage (the estimate above includes this reduction).";
			if(newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
				&& battle.battleGetOwner(shooter) != battle.battleGetOwner(targetStack))
			{
				if(targetStack->defended() && !shooter->isTurret()
					&& !shooter->hasBonusOfType(BonusType::SIEGE_WEAPON)
					&& shooter->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
					&& !shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
					&& newHorizonsCombatSkills::paviseReductionPercent(battle.battleGetOwnerHero(targetStack)) > 0)
					result += "\nPavise: 25% independent ranged physical reduction (included above; combined cap applies).";
				const auto side = static_cast<int32_t>(battle.playerToSide(battle.battleGetOwner(shooter)));
				const auto penetration = battle.battleGetRangedAttackPenetration(attackInfo);
				if(penetration.creatureDefenseIgnoreBasisPoints > 0)
					result += "\nArcane Breach: " + newHorizonsBattleStatus::formatBasisPoints(penetration.creatureDefenseIgnoreBasisPoints)
						+ " Creature Defense ignored (included above).";
				if(penetration.physicalDamageReductionIgnorePercent > 0)
					result += "\nArcane Ballistics: " + std::to_string(penetration.physicalDamageReductionIgnorePercent)
						+ "% of combined Physical Damage Reduction ignored (included above).";
				const auto focus = newHorizonsBattleStatus::focusMagicStatus(
					*shooter->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER));
				if(focus && focus->beneficiarySide == side)
					result += "\nFocus Magic: a damaging hit adds or refreshes a mark if the target survives.";
			}
			return result;
		}

		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		{
			const CSpell * spell = action.spell().toSpell();
			const auto & chainPreview = getChainLightningPreview();
			if(chainPreview.active && chainPreview.aimedHex == targetHex && chainPreview.spell == action.spell())
				return chainPreview.consoleText;

			if(isBlinkSpell(spell))
			{
				const auto preview = getBlinkDestinationPreview(spell, targetHex);
				if(preview)
				{
					const auto destinationCount = preview->legalDestinations.size();
					std::string message = spell->getNameTranslated() + ": " + targetStack->getName()
						+ " is randomly relocated within radius " + std::to_string(preview->radius)
						+ " (" + std::to_string(destinationCount) + " legal destination"
						+ (destinationCount == 1 ? ")" : "s)");
					if(destinationCount == 0)
						message += ". No legal destination; the cast is rejected before Mana or Hero Action is spent.";
					else
						message += ". Highlighted hexes show possible landings; the actual landing remains random.";

					if(preview->blinkmaster)
						message += " Blinkmaster automatically uses the farther of two random legal destinations; ties use hex order.";
					return message;
				}
			}

			auto spellEffectValue =
					owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);
			if(isHydrasVitalitySpell(spell))
			{
				spells::BattleCast cast(owner.getBattle().get(), getCurrentSpellcaster(), getCurrentCastMode(), spell);
				const auto * battle = owner.getBattle().get();
				const auto side = battle->battleGetMySide();
				const bool followup = side != BattleSide::NONE
					&& battle->battleCanUseMetamagicFollowup(side, spell->getId());
				cast.setMetamagicFollowup(followup);
				const auto mechanics = spell->battleMechanics(&cast);
				const auto capacityIncreasePercentMillionths = mechanics ? mechanics->getEffectValue() : 0;
				return prepareHydrasVitalityText(spell,
					targetStack ? targetStack->getName() : "", capacityIncreasePercentMillionths,
					mechanics ? mechanics->adjustEffectDuration(3) : 3);
			}
			if(isVerdantPrisonSpell(spell))
				return prepareVerdantPrisonText(spell, *spellEffectValue,
					targetStack ? targetStack->getName() : "", getVerdantPrisonTargetHexes(spell, targetHex).size());

			// "Cast %s on %s" plus dmg and kills info or how many units are risen/summoned
			return prepareSpellEffectText(27, *spellEffectValue, spell->getNameTranslated(), targetStack->getName());
		}

		case PossiblePlayerBattleAction::LIFE_DRAIN:
			return lifeDrainSelectedUnitIds.empty()
				? "Life Drain: choose a living enemy stack."
				: "Life Drain: choose a living friendly stack to receive healing.";

		case PossiblePlayerBattleAction::ANY_LOCATION:
		{
			const CSpell * spell = action.spell().toSpell();
			if(!spell)
				return {};

			auto spellEffectValue =
					owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);

			// "Cast %s" plus dmg and kills info
			if(isTransfigureMatterSpell(spell))
				return prepareTransfigureMatterText(spell, *spellEffectValue);
			if(isSummonTrollsSpell(spell))
				return prepareSummonTrollsText(spell, *spellEffectValue);
			return prepareSpellEffectText(26, *spellEffectValue, spell->getNameTranslated(), "");
		}

		case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
		{
			const CSpell * spell = getStackSpellToCast(targetHex);
			assert(spell);

			auto spellEffectValue =
					owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);

			// "Cast %s on %s" plus dmg and kills info
			return prepareSpellEffectText(27, *spellEffectValue, spell->getNameTranslated(), targetStack->getName());
		}

		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL: //we assume that teleport / sacrifice will never be available as random spell
			return formatWithStackName("core.genrltxt.301", targetStack); //Cast a spell on %s

		case PossiblePlayerBattleAction::TELEPORT:
		{
			if(!selectedStack) // Phase 1: hovering over unit to teleport
			{
				const CSpell * spell = action.spell().toSpell();
				if(!spell || !targetStack)
					return {};
				auto spellEffectValue = owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);
				return prepareSpellEffectText(27, *spellEffectValue, spell->getNameTranslated(), targetStack->getName());
			}
			return LIBRARY->generaltexth->allTexts[25]; //Teleport Here
		}

		case PossiblePlayerBattleAction::OBSTACLE:
		{
			const CSpell * spell = action.spell().toSpell();
			if(isTransfigureMatterSpell(spell))
			{
				auto spellEffectValue = owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);
				return prepareTransfigureMatterText(spell, *spellEffectValue);
			}
			return LIBRARY->generaltexth->allTexts[550];
		}

		case PossiblePlayerBattleAction::SACRIFICE:
		{
			const CSpell * spell = action.spell().toSpell();
			if(!spell)
				return {};

			auto spellEffectValue =
					owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);

			if(!selectedStack) // Phase 1: hovering over dead unit to resurrect
				return prepareSpellEffectText(27, *spellEffectValue, spell->getNameTranslated(), targetStack ? targetStack->getName() : "");

			//sacrifice the %s
			return prepareSpellEffectText(549, *spellEffectValue, "", targetStack->getName());
		}

		case PossiblePlayerBattleAction::FREE_LOCATION:
		{
			const CSpell * spell = action.spell().toSpell();
			if(isSummonTrollsSpell(spell))
			{
				auto spellEffectValue = owner.getBattle()->getSpellEffectValue(
					spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);
				return prepareSummonTrollsText(spell, *spellEffectValue);
			}

			MetaString text = MetaString::createFromTextID("core.genrltxt.26"); //Cast %s
			text.replaceName(action.spell());
			return text.toString(&GAME->translator());
		}

		case PossiblePlayerBattleAction::HEAL:
		{
			spells::effects::SpellEffectValue value = {};
			value.hpDelta = owner.getBattle()->getFirstAidHealValue(owner.currentHero(), targetStack);
			//Apply first aid to the %s plus heal value
			return prepareSpellEffectText(419, value, "", targetStack->getName());
		}

		case PossiblePlayerBattleAction::CATAPULT:
		{
			const auto * catapult = owner.stacksController->getActiveStack();
			const auto battle = owner.getBattle();
			if(!catapult || !catapult->isCatapult() || !battle)
				return {};

			const auto wallPart = battle->battleHexToWallPart(targetHex);
			if(!battle->isWallPartAttackable(wallPart))
				return {};

			const auto wallHp = std::max<int32_t>(0, battle->getWallStructuralHP(wallPart));
			if(wallHp <= 0)
				return {};

			const auto rawDamage = std::max<int32_t>(0,
				battle->battleGetCatapultStructuralDamage(catapult, 1));
			const auto predictedDamage = std::min(wallHp, rawDamage);
			return "Catapult on a normal hit: " + formatPlural(predictedDamage,
				"vcmi.battleWindow.damageEstimation.damage")
				+ " structural HP damage; wall currently has " + std::to_string(wallHp) + " HP.";
		}

		case PossiblePlayerBattleAction::CREATURE_INFO:
			return formatWithStackName("core.genrltxt.297", targetStack); //View %s info.

		case PossiblePlayerBattleAction::HERO_INFO:
			return  LIBRARY->generaltexth->translate("core.genrltxt.417"); // "View Hero Stats"

		case PossiblePlayerBattleAction::DEMONIC_GATE:
		{
			const auto * source = owner.stacksController->getActiveStack();
			if(!source)
				return "Open Gate";
			const auto * hero = source ? owner.getBattle()->battleGetFightingHero(source->unitSide()) : nullptr;
			if(hero && hero->hasActivePerk("new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate")
				&& !demonicGatingMovement.isValid())
				return "Move up to half Speed, then place the Gate.";
			const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(
				source->unitSide());
			const auto found = reserve.find(demonicGatingCreature);
			if(found == reserve.end())
				return "Open Gate";
			return "Open Gate for " + std::to_string(found->second) + " "
				+ (found->second == 1 ? demonicGatingCreature.toCreature()->getNameSingularTranslated()
					: demonicGatingCreature.toCreature()->getNamePluralTranslated());
		}
	}
	assert(0);
	return "";
}

std::string BattleActionsController::actionGetStatusMessageBlocked(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	invalidateChainLightningPreview();
	switch (action.get())
	{
		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
		case PossiblePlayerBattleAction::LIFE_DRAIN:
			return LIBRARY->generaltexth->allTexts[23];
			break;
		case PossiblePlayerBattleAction::TELEPORT:
			if(!selectedStack)
				return LIBRARY->generaltexth->allTexts[23];
			return LIBRARY->generaltexth->allTexts[24]; //Invalid Teleport Destination
			break;
		case PossiblePlayerBattleAction::SACRIFICE:
			if(!selectedStack)
				return LIBRARY->generaltexth->allTexts[23];
			return LIBRARY->generaltexth->allTexts[543]; //choose army to sacrifice
			break;
		case PossiblePlayerBattleAction::FREE_LOCATION:
		{
			MetaString text = MetaString::createFromTextID("core.genrltxt.181"); //No room to place %s here
			text.replaceName(action.spell());
			return text.toString(&GAME->translator());
		}
		case PossiblePlayerBattleAction::DEMONIC_GATE:
		{
			const auto * source = owner.stacksController->getActiveStack();
			const auto * hero = source ? owner.getBattle()->battleGetFightingHero(source->unitSide()) : nullptr;
			if(hero && hero->hasActivePerk("new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate")
				&& !demonicGatingMovement.isValid())
				return "Choose a reachable movement destination within half Speed.";
			return demonicGatingMovement.isValid()
				? "A Gate must be opened on an empty hex within range of the selected destination."
				: "A Gate must be opened on an empty hex within range.";
		}
		default:
			return "";
	}
}

bool BattleActionsController::actionIsLegal(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	const CStack * targetStack = getStackForHex(targetHex);
	bool targetStackOwned = targetStack && targetStack->unitOwner() == owner.curInt->playerID;

	switch (action.get())
	{
		case PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK:
			return (targetStack && targetStackOwned && targetStack->getMovementRange() > 0);

		case PossiblePlayerBattleAction::CREATURE_INFO:
			return (targetStack && targetStack->alive());

		case PossiblePlayerBattleAction::HERO_INFO:
			if (targetHex == BattleHex::HERO_ATTACKER)
				return owner.attackingHero != nullptr;

			if (targetHex == BattleHex::HERO_DEFENDER)
				return owner.defendingHero != nullptr;

			return false;

		case PossiblePlayerBattleAction::MOVE_TACTICS:
		case PossiblePlayerBattleAction::MOVE_STACK:
			if (!(targetStack && targetStack->alive())) //we can walk on dead stacks
			{
				const CStack * currentStack = owner.stacksController->getActiveStack();
				return currentStack && owner.getBattle()->toWhichHexMove(currentStack, targetHex).isValid();
			}
			return false;

		case PossiblePlayerBattleAction::DEMONIC_GATE:
		{
			const auto * source = owner.stacksController->getActiveStack();
			if(!source || !demonicGatingCreature.hasValue())
				return false;
			const auto * creature = demonicGatingCreature.toCreature();
			if(!creature)
				return false;
			const auto * hero = owner.getBattle()->battleGetFightingHero(source->unitSide());
			const bool mobileGate = hero && hero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate");
			if(mobileGate && !demonicGatingMovement.isValid())
			{
				if(targetHex == source->getPosition())
					return true;
				const auto movement = owner.getBattle()->toWhichHexMove(source, targetHex);
				if(!movement.isValid())
					return false;
				const auto [path, distance] = owner.getBattle()->getPath(source->getPosition(), movement, source);
				return !path.empty() && distance >= 0
					&& distance <= static_cast<int>(source->getMovementRange(0) / 2);
			}
			const int placementRange = hero && hero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
			if(!source || !creature)
				return false;
			const BattleHex sourcePosition = demonicGatingMovement.isValid()
				? demonicGatingMovement : source->getPosition();
			const BattleHex occupiedTail = source->doubleWide()
				? source->occupiedHex(sourcePosition) : BattleHex::INVALID;
			const BattleHex gatedTail = battle::Unit::occupiedHex(
				targetHex, creature->isDoubleWide(), source->unitSide());
			if(!targetHex.isAvailable()
				|| targetHex == sourcePosition || targetHex == occupiedTail
				|| (gatedTail.isValid() && (gatedTail == sourcePosition || gatedTail == occupiedTail))
				|| BattleHex::getDistance(sourcePosition, targetHex) > placementRange
				|| owner.getBattle()->battleGetUnitByPos(targetHex, true)
				|| !owner.getBattle()->battleGetAllObstaclesOnPos(targetHex, false).empty())
				return false;
			const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(source->unitSide());
			const auto found = reserve.find(demonicGatingCreature);
			return found != reserve.end() && found->second > 0
				&& owner.getBattle()->getAccessibility().accessible(targetHex, creature->isDoubleWide(), source->unitSide());
		}

		case PossiblePlayerBattleAction::SKIRMISHER_ATTACK:
		{
			const auto * active = owner.stacksController->getActiveStack();
			if(!active || !owner.getBattle())
				return false;
			if(!skirmisherTargetHex.isValid())
				return vstd::contains(getSkirmisherLegalTargetHexes(), targetHex);
			return vstd::contains(getSkirmisherLegalFiringHexes(), targetHex);
		}

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN:
			{
				const CStack * currentStack = owner.stacksController->getActiveStack();
				bool allowLongWeapon = action.get() == PossiblePlayerBattleAction::LONG_WEAPON_ATTACK;
				return currentStack &&
					owner.getBattle()->battleCanAttackUnitAction(currentStack, targetStack) &&
					owner.getBattle()->battleCanAttackHex(currentStack, targetHex) &&
					findAttackFromHex(owner, currentStack, targetHex, allowLongWeapon).isValid();
			}
		case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
			{
				const CStack * currentStack = owner.stacksController->getActiveStack();
				if (!currentStack || !targetStack)
					return false;

				if (targetStack == currentStack)
					return false;

				return owner.getBattle()->battleCanAttackHex(currentStack, targetHex) && isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);
			}
		case PossiblePlayerBattleAction::SHOOT:
			{
				auto currentStack = owner.stacksController->getActiveStack();
				if(!owner.getBattle()->battleCanShootAction(currentStack, targetHex))
					return false;

				if((targetStack == nullptr || targetStack->isInvincible()) && owner.getBattle()->battleCanTargetEmptyHex(currentStack))
				{
					auto spellLikeAttackBonus = currentStack->getBonus(Selector::type()(BonusType::SPELL_LIKE_ATTACK));
					const CSpell * spellDataToCheck = spellLikeAttackBonus->subtype.as<SpellID>().toSpell();
					return isCastingPossibleHere(spellDataToCheck, nullptr, targetHex);
				}

				return true;
			}

		case PossiblePlayerBattleAction::NO_LOCATION:
			return false;

		case PossiblePlayerBattleAction::ANY_LOCATION:
			if(isTransfigureMatterSpell(action.spell().toSpell()) && !isValidTransfigureMatterTarget(targetHex))
				return false;
			return isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);

		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
			return !selectedStack && targetStack && isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);

		case PossiblePlayerBattleAction::LIFE_DRAIN:
			return lifeDrainTargetHexIsLegal(targetHex);

		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
			if(targetStack && targetStackOwned && targetStack != owner.stacksController->getActiveStack() && targetStack->alive()) //only positive spells for other allied creatures
			{
				SpellID spellID = owner.getBattle()->getRandomBeneficialSpell(CRandomGenerator::getDefault(), owner.stacksController->getActiveStack(), targetStack);
				return spellID != SpellID::NONE;
			}
			return false;

		case PossiblePlayerBattleAction::TELEPORT:
			if(!selectedStack)
				return targetStack && isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);
			return isCastingPossibleHere(action.spell().toSpell(), selectedStack, targetHex);

		case PossiblePlayerBattleAction::SACRIFICE: //choose our living stack to sacrifice
		{
			if(!selectedStack)
				return targetStack && isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);

			if(!targetStack)
				return false;

			auto unit = targetStack->acquire();
			return targetStack != selectedStack && targetStackOwned && targetStack->alive()
					&& unit->isLiving() && !unit->hasBonusOfType(BonusType::MECHANICAL);
		}

		case PossiblePlayerBattleAction::OBSTACLE:
		case PossiblePlayerBattleAction::FREE_LOCATION:
			if(isTransfigureMatterSpell(action.spell().toSpell()) && !isValidTransfigureMatterTarget(targetHex))
				return false;
			return isCastingPossibleHere(action.spell().toSpell(), nullptr, targetHex);

		case PossiblePlayerBattleAction::CATAPULT:
			return owner.siegeController && owner.siegeController->isAttackableByCatapult(targetHex);

		case PossiblePlayerBattleAction::HEAL:
			return targetStack && targetStackOwned && targetStack->canBeHealed();
	}

	assert(0);
	return false;
}

void BattleActionsController::actionRealize(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	const CStack * targetStack = getStackForHex(targetHex);
	if(action.get() == PossiblePlayerBattleAction::SKIRMISHER_ATTACK)
	{
		const auto * attacker = owner.stacksController->getActiveStack();
		if(!attacker || !owner.getBattle())
			return;
		if(!skirmisherTargetHex.isValid())
		{
			if(vstd::contains(getSkirmisherLegalTargetHexes(), targetHex))
			{
				skirmisherTargetHex = targetHex;
				invalidateSkirmisherFiringCache();
			}
			ENGINE->fakeMouseMove();
			return;
		}
		const auto * selectedTarget = owner.getBattle()->battleGetStackByPos(skirmisherTargetHex);
		if(!selectedTarget || !vstd::contains(getSkirmisherLegalFiringHexes(), targetHex))
			return;
		BattleAction command = BattleAction::makeMeleeAttack(attacker, skirmisherTargetHex, targetHex, false);
		command.archerySkirmisherAttack = true;
		skirmisherTargetHex = BattleHex::INVALID;
		invalidateSkirmisherTargetCache();
		owner.sendCommand(command, attacker);
		return;
	}

	switch (action.get()) //display console message, realize selected action
	{
		case PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK:
		{
			owner.stackActivated(targetStack);
			return;
		}

		case PossiblePlayerBattleAction::DEMONIC_GATE:
		{
			const auto * active = owner.stacksController->getActiveStack();
			if(!active || !demonicGatingCreature.hasValue() || !demonicGatingCreature.toCreature())
				return;
			const auto * hero = owner.getBattle()->battleGetFightingHero(active->unitSide());
			const bool mobileGate = hero && hero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate");
			if(mobileGate && !demonicGatingMovement.isValid())
			{
				demonicGatingMovement = targetHex == active->getPosition()
					? active->getPosition() : owner.getBattle()->toWhichHexMove(active, targetHex);
				ENGINE->fakeMouseMove();
				return;
			}
			BattleAction command;
			command.actionType = EActionType::DEMONIC_GATING;
			command.side = active->unitSide();
			command.stackNumber = active->unitId();
			command.gatingCreature = demonicGatingCreature;
			if(demonicGatingMovement.isValid() && demonicGatingMovement != active->getPosition())
				command.aimToHex(demonicGatingMovement);
			command.aimToHex(targetHex);
			owner.sendCommand(command, active);
			return;
		}

		case PossiblePlayerBattleAction::MOVE_TACTICS:
		case PossiblePlayerBattleAction::MOVE_STACK:
		{
			const auto * activeStack = owner.stacksController->getActiveStack();
			auto toHex = owner.getBattle()->toWhichHexMove(activeStack, targetHex);
			assert(toHex.isValid());
			owner.giveCommand(EActionType::WALK, toHex);
			return;
		}

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN: //TODO: allow to disable return
		{
			bool returnAfterAttack = action.get() == PossiblePlayerBattleAction::ATTACK_AND_RETURN;
			bool allowLongWeapon = action.get() == PossiblePlayerBattleAction::LONG_WEAPON_ATTACK;
			auto attacker = owner.stacksController->getActiveStack();
			BattleHex attackFromHex = findAttackFromHex(owner, attacker, targetHex, allowLongWeapon);
			assert(attackFromHex.isValid());
			if(!attackFromHex.isValid())
				return;
			BattleAction command = BattleAction::makeMeleeAttack(attacker, targetHex, attackFromHex, returnAfterAttack);
			owner.sendCommand(command, attacker);
			return;
		}

		case PossiblePlayerBattleAction::SHOOT:
		{
			owner.giveCommand(EActionType::SHOOT, targetHex);
			return;
		}

		case PossiblePlayerBattleAction::HEAL:
		{
			owner.giveCommand(EActionType::STACK_HEAL, targetHex);
			return;
		};

		case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
		{
			auto stack = owner.stacksController->getActiveStack();
			BattleHex attackFromHex = owner.getBattle()->fromWhichHexAttack(stack, targetHex, owner.fieldController->selectAttackDirection(targetHex));
			if (attackFromHex.isValid())
			{
				BattleAction command = BattleAction::makeWalkAndCast(stack, attackFromHex, targetStack, getStackSpellToCast(targetHex)->id);
				owner.sendCommand(command, stack);
			}
			return;
		}

		case PossiblePlayerBattleAction::CATAPULT:
		{
			owner.giveCommand(EActionType::CATAPULT, targetHex);
			return;
		}

		case PossiblePlayerBattleAction::CREATURE_INFO:
		{
			ENGINE->windows().createAndPushWindow<CStackWindow>(targetStack, false);
			return;
		}

		case PossiblePlayerBattleAction::HERO_INFO:
		{
			if (targetHex == BattleHex::HERO_ATTACKER)
				owner.attackingHero->heroLeftClicked();

			if (targetHex == BattleHex::HERO_DEFENDER)
				owner.defendingHero->heroLeftClicked();

			return;
		}

		case PossiblePlayerBattleAction::LIFE_DRAIN:
			selectLifeDrainTarget(targetHex);
			return;

		case PossiblePlayerBattleAction::SACRIFICE:
		{
			if(!selectedStack)
			{
				// Phase 1: select dead unit to resurrect
				monsterCaster = owner.stacksController->getActiveStack();
				owner.windowObject->blockUI(true);
				owner.stacksController->deactivateStack();
				if(heroSpellToCast)
					heroSpellToCast->aimToHex(targetHex);
				else
					monsterSpellTargets.push_back(targetHex);
				selectedStack = targetStack;
				return;
			}
			[[fallthrough]];
		}
		case PossiblePlayerBattleAction::TELEPORT:
		{
			if(!selectedStack)
			{
				// Phase 1: select unit to teleport
				monsterCaster = owner.stacksController->getActiveStack();
				owner.windowObject->blockUI(true);
				owner.stacksController->deactivateStack();
				if(heroSpellToCast)
					heroSpellToCast->aimToUnit(targetStack);
				else
					monsterSpellTargets.push_back(targetHex);
				selectedStack = targetStack;
				return;
			}
			[[fallthrough]];
		}
		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::ANY_LOCATION:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL: //we assume that teleport / sacrifice will never be available as random spell
		case PossiblePlayerBattleAction::OBSTACLE:
		case PossiblePlayerBattleAction::FREE_LOCATION:
		{
			if(action.get() == PossiblePlayerBattleAction::ANY_LOCATION
				&& heroSpellToCast
				&& heroSpellToCast->spell == newHorizonsPurify::spellID()
				&& purifyPicker)
			{
				BattleAction pending = *heroSpellToCast;
				pending.target.clear();
				pending.aimToHex(targetHex);
				if(purifyPicker(pending, targetHex))
					return;
			}

			if(action.get() == PossiblePlayerBattleAction::AIMED_SPELL_CREATURE
				&& heroSpellToCast && targetStack && cureAfflictionPicker)
			{
				BattleAction pending = *heroSpellToCast;
				pending.target.clear();
				pending.aimToUnit(targetStack);
				if(cureAfflictionPicker(pending, targetStack))
					return;
			}

			if(action.get() == PossiblePlayerBattleAction::AIMED_SPELL_CREATURE
				&& heroSpellToCast
				&& heroSpellToCast->spell == SpellID(SpellID::DISPEL)
				&& targetStack
				&& selectiveDispelFactory)
			{
				BattleAction pending = *heroSpellToCast;
				pending.target.clear();
				pending.aimToUnit(targetStack);
				if(const auto context = selectiveDispelFactory(pending, targetStack))
				{
					ENGINE->windows().createAndPushWindow<SelectiveDispelWindow>(*context);
					return;
				}
			}

			// Shadow Gift's sacrifice is chosen only after the generic selector
			// accepts a friendly stack. Runtime installs this adapter only for
			// the saved v3 spell roster.
			if(action.get() == PossiblePlayerBattleAction::AIMED_SPELL_CREATURE
				&& heroSpellToCast
				&& isShadowGiftSpell(heroSpellToCast->spell.toSpell())
				&& shadowGiftFactory)
			{
				const BattleAction pending = *heroSpellToCast;
				if(const auto context = shadowGiftFactory(pending, targetHex, targetStack))
				{
					ENGINE->windows().createAndPushWindow<ShadowGiftWindow>(*context);
					return;
				}
			}

			// Magic Arrow's optional Overcharge is chosen only after its generic
			// target selector accepts a legal enemy stack.
			if(action.get() == PossiblePlayerBattleAction::AIMED_SPELL_CREATURE
				&& heroSpellToCast
				&& heroSpellToCast->spell == SpellID(SpellID::MAGIC_ARROW)
				&& magicArrowOverchargeFactory)
			{
				const BattleAction pending = *heroSpellToCast;
				if(const auto context = magicArrowOverchargeFactory(pending, targetHex, targetStack))
				{
					ENGINE->windows().createAndPushWindow<MagicArrowOverchargeWindow>(*context);
					return;
				}
			}

			if(action.get() == PossiblePlayerBattleAction::AIMED_SPELL_CREATURE)
			{
				monsterCaster = owner.stacksController->getActiveStack();
				owner.windowObject->blockUI(true);
				owner.stacksController->deactivateStack();
			}

			if (!heroSpellcastingModeActive())
			{
				if(monsterCaster)
					owner.stacksController->activateStack();

				if (action.spell().hasValue())
				{
					monsterSpellTargets.push_back(targetHex);
					owner.giveCommand(EActionType::MONSTER_SPELL, monsterSpellTargets, action.spell());
				}
				else //unknown random spell
				{
					monsterSpellTargets.push_back(targetHex);
					owner.giveCommand(EActionType::MONSTER_SPELL, monsterSpellTargets);
				}
				endCastingSpell();
			}
			else
			{
				assert(getHeroSpellToCast());
				if(action.get() == PossiblePlayerBattleAction::SACRIFICE)
					heroSpellToCast->aimToUnit(targetStack); //victim
				else
					heroSpellToCast->aimToHex(targetHex);
				if(submitHeroSpellAction(*heroSpellToCast))
					endCastingSpell();
			}
			selectedStack = nullptr;
			return;
		}
	}
	assert(0);
	return;
}

PossiblePlayerBattleAction BattleActionsController::selectAction(const BattleHex & targetHex)
{
	auto currentStack = monsterCaster ? monsterCaster : owner.stacksController->getActiveStack();
	assert(currentStack != nullptr);
	assert(!possibleActions.empty());
	assert(targetHex.isValid());

	if(currentStack == nullptr)
		return PossiblePlayerBattleAction::INVALID;

	if (possibleActions.empty())
		return PossiblePlayerBattleAction::INVALID;

	const CStack * targetStack = getStackForHex(targetHex);

	reorderPossibleActionsPriority(currentStack, targetStack);

	for (PossiblePlayerBattleAction action : possibleActions)
	{
		if (actionIsLegal(action, targetHex))
			return action;
	}
	return possibleActions.front();
}

void BattleActionsController::onHexHovered(const BattleHex & hoveredHex)
{
	if (owner.openingPlaying())
	{
		invalidateChainLightningPreview();
		currentConsoleMsg = LIBRARY->generaltexth->translate("vcmi.battleWindow.pressKeyToSkipIntro");
		ENGINE->statusbar()->write(currentConsoleMsg);
		return;
	}

	if(repeatedPlacementModeActive() || stormOfDaggersTargetSelectionModeActive()
		|| soulChainTargetSelectionModeActive() || lifeDrainTargetSelectionModeActive()
		|| fireWallPlacementModeActive() || vengefulVinesTargetSelectionModeActive()
		|| heroOrderTargetingModeActive())
		invalidateChainLightningPreview();

	if(repeatedPlacementModeActive())
	{
		if(hoveredHex == BattleHex::INVALID)
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		else if(repeatedPlacementHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);

		updateRepeatedPlacementStatus(hoveredHex);
		return;
	}

	if(stormOfDaggersTargetSelectionModeActive())
	{
		if(hoveredHex.isValid() && stormOfDaggersTargetHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateStormOfDaggersSelectionStatus(hoveredHex);
		return;
	}

	if(soulChainTargetSelectionModeActive())
	{
		if(hoveredHex.isValid() && soulChainTargetHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateSoulChainSelectionStatus(hoveredHex);
		return;
	}

	if(lifeDrainTargetSelectionModeActive())
	{
		if(hoveredHex.isValid() && lifeDrainTargetHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateLifeDrainSelectionStatus(hoveredHex);
		return;
	}

	if(fireWallPlacementModeActive())
	{
		if(hoveredHex == BattleHex::INVALID)
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		else if((!fireWallPlacementStartSelected() && fireWallPlacementStartIsLegal(hoveredHex))
			|| (fireWallPlacementStartSelected() && fireWallPlacementEndpointIsLegal(hoveredHex)))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);

		updateFireWallPlacementStatus(hoveredHex);
		return;
	}

	if(vengefulVinesTargetSelectionModeActive())
	{
		if(!vengefulVinesSelectionContextIsCurrent())
		{
			endCastingSpell();
			return;
		}

		if(vengefulVinesHexIsLegalCandidate(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);

		updateVengefulVinesStatus(hoveredHex);
		return;
	}

	if(heroOrderTargetingModeActive())
	{
		if(hoveredHex == BattleHex::INVALID)
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		else if(heroOrderTargetingHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateHeroOrderTargetingStatus(hoveredHex);
		return;
	}

	if (owner.stacksController->getActiveStack() == nullptr && monsterCaster == nullptr)
	{
		invalidateChainLightningPreview();
		return;
	}

	if (hoveredHex == BattleHex::INVALID)
	{
		invalidateChainLightningPreview();
		if (!currentConsoleMsg.empty())
			ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);

		currentConsoleMsg.clear();
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		return;
	}

	auto action = selectAction(hoveredHex);

	std::string newConsoleMsg;

	if (actionIsLegal(action, hoveredHex))
	{
		actionSetCursor(action, hoveredHex);
		updateChainLightningPreview(action, hoveredHex);
		newConsoleMsg = actionGetStatusMessage(action, hoveredHex);
	}
	else
	{
		invalidateChainLightningPreview();
		actionSetCursorBlocked(action, hoveredHex);
		newConsoleMsg = actionGetStatusMessageBlocked(action, hoveredHex);
	}

	if (owner.siegeController && owner.siegeController->isTowerHex(hoveredHex))
	{
		ENGINE->cursor().set(Cursor::Combat::QUERY); // question cursor over a siege tower
		newConsoleMsg = LIBRARY->generaltexth->translate("core.genrltxt.156"); // "View arrow tower info."
	}

	if (!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);

	if (!newConsoleMsg.empty())
		ENGINE->statusbar()->write(newConsoleMsg);

	currentConsoleMsg = newConsoleMsg;
}

void BattleActionsController::onHoverEnded()
{
	invalidateChainLightningPreview();
	if(repeatedPlacementModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateRepeatedPlacementStatus(BattleHex::INVALID);
		return;
	}

	if(stormOfDaggersTargetSelectionModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateStormOfDaggersSelectionStatus(BattleHex::INVALID);
		return;
	}

	if(soulChainTargetSelectionModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateSoulChainSelectionStatus(BattleHex::INVALID);
		return;
	}

	if(lifeDrainTargetSelectionModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateLifeDrainSelectionStatus(BattleHex::INVALID);
		return;
	}

	if(fireWallPlacementModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateFireWallPlacementStatus(BattleHex::INVALID);
		return;
	}

	if(vengefulVinesTargetSelectionModeActive())
	{
		if(!vengefulVinesSelectionContextIsCurrent())
		{
			endCastingSpell();
			return;
		}
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateVengefulVinesStatus(BattleHex::INVALID);
		return;
	}

	if(heroOrderTargetingModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateHeroOrderTargetingStatus(BattleHex::INVALID);
		return;
	}

	ENGINE->cursor().set(Cursor::Combat::POINTER);

	if (!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);

	currentConsoleMsg.clear();
}

void BattleActionsController::onHexLeftClicked(const BattleHex & clickedHex)
{
	if(repeatedPlacementModeActive())
	{
		selectOrUndoRepeatedPlacementHex(clickedHex);
		return;
	}

	if(stormOfDaggersTargetSelectionModeActive())
	{
		selectStormOfDaggersTarget(clickedHex);
		return;
	}

	if(soulChainTargetSelectionModeActive())
	{
		selectSoulChainTarget(clickedHex);
		return;
	}

	if(lifeDrainTargetSelectionModeActive())
	{
		selectLifeDrainTarget(clickedHex);
		return;
	}

	if(fireWallPlacementModeActive())
	{
		selectFireWallStartOrDirection(clickedHex);
		return;
	}

	if(vengefulVinesTargetSelectionModeActive())
	{
		selectVengefulVinesHex(clickedHex);
		return;
	}

	if(heroOrderTargetingModeActive())
	{
		selectHeroOrderTarget(clickedHex);
		return;
	}

	if (owner.stacksController->getActiveStack() == nullptr && monsterCaster == nullptr)
		return;

	auto action = selectAction(clickedHex);

	std::string newConsoleMsg;

	if (!actionIsLegal(action, clickedHex))
		return;
	
	actionRealize(action, clickedHex);
	ENGINE->statusbar()->clear();
}

void BattleActionsController::tryActivateStackSpellcasting(const CStack * casterStack)
{
	creatureSpells.clear();
	TConstBonusListPtr bl = casterStack->getBonusesOfType(BonusType::SPELLCASTER);

	if(casterStack->canCast() && !bl->empty())
	{
		// faerie dragon can cast only one, randomly selected spell until their next move
		//TODO: faerie dragon type spell should be selected by server
		const auto spellToCast = owner.getBattle()->getRandomCastedSpell(CRandomGenerator::getDefault(), casterStack);

		if(spellToCast.hasValue())
			creatureSpells.push_back(spellToCast.toSpell());
	}

	for(const auto & bonus : *bl)
	{
		if(!bonus->parameters && bonus->subtype.as<SpellID>().hasValue())
			creatureSpells.push_back(bonus->subtype.as<SpellID>().toSpell());
	}
}

const spells::Caster * BattleActionsController::getCurrentSpellcaster() const
{
	if (heroSpellToCast)
	{
		actionControllerCaster.reset();
		return owner.currentHero();
	}

	const CStack * creatureCaster = monsterCaster ? monsterCaster : owner.stacksController->getActiveStack();
	if(!creatureCaster)
	{
		actionControllerCaster.reset();
		return nullptr;
	}

	const auto battle = owner.getBattle();
	if(!battle)
	{
		actionControllerCaster.reset();
		return creatureCaster;
	}

	actionControllerCaster = std::make_unique<newHorizonsPuppetMaster::ActionControllerCaster>(
		creatureCaster, battle->battleGetActionController(creatureCaster));
	return actionControllerCaster.get();
}

spells::Mode BattleActionsController::getCurrentCastMode() const
{
	if(heroSpellToCast)
		return spells::Mode::HERO;
	else
		return spells::Mode::CREATURE_ACTIVE;
}

bool BattleActionsController::isCastingPossibleHere(const CSpell * currentSpell, const CStack *targetStack, const BattleHex & targetHex)
{
	assert(currentSpell);
	if (!currentSpell)
		return false;

	auto caster = getCurrentSpellcaster();

	const spells::Mode mode = heroSpellToCast ? spells::Mode::HERO : spells::Mode::CREATURE_ACTIVE;

	spells::Target target;
	if(targetStack)
		target.emplace_back(targetStack);
	target.emplace_back(targetHex);

	spells::BattleCast cast(owner.getBattle().get(), caster, mode, currentSpell);
	if(mode == spells::Mode::HERO)
	{
		const auto side = owner.getBattle()->battleGetMySide();
		const bool followup = side != BattleSide::NONE
			&& owner.getBattle()->battleCanUseMetamagicFollowup(side, currentSpell->getId());
		cast.setMetamagicFollowup(followup);
	}

	auto m = currentSpell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem; //todo: display problem in status bar
	if(m->canBeCastAt(target, problem))
		return true;

	// A physical affliction is selected after the stack, not before hovering it.
	// Try legal choices without mutating the pending action or battle state.
	const auto & rules = owner.getBattle()->getBattle()->getMagicRules();
	if(mode == spells::Mode::HERO && newHorizonsMagic::cureEnabled(rules, currentSpell->getId()))
	{
		const auto * cureTarget = targetStack ? targetStack : getStackForHex(targetHex);
		for(const auto affliction : newHorizonsMagic::cureAfflictions(rules, cureTarget))
		{
			cast.setCureAffliction(affliction);
			auto cureMechanics = currentSpell->battleMechanics(&cast);
			spells::detail::ProblemImpl cureProblem;
			if(cureMechanics->canBeCastAt(target, cureProblem))
				return true;
		}
		return false;
	}

	// The Selective Dispel perk expands the legal target set: basic ordinary
	// Dispel is smart-targeted, while selective mode explicitly supports both
	// friendly and enemy stacks. Accept either mode here so the post-target
	// chooser remains reachable; the selected mode is validated again before
	// the action is submitted and then authoritatively by the server.
	const auto * hero = mode == spells::Mode::HERO ? owner.currentHero() : nullptr;
	if(currentSpell->getId() != SpellID::DISPEL || !hero
		|| !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"))
		return false;

	spells::BattleCast selectiveCast(owner.getBattle().get(), caster, mode, currentSpell);
	if(mode == spells::Mode::HERO)
	{
		const auto side = owner.getBattle()->battleGetMySide();
		const bool followup = side != BattleSide::NONE
			&& owner.getBattle()->battleCanUseMetamagicFollowup(side, currentSpell->getId());
		selectiveCast.setMetamagicFollowup(followup);
	}
	selectiveCast.setSelectiveDispel(true);
	auto selectiveMechanics = currentSpell->battleMechanics(&selectiveCast);
	spells::detail::ProblemImpl selectiveProblem;
	return selectiveMechanics->canBeCastAt(target, selectiveProblem);
}

void BattleActionsController::activateStack()
{
	invalidateChainLightningPreview();
	if(vengefulVinesTargetSelectionModeActive() && !vengefulVinesSelectionContextIsCurrent())
		endCastingSpell();
	cancelHeroOrderTargeting();
	skirmisherTargetHex = BattleHex::INVALID;
	invalidateSkirmisherTargetCache();
	demonicGatingCreature = CreatureID();
	demonicGatingMovement = BattleHex::INVALID;
	const CStack * s = owner.stacksController->getActiveStack();
	if(s)
	{
		tryActivateStackSpellcasting(s);

		possibleActions = getPossibleActionsForStack(s);
		owner.windowObject->setPossibleActions(possibleActions);
	}
}

void BattleActionsController::onHexRightClicked(const BattleHex & clickedHex)
{
	if(skirmisherActionModeActive())
	{
		if(skirmisherTargetHex.isValid())
		{
			skirmisherTargetHex = BattleHex::INVALID;
			invalidateSkirmisherFiringCache();
		}
		else
			resetCurrentStackPossibleActions();
		ENGINE->fakeMouseMove();
		return;
	}
	if(heroOrderTargetingModeActive())
	{
		cancelHeroOrderTargeting();
		if(owner.getBattle()->battleHasPendingDoubleCommand(owner.getBattle()->battleGetMySide())
			|| owner.getBattle()->battleHasPendingPreCombatOrder(owner.getBattle()->battleGetMySide()))
		{
			owner.presentPendingHeroOrderChoice();
			return;
		}
		CRClickPopup::createAndPush("Order target selection cancelled.");
		return;
	}
	if(repeatedPlacementModeActive())
	{
		endCastingSpell();
		CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("core.genrltxt.731")); // spell cancelled
		return;
	}

	if(fireWallPlacementModeActive())
	{
		endCastingSpell();
		CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("core.genrltxt.731")); // spell cancelled
		return;
	}
	if(vengefulVinesTargetSelectionModeActive())
	{
		endCastingSpell();
		CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("core.genrltxt.731")); // spell cancelled
		return;
	}
	if(demonicGatingCreature.hasValue())
	{
		resetCurrentStackPossibleActions();
		CRClickPopup::createAndPush("Gate placement cancelled.");
		return;
	}

	bool isCurrentStackInSpellcastMode = creatureSpellcastingModeActive();

	if (heroSpellcastingModeActive() || isCurrentStackInSpellcastMode)
	{
		endCastingSpell();
		CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("core.genrltxt.731")); // spell cancelled
		return;
	}

	auto selectedStack = owner.getBattle()->battleGetStackByPos(clickedHex, true);

	if (selectedStack != nullptr)
		ENGINE->windows().createAndPushWindow<CStackWindow>(selectedStack, true);
	else if (owner.siegeController && owner.siegeController->isTowerHex(clickedHex))
		CRClickPopup::createAndPush(owner.siegeController->getTowersInfoText());

	if (clickedHex == BattleHex::HERO_ATTACKER && owner.attackingHero)
		owner.attackingHero->heroRightClicked();

	if (clickedHex == BattleHex::HERO_DEFENDER && owner.defendingHero)
		owner.defendingHero->heroRightClicked();
}

bool BattleActionsController::heroSpellcastingModeActive() const
{
	return heroSpellToCast != nullptr;
}

bool BattleActionsController::creatureSpellcastingModeActive() const
{
	auto spellcastModePredicate = [](const PossiblePlayerBattleAction & action)
	{
		return action.spellcast() || action.get() == PossiblePlayerBattleAction::SHOOT; //for hotkey-eligible SPELL_LIKE_ATTACK creature should have only SHOOT action
	};

	return !possibleActions.empty() && std::all_of(possibleActions.begin(), possibleActions.end(), spellcastModePredicate);
}

bool BattleActionsController::currentActionSpellcasting(const BattleHex & hoveredHex)
{
	if (heroSpellToCast)
		return true;

	if (!owner.stacksController->getActiveStack())
		return false;

	auto action = selectAction(hoveredHex);

	return action.spellcast();
}

bool BattleActionsController::currentActionWalkAndCast(const BattleHex & hoveredHex)
{
	if (heroSpellToCast)
		return false;

	if (!owner.stacksController->getActiveStack())
		return false;

	return selectAction(hoveredHex).get() == PossiblePlayerBattleAction::WALK_AND_SPELLCAST;
}

bool BattleActionsController::currentActionUsesLongWeapon(const BattleHex & hoveredHex)
{
	if (heroSpellToCast)
		return false;

	if (!owner.stacksController->getActiveStack())
		return true;

	return selectAction(hoveredHex).get() == PossiblePlayerBattleAction::LONG_WEAPON_ATTACK;
}

const std::vector<PossiblePlayerBattleAction> & BattleActionsController::getPossibleActions() const
{
	return possibleActions;
}

void BattleActionsController::setPriorityActions(const std::vector<PossiblePlayerBattleAction> & actions)
{
	invalidateChainLightningPreview();
	skirmisherTargetHex = BattleHex::INVALID;
	invalidateSkirmisherTargetCache();
	possibleActions = actions;
}

bool BattleActionsController::skirmisherActionModeActive() const
{
	return vstd::contains_if(possibleActions, [](const PossiblePlayerBattleAction & action)
		{ return action.get() == PossiblePlayerBattleAction::SKIRMISHER_ATTACK; });
}

const BattleHexArray & BattleActionsController::getSkirmisherLegalTargetHexes() const
{
	const auto * attacker = owner.stacksController->getActiveStack();
	static const BattleHexArray empty;
	if(!skirmisherActionModeActive() || skirmisherTargetHex.isValid() || !attacker || !owner.getBattle())
		return empty;
	if(!skirmisherTargetHexesCached)
	{
		skirmisherTargetHexesCache = owner.getBattle()->battleGetSkirmisherTargetHexes(attacker);
		skirmisherTargetHexesCached = true;
	}
	return skirmisherTargetHexesCache;
}

const BattleHexArray & BattleActionsController::getSkirmisherLegalFiringHexes() const
{
	const auto * attacker = owner.stacksController->getActiveStack();
	static const BattleHexArray empty;
	if(!skirmisherActionModeActive() || !skirmisherTargetHex.isValid() || !attacker || !owner.getBattle())
		return empty;
	if(!skirmisherFiringHexesCached)
	{
		skirmisherFiringHexesCache = owner.getBattle()->battleGetSkirmisherAttackFromHexes(
			attacker, skirmisherTargetHex, &skirmisherFiringDistancesCache);
		skirmisherFiringHexesCached = true;
	}
	return skirmisherFiringHexesCache;
}

void BattleActionsController::invalidateSkirmisherTargetCache()
{
	skirmisherTargetHexesCached = false;
	skirmisherTargetHexesCache.clear();
	invalidateSkirmisherFiringCache();
}

void BattleActionsController::invalidateSkirmisherFiringCache()
{
	skirmisherFiringHexesCached = false;
	skirmisherFiringHexesCache.clear();
	skirmisherFiringDistancesCache.fill(ReachabilityInfo::INFINITE_DIST);
}

void BattleActionsController::selectDemonicGatingCreature(CreatureID creature)
{
	if(!creature.hasValue() || !creature.toCreature())
		return;

	invalidateChainLightningPreview();
	demonicGatingCreature = creature;
	demonicGatingMovement = BattleHex::INVALID;
	possibleActions = {PossiblePlayerBattleAction::DEMONIC_GATE};
	ENGINE->fakeMouseMove();
}

void BattleActionsController::resetCurrentStackPossibleActions()
{
	invalidateChainLightningPreview();
	skirmisherTargetHex = BattleHex::INVALID;
	invalidateSkirmisherTargetCache();
	demonicGatingCreature = CreatureID();
	demonicGatingMovement = BattleHex::INVALID;
	possibleActions = getPossibleActionsForStack(owner.stacksController->getActiveStack());
}
