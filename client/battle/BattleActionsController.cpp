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
#include "BattleSiegeController.h"
#include "BattleStacksController.h"
#include "BattleWindow.h"
#include "TemporalFieldWindow.h"

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
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/effects/Effect.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/texts/CGeneralTextHandler.h"

struct TextReplacement
{
	std::string placeholder;
	std::string replacement;
};

using TextReplacementList = std::vector<TextReplacement>;

constexpr std::string_view transfigureMatterJsonKey = "new-horizons:transfigureMatter";

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

int BattleActionsController::landMinePlacementRequiredHexes() const
{
	if(!landMinePlacementModeActive())
		return 0;

	const auto * spell = heroSpellToCast->spell.toSpell();
	const auto hero = owner.currentHero();
	spells::BattleCast cast(owner.getBattle().get(), hero, spells::Mode::HERO, spell);
	cast.setMetamagicFollowup(heroSpellToCast->metamagicFollowup);
	cast.setMetamagicGrand(heroSpellToCast->metamagicGrand);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return 0;

	return newHorizonsMagic::landMineHexCount(mechanics->getEffectPower());
}

bool BattleActionsController::landMinePlacementReady() const
{
	const int required = landMinePlacementRequiredHexes();
	return required > 0 && static_cast<int>(landMineSelectedHexes.size()) == required;
}

const std::vector<BattleHex> & BattleActionsController::landMinePlacementSelectedHexes() const
{
	return landMineSelectedHexes;
}

bool BattleActionsController::landMinePlacementHexIsLegal(const BattleHex & hex) const
{
	if(!landMinePlacementModeActive() || !owner.getBattle())
		return false;

	return canonicalLandMineHexIsEmpty(*owner.getBattle(), hex);
}

bool BattleActionsController::landMinePlacementHexIsSelected(const BattleHex & hex) const
{
	return std::find(landMineSelectedHexes.begin(), landMineSelectedHexes.end(), hex)
		!= landMineSelectedHexes.end();
}

BattleHexArray BattleActionsController::getLandMinePlacementLegalHexes() const
{
	BattleHexArray result;
	if(!landMinePlacementModeActive())
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(landMinePlacementHexIsLegal(hex))
			result.insert(hex);
	}
	return result;
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
	cast.setMetamagicGrand(heroSpellToCast->metamagicGrand);
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
		owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), action);
		endCastingSpell();
	}
}

