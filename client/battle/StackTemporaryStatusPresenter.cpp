/*
 * StackTemporaryStatusPresenter.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "StackTemporaryStatusPresenter.h"
#include "ArcaneMarkPresentation.h"
#include "FrozenStatusPresentation.h"
#include "StackSpellStatusPresentation.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"

namespace
{
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

StackTemporaryStatusPresenter::StackTemporaryStatusPresenter(const CStack * stack,
	const newHorizonsBattleStatus::StackInfoStatusSnapshot & status, const CBattleInfoCallback * battle)
	: stack(stack), status(status), inspiration(arcaneInspirationPresentation::forStack(*stack, battle)),
	attunement(elementalAttunementPresentation::forUnit(*stack))
{
	const auto spells = stack->activeSpells();

	const auto arcaneMarks = arcaneMarkPresentation::forStack(*stack);

	std::vector<newHorizonsBattleStatus::StackStatusIconKind> statusKinds;
	std::size_t hiddenReanimateSpellEffects = 0;
	std::size_t hiddenJudgedSpellEffects = 0;
	std::size_t hiddenConfusionSpellEffects = 0;
	for(const auto effect : spells)
	{
		//not all effects have graphics (for eg. Acid Breath)
		//for modded spells iconEffect is added to SpellInt.def
		const bool hasGraphics = (effect < SpellID::THUNDERBOLT) || (effect >= SpellID::AFTER_LAST);

		if(!hasGraphics)
			continue;
		const auto * spell = effect.toSpell();
		if(spell && newHorizonsBattleStatus::isConfusion(spell->getJsonKey())
			&& !status.confusion.active())
		{
			++hiddenConfusionSpellEffects;
			continue;
		}
		if(spell && newHorizonsBattleStatus::isReanimate(spell->getJsonKey()))
		{
			// One-battle resurrected creatures are shown once as a generic temporary
			// count below, rather than as both a spell and a creature-status icon.
			++hiddenReanimateSpellEffects;
			continue;
		}
		if(spell && newHorizonsBattleStatus::isDivineRetribution(spell->getJsonKey())
			&& !status.divineRetribution.active())
		{
			// Judged carries the spell source ID, but is not protection on this stack.
			++hiddenJudgedSpellEffects;
			continue;
		}
		const auto kind = newHorizonsStackSpellPresentation::statusIconKind(effect);
		entries.push_back({kind, effect});
		statusKinds.push_back(kind);
	}

	const auto physicalPoison = status.physicalPoison;
	if(status.frozen)
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::FROZEN, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::FROZEN);
	}
	if(physicalPoison.active())
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON);
	}
	if(status.shadowGift.hasMaximumHealthLoss())
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::SHADOW_GIFT_CAP, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::SHADOW_GIFT_CAP);
	}
	const auto retributionJudged = status.divineRetributionJudged;
	if(retributionJudged.active())
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::DIVINE_RETRIBUTION_JUDGED, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::DIVINE_RETRIBUTION_JUDGED);
	}
	const auto temporaryCreatures = status.temporaryCreatures;
	if(temporaryCreatures.active())
	{
		const Entry temporaryEntry{
			newHorizonsBattleStatus::StackStatusIconKind::TEMPORARY_CREATURES, std::nullopt};
		entries.insert(entries.begin(), temporaryEntry);
		statusKinds.insert(statusKinds.begin(), newHorizonsBattleStatus::StackStatusIconKind::TEMPORARY_CREATURES);
	}
	const auto battleForm = status.battleForm;
	if(battleForm.active())
	{
		const Entry battleFormEntry{
			newHorizonsBattleStatus::StackStatusIconKind::BATTLE_FORM, std::nullopt};
		entries.insert(entries.begin(), battleFormEntry);
		statusKinds.insert(statusKinds.begin(), newHorizonsBattleStatus::StackStatusIconKind::BATTLE_FORM);
	}
	if(!arcaneMarks.empty())
	{
		// A display priority category only: these counters remain independent
		// of Focus Magic's separate Arcane Breach state and three-mark cap.
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::FOCUS_OR_ARCANE, std::nullopt, true});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::FOCUS_OR_ARCANE);
	}
	if(inspiration.active())
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::ORDINARY, std::nullopt, false, true});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::ORDINARY);
	}
	if(attunement.attuned)
	{
		entries.push_back({newHorizonsBattleStatus::StackStatusIconKind::ELEMENTAL_ATTUNEMENT, std::nullopt});
		statusKinds.push_back(newHorizonsBattleStatus::StackStatusIconKind::ELEMENTAL_ATTUNEMENT);
	}

	totalEffectCount = spells.size() - hiddenReanimateSpellEffects - hiddenJudgedSpellEffects - hiddenConfusionSpellEffects
		+ (temporaryCreatures.active() ? 1 : 0) + (physicalPoison.active() ? 1 : 0)
		+ (status.frozen ? 1 : 0)
		+ (status.shadowGift.hasMaximumHealthLoss() ? 1 : 0)
		+ (retributionJudged.active() ? 1 : 0) + (battleForm.active() ? 1 : 0)
		+ (!arcaneMarks.empty() ? 1 : 0) + (inspiration.active() ? 1 : 0) + (attunement.attuned ? 1 : 0);
	displayPlan = newHorizonsBattleStatus::stackStatusDisplayPlan(statusKinds, totalEffectCount);
	empty = totalEffectCount == 0;
}

bool StackTemporaryStatusPresenter::hasPriorityStatus() const
{
	return status.physicalPoison.active() || status.frozen
		|| !arcaneMarkPresentation::forStack(*stack).empty();
}

bool StackTemporaryStatusPresenter::append(std::size_t index, const Point & position, Widgets widgets) const
{
	const auto & entry = entries.at(index);
	const auto slotX = position.x;
	const auto slotY = position.y;
	const auto physicalPoison = status.physicalPoison;
	const auto temporaryCreatures = status.temporaryCreatures;
	const auto retributionJudged = status.divineRetributionJudged;
	const auto battleForm = status.battleForm;
	const auto hiddenEffectCount = displayPlan.overflow
		? totalEffectCount - displayPlan.visibleEntryIndices.size() : 0;
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::ELEMENTAL_ATTUNEMENT)
	{
		widgets.physicalStatusIcons.push_back(std::make_shared<CAnimImage>(
			AnimationPath::builtin(elementalAttunementPresentation::ICON), 0, Rect(slotX + 8, slotY + 2, 32, 32)));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW, "A"));
		const auto tooltip = elementalAttunementPresentation::tooltip(attunement);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
			Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.inspiration)
	{
		widgets.physicalStatusIcons.push_back(std::make_shared<CPicture>(
			ImagePath::builtin(arcaneInspirationPresentation::SMALL_ICON), slotX + 8, slotY + 2));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW, inspiration.compact));
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
			Rect(slotX, slotY, 48, 36), inspiration.details, inspiration.details));
		return true;
	}
	if(entry.arcaneMarks)
	{
		widgets.physicalStatusIcons.push_back(std::make_shared<CPicture>(ImagePath::builtin(arcaneMarkPresentation::SMALL_ICON), slotX + 8, slotY + 2));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW, arcaneMarkPresentation::compactCounts(arcaneMarkPresentation::counts(*stack))));
		const auto tooltip = arcaneMarkPresentation::combinedTooltip(arcaneMarkPresentation::forStack(*stack));
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::FROZEN)
	{
		widgets.physicalStatusIcons.push_back(std::make_shared<newHorizonsFrozenPresentation::FrozenStatusIcon>(Point(slotX, slotY)));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, ColorRGBA(178, 224, 244, 255), "F"));
		const auto tooltip = newHorizonsFrozenPresentation::tooltip(status.frozenShatterBonusPercent);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::BATTLE_FORM)
	{
		const auto originalCreature = stack->battleFormOriginalCreature();
		widgets.icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("CPRSMALL"),
			originalCreature.toCreature()->getIconIndex(), 0, slotX + 8, slotY + 2));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW,
			newHorizonsBattleStatus::battleFormDurationLabel(battleForm)));
		const auto tooltip = newHorizonsBattleStatus::battleFormTooltip(battleForm);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::PHYSICAL_POISON)
	{
		// Physical Poison is a saved stack condition, not a magical Poison spell.
		// Reuse the stock Poison frame without inserting a fake active-spell ID.
		const auto poisonIconFrame = SpellID(SpellID::POISON).getNum() + 1;
		widgets.icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), poisonIconFrame, 0, slotX, slotY));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE,
			std::to_string(physicalPoison.activationsRemaining)));
		const auto tooltip = physicalPoisonTooltip(physicalPoison, hiddenEffectCount);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::SHADOW_GIFT_CAP)
	{
		const SpellID shadowGiftId = SpellID::decode(std::string(newHorizonsBattleStatus::SHADOW_GIFT_SPELL_KEY));
		const auto shadowGiftFrame = shadowGiftId.getNum() + 1;
		widgets.icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), shadowGiftFrame, 0, slotX, slotY));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW,
			"-" + TextOperations::formatMetric(status.shadowGift.maximumHealthLost, 4)));
		const auto tooltip = newHorizonsBattleStatus::shadowGiftCapTooltip(status.shadowGift);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::TEMPORARY_CREATURES)
	{
		// Use the authored Re-animate art at its 30px battle-status size, centered
		// in the existing SpellInt slot. The count and generic label explain the
		// shared one-battle resurrection state without inventing a new frame.
		widgets.temporaryCreatureIcons.push_back(std::make_shared<CPicture>(
			ImagePath::builtin("NH_spell_reanimate_30.png"), Point(slotX + 9, slotY + 3)));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::WHITE,
			TextOperations::formatMetric(temporaryCreatures.remainingCount, 4)));
		const auto tooltip = newHorizonsBattleStatus::temporaryCreatureTooltip(
			temporaryCreatures.remainingCount);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	if(entry.kind == newHorizonsBattleStatus::StackStatusIconKind::DIVINE_RETRIBUTION_JUDGED)
	{
		const SpellID divineRetributionId = SpellID::decode(
			std::string(newHorizonsBattleStatus::DIVINE_RETRIBUTION_SPELL_KEY));
		const auto divineRetributionFrame = divineRetributionId.getNum() + 1;
		widgets.icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"),
			divineRetributionFrame, 0, slotX, slotY));
		widgets.labels.push_back(std::make_shared<CLabel>(slotX + 46, slotY + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, Colors::YELLOW,
			TextOperations::formatMetric(retributionJudged.pendingHolyDamage, 4)));
		const auto tooltip = newHorizonsBattleStatus::divineRetributionJudgedTooltip(retributionJudged);
		widgets.statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(slotX, slotY, 48, 36), tooltip, tooltip));
		return true;
	}
	return false;
}
