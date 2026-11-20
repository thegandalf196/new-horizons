/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
// Active spell and status strip of the battle creature inspector, extracted from CCreatureWindow.cpp.
#include "StdInc.h"
#include "CCreatureWindow.h"
#include "CreatureUnitView.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../GameInstance.h"
#include "../battle/NewHorizonsBattleStatus.h"
#include "../battle/FrozenStatusPresentation.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../widgets/MiscWidgets.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/Languages.h"
#include "../../lib/texts/MetaString.h"
#include <vcmi/spells/Spell.h>
#include <vcmi/spells/Service.h>
#include <algorithm>
#include <optional>

namespace
{
struct EffectTile
{
	int frame;
	std::string badge;
	std::string description;
	std::string name;
};

/// Active spells, New Horizons combat statuses first (Doom leading), else in original order.
std::vector<SpellID> orderedEffects(const CStack & battleStack)
{
	std::vector<SpellID> spells = battleStack.activeSpells();
	const auto prioritizedEnd = std::stable_partition(spells.begin(), spells.end(), [](const SpellID effect)
	{
		const auto spellKey = effect.toSpell()->getJsonKey();
		return newHorizonsBattleStatus::isConfusion(spellKey)
			|| newHorizonsBattleStatus::isDoom(spellKey)
			|| newHorizonsBattleStatus::isFocusMagic(spellKey);
	});
	std::stable_partition(spells.begin(), prioritizedEnd, [](const SpellID effect)
	{
		return newHorizonsBattleStatus::isDoom(effect.toSpell()->getJsonKey());
	});
	return spells;
}

/// Icon frame, badge and complete help for one active effect; empty when it has no icon or is inactive.
std::optional<EffectTile> describeEffect(const CStack & battleStack, SpellID effect)
{
	// Not all effects have graphics (e.g. Acid Breath); modded spells add iconEffect to SpellInt.def.
	if(!(effect < SpellID::THUNDERBOLT || effect >= SpellID::AFTER_LAST))
		return std::nullopt;
	const spells::Spell * spell = LIBRARY->spells()->getById(effect);
	auto spellBonuses = battleStack.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(effect)));
	if(spellBonuses->empty())
		throw std::runtime_error("Failed to find effects for spell " + effect.toSpell()->getJsonKey());

	const int duration = spellBonuses->front()->turnsRemain;
	const auto spellKey = spell->getJsonKey();
	const bool confusion = newHorizonsBattleStatus::isConfusion(spellKey);
	const auto confusionStatus = confusion
		? newHorizonsBattleStatus::confusionStatus(*spellBonuses, battleStack.confusionState)
		: newHorizonsBattleStatus::ConfusionStatus{};
	if(confusion && !confusionStatus.active())
		return std::nullopt;
	const bool focusMagic = newHorizonsBattleStatus::isFocusMagic(spellKey);
	const bool doom = newHorizonsBattleStatus::isDoom(spellKey);
	const auto doomEffect = doom ? newHorizonsBattleStatus::doomStatus(*spellBonuses) : newHorizonsBattleStatus::DoomStatus{};

	MetaString spellText;
	spellText.appendTextID(spell->getDescriptionTextID(0)); // TODO: select correct mastery level?
	spellText.appendRawString("\n");
	if(!confusion)
	{
		spellText.appendTextID(Languages::getPluralFormTextID(LIBRARY->generaltexth->getPreferredLanguage(), duration,
			"vcmi.battleResultsWindow.spellDurationRemaining"));
		spellText.replaceNumber(duration);
	}
	std::string description = spellText.toString(&GAME->translator());
	// The generic text describes the untrained spell; the stack's own bonuses give
	// the values actually in effect at the caster's mastery and current rules.
	std::vector<std::string> actual;
	for(const auto & bonus : *spellBonuses)
	{
		auto line = LIBRARY->getBth()->bonusToString(bonus, &battleStack);
		if(!line.empty() && std::find(actual.begin(), actual.end(), line) == actual.end())
			actual.push_back(std::move(line));
	}
	if(!actual.empty())
	{
		description += "\n\n{Current effect}";
		for(const auto & line : actual)
			description += "\n" + line;
	}
	if(confusion)
		description = newHorizonsBattleStatus::confusionTooltip(spell->getDescriptionTranslated(0), confusionStatus);
	else if(focusMagic)
		description = spell->getDescriptionTranslated(0)
			+ "\n\nEnergy shots remaining: " + std::to_string(newHorizonsFocusMagic::charges(battleStack)) + ".";
	if(doom && doomEffect.active())
		description = newHorizonsBattleStatus::doomTooltip(spell->getDescriptionTranslated(0), doomEffect);

	const std::string badge = confusion ? std::string(newHorizonsBattleStatus::CONFUSION_BADGE)
		: focusMagic ? std::to_string(newHorizonsFocusMagic::charges(battleStack))
		: doom && doomEffect.active()
			? newHorizonsBattleStatus::formatBasisPoints(static_cast<int64_t>(doomEffect.damagePenaltyPercent) * 100)
			: std::to_string(duration);
	return EffectTile{effect + 1, badge, description, spell->getNameTranslated()};
}
}

