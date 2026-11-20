/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
// Creature inspector subject, shared by the inspector's section implementations.
#include "../../lib/CCreatureHandler.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/mapObjects/army/CCommanderInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include <functional>
#include <optional>

class CStack;
class CGHeroInstance;
class IGameInfoCallback;

class UnitView
{
public:
	// helper structs
	struct CommanderLevelInfo
	{
		std::vector<ui32> skills;
		std::function<void(ui32)> callback;
	};
	struct StackDismissInfo
	{
		std::function<void()> callback;
	};
	struct StackUpgradeInfo
	{
		StackUpgradeInfo() = delete;
		StackUpgradeInfo(const UpgradeInfo & upgradeInfo)
			: info(upgradeInfo)
		{ }
		UpgradeInfo info;
		std::function<void(CreatureID)> callback;
	};

	// pointers to permanent objects in game state
	const CCreature * creature;
	const CCommanderInstance * commander;
	const CStackInstance * stackNode;
	const CStack * stack;
	const CGHeroInstance * owner;
	const IGameInfoCallback * categoryContext;

	// temporary objects which should be kept as copy if needed
	std::optional<CommanderLevelInfo> levelupInfo;
	std::optional<StackDismissInfo> dismissInfo;
	std::optional<StackUpgradeInfo> upgradeInfo;
	std::optional<newHorizonsCreatures::CreatureCategoryView> category;

	// misc fields
	unsigned int creatureCount;
	bool popupWindow;

	UnitView()
		: creature(nullptr),
		commander(nullptr),
		stackNode(nullptr),
		stack(nullptr),
		owner(nullptr),
		categoryContext(nullptr),
		creatureCount(0),
		popupWindow(false)
	{
	}

	std::string getName() const
	{
		if(commander)
			return commander->getType()->getNameSingularTranslated();
		else
			return creature->getNamePluralTranslated();
	}
private:

};
