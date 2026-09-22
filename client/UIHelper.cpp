/*
 * UIHelper.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "UIHelper.h"

#include "widgets/CComponent.h"
#include "CPlayerInterface.h"

#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../lib/networkPacks/ArtifactLocation.h"
#include "../lib/CRandomGenerator.h"
#include "GameInstance.h"

std::vector<Component> UIHelper::getArtifactsComponents(const CArtifactSet & artSet, const std::vector<MoveArtifactInfo> & movedPack)
{
	std::vector<Component> components;
	for(const auto & artMoveInfo : movedPack)
	{
		const auto art = artSet.getArt(artMoveInfo.dstPos);
		assert(art);

		if(art->isScroll())
			components.emplace_back(ComponentType::SPELL_SCROLL, art->getScrollSpellID());
		else
			components.emplace_back(ComponentType::ARTIFACT, art->getTypeId());
	}
	return components;
}

std::vector<Component> UIHelper::getSpellsComponents(const std::set<SpellID> & spells)
{
	std::vector<Component> components;
	for(const auto & spell : spells)
		components.emplace_back(ComponentType::SPELL, spell);
	return components;
}

soundBase::soundID UIHelper::getNecromancyInfoWindowSound()
{
	return soundBase::soundID(soundBase::pickup01 + CRandomGenerator::getDefault().nextInt(6));
}

std::string UIHelper::getNecromancyInfoWindowText(const CStackBasicDescriptor & stack)
{
	MetaString text;
	if(stack.getCount() > 1) // Practicing the dark arts of necromancy, ... (plural)
	{
		text.appendTextID("core.genrltxt.145");
		text.replaceNumber(stack.getCount());
	}
	else // Practicing the dark arts of necromancy, ... (singular)
	{
		text.appendTextID("core.genrltxt.146");
	}
	text.replaceName(stack);
	return text.toString(&GAME->translator());
}

std::vector<Component> UIHelper::getNewHorizonsNecromancyComponents(const newHorizonsNecromancy::NecromancyResult & result)
{
	std::vector<Component> components;
	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	if(result.skeletonsRaised > 0)
		components.emplace_back(ComponentType::CREATURE, skeleton, result.skeletonsRaised);
	if(result.zombiesRaised > 0)
		components.emplace_back(ComponentType::CREATURE, zombie, result.zombiesRaised);
	return components;
}

std::string UIHelper::getNewHorizonsNecromancyInfoWindowText(const newHorizonsNecromancy::NecromancyResult & result)
{
	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	MetaString text;
	text.appendRawString("Necromancy\n");

	if(result.eligibleCasualties > 0)
	{
		text.appendRawString("Eligible casualties: ");
		text.appendNumber(result.eligibleCasualties);
		text.appendRawString("\n");
	}

	if(result.skeletonsOffered > 0)
	{
		text.appendRawString("Generated: ");
		text.appendNumber(result.skeletonsOffered);
		text.appendRawString(" ");
		text.appendName(skeleton, result.skeletonsOffered);
		text.appendRawString("\n");
	}

	if(result.darkConversionChosen && result.zombiesRaised > 0)
	{
		text.appendRawString("Converted: ");
		text.appendNumber(result.zombiesRaised);
		text.appendRawString(" ");
		text.appendName(zombie, result.zombiesRaised);
		text.appendRawString(" from groups of three Skeletons\n");
	}

	if(result.skeletonsRaised > 0 || result.zombiesRaised > 0)
	{
		text.appendRawString("Delivered to army: ");
		bool hasOutput = false;
		if(result.skeletonsRaised > 0)
		{
			text.appendNumber(result.skeletonsRaised);
			text.appendRawString(" ");
			text.appendName(skeleton, result.skeletonsRaised);
			hasOutput = true;
		}
		if(result.zombiesRaised > 0)
		{
			if(hasOutput)
				text.appendRawString(" and ");
			text.appendNumber(result.zombiesRaised);
			text.appendRawString(" ");
			text.appendName(zombie, result.zombiesRaised);
		}
		text.appendRawString("\n");
	}

	if(result.manaRecovered > 0)
	{
		text.appendRawString("Black Harvest recovered +");
		text.appendNumber(result.manaRecovered);
		text.appendRawString(" Mana.\n");
	}

	if(result.blockedByArmyCapacity)
	{
		text.appendRawString("No creatures were delivered: the hero has no legal army slot for the Necromancy result.");
	}
	else if(result.eligibleCasualties == 0)
	{
		text.appendRawString("No eligible casualties were available for raising.");
	}
	else if(result.skeletonsOffered == 0)
	{
		text.appendRawString("The eligible casualty count was below the current Necromancy raising threshold.");
	}

	return text.toString(&GAME->translator());
}

std::string UIHelper::getArtifactsInfoWindowText()
{
	MetaString text;
	text.appendTextID("core.genrltxt.30");
	return text.toString(&GAME->translator());
}

std::string UIHelper::getEagleEyeInfoWindowText(const CGHeroInstance & hero, const std::set<SpellID> & spells)
{
	MetaString text;
	text.appendTextID("core.genrltxt.221"); // Through eagle-eyed observation, %s is able to learn %s
	text.replaceTextID(hero.getNameTextID());

	auto curSpell = spells.begin();
	text.replaceName(*curSpell++);
	for(int i = 1; i < spells.size(); i++, curSpell++)
	{
		if(i + 1 == spells.size())
			text.appendTextID("core.genrltxt.141"); // " and "
		else
			text.appendRawString(", ");
		text.appendName(*curSpell);
	}
	text.appendRawString(".");
	return text.toString(&GAME->translator());
}

bool UIHelper::checkLeadershipResult(const CArmedInstance * destination, CreatureID creature, TQuantity resultingCount)
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(destination);
	if(!hero)
		return true;

	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	if(!capacity || capacity->accepts(resultingCount))
		return true;

	GAME->interface()->showInfoDialog(
		"Leadership limit exceeded: this hero can command at most "
		+ std::to_string(capacity->maximum) + " creatures of this type ("
		+ std::to_string(capacity->requirement) + " Leadership each; hero Leadership "
		+ std::to_string(capacity->leadership) + ").");
	return false;
}

bool UIHelper::checkLeadershipTransfer(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot, TQuantity amount)
{
	if(!source || !destination || amount <= 0)
		return true;

	const auto * sourceCreature = source->getCreature(sourceSlot);
	if(!sourceCreature)
		return true;

	const auto * destinationCreature = destination->getCreature(destinationSlot);
	if(destinationCreature && destinationCreature != sourceCreature)
		return true; // The server's normal type validation reports this case.

	const TQuantity existing = destinationCreature
		? destination->getStackCount(destinationSlot)
		: 0;
	return checkLeadershipResult(destination, sourceCreature->getId(), existing + amount);
}