CStackWindow::ActiveSpellsSection::ActiveSpellsSection(CStackWindow * owner, int yOffset)
	: CWindowSection(owner, ImagePath::builtin("stackWindow/spell-effects"), yOffset)
{
	static const Point firstPos(6, 2); // position of 1st spell box
	static const Point offset(54, 0);  // offset of each spell box from previous

	OBJECT_CONSTRUCTION;

	const CStack * battleStack = parent->info->stack;
	assert(battleStack); // Section should be created only for battles

	int printed = 0; // effect slots already used
	if(const auto frozen = newHorizonsFrozenPresentation::currentTooltip(*battleStack))
	{
		physicalStatusIcons.push_back(std::make_shared<newHorizonsFrozenPresentation::FrozenStatusIcon>(firstPos));
		labels.push_back(std::make_shared<CLabel>(firstPos.x + 46, firstPos.y + 36, EFonts::FONT_TINY,
			ETextAlignment::BOTTOMRIGHT, ColorRGBA(178, 224, 244, 255), "F"));
		clickableAreas.push_back(std::make_shared<LRClickableAreaWText>(Rect(firstPos, Point(50, 38)), *frozen, *frozen));
		++printed;
	}
	std::vector<EffectTile> tiles;
	for(SpellID effect : orderedEffects(*battleStack))
		if(auto tile = describeEffect(*battleStack, effect))
			tiles.push_back(std::move(*tile));

	// Eight slots fit the panel. When effects overflow, the last slot shows "+N"
	// and lists every hidden effect with its own description and remaining duration.
	constexpr int SLOTS = 8;
	const auto available = static_cast<size_t>(std::max(1, SLOTS - printed));
	const bool overflow = tiles.size() > available;
	const size_t shown = overflow ? available - 1 : tiles.size();
	for(size_t i = 0; i < shown; ++i, ++printed)
	{
		const Point slot = firstPos + offset * printed;
		spellIcons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), tiles[i].frame, 0, slot.x, slot.y));
		labels.push_back(std::make_shared<CLabel>(slot.x + 46, slot.y + 36, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, tiles[i].badge));
		clickableAreas.push_back(std::make_shared<LRClickableAreaWText>(Rect(slot, Point(50, 38)), tiles[i].description, tiles[i].description));
	}
	if(overflow)
	{
		const auto hidden = tiles.size() - shown;
		std::string details = "{" + std::to_string(hidden) + " more active effects}";
		for(size_t i = shown; i < tiles.size(); ++i)
			details += "\n\n{" + tiles[i].name + "}\n" + tiles[i].description;
		const Point slot = firstPos + offset * printed;
		labels.push_back(std::make_shared<CLabel>(slot.x + 25, slot.y + 19, EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, "+" + std::to_string(hidden)));
		clickableAreas.push_back(std::make_shared<LRClickableAreaWText>(Rect(slot, Point(50, 38)), details, details));
	}
}
