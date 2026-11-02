/*
 * CBattleGameInterface.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "IBattleEventsReceiver.h"
#include <stdexcept>

#include "../battle/AutocombatPreferences.h"

static constexpr int AI_INTERFACE_VER = 1;

class CBattleCallback;
class Environment;
class CBattleInfoCallback;
namespace battle { class OwnedBattleFrameBatch; }

class DLL_LINKAGE CBattleGameInterface : public IBattleEventsReceiver
{
public:
	bool human = false;
	PlayerColor playerID;
	std::string dllName;

	virtual ~CBattleGameInterface() {};
	virtual void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB){};
	virtual void initBattleInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CBattleCallback> CB, AutocombatPreferences autocombatPreferences){};

	/// Willingness is a consumer choice, not source authority. Default call-in preserves existing behavior.
	virtual bool usesOwnedRecipientPreparation() const { return false; }
	/// Observation-only rendezvous before preparation. Called with VALUES while
	/// the real receive unique_lock is temporarily unowned and no child/source
	/// lease exists. Default does nothing; this never grants source admission.
	virtual bool awaitOwnedPreparationObservation(BattleID, uint32_t, int32_t) { return true; }
	/// Cancel observation waits BEFORE network/simulation joins; never waits itself.
	virtual void cancelOwnedPreparationObservation(const std::string &) {}
	virtual void completeOwnedPreparationObservation() {}
	/// Called synchronously by the actual receive owner while state/root/native
	/// admission is retained. The callback is an interval-only input, NEVER a
	/// deferred query/factory. Retain only prepared owned decision data.
	virtual void prepareOwnedRecipientDecision(const std::shared_ptr<CBattleInfoCallback> &,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>)
	{ throw std::logic_error("Willing owned consumer must implement synchronous preparation"); }
	virtual void activeStackWithOwnedFrames(const BattleID & battleID, const CStack * stack,
		std::shared_ptr<const battle::OwnedBattleFrameBatch> frames)
	{
		if(frames) throw std::logic_error("Willing owned consumer must consume its prepared decision");
		activeStack(battleID, stack);
	}

	//battle call-ins
	virtual void activeStack(const BattleID & battleID, const CStack * stack)=0; //called when it's turn of that stack
	virtual void yourTacticPhase(const BattleID & battleID, int distance)=0; //called when interface has opportunity to use Tactics skill -> use cb->battleMakeTacticAction from this function
};
