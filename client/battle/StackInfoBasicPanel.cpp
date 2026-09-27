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

namespace
{
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
	result.bulwark = newHorizonsBattleStatus::makeBulwarkStatus(rank,
		hero->getPrimSkillLevel(PrimarySkill::DEFENSE), mirebornTerrain,
		newHorizonsBulwark::hasBogAmbush(hero), newHorizonsBulwark::hasThickHide(hero),
		stack->bulwarkPreemptiveUsed);
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

	displayedDefendStatus = currentDefendStatus(stack, battleCallback.get());
	if(displayedDefendStatus.defending)
	{
		const auto badge = displayedDefendStatus.bulwark ? "BULWARK" : "DEFEND";
		const auto tooltip = newHorizonsBattleStatus::defendStatusTooltip(displayedDefendStatus);
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

	int printed=0; //how many effect pics have been printed
	std::vector<SpellID> spells = stack->activeSpells();
	// Keep the New Horizons combat statuses visible when the compact panel reaches
	// its two-effect display limit. stable_partition preserves their relative
	// order and the existing order of all other effects.
	const auto prioritizedEnd = std::stable_partition(spells.begin(), spells.end(), [](const SpellID effect)
	{
		const auto spellKey = effect.toSpell()->getJsonKey();
		return newHorizonsBattleStatus::isTimeStop(spellKey)
			|| newHorizonsBattleStatus::isSpellLock(spellKey)
			|| newHorizonsBattleStatus::isFocusMagic(spellKey)
			|| newHorizonsBattleStatus::isArcaneBreach(spellKey);
	});
	const auto afterTimeStop = std::stable_partition(spells.begin(), prioritizedEnd, [](const SpellID effect)
	{
		return newHorizonsBattleStatus::isTimeStop(effect.toSpell()->getJsonKey());
	});
	std::stable_partition(afterTimeStop, prioritizedEnd, [](const SpellID effect)
	{
		return newHorizonsBattleStatus::isSpellLock(effect.toSpell()->getJsonKey());
	});
	for(SpellID effect : spells)
	{
		//not all effects have graphics (for eg. Acid Breath)
		//for modded spells iconEffect is added to SpellInt.def
		const bool hasGraphics = (effect < SpellID::THUNDERBOLT) || (effect >= SpellID::AFTER_LAST);

		if (hasGraphics)
		{
			//FIXME: support permanent duration
			auto spellBonuses = stack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(effect)));

			if (spellBonuses->empty())
				throw std::runtime_error("Failed to find effects for spell " + effect.toSpell()->getJsonKey());

			int duration = spellBonuses->front()->turnsRemain;
			const auto spellKey = effect.toSpell()->getJsonKey();
			const bool timeStop = newHorizonsBattleStatus::isTimeStop(spellKey);
			const bool spellLock = newHorizonsBattleStatus::isSpellLock(spellKey);
			const bool focusMagic = newHorizonsBattleStatus::isFocusMagic(spellKey);
			const bool arcaneBreach = newHorizonsBattleStatus::isArcaneBreach(spellKey);
			const auto lockStatus = spellLock
				? newHorizonsBattleStatus::spellLockStatus(*spellBonuses)
				: std::optional<newHorizonsBattleStatus::SpellLockStatus>{};
			const auto arcaneStatus = arcaneBreach
				? newHorizonsBattleStatus::arcaneBreachStatus(*spellBonuses)
				: newHorizonsBattleStatus::ArcaneBreachStatus{};

			icons.push_back(std::make_shared<CAnimImage>(AnimationPath::builtin("SpellInt"), effect.getNum() + 1, 0, firstPos.x + offset.x * printed, firstPos.y + offset.y * printed));
			if(settings["general"]["enableUiEnhancements"].Bool() || timeStop || spellLock || arcaneBreach)
			{
				const std::string badge = timeStop
					? std::string(newHorizonsBattleStatus::TIME_STOP_BADGE)
					: spellLock ? std::string(newHorizonsBattleStatus::SPELL_LOCK_BADGE)
					: arcaneBreach ? std::to_string(arcaneStatus.markCount()) : std::to_string(duration);
				labels.push_back(std::make_shared<CLabel>(firstPos.x + offset.x * printed + 46, firstPos.y + offset.y * printed + 36, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, timeStop ? Colors::YELLOW : Colors::WHITE, badge));
			}

			if(timeStop)
			{
				const std::string tooltip = newHorizonsBattleStatus::timeStopTooltip(effect.toSpell()->getDescriptionTranslated(0));
				statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
					Rect(firstPos.x + offset.x * printed, firstPos.y + offset.y * printed, 48, 36), tooltip, tooltip));
			}
			else if(spellLock)
			{
				const std::string tooltip = lockStatus
					? newHorizonsBattleStatus::spellLockTooltip(effect.toSpell()->getDescriptionTranslated(0), *lockStatus)
					: effect.toSpell()->getDescriptionTranslated(0);
				statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
					Rect(firstPos.x + offset.x * printed, firstPos.y + offset.y * printed, 48, 36), tooltip, tooltip));
			}
			else if(focusMagic)
			{
				const auto tooltipStatus = newHorizonsBattleStatus::focusMagicStatus(*spellBonuses);
				const std::string tooltip = tooltipStatus
					? newHorizonsBattleStatus::focusMagicTooltip(effect.toSpell()->getDescriptionTranslated(0), *tooltipStatus)
					: effect.toSpell()->getDescriptionTranslated(0);
				statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
					Rect(firstPos.x + offset.x * printed, firstPos.y + offset.y * printed, 48, 36), tooltip, tooltip));
			}
			else if(arcaneBreach)
			{
				const std::string tooltip = newHorizonsBattleStatus::arcaneBreachTooltip(arcaneStatus);
				statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(
					Rect(firstPos.x + offset.x * printed, firstPos.y + offset.y * printed, 48, 36), tooltip, tooltip));
			}

			++printed;
			if(printed >= 3 || (printed == 2 && spells.size() > 3)) // interface limit reached
				break;
		}
	}

	if(spells.empty())
		labelsMultiline.push_back(std::make_shared<CMultiLineLabel>(Rect(firstPos.x, firstPos.y, 48, 36), EFonts::FONT_TINY, ETextAlignment::CENTER, Colors::WHITE, LIBRARY->generaltexth->allTexts[674]));
	if(spells.size() > 3)
		labelsMultiline.push_back(std::make_shared<CMultiLineLabel>(Rect(firstPos.x + offset.x * 2, firstPos.y + offset.y * 2 - 4, 48, 36), EFonts::FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "..."));
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

	const auto current = currentDefendStatus(updatedInfo, battleCallback.get());
	if(current == displayedDefendStatus)
		return;

	update(updatedInfo);
}

bool StackInfoBasicPanel::containsPoint(const Point & point) const
{
	return (background && background->pos.isInside(point))
		|| (background2 && background2->pos.isInside(point));
}
