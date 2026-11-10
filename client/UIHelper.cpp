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
#include "NewHorizonsNecromancyText.h"

#include "widgets/CComponent.h"
#include "CPlayerInterface.h"

#include "../lib/callback/CCallback.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../lib/networkPacks/ArtifactLocation.h"
#include "../lib/CRandomGenerator.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "windows/CommandPresentation.h"
#include "../lib/entities/creature/NewHorizonsAssimilation.h"
#include "../lib/entities/hero/NewHorizonsCommandArmy.h"
#include "GameInstance.h"
#include <boost/algorithm/string/replace.hpp>

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
	const auto skeleton = result.skeletonCreature.hasValue()
		? result.skeletonCreature
		: CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	const auto wight = CreatureID(CreatureID::decode("core:wight"));
	const auto boneDragon = CreatureID(CreatureID::decode("core:boneDragon"));
	if(result.skeletonsRaised > 0)
		components.emplace_back(ComponentType::CREATURE, skeleton, result.skeletonsRaised);
	if(result.zombiesRaised > 0)
		components.emplace_back(ComponentType::CREATURE, zombie, result.zombiesRaised);
	if(result.wightsRaised > 0)
		components.emplace_back(ComponentType::CREATURE, wight, result.wightsRaised);
	if(result.boneDragonsRaised > 0)
		components.emplace_back(ComponentType::CREATURE, boneDragon, result.boneDragonsRaised);
	for(const auto & recovered : necromancyPresentation::recoveryOutputs(result))
		components.emplace_back(ComponentType::CREATURE, recovered.creature, recovered.count);
	return components;
}

std::string UIHelper::getNewHorizonsNecromancyInfoWindowText(const newHorizonsNecromancy::NecromancyResult & result)
{
	const auto baseSkeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const CGTownInstance * ossuaryTown = nullptr;
	if(result.ossuaryTown != ObjectInstanceID::NONE && GAME->interface() && GAME->interface()->cb)
		ossuaryTown = GAME->interface()->cb->getTown(result.ossuaryTown);
	MetaString text;
	text.appendRawString("Necromancy\n");

	if(result.eligibleCasualties > 0)
	{
		text.appendRawString("Ordinary living casualties: ");
		text.appendNumber(result.eligibleCasualties);
		text.appendRawString(" (normal rate ");
		text.appendNumber(result.percentage);
		text.appendRawString("%)");
		text.appendRawString("\n");
	}

	if(result.deathLordCasualties > 0)
	{
		text.appendRawString("Death Lord: ");
		text.appendNumber(result.deathLordCasualties);
		text.appendRawString(" Construct/Elemental casualties -> ");
		text.appendNumber(result.deathLordSkeletons);
		text.appendRawString(" generated ");
		text.appendName(baseSkeleton, result.deathLordSkeletons);
		text.appendRawString(result.deathLordSkeletons == 1
			? " (base equivalent) at 25% of the normal rate.\n"
			: " (base equivalents) at 25% of the normal rate.\n");
	}

	if(result.graveKnowledgeCasualties > 0)
	{
		text.appendRawString("Grave Knowledge: ");
		text.appendNumber(result.graveKnowledgeCasualties);
		text.appendRawString(" Undead casualties -> ");
		text.appendNumber(result.graveKnowledgeSkeletons);
		text.appendRawString(" generated ");
		text.appendName(baseSkeleton, result.graveKnowledgeSkeletons);
		text.appendRawString(result.graveKnowledgeSkeletons == 1
			? " (base equivalent) at the fixed 20% rate.\n"
			: " (base equivalents) at the fixed 20% rate.\n");
	}

	if(result.skeletonsOffered > 0)
	{
		text.appendRawString("Generated: ");
		text.appendNumber(result.skeletonsOffered);
		text.appendRawString(" ");
		text.appendName(baseSkeleton, result.skeletonsOffered);
		text.appendRawString("\n");
	}

	necromancyPresentation::appendConversions(text, result);

	necromancyPresentation::appendFreshDelivery(text, result,
		ossuaryTown ? ossuaryTown->getObjectName().toString(&GAME->translator()) : std::string());
	necromancyPresentation::appendRecovery(text, result);

	if(result.manaRecovered > 0)
	{
		text.appendRawString("Necromancy recovered +");
		text.appendNumber(result.manaRecovered);
		text.appendRawString(" Mana.\n");
	}

	if(result.blockedByArmyCapacity)
	{
		text.appendRawString("No newly raised creatures were delivered: no legal destination was available for the Necromancy result.");
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

std::string UIHelper::armySlotProblem(const CArmedInstance * army, SlotID slot, CreatureID creature)
{
	if(!army)
		return GAME->translator().translate("new-horizons.command.placement.noArmy");
	if(!slot.validSlot())
		return GAME->translator().translate("new-horizons.command.placement.noPosition");
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(!hero)
		return {};
	const auto error = newHorizonsHeroes::commandArmySlotEligibility(hero, slot, creature);
	using newHorizonsHeroes::CommandPlacementError;
	switch(error)
	{
	case CommandPlacementError::NONE: return {};
	case CommandPlacementError::INVALID_SLOT:
		return GAME->translator().translate("new-horizons.command.placement.invalidSlot");
	case CommandPlacementError::LOCKED_SLOT:
	{
		auto text = GAME->translator().translate("new-horizons.command.slot.locked");
		boost::replace_all(text, "%REQUIRED%", std::to_string(
			newHorizonsHeroes::commandSlotView(hero->getArmyCommand()->effective(), slot).unlockCommand));
		return text;
	}
	case CommandPlacementError::UNKNOWN_CREATURE_TIER:
		return GAME->translator().translate("new-horizons.command.placement.unknownTier");
	case CommandPlacementError::TIER_NOT_PERMITTED:
	{
		const auto grade = newHorizonsHeroes::commandSlotView(hero->getArmyCommand()->effective(), slot).grade;
		auto text = GAME->translator().translate("new-horizons.command.slot.illegalTier");
		boost::replace_all(text, "%SLOT%", commandPresentation::slotName(slot));
		boost::replace_all(text, "%TIERS%", GAME->translator().translate(grade == newHorizonsHeroes::CommandSlotGrade::CORE
			? "new-horizons.command.slot.tiers.core" : grade == newHorizonsHeroes::CommandSlotGrade::ELITE
				? "new-horizons.command.slot.tiers.elite" : "new-horizons.command.slot.tiers.champion"));
		boost::replace_all(text, "%CREATURE%", GAME->translator().translate(creature.toCreature()->getNamePluralTextID()));
		return text;
	}
	}
	return GAME->translator().translate("new-horizons.command.placement.unavailable");
}

bool UIHelper::checkArmySlot(const CArmedInstance * army, SlotID slot, CreatureID creature)
{
	const auto problem = armySlotProblem(army, slot, creature);
	if(problem.empty())
		return true;
	GAME->interface()->showInfoDialog(problem);
	return false;
}

std::string UIHelper::armySlotPreview(const CArmedInstance * army, SlotID slot, CreatureID creature)
{
	std::string text = armySlotProblem(army, slot, creature);
	if(text.empty())
	{
		text = GAME->translator().translate("new-horizons.command.placement.legal");
		boost::replace_all(text, "%SLOT%", armyPositionName(army, slot));
	}
	if(GAME && GAME->interface() && GAME->interface()->cb)
		if(const auto category = GAME->interface()->cb->getCreatureCategory(creature))
		{
			auto tier = GAME->translator().translate("new-horizons.command.placement.creatureTier");
			boost::replace_all(tier, "%TIER%", GAME->translator().translate(category->nameTextId));
			text += "\n" + tier;
		}
	if(newHorizonsCreatures::hasIntrinsicAssimilation(creature))
		text += "\n" + GAME->translator().translate("new-horizons.command.assimilation.preview");
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
		hero && newHorizonsCreatures::isCommandSpecialistCreature(*hero, creature))
		text += "\n" + GAME->translator().translate("new-horizons.command.placement.specialty");
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(army); hero && slot.validSlot())
		text += "\n" + commandPresentation::slotHelp(*hero, slot);
	return text;
}

