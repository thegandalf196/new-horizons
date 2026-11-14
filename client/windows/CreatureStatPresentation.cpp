/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureWindow.h"
#include "CreatureDefensePresentation.h"
#include "CreatureSupplementalStats.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../widgets/Images.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../render/EFont.h"
#include "../widgets/TextControls.h"
#include "../../lib/texts/MetaString.h"

void CStackWindow::MainSection::addStatLabel(EStat index, int64_t value1, int64_t value2)
{
	const auto title = statNames.at(static_cast<size_t>(index));
	const auto row = statRow(index);
	stats.push_back(std::make_shared<CLabel>(145, 32 + static_cast<int>(row) * 19, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, title));

	const bool useRange = value1 != value2;

	MetaString value = MetaString::createFromRawString(useRange ? statFormats.at(static_cast<size_t>(index)) : "%d");
	value.replaceNumber(value1);
	if(useRange)
		value.replaceNumber(value2);

	stats.push_back(std::make_shared<CLabel>(307, 48 + static_cast<int>(row) * 19, FONT_SMALL, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, value.toString(&GAME->translator())));
}

void CStackWindow::MainSection::addStatLabel(EStat index, int64_t value)
{
	addStatLabel(index, value, value);
}

void CStackWindow::MainSection::addStatLabel(EStat index, const std::string & value)
{
	const auto title = statNames.at(static_cast<size_t>(index));
	const auto row = statRow(index);
	stats.push_back(std::make_shared<CLabel>(145, 32 + static_cast<int>(row) * 19, FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE, title));
	stats.push_back(std::make_shared<CLabel>(307, 48 + static_cast<int>(row) * 19, FONT_SMALL,
		ETextAlignment::BOTTOMRIGHT, Colors::WHITE, value));
}

size_t CStackWindow::MainSection::statRow(EStat index) const
{
	return creatureStatLayout::row(index, showNewHorizonsStats);
}

void CStackWindow::MainSection::addDefenseReadout(const AFactionMember & creature,
	const AFactionMember & subject, bool ranged)
{
	const auto base = creatureDefensePresentation::query(creature, ranged);
	const auto current = creatureDefensePresentation::query(subject, ranged);
	addStatLabel(EStat::DEFENCE, base.physical, current.physical);
	if(current.physical == current.general)
		return;

	const auto physical = "Physical Defense: " + std::to_string(current.physical);
	const auto magical = "Defense against magical creature attacks: " + std::to_string(current.general);
	defenseArea = std::make_shared<LRClickableAreaWText>(
		Rect(114, 31 + static_cast<int>(statRow(EStat::DEFENCE)) * 19, 194, 19),
		physical + "; " + magical,
		physical + "\n" + magical + "\n\nPhysical-only Defense bonuses do not apply against magical creature attacks.");
}

void CStackWindow::MainSection::initializeStatMetadata()
{
	statNames =
	{
		LIBRARY->generaltexth->translate("core.priskill.0"), //ATTACK
		LIBRARY->generaltexth->translate("core.priskill.1"),//DEFENCE
		LIBRARY->generaltexth->allTexts[198],//SHOTS
		LIBRARY->generaltexth->allTexts[199],//DAMAGE

		LIBRARY->generaltexth->allTexts[388],//HEALTH
		LIBRARY->generaltexth->allTexts[200],//HEALTH_LEFT
		LIBRARY->generaltexth->zelp[441].first,//SPEED
		"Initiative",
		"Retaliations",
		"Physical Resistance",
		"Magical Resistance",
		LIBRARY->generaltexth->allTexts[399],//MANA
		"Leadership Cost",
		"Rank"
	};

	statFormats =
	{
		"%d (%d)",
		"%d (%d)",
		"%d (%d)",
		"%d - %d",

		"%d (%d)",
		"%d (%d)",
		"%d (%d)",
		"%d (%d)",
		"%s",
		"%s",
		"%s",
		"%d (%d)",
		"%s",
		"%s"
	};
}

