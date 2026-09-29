/*
 * StackInfoBasicPanel.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "StackInfoBasicPanel.h"

#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"

#include "NewHorizonsBattleStatus.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"

#include <algorithm>
#include <string_view>

namespace
{
constexpr std::string_view FRAILTY_SPELL_KEY = "new-horizons:frailty";
constexpr std::string_view PLAGUE_SPELL_KEY = "new-horizons:plague";

struct StackStatusEntry
{
	newHorizonsBattleStatus::StackStatusIconKind kind;
	std::optional<SpellID> spell;
};

struct FrailtyStatus
{
	int32_t accumulatedBasisPoints = 0;
	int64_t defenseLoss = 0;
	bool hasAccumulatedBasisPoints = false;
};

std::optional<FrailtyStatus> currentFrailtyStatus(const CStack * stack, std::string_view spellKey,
	const TConstBonusListPtr & spellBonuses)
{
	if(spellKey != FRAILTY_SPELL_KEY || !stack || !spellBonuses)
		return std::nullopt;

	FrailtyStatus result;
	bool hasFrailtyBonus = false;
	for(const auto & bonus : *spellBonuses)
	{
		if(!bonus || bonus->type != BonusType::PRIMARY_SKILL
			|| bonus->subtype != BonusSubtypeID(PrimarySkill::DEFENSE) || bonus->val > 0)
			continue;

		hasFrailtyBonus = true;
		result.defenseLoss += -static_cast<int64_t>(bonus->val);
		if(!bonus->parameters)
			continue;

		try
		{
			const auto basisPoints = bonus->parameters->toNumber();
			if(basisPoints < 0 || basisPoints > 6000)
				continue;

			result.accumulatedBasisPoints = std::max(result.accumulatedBasisPoints, basisPoints);
			result.hasAccumulatedBasisPoints = true;
		}
		catch(const std::exception &)
		{
			// Bonuses without the scalar addInfo still show their actual Defense penalty below.
		}
	}

	if(!hasFrailtyBonus)
		return std::nullopt;

	if(!result.hasAccumulatedBasisPoints)
	{
		const auto baseDefense = LIBRARY->creatures()->getByIndex(stack->creatureIndex())->getDefense(stack->isShooter());
		if(baseDefense > 0)
		{
			const auto basisPoints = (result.defenseLoss * 10000 + baseDefense / 2) / baseDefense;
			result.accumulatedBasisPoints = static_cast<int32_t>(std::clamp<int64_t>(basisPoints, 0, 6000));
			result.hasAccumulatedBasisPoints = true;
		}
	}

	return result;
}

std::string frailtyTooltip(std::string_view spellDescription, const FrailtyStatus & status)
{
	std::string result(spellDescription);
	if(status.hasAccumulatedBasisPoints)
		result += "\n\nAccumulated reduction: " + newHorizonsBattleStatus::formatBasisPoints(status.accumulatedBasisPoints)
			+ " of base Creature Defense.";
	if(status.defenseLoss > 0)
		result += "\nCurrent Creature Defense penalty: " + std::to_string(status.defenseLoss) + " points.";
	if(!status.hasAccumulatedBasisPoints)
		result += "\nThe current Defense penalty could not be normalized to base Creature Defense.";
	result += "\nBattle-long; there is no duration counter. Dispel removes the accumulated effect.";
	return result;
}

newHorizonsBattleStatus::DefendStatus currentDefendStatus(
	const CStack * stack, const CPlayerBattleCallback * battleCallback)
{
	newHorizonsBattleStatus::DefendStatus result;
	if(!stack || !stack->defended())
		return result;

	result.defending = true;
	if(!battleCallback || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack))
		return result;

	const auto ownerSide = battleCallback->playerToSide(battleCallback->battleGetOwner(stack));
	if(ownerSide != BattleSide::ATTACKER && ownerSide != BattleSide::DEFENDER)
		return result;

	// This player-scoped query hides the opposing hero's private skill data.
	const auto * hero = battleCallback->battleGetFightingHero(ownerSide);
	if(!hero)
		return result;

	const int rank = newHorizonsBulwark::rank(hero);
	if(rank <= 0)
		return result;

	const auto terrain = stack->getCurrentTerrain();
	const bool mirebornTerrain = newHorizonsBulwark::hasMireborn(hero)
		&& (terrain == TerrainId::SWAMP || terrain == TerrainId::ROUGH);
	bool sharedCoverApplies = false;
	if(newHorizonsBulwark::hasSharedCover(hero))
	{
		const auto adjacentUnits = battleCallback->battleAdjacentUnits(stack);
		const auto stackSide = stack->unitSide();
		sharedCoverApplies = std::any_of(adjacentUnits.begin(), adjacentUnits.end(), [stackSide](const auto * adjacent)
		{
			return adjacent->unitSide() == stackSide && adjacent->defended()
				&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(adjacent);
		});
	}
	result.bulwark = newHorizonsBattleStatus::makeBulwarkStatus(rank,
		hero->getPrimSkillLevel(PrimarySkill::DEFENSE), mirebornTerrain,
		newHorizonsBulwark::hasBogAmbush(hero), newHorizonsBulwark::hasThickHide(hero),
		stack->bulwarkPreemptiveUsed, sharedCoverApplies,
		newHorizonsBulwark::hasVengefulMire(hero));
	return result;
}

newHorizonsBattleStatus::StackInfoStatusSnapshot currentStackInfoStatus(
	const CStack * stack, const CPlayerBattleCallback * battleCallback)
{
	newHorizonsBattleStatus::StackInfoStatusSnapshot result;
	result.defend = currentDefendStatus(stack, battleCallback);
	if(stack)
	{
		result.physicalPoison = newHorizonsBattleStatus::makePhysicalPoisonStatus(
			stack->physicalPoisonBaseDamage,
			stack->physicalPoisonActivationsRemaining,
			newHorizonsBulwark::physicalPoisonTickDamage(stack));
		for(const auto effect : stack->activeSpells())
		{
			const auto * spell = effect.toSpell();
			if(!spell || !newHorizonsBattleStatus::isRegeneration(spell->getJsonKey()))
				continue;

			const auto spellBonuses = stack->getBonuses(
				Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(effect)));
			const auto remainingRounds = spellBonuses->empty()
				? 0 : std::max<int>(0, spellBonuses->front()->turnsRemain);
			result.regeneration = {
				stack->regenerationRateMillionths,
				stack->regenerationProjectedHeal(),
				remainingRounds};
			break;
		}
	}
	return result;
}

newHorizonsBattleStatus::StackStatusIconKind statusIconKind(SpellID effect)
{
	const auto spellKey = effect.toSpell()->getJsonKey();
	if(newHorizonsBattleStatus::isTimeStop(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::TIME_STOP;
	if(newHorizonsBattleStatus::isSpellLock(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::SPELL_LOCK;
	if(newHorizonsBattleStatus::isRegeneration(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::REGENERATION;
	if(newHorizonsBattleStatus::isFocusMagic(spellKey) || newHorizonsBattleStatus::isArcaneBreach(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::FOCUS_OR_ARCANE;
	return newHorizonsBattleStatus::StackStatusIconKind::ORDINARY;
}

std::string replaceStatusPlaceholder(std::string text, std::string_view token, const std::string & replacement)
{
	std::size_t position = 0;
	while((position = text.find(token, position)) != std::string::npos)
	{
		text.replace(position, token.size(), replacement);
		position += replacement.size();
	}
	return text;
}

std::string physicalPoisonTooltip(const newHorizonsBattleStatus::PhysicalPoisonStatus & status, std::size_t hiddenEffectCount)
{
	auto result = LIBRARY->generaltexth->translate("new-horizons.combat.physicalPoison.tooltip");
	result = replaceStatusPlaceholder(result, "%LABEL",
		LIBRARY->generaltexth->translate("new-horizons.combat.physicalPoison.label"));
	result = replaceStatusPlaceholder(result, "%DAMAGE", std::to_string(status.nextTickDamage));
	result = replaceStatusPlaceholder(result, "%ACTIVATIONS", std::to_string(status.activationsRemaining));
	if(hiddenEffectCount > 0)
	{
		auto hidden = LIBRARY->generaltexth->translate("new-horizons.combat.physicalPoison.hiddenEffects");
		hidden = replaceStatusPlaceholder(hidden, "%COUNT", std::to_string(hiddenEffectCount));
		result += "\n" + hidden;
	}
	return result;
}
}

StackInfoBasicPanel::StackInfoBasicPanel(
	const CStack * stack, std::shared_ptr<CPlayerBattleCallback> battleCallback, bool initializeBackground)
	: BattleSidePanel(0), battleCallback(std::move(battleCallback))
{
	OBJECT_CONSTRUCTION;

	if(initializeBackground)
	{
		background = std::make_shared<CPicture>(ImagePath::builtin("CCRPOP"), Rect(1, 1, 76, 286), 1, 1);
		background->pos.y += 37;
		background->setPlayerColor(stack->getOwner());
		background2 = std::make_shared<CPicture>(ImagePath::builtin("CHRPOP"), Rect(1, 1, 76, 200), 1, 1);
		background2->setPlayerColor(stack->getOwner());
	}

	initializeData(stack);
}

void StackInfoBasicPanel::initializeData(const CStack * stack)
{
	OBJECT_CONSTRUCTION;

	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("TWCRPORT"), stack->creatureId().getNum() + 2, 0, 10, 6));
	labels.push_back(std::make_shared<CLabel>(10 + 58, 6 + 64, FONT_MEDIUM, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, TextOperations::formatMetric(stack->getCount(), 4)));

	int damageMultiplier = 1;
	if (stack->hasBonusOfType(BonusType::SIEGE_WEAPON))
	{
		static const auto bonusSelector =
			Selector::sourceTypeSel(BonusSource::ARTIFACT).Or(
															  Selector::sourceTypeSel(BonusSource::HERO_BASE_SKILL)).And(
					Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)));

		damageMultiplier += stack->valOfBonuses(bonusSelector);
	}

	auto attack = std::to_string(LIBRARY->creatures()->getByIndex(stack->creatureIndex())->getAttack(stack->isShooter())) + "(" + std::to_string(stack->getAttack(stack->isShooter())) + ")";
	auto defense = std::to_string(LIBRARY->creatures()->getByIndex(stack->creatureIndex())->getDefense(stack->isShooter())) + "(" + std::to_string(stack->getDefense(stack->isShooter())) + ")";
	auto damage = std::to_string(damageMultiplier * stack->getMinDamage(stack->isShooter())) + "-" + std::to_string(damageMultiplier * stack->getMaxDamage(stack->isShooter()));
	auto health = stack->getMaxHealth();
	auto morale = stack->moraleVal();
	auto luck = stack->luckVal();

	auto killed = stack->getKilled();
	auto healthRemaining = TextOperations::formatMetric(stack->getFirstHPleft(), 4);
	if(stack->health.getTemporaryHitPoints() > 0)
		healthRemaining += "+" + TextOperations::formatMetric(stack->health.getTemporaryHitPoints(), 4);

	//primary stats*/
	labels.push_back(std::make_shared<CLabel>(9, 75, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[380] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 87, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[381] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 99, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[386] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 111, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[389] + ":"));

	labels.push_back(std::make_shared<CLabel>(69, 87, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, attack));
	labels.push_back(std::make_shared<CLabel>(69, 99, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, defense));
	labels.push_back(std::make_shared<CLabel>(69, 111, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, damage));
	labels.push_back(std::make_shared<CLabel>(69, 123, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(health)));

	//morale+luck
	labels.push_back(std::make_shared<CLabel>(9, 131, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[384] + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 143, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[385] + ":"));

	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("IMRL22"), std::clamp(morale + 3, 0, 6), 0, 47, 131));
	icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("ILCK22"), std::clamp(luck + 3, 0, 6), 0, 47, 143));

	displayedStatus = currentStackInfoStatus(stack, battleCallback.get());
	if(displayedStatus.defend.defending)
	{
		const auto badge = displayedStatus.defend.bulwark ? "BULWARK" : "DEFEND";
		const auto tooltip = newHorizonsBattleStatus::defendStatusTooltip(displayedStatus.defend);
		labels.push_back(std::make_shared<CLabel>(8, 155, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::YELLOW, badge));
		statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(7, 153, 39, 13), tooltip, tooltip));
	}

	//extra information
	labels.push_back(std::make_shared<CLabel>(9, 168, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.battleWindow.killed") + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 180, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[389] + ":"));

	labels.push_back(std::make_shared<CLabel>(69, 180, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(killed)));
	labels.push_back(std::make_shared<CLabel>(69, 192, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, healthRemaining));

	//spells
	static const Point firstPos(15, 206); // position of 1st spell box
	static const Point offset(0, 38);  // offset of each spell box from previous

	for(int i = 0; i < 3; i++)
		icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), 78, 0, firstPos.x + offset.x * i, firstPos.y + offset.y * i));

	std::vector<SpellID> spells = stack->activeSpells();
	std::vector<StackStatusEntry> statusEntries;
	std::vector<newHorizonsBattleStatus::StackStatusIconKind> statusKinds;
	for(const auto effect : spells)
	{
		//not all effects have graphics (for eg. Acid Breath)
		//for modded spells iconEffect is added to SpellInt.def
		const bool hasGraphics = (effect < SpellID::THUNDERBOLT) || (effect >= SpellID::AFTER_LAST);

		if(!hasGraphics)
			continue;
		const auto kind = statusIconKind(effect);
		statusEntries.push_back({kind, effect});
		statusKinds.push_back(kind);
	}

	const auto physicalPoison = displayedStatus.physicalPoison;
	if(physicalPoison.active())
	{
		statusEntries.push_back({newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON);
	}
	const auto totalEffectCount = spells.size() + (physicalPoison.active() ? 1 : 0);
	const auto displayPlan = newHorizonsBattleStatus::stackStatusDisplayPlan(statusKinds, totalEffectCount);
	int printed = 0;
	for(const auto entryIndex : displayPlan.visibleEntryIndices)
	{
		const auto & entry = statusEntries[entryIndex];
		const auto slotX = firstPos.x + offset.x * printed;
		const auto slotY = firstPos.y + offset.y * printed;
		if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON)
		{
			// Physical Poison is a saved stack condition, not a magical Poison spell.
			// Reuse the stock Poison frame without inserting a fake active-spell ID.
			const auto poisonIconFrame = SpellID(SpellID::POISON).getNum() + 1;
			icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), poisonIconFrame, 0, slotX, slotY));
			labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE,
				std::to_string(physicalPoison.activationsRemaining)));
			const auto hiddenEffectCount = displayPlan.overflow ? totalEffectCount - displayPlan.visibleEntryIndices.size() : 0;
			const auto tooltip = physicalPoisonTooltip(physicalPoison, hiddenEffectCount);
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
			++printed;
			continue;
		}

		const SpellID effect = *entry.spell;
		//FIXME: support permanent duration
		auto spellBonuses = stack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(effect)));
		if(spellBonuses->empty())
			throw std::runtime_error("Failed to find effects for spell " + effect.toSpell()->getJsonKey());

		int duration = spellBonuses->front()->turnsRemain;
		const auto spellKey = effect.toSpell()->getJsonKey();
		const bool timeStop = newHorizonsBattleStatus::isTimeStop(spellKey);
		const bool spellLock = newHorizonsBattleStatus::isSpellLock(spellKey);
		const bool focusMagic = newHorizonsBattleStatus::isFocusMagic(spellKey);
		const bool arcaneBreach = newHorizonsBattleStatus::isArcaneBreach(spellKey);
		const bool plague = spellKey == PLAGUE_SPELL_KEY;
		const auto frailty = currentFrailtyStatus(stack, spellKey, spellBonuses);
		const auto lockStatus = spellLock
			? newHorizonsBattleStatus::spellLockStatus(*spellBonuses)
			: std::optional<newHorizonsBattleStatus::SpellLockStatus>{};
		const auto arcaneStatus = arcaneBreach
			? newHorizonsBattleStatus::arcaneBreachStatus(*spellBonuses)
			: newHorizonsBattleStatus::ArcaneBreachStatus{};

		icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), effect.getNum() + 1, 0, slotX, slotY));
		if(settings["general"]["enableUiEnhancements"].Bool() || timeStop || spellLock || arcaneBreach || frailty || plague)
		{
			const std::string badge = timeStop
				? std::string(newHorizonsBattleStatus::TIME_STOP_BADGE)
				: spellLock ? std::string(newHorizonsBattleStatus::SPELL_LOCK_BADGE)
				: arcaneBreach ? std::to_string(arcaneStatus.markCount())
				: frailty ? (frailty->hasAccumulatedBasisPoints
					? newHorizonsBattleStatus::formatBasisPoints(frailty->accumulatedBasisPoints)
					: "-" + std::to_string(frailty->defenseLoss))
				: std::to_string(duration);
			labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, timeStop ? Colors::YELLOW : Colors::WHITE, badge));
		}

		if(timeStop)
		{
			const std::string tooltip = newHorizonsBattleStatus::timeStopTooltip(effect.toSpell()->getDescriptionTranslated(0));
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(spellLock)
		{
			const std::string tooltip = lockStatus
				? newHorizonsBattleStatus::spellLockTooltip(effect.toSpell()->getDescriptionTranslated(0), *lockStatus)
				: effect.toSpell()->getDescriptionTranslated(0);
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(newHorizonsBattleStatus::isRegeneration(spellKey))
		{
			const std::string tooltip = newHorizonsBattleStatus::regenerationTooltip(
				effect.toSpell()->getDescriptionTranslated(0), displayedStatus.regeneration);
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(focusMagic)
		{
			const auto tooltipStatus = newHorizonsBattleStatus::focusMagicStatus(*spellBonuses);
			const std::string tooltip = tooltipStatus
				? newHorizonsBattleStatus::focusMagicTooltip(effect.toSpell()->getDescriptionTranslated(0), *tooltipStatus)
				: effect.toSpell()->getDescriptionTranslated(0);
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(arcaneBreach)
		{
			const std::string tooltip = newHorizonsBattleStatus::arcaneBreachTooltip(arcaneStatus);
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(spellKey == FRAILTY_SPELL_KEY)
		{
			const std::string tooltip = frailty
				? frailtyTooltip(effect.toSpell()->getDescriptionTranslated(0), *frailty)
				: effect.toSpell()->getDescriptionTranslated(0)
					+ "\n\nBattle-long; there is no duration counter. Dispel removes the accumulated effect.";
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}
		else if(plague)
		{
			const std::string tooltip = effect.toSpell()->getDescriptionTranslated(0)
				+ "\n\n" + std::to_string(duration) + " rounds remaining. Plague damages this stack at the end of its turn, then may spread to an adjacent uninfected stack on either side.";
			statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		}

		++printed;
	}

	if(spells.empty() && !physicalPoison.active())
		labelsMultiline.push_back(std::make_shared<CMultiLineLabel>(Rect(firstPos.x, firstPos.y, 48, 36), EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[674]));
	if(displayPlan.ellipsisUsesSlot)
		labelsMultiline.push_back(std::make_shared<CMultiLineLabel>(Rect(firstPos.x + offset.x * 2, firstPos.y + offset.y * 2 - 4, 48, 36), EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "..."));
	else if(displayPlan.overflow && physicalPoison.active())
	{
		if(printed < 3)
			labelsMultiline.push_back(std::make_shared<CMultiLineLabel>(Rect(firstPos.x + offset.x * 2, firstPos.y + offset.y * 2 - 4, 48, 36), EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "..."));
		else
		{
			// The narrow gutter to the right of the third SpellInt slot keeps the overflow mark off the status art.
			labels.push_back(std::make_shared<CLabel>(66, firstPos.y + offset.y * 2 + 18, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "..."));
		}
	}
}

void StackInfoBasicPanel::update(const CStack * updatedInfo)
{
	icons.clear();
	labels.clear();
	labelsMultiline.clear();
	statusTooltips.clear();

	initializeData(updatedInfo);
	redraw();
}

void StackInfoBasicPanel::refreshDefendStatus(const CStack * updatedInfo)
{
	if(!updatedInfo)
		return;

	const auto current = currentStackInfoStatus(updatedInfo, battleCallback.get());
	if(current == displayedStatus)
		return;

	update(updatedInfo);
}

bool StackInfoBasicPanel::containsPoint(const Point & point) const
{
	return (background && background->pos.isInside(point))
		|| (background2 && background2->pos.isInside(point));
}
