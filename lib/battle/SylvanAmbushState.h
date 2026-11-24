/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../Global.h"
#include "BattleHex.h"
#include "../serializer/ESerializationVersion.h"
#include <stdexcept>
class JsonNode;
namespace battle
{
/// Sylvan Tactics 5 (Ambush) per-unit battle state. Plain unit state, kept out
/// of the bonus system: Concealment, its slot source and last round, the pending
/// Ambush strike with the hex it hid on, the per-round harm flag, the approach
/// record of voluntary movement, and the victim marks for Hunting Pack and
/// Startle. Rounds use -1 for "none". No hero or creature identity is stored.
struct DLL_LINKAGE SylvanAmbushState
{
	enum class Source : uint8_t { NONE = 0, RANK = 1, FOREST_COVER = 2 };

	bool concealed = false;
	Source source = Source::NONE; ///< Slot used by the latest Concealment.
	int32_t concealedRound = -1; ///< Latest round this stack was Concealed or made an Ivor ambush.
	bool strikePending = false;
	BattleHex strikeOrigin; ///< Hex the stack hid on, measured for prey.
	bool harmedThisRound = false; ///< Attacked or damaged this round.
	int32_t approachRound = -1;
	BattleHex approachStart;
	BattleHex approachEnd;
	int32_t huntingPackRound = -1; ///< Round this stack was hit by an Ambush strike.
	int32_t startledBy = -1; ///< Ambusher whose strike this stack has not answered yet.

	bool hasState() const;
	void validate() const;
	/// Round-relative rules, checked where the battle round is known.
	void validateAt(int32_t round) const;

	void conceal(Source slot, int32_t round);
	void reveal();
	void harm();
	/// Concealment ends with any activation; only a delayed one arms the strike.
	void beginActivation(bool delayed, int32_t round, BattleHex position, bool ivorAmbush);
	void finishActivation();
	void finishAttack(bool counter);
	void newRound();
	void retire();
	/// Chain rule: a movement starting elsewhere than the record's end restarts it.
	void recordApproach(int32_t round, BattleHex before, BattleHex after);

	JsonNode toJson() const;
	static SylvanAmbushState fromJson(const JsonNode & data);
	bool operator==(const SylvanAmbushState &) const = default;

	template<typename Handler> void validateSerialization(const Handler & h) const
	{
		validate();
		if(hasState() && !h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_AMBUSH))
			throw std::runtime_error("Cannot discard Sylvan Ambush state in an older format");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_AMBUSH))
		{
			h & concealed;
			h & source;
			h & concealedRound;
			h & strikePending;
			h & strikeOrigin;
			h & harmedThisRound;
			h & approachRound;
			h & approachStart;
			h & approachEnd;
			h & huntingPackRound;
			h & startledBy;
			if(!h.saving)
				validateSerialization(h);
		}
		else if(!h.saving)
			*this = {};
	}
};
/// Parse and validate the optional "sylvanAmbush" field of a serialized unit.
DLL_LINKAGE SylvanAmbushState sylvanAmbushStateFromUnitJson(const JsonNode & unit);
}
