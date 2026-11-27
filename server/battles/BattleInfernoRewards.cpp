/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfernoRewards.h"
#include "../CGameHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/entities/hero/NewHorizonsInfernoAdventureSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"

namespace battleInfernoRewards
{
void grantCutpurse(CGameHandler & handler, const CGHeroInstance * winner, const CGHeroInstance * loser)
{
	if(!winner || !loser || !winner->getOwner().isValidPlayer() || !loser->getOwner().isValidPlayer()
		|| winner->getOwner() == loser->getOwner())
		return;
	const auto * defeated = handler.gameInfo().getPlayerState(loser->getOwner());
	const auto * victorious = handler.gameInfo().getPlayerState(winner->getOwner());
	if(!defeated || !victorious)
		return;
	const auto amount = newHorizonsHeroes::cutpurseGold(winner,
		defeated->resources[EGameResID::GOLD], victorious->resources[EGameResID::GOLD]);
	if(amount <= 0)
		return;
	// Both updates execute synchronously in this authoritative battle completion.
	SetResources debit;
	debit.mode = ChangeValueMode::RELATIVE;
	debit.player = loser->getOwner();
	debit.res[EGameResID::GOLD] = -amount;
	SetResources credit;
	credit.mode = ChangeValueMode::RELATIVE;
	credit.player = winner->getOwner();
	credit.res[EGameResID::GOLD] = amount;
	// Battle completion checks both players after all rewards and removals.
	handler.sendAndApply(static_cast<CPackForClient &>(debit));
	handler.sendAndApply(static_cast<CPackForClient &>(credit));
	InfoWindow dialog;
	dialog.player = winner->getOwner();
	dialog.text.appendTextID("new-horizons.inferno.cutpurse.victory");
	dialog.text.replaceTextID(winner->getNameTextID());
	dialog.text.replaceNumber(amount);
	dialog.components.emplace_back(ComponentType::RESOURCE, GameResID(GameResID::GOLD), amount);
	handler.showInfoDialog(&dialog);
}
}