void BattleActionsController::updateLandMinePlacementStatus(const BattleHex & hoveredHex)
{
	if(!landMinePlacementModeActive())
		return;

	const int required = landMinePlacementRequiredHexes();
	std::string message = "Land Mine " + std::to_string(landMineSelectedHexes.size())
		+ "/" + std::to_string(required) + ".";

	if(!landMineSelectedHexes.empty())
	{
		message += " ";
		for(size_t index = 0; index < landMineSelectedHexes.size(); ++index)
		{
			if(index != 0)
				message += " ";
			message += "#" + std::to_string(index + 1) + "="
				+ std::to_string(landMineSelectedHexes[index].toInt());
		}
	}

	if(hoveredHex.isValid())
	{
		const bool selected = std::find(landMineSelectedHexes.begin(), landMineSelectedHexes.end(), hoveredHex)
			!= landMineSelectedHexes.end();
		if(selected)
		{
			if(landMinePlacementHexIsLegal(hoveredHex))
				message += " Click selected hex to undo.";
			else
				message += " Selection changed; undo it.";
		}
		else if(!landMinePlacementHexIsLegal(hoveredHex))
		{
			message += " Hex unavailable.";
		}
		else if(landMinePlacementReady())
		{
			message += " Click Place Mines or press Enter.";
		}
		else
		{
			message += " Click empty hex.";
		}
	}
	else if(landMinePlacementReady())
	{
		message += " Click Place Mines or press Enter.";
	}
	else
	{
		message += " Select empty hexes. Backspace undo; Esc cancels.";
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

void BattleActionsController::selectOrUndoLandMineHex(const BattleHex & clickedHex)
{
	if(!landMinePlacementModeActive())
		return;

	const auto selected = std::find(landMineSelectedHexes.begin(), landMineSelectedHexes.end(), clickedHex);
	if(selected != landMineSelectedHexes.end())
	{
		landMineSelectedHexes.erase(selected);
	}
	else if(landMinePlacementHexIsLegal(clickedHex)
		&& static_cast<int>(landMineSelectedHexes.size()) < landMinePlacementRequiredHexes())
	{
		landMineSelectedHexes.push_back(clickedHex);
	}

	if(owner.windowObject)
		owner.windowObject->updateLandMinePlacementControls();
	updateLandMinePlacementStatus(clickedHex);
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::landMinePlacementTargetsValid() const
{
	if(!landMinePlacementReady() || !owner.getBattle() || !owner.currentHero())
		return false;

	std::set<int> selected;
	for(const auto & hex : landMineSelectedHexes)
	{
		if(!selected.insert(hex.toInt()).second || !landMinePlacementHexIsLegal(hex))
			return false;
	}

	const auto * spell = heroSpellToCast->spell.toSpell();
	spells::BattleCast cast(owner.getBattle().get(), owner.currentHero(), spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	if(!mechanics)
		return false;

	spells::detail::ProblemImpl problem;
	if(!mechanics->canBeCast(problem))
		return false;

	battle::Target target;
	for(const auto & hex : landMineSelectedHexes)
		target.emplace_back(hex);
	return mechanics->canBeCastAt(target, problem);
}

void BattleActionsController::confirmLandMinePlacement()
{
	if(!landMinePlacementModeActive())
		return;

	if(!landMinePlacementReady())
	{
		updateLandMinePlacementStatus(BattleHex::INVALID);
		return;
	}

	// The battlefield may have changed while the player was choosing.  Re-run
	// the same live-snapshot checks as the generic mechanics before creating a
	// request; the server remains the final authority on this packet.
	if(!landMinePlacementTargetsValid())
	{
		updateLandMinePlacementStatus(BattleHex::INVALID);
		currentConsoleMsg += ". Selection is no longer legal; undo or cancel.";
		ENGINE->statusbar()->write(currentConsoleMsg);
		return;
	}

	BattleAction action = *heroSpellToCast;
	action.target.clear();
	for(const auto & hex : landMineSelectedHexes)
		action.aimToHex(hex);

	if(!owner.curInt || !owner.curInt->cb)
		return;
	owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), action);
	endCastingSpell();
}

void BattleActionsController::undoLandMinePlacement()
{
	if(!landMinePlacementModeActive() || landMineSelectedHexes.empty())
		return;

	landMineSelectedHexes.pop_back();
	if(owner.windowObject)
		owner.windowObject->updateLandMinePlacementControls();
	ENGINE->fakeMouseMove();
	ENGINE->windows().totalRedraw();
}

bool BattleActionsController::isTransfigureMatterSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == transfigureMatterJsonKey;
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

void BattleActionsController::setMagicArrowOverchargeFactory(MagicArrowOverchargeFactory factory)
{
	magicArrowOverchargeFactory = std::move(factory);
}

void BattleActionsController::setSelectiveDispelFactory(SelectiveDispelFactory factory)
{
	selectiveDispelFactory = std::move(factory);
}

void BattleActionsController::setTemporalFieldFactory(TemporalFieldFactory factory)
{
	temporalFieldFactory = std::move(factory);
}

void BattleActionsController::endCastingSpell()
{
	// The battle's Escape shortcut also reaches this method outside spell mode.
	owner.clearPerfectMoment();
	cancelHeroOrderTargeting();
	const bool wasLandMinePlacement = landMinePlacementModeActive();
	const bool wasFireWallPlacement = fireWallPlacementModeActive();
	const bool wasMetamagicFollowup = metamagicFollowupMode;
	if(heroSpellToCast)
	{
		heroSpellToCast.reset();
		owner.windowObject->blockUI(false);
	}
	if(wasMetamagicFollowup)
	{
		metamagicFollowupMode = false;
		metamagicGrandMode = false;
	}

	if(monsterCaster)
	{
		monsterCaster = nullptr;
		owner.stacksController->activateStack();
	}
	monsterSpellTargets.clear();
	landMineSelectedHexes.clear();
	fireWallSelectedStart = BattleHex::INVALID;
	if((wasLandMinePlacement || wasFireWallPlacement) && !currentConsoleMsg.empty())
	{
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
		currentConsoleMsg.clear();
	}

	if(owner.stacksController->getActiveStack())
	{
		possibleActions = getPossibleActionsForStack(owner.stacksController->getActiveStack()); //restore actions after they were cleared
		owner.windowObject->setPossibleActions(possibleActions);
	}

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

		const spells::Caster * caster = owner.stacksController->getActiveStack();
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
				if(targetStack == nullptr || targetStack->unitSide() == stack->unitSide() || !targetStack->alive())
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
	cancelHeroOrderTargeting();
	if(!owner.curInt)
		return;
	const auto * castingHero = owner.currentHero();
	if(!castingHero)
		return;

	heroSpellToCast = std::make_shared<BattleAction>();
	heroSpellToCast->actionType = EActionType::HERO_SPELL;
	heroSpellToCast->spell = spellID;
	heroSpellToCast->stackNumber = -1;
	heroSpellToCast->side = owner.curInt->cb->getBattle(owner.getBattleID())->battleGetMySide();
	heroSpellToCast->metamagicFollowup = metamagicFollowupMode;
	heroSpellToCast->metamagicGrand = metamagicGrandMode;

	// Canonical New Horizons Land Mine is an ordered multi-hex action.  It must
	// not enter the generic NO_TARGET path, which would immediately submit the
	// legacy/random obstacle action.  Leave all other spells on the existing
	// selector unchanged.
	if(landMinePlacementModeActive())
	{
		landMineSelectedHexes.clear();
		possibleActions.clear();
		owner.windowObject->blockUI(true);
		if(owner.windowObject)
			owner.windowObject->updateLandMinePlacementControls();
		updateLandMinePlacementStatus(BattleHex::INVALID);
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
		owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), *heroSpellToCast);
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

void BattleActionsController::beginMetamagicFollowup()
{
	if(!owner.curInt || !owner.currentHero())
		return;
	const auto side = owner.getBattle()->battleGetMySide();
	if(!owner.getBattle()->battleCanUseMetamagicFollowup(side))
		return;
	metamagicFollowupMode = true;
	metamagicGrandMode = false;
}

bool BattleActionsController::metamagicFollowupModeActive() const
{
	return metamagicFollowupMode;
}

void BattleActionsController::toggleMetamagicGrandFollowup()
{
	if(!metamagicFollowupMode || !owner.curInt || !owner.currentHero())
		return;
	const auto side = owner.getBattle()->battleGetMySide();
	const bool available = side != BattleSide::NONE
		&& owner.getBattle()->battleMetamagicPendingCount(side) == 1
		&& owner.getBattle()->battleMetamagicSequenceSpells(side).size() == 1
		&& !owner.getBattle()->battleMetamagicGrandUsed(side)
		&& newHorizonsMagic::metamagicRank(owner.currentHero()) >= 3
		&& newHorizonsMagic::hasMetamagicPerk(owner.currentHero(), newHorizonsMagic::METAMAGIC_GRAND);
	if(!available)
		return;
	metamagicGrandMode = !metamagicGrandMode;
	if(owner.windowObject)
		owner.windowObject->updateCounterspellStatus();
}

bool BattleActionsController::metamagicGrandModeActive() const
{
	return metamagicGrandMode;
}

bool BattleActionsController::continueOrdinarySpellcast()
{
	if(!owner.curInt || !heroSpellToCast)
		return false;

	const auto * castingHero = owner.currentHero();
	const auto * spell = heroSpellToCast->spell.toSpell();
	if(!castingHero || !spell)
		return false;

	const auto spellSelMode = owner.getBattle()->getCasterAction(spell, castingHero, spells::Mode::HERO);
	if(spellSelMode.get() == PossiblePlayerBattleAction::INVALID)
		return false;

	if(spellSelMode.get() == PossiblePlayerBattleAction::NO_LOCATION)
	{
		heroSpellToCast->aimToHex(BattleHex::INVALID);
		owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), *heroSpellToCast);
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

const CStack * BattleActionsController::getStackForHex(const BattleHex & hoveredHex)
{
	const CStack * shere = owner.getBattle()->battleGetStackByPos(hoveredHex, true);
	if(shere)
		return shere;
	return owner.getBattle()->battleGetStackByPos(hoveredHex, false);
}

void BattleActionsController::actionSetCursor(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
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

				return formatMeleeAttack(estimation, targetStack->getName()) + "\n" + formatRetaliation(retaliation, enemyMayBeKilled);
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
			return formatRangedAttack(estimation, targetStack->getName(), shooter->shots.available());
		}

		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		{
			const CSpell * spell = action.spell().toSpell();

			auto spellEffectValue =
					owner.getBattle()->getSpellEffectValue(spell, getCurrentSpellcaster(), getCurrentCastMode(), targetHex);

			// "Cast %s on %s" plus dmg and kills info or how many units are risen/summoned
			return prepareSpellEffectText(27, *spellEffectValue, spell->getNameTranslated(), targetStack->getName());
		}

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
			return ""; // TODO

		case PossiblePlayerBattleAction::CREATURE_INFO:
			return formatWithStackName("core.genrltxt.297", targetStack); //View %s info.

		case PossiblePlayerBattleAction::HERO_INFO:
			return  LIBRARY->generaltexth->translate("core.genrltxt.417"); // "View Hero Stats"

		case PossiblePlayerBattleAction::DEMONIC_GATE:
		{
			const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(
				owner.stacksController->getActiveStack()->unitSide());
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
	switch (action.get())
	{
		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
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
			return "A Gate must be opened on an empty hex within range.";
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
			const auto * creature = demonicGatingCreature.toCreature();
			if(!source || !creature || !targetHex.isAvailable()
				|| BattleHex::getDistance(source->getPosition(), targetHex) > 3
				|| owner.getBattle()->battleGetUnitByPos(targetHex, true)
				|| !owner.getBattle()->battleGetAllObstaclesOnPos(targetHex, false).empty())
				return false;
			const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(source->unitSide());
			const auto found = reserve.find(demonicGatingCreature);
			return found != reserve.end() && found->second > 0
				&& owner.getBattle()->getAccessibility().accessible(targetHex, creature->isDoubleWide(), source->unitSide());
		}

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN:
			{
				const CStack * currentStack = owner.stacksController->getActiveStack();
				bool allowLongWeapon = action.get() == PossiblePlayerBattleAction::LONG_WEAPON_ATTACK;
				return currentStack &&
					owner.getBattle()->battleCanAttackUnit(currentStack, targetStack) &&
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
				if(!owner.getBattle()->battleCanShoot(currentStack, targetHex))
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
			BattleAction command;
			command.actionType = EActionType::DEMONIC_GATING;
			command.side = active->unitSide();
			command.stackNumber = active->unitId();
			command.gatingCreature = demonicGatingCreature;
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

			// Magic Arrow is the one New Horizons spell whose optional cost is
			// chosen only after the generic target selector has accepted a legal
			// enemy stack.  Runtime supplies the adapter only for the saved V2
			// ruleset; all legacy games and every other spell follow the existing
			// request path unchanged.
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
				owner.curInt->cb->battleMakeSpellAction(owner.getBattleID(), *heroSpellToCast);
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
		currentConsoleMsg = LIBRARY->generaltexth->translate("vcmi.battleWindow.pressKeyToSkipIntro");
		ENGINE->statusbar()->write(currentConsoleMsg);
		return;
	}

	if(landMinePlacementModeActive())
	{
		if(hoveredHex == BattleHex::INVALID)
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		else if(landMinePlacementHexIsLegal(hoveredHex))
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
		else
			ENGINE->cursor().set(Cursor::Combat::BLOCKED);

		updateLandMinePlacementStatus(hoveredHex);
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
		return;

	if (hoveredHex == BattleHex::INVALID)
	{
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
		newConsoleMsg = actionGetStatusMessage(action, hoveredHex);
	}
	else
	{
		actionSetCursorBlocked(action, hoveredHex);
		newConsoleMsg = actionGetStatusMessageBlocked(action, hoveredHex);
	}

	if (owner.siegeController && owner.siegeController->isTowerHex(hoveredHex))
	{
		ENGINE->cursor().set(Cursor::Combat::QUERY); // question cursor over a siege tower
		newConsoleMsg = LIBRARY->generaltexth->translate("core.genrltxt.156"); // "View arrow tower info."
	}

	if(owner.isPerfectMomentArmed())
		newConsoleMsg = "Perfect Moment armed: next attack. Esc/right-click cancels. " + newConsoleMsg;
	if (!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);

	if (!newConsoleMsg.empty())
		ENGINE->statusbar()->write(newConsoleMsg);

	currentConsoleMsg = newConsoleMsg;
}

void BattleActionsController::onHoverEnded()
{
	if(landMinePlacementModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateLandMinePlacementStatus(BattleHex::INVALID);
		return;
	}

	if(fireWallPlacementModeActive())
	{
		ENGINE->cursor().set(Cursor::Combat::BLOCKED);
		updateFireWallPlacementStatus(BattleHex::INVALID);
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
	if(landMinePlacementModeActive())
	{
		selectOrUndoLandMineHex(clickedHex);
		return;
	}

	if(fireWallPlacementModeActive())
	{
		selectFireWallStartOrDirection(clickedHex);
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
		return owner.currentHero();
	else if(monsterCaster)
		return monsterCaster;
	else
		return owner.stacksController->getActiveStack();
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
		const bool followup = metamagicFollowupMode
			|| owner.getBattle()->battleCanUseMetamagicFollowup(owner.getBattle()->battleGetMySide());
		cast.setMetamagicFollowup(followup);
		cast.setMetamagicGrand(metamagicGrandMode);
	}

	auto m = currentSpell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem; //todo: display problem in status bar
	if(m->canBeCastAt(target, problem))
		return true;

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
		const bool followup = metamagicFollowupMode
			|| owner.getBattle()->battleCanUseMetamagicFollowup(owner.getBattle()->battleGetMySide());
		selectiveCast.setMetamagicFollowup(followup);
		selectiveCast.setMetamagicGrand(metamagicGrandMode);
	}
	selectiveCast.setSelectiveDispel(true);
	auto selectiveMechanics = currentSpell->battleMechanics(&selectiveCast);
	spells::detail::ProblemImpl selectiveProblem;
	return selectiveMechanics->canBeCastAt(target, selectiveProblem);
}

void BattleActionsController::activateStack()
{
	cancelHeroOrderTargeting();
	demonicGatingCreature = CreatureID();
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
	owner.clearPerfectMoment();
	if(metamagicFollowupModeActive())
	{
		owner.declineMetamagicFollowup();
		CRClickPopup::createAndPush(LIBRARY->generaltexth->translate("core.genrltxt.731")); // spell cancelled
		return;
	}
	if(heroOrderTargetingModeActive())
	{
		cancelHeroOrderTargeting();
		CRClickPopup::createAndPush("Order target selection cancelled.");
		return;
	}
	if(landMinePlacementModeActive())
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
	possibleActions = actions;
}

void BattleActionsController::selectDemonicGatingCreature(CreatureID creature)
{
	demonicGatingCreature = creature;
	possibleActions = {PossiblePlayerBattleAction::DEMONIC_GATE};
	ENGINE->fakeMouseMove();
}

void BattleActionsController::resetCurrentStackPossibleActions()
{
	demonicGatingCreature = CreatureID();
	possibleActions = getPossibleActionsForStack(owner.stacksController->getActiveStack());
}
