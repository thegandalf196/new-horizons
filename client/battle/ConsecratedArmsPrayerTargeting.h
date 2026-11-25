/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "ConsecratedArmsPrayerChoice.h"
#include "../../lib/battle/BattleHexArray.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class BattleInterface;

/// Battlefield phase of a committed Chosen Prayers Order, or of Loynis's
/// Sermon spell, after its prayer was picked in the spellbook: the player
/// clicks the stack that receives it. Presentation only. Every way out sends
/// one ordinary validated request: the action with the prayer after a legal
/// click, otherwise the same action without it. Only a changed battle (another
/// round, side, hero or player, or the player no longer commanding) sends
/// nothing, and that is reported to the player.
class PrayerRecipientTargeting
{
public:
	using Sender = std::function<void(const BattleAction &)>;
	enum class Outcome
	{
		WAITING,             ///< Still choosing; nothing decided or sent.
		SENT_WITH_PRAYER,
		SENT_WITHOUT_PRAYER,
		NOT_SENT             ///< The battle changed; the action could not be issued.
	};
	struct Resolution
	{
		Outcome outcome = Outcome::WAITING;
		BattleAction request;
	};

private:
	BattleInterface & owner;
	BattleAction action;
	consecratedArmsPrayerChoice::Selection selection;
	Sender send;
	BattleID battleID;
	BattleSide side = BattleSide::NONE;
	int32_t round = -1;
	ObjectInstanceID heroID = ObjectInstanceID::NONE;
	PlayerColor player = PlayerColor::CANNOT_DETERMINE;
	/// Last freshly computed recipients; paint reads this, hover and click recompute it.
	std::vector<uint32_t> eligible;
	std::optional<Resolution> decided;

	consecratedArms::ChoiceRequest currentRequest() const;
	std::string actionName() const;
	std::string prayerNames() const;
	std::string stackName(uint32_t unitId) const;

public:
	PrayerRecipientTargeting(BattleInterface & owner, BattleAction action,
		consecratedArmsPrayerChoice::Selection selection, Sender send);

	/// Same commanding-player identity check as the targeted Order selector.
	bool contextIsCurrent() const;
	bool hexIsLegal(const BattleHex & hex) const;
	BattleHexArray legalHexes() const;
	/// Status bar text for the hovered hex; refreshes the eligible recipients.
	std::string status(const BattleHex & hoveredHex);
	/// A left-click. Anything but WAITING is final and waits for `conclude`.
	Outcome click(const BattleHex & hex);
	/// The decided click, otherwise the skipped prayer: right-click, Escape or
	/// any other way out sends the action without the prayer.
	Resolution conclude();
	/// Sends the request, or reports why nothing was sent. Call once, after the
	/// selector has been cleared, so the sender may re-enter the controller.
	void deliver(const Resolution & resolution) const;
	std::string cancelText() const;
};
