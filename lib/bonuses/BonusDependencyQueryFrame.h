/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusDependencyFacts.h"
#include "BonusOutputOrigin.h"
#include "IBonusBearer.h"
#include "BonusList.h"
#include "../battle/BattleSide.h"
#include <bitset>
#include <map>
namespace battle { class OwnedUnitCreatureResolver; }
namespace bonusDependencies
{
class CapturedModifiers;
class OwnedModifierOverlay;
/// Value binding, never an authority/native admission certificate. Descriptor
/// owner contains immutable creature values only; no frame/program back-edge.
struct QueryFrameBinding
{
	uint32_t recipientId = 0;
	CreatureID creature;
	int32_t count = 0;
	PlayerColor owner;
	BattleSide side = BattleSide::NONE;
	SlotID slot;
	bool ghost = false, summoned = false, natureSummoned = false;
	std::shared_ptr<const battle::OwnedUnitCreatureResolver> profileOwner;
	bool operator==(const QueryFrameBinding &) const = default;
};
/// Native recipient answers BEFORE CUnitState's creature-ability suppression.
/// No Unit, node, provider, environment or callback is retained. Materialization
/// occurs only at capture/real change/scratch count change, never per probe.
class DLL_LINKAGE OwnedBonusQueryFrame final : public IBonusBearer,
	public std::enable_shared_from_this<OwnedBonusQueryFrame>
{
	friend class CapturedModifiers;
	std::shared_ptr<const uint8_t> leafBaseIdentity;
	std::shared_ptr<const uint8_t> captureIdentity;
	QueryFrameBinding context;
	std::vector<Values> scalar;
	std::shared_ptr<const ownedBonusPayloads::Captured> outputOwner;
	std::shared_ptr<const ownedBonusPayloads::Captured> localSpellOwner;
	std::vector<Bonus> localSpells;
	TConstBonusListPtr stacked, unstacked;
	OutputOrigins outputOrigins;
	std::shared_ptr<const OwnedModifierOverlay> ownLeafSource;
	std::bitset<static_cast<size_t>(BonusType::BUILTIN_BONUSES_COUNT)> rawTypes;
public:
	const QueryFrameBinding & binding() const noexcept { return context; }
	bool matches(uint32_t recipientId, int32_t count, CreatureID creature) const noexcept;
	std::span<const Bonus> sourceLocalSpellEffects() const noexcept { return localSpells; }
	TConstBonusListPtr query(const CSelector & selector, bool unstacked) const;
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = std::string()) const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
	bool mayHaveBonusType(BonusType type) const override;
	bool isBonusCacheable() const override { return false; }
	int32_t getTreeVersion() const override { return 0; }
};
}
