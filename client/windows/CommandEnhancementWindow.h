/*
 * CommandEnhancementWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowObject.h"
#include "CommandEnhancementDialogState.h"
#include "../../lib/entities/hero/NewHorizonsCommandRules.h"
#include <array>
#include <functional>

class CButton;
class CLabel;

/// Detached, already-valued offer. The network adapter copies the server's award snapshot.
struct CommandEnhancementOfferView
{
	struct Option
	{
		newHorizonsHeroes::CommandEnhancement enhancement = newHorizonsHeroes::CommandEnhancement::READINESS;
		int64_t contribution = 0;
	};
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	int64_t milestone = 0;
	uint32_t acquisitionLevel = 0;
	newHorizonsHeroes::CommandRecipient recipient = newHorizonsHeroes::CommandRecipient::CORE_BLOCK;
	std::string heroName;
	std::array<Option, 9> options;
};

/// Mandatory Command award selector, separate from ordinary Skill/perk advancement.
class CommandEnhancementWindow : public CWindowObject
{
	CommandEnhancementOfferView offer;
	CommandEnhancementDialogState state;
	std::function<int(int)> submit;
	std::array<std::shared_ptr<CButton>, 9> choices;
	std::shared_ptr<CButton> confirm;
	std::shared_ptr<CLabel> status;
	std::vector<std::shared_ptr<CIntObject>> elements;
	void select(int index);
	void confirmSelection();
	void refreshControls();

public:
	CommandEnhancementWindow(CommandEnhancementOfferView offer, QueryID query, std::function<int(int)> callback);
	void requestApplied(uint32_t requestID, bool success);
	void queryResolved(QueryID query);
	ObjectInstanceID heroID() const { return offer.hero; }
	bool matchesQuery(QueryID query) const { return state.matchesQuery(query.getNum()); }
	void keyPressed(EShortcut key) override;
	void close() override;
};