void CStackWindow::MainSection::initializeStatIcons(bool showLeadership, bool showRank)
{
	static const std::array<std::string, 7> baseIconNames = {
		"stackWindow/iconAttack", "stackWindow/iconDefense", "stackWindow/iconShots", "stackWindow/iconDamage",
		"stackWindow/iconHealth", "stackWindow/iconHealthLeft", "stackWindow/iconSpeed"
	};
	for(size_t i = 0; i < baseIconNames.size(); i++)
		statIcons[i] = std::make_shared<CPicture>(ImagePath::builtin(baseIconNames[i]), 117, creatureStatLayout::iconY(static_cast<EStat>(i), showNewHorizonsStats));


	if(showNewHorizonsStats)
	{
		const std::array<std::pair<EStat, const char *>, 4> additionalIcons{{
			{EStat::INITIATIVE, "stackWindow/iconInitiative"},
			{EStat::RETALIATIONS, "stackWindow/iconRetaliations"},
			{EStat::PHYSICAL_RESISTANCE, "stackWindow/iconPhysicalResistance"},
			{EStat::MAGICAL_RESISTANCE, "stackWindow/iconMagicalResistance"}
		}};
		for(const auto & [stat, path] : additionalIcons)
			statIcons[static_cast<size_t>(stat)] = std::make_shared<CPicture>(
				ImagePath::builtin(path), 117, creatureStatLayout::iconY(stat, true));
	}
	statIcons[static_cast<size_t>(EStat::MANA)] = std::make_shared<CPicture>(
		ImagePath::builtin("stackWindow/iconMana"), 117, creatureStatLayout::iconY(EStat::MANA, showNewHorizonsStats));

	if(showLeadership)
		statIcons[static_cast<size_t>(EStat::LEADERSHIP)] = std::make_shared<CAnimImage>(
			AnimationPath::builtin("NH_creature_leadership_20"), 0,
			Rect(116, creatureStatLayout::iconY(EStat::LEADERSHIP, showNewHorizonsStats), 20, 20));

	if(showRank)
		statIcons[static_cast<size_t>(EStat::RANK)] = std::make_shared<CAnimImage>(
			AnimationPath::builtin("NH_creature_rank_20"), 0,
			Rect(116, creatureStatLayout::iconY(EStat::RANK, showNewHorizonsStats), 20, 20));
}

