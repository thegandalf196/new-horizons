/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/battle/VisibleHeroPolicy.h"
#include "../../lib/bonuses/OwnedBonusCaptureCodec.h"
/// Incomplete owned source-copy stage, NOT a source lease or executable FRAME.
/// No world/callback/native/library pointers or raw Bonus aliases may escape.
struct OwnedRecipientCaptureMetadata
{
	struct Occurrence
	{
		battle::OwnedUnitBonusFrame::Identity identity;
		CreatureID effectiveCreature;
		BattleHex position;
		int32_t count;
		bool alive, ghost;
	};
	std::optional<OwnedBonusCaptureCodec> captureCodec; // Actual established connection, never inferred Hero rules/defaults.
	BattleID battle;
	PlayerColor observer;
	size_t recipientOccurrence = 0;
	std::vector<Occurrence> roster; // Exact callback order/multiplicity, including dead/ghost/cart occurrences.
	std::map<CreatureID, battle::OwnedUnitCreatureResolver::Creature> creatures;
	BattleSideArray<VisibleHeroPolicy::Knowledge> heroKnowledge;
};