std::string UIHelper::armyPositionName(const CArmedInstance * army, SlotID slot)
{
	if(!slot.validSlot())
		return GAME->translator().translate("new-horizons.command.placement.noPosition");
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(hero && hero->getArmyCommand())
		return commandPresentation::slotName(slot);
	auto text = GAME->translator().translate("new-horizons.command.placement.position");
	boost::replace_all(text, "%POSITION%", std::to_string(slot.getNum() + 1));
	return text;
}

SlotID UIHelper::recruitmentPosition(const CArmedInstance * army, CreatureID creature, int64_t count)
{
	if(!army || count <= 0)
		return SlotID();
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(!hero || !hero->getArmyCommand())
		return army->getSlotFor(creature.toCreature());
	const std::array incoming = {newHorizonsHeroes::RecruitmentSlot{SlotID(0), creature, count}};
	const auto plan = newHorizonsHeroes::planCommandArmyAddition(*hero,
		newHorizonsHeroes::commandArmySlots(*army), incoming);
	if(plan)
		for(const auto & move : plan->moves)
			if(!move.withinDestination)
				return move.destination;
	return SlotID();
}

bool UIHelper::checkRecruitmentPlan(const CArmedInstance * army,
	std::span<const newHorizonsHeroes::RecruitmentSlot> incoming)
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(!hero || !hero->getArmyCommand() || incoming.empty())
		return true;
	if(newHorizonsHeroes::planCommandArmyAddition(*hero,
		newHorizonsHeroes::commandArmySlots(*army), incoming))
		return true;
	GAME->interface()->showInfoDialog(GAME->translator().translate("new-horizons.command.placement.recruitment"));
	return false;
}