void CStackWindow::MainSection::addUnitStats(const CCreature & creature, const CStackInstance & armyStack,
	const CStack * battleStack, int damageMultiplier)
{
	if(battleStack != nullptr) // in battle
	{
		addStatLabel(EStat::ATTACK, creature.getAttack(battleStack->isShooter()), battleStack->getAttack(battleStack->isShooter()));
		addDefenseReadout(creature, *battleStack, battleStack->isShooter());
		addStatLabel(EStat::DAMAGE, armyStack.getMinDamage(battleStack->isShooter()) * damageMultiplier, battleStack->getMaxDamage(battleStack->isShooter()) * damageMultiplier);
		addStatLabel(EStat::HEALTH, creature.getMaxHealth(), battleStack->getMaxHealth());
		addStatLabel(EStat::SPEED, creature.getMovementRange(), battleStack->getMovementRange());
		if(showNewHorizonsStats)
			addStatLabel(EStat::INITIATIVE, creature.getBaseInitiative(), battleStack->getInitiative());

		if(battleStack->isShooter())
			addStatLabel(EStat::SHOTS, battleStack->shots.total(), battleStack->shots.available());
		if(battleStack->isCaster())
			addStatLabel(EStat::MANA, battleStack->casts.total(), battleStack->casts.available());
		addStatLabel(EStat::HEALTH_LEFT, battleStack->getFirstHPleft());

		const auto * battleInfo = battleStack->getBattle();
		auto battleCallback = battleInfo && GAME->interface() && GAME->interface()->cb
			? GAME->interface()->cb->getBattle(battleInfo->getBattleID())
			: nullptr;
		if(battleCallback && battleCallback->getBattle()
			&& newHorizonsMagic::rulesActive(battleCallback->getBattle()->getMagicRules()))
		{
			morale->set(battleStack, battleCallback.get());
			luck->set(battleStack, battleCallback.get());
		}
		else
		{
			morale->set(battleStack);
			luck->set(battleStack);
		}
	}
	else
	{
		const bool shooter = armyStack.hasBonusOfType(BonusType::SHOOTER) && armyStack.valOfBonuses(BonusType::SHOTS);
		const bool caster = armyStack.valOfBonuses(BonusType::CASTS);

		addStatLabel(EStat::ATTACK, creature.getAttack(shooter), armyStack.getAttack(shooter));
		addDefenseReadout(creature, armyStack, shooter);
		addStatLabel(EStat::DAMAGE, armyStack.getMinDamage(shooter), armyStack.getMaxDamage(shooter));
		addStatLabel(EStat::HEALTH, creature.getMaxHealth(), armyStack.getMaxHealth());
		addStatLabel(EStat::SPEED, creature.getMovementRange(), armyStack.getMovementRange());
		if(showNewHorizonsStats)
			addStatLabel(EStat::INITIATIVE, creature.getBaseInitiative(), armyStack.getInitiative());

		if(shooter)
			addStatLabel(EStat::SHOTS, armyStack.valOfBonuses(BonusType::SHOTS));
		if(caster)
			addStatLabel(EStat::MANA, armyStack.valOfBonuses(BonusType::CASTS));

		morale->set(&armyStack);
		luck->set(&armyStack);
	}

}

void CStackWindow::MainSection::addSupplementalStats(const CStackInstance & armyStack,
	const CStack * battleStack, const CGHeroInstance * hero)
{
	const JsonNode noRules;
	const auto * callback = GAME->interface() ? GAME->interface()->cb.get() : nullptr;
	const auto & rules = battleStack && battleStack->getBattle() ? battleStack->getBattle()->getMagicRules()
		: (hero ? hero->getMagicRules() : (callback ? callback->getMagicRules() : noRules));
	const auto readout = creatureSupplementalStats::query(armyStack, battleStack, hero, rules);
	addStatLabel(EStat::RETALIATIONS, creatureSupplementalStats::retaliationText(readout.retaliations));
	addStatLabel(EStat::PHYSICAL_RESISTANCE, readout.physicalReductionBasisPoints
		? creatureSupplementalStats::percentageText(*readout.physicalReductionBasisPoints) : "--");
	addStatLabel(EStat::MAGICAL_RESISTANCE,
		creatureSupplementalStats::percentageText(readout.magicResistancePercent * 100));

	const std::array<std::pair<EStat, const char *>, 3> help{{
		{EStat::RETALIATIONS, "Available retaliations / round allowance. Unlimited retaliations are shown as ∞. Attack-specific restrictions can still prevent retaliation."},
		{EStat::PHYSICAL_RESISTANCE, "Baseline damage reduction against ordinary physical creature attacks. Range-, position- and attacker-specific effects and penetration can change the result. Physical Defense is separate."},
		{EStat::MAGICAL_RESISTANCE, "Chance to reject an eligible hostile spell. Battle auras are included during combat; no battlefield adjacency is assumed outside combat. Caster-specific effects may change the chance. Immunity and magical damage reduction are separate."}
	}};
	for(size_t i = 0; i < help.size(); ++i)
		supplementalAreas[i] = std::make_shared<LRClickableAreaWText>(
			Rect(114, 30 + static_cast<int>(statRow(help[i].first)) * creatureStatLayout::ROW_HEIGHT, 194, 20),
			statNames[static_cast<size_t>(help[i].first)], help[i].second);
}
