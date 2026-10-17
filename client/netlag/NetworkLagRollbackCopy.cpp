/*
 * NetworkLagRollbackCopy.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkLagRollbackCopy.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include <stdexcept>
#include <typeinfo>

std::unique_ptr<CPackForClient> copyCurrentPredictionRollback(const CPackForClient & packet)
{
	// EXACT current outputs of PackRollbackGeneratorVisitor, not a live action
	// filter. No base slicing, serialization, source mutation or native binding.
	if(typeid(packet) == typeid(SwapStacks))
		return std::make_unique<SwapStacks>(static_cast<const SwapStacks &>(packet));
	if(typeid(packet) == typeid(RebalanceStacks))
		return std::make_unique<RebalanceStacks>(static_cast<const RebalanceStacks &>(packet));
	if(typeid(packet) == typeid(HeroVisitCastle))
		return std::make_unique<HeroVisitCastle>(static_cast<const HeroVisitCastle &>(packet));
	if(typeid(packet) == typeid(TryMoveHero))
		return std::make_unique<TryMoveHero>(static_cast<const TryMoveHero &>(packet));
	if(typeid(packet) == typeid(FoWChange))
		return std::make_unique<FoWChange>(static_cast<const FoWChange &>(packet));
	throw std::logic_error("Prediction rollback producer requires a new owned-copy implementation");
}
