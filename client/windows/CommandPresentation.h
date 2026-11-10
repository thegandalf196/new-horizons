/*
 * CommandPresentation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/entities/hero/NewHorizonsCommandRules.h"
#include <string>

class CGHeroInstance;

/// Read-only formatting of shared Command rules and permanent acquisition receipts.
namespace commandPresentation
{
std::string slotName(SlotID slot);
std::string recipientName(newHorizonsHeroes::CommandRecipient recipient);
std::string enhancementName(newHorizonsHeroes::CommandEnhancement enhancement);
std::string modifiers(const newHorizonsHeroes::CommandModifiers & values);
std::string commandHelp(const CGHeroInstance & hero);
std::string slotHelp(const CGHeroInstance & hero, SlotID slot);
}
