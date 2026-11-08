/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEffect.h"
#include "../../../bonuses/Bonus.h"
#include <optional>

namespace nativeEvents { struct Context; }
namespace spells::effects::native
{
class DLL_LINKAGE Timed final : public UnitEffect
{
public:
	explicit Timed(bool bindPatch = true, bool shieldPatch = true) : bindPatch(bindPatch), shieldPatch(shieldPatch) {}
	ResistanceShareKind resistanceShareKind() const override { return ResistanceShareKind::Timed; }
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
	/// Current numeric records only; unsupported descriptors have no read-only plan.
	std::optional<std::vector<Bonus>> preparedRecipientBonuses(const Mechanics *, const battle::Unit *) const;
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::CANONICAL_TIMED; }
private:
	struct CastPlan
	{
		bool allowed = true;
		std::string key;
		JsonMap converted;
		std::optional<Bonus> prepared;
		std::string_view preparedKey;
	};
	struct RecipientPlan
	{
		JsonMap buffer;
		std::optional<Bonus> prepared;
		std::string_view preparedKey;
	};
	CastPlan prepareCast(const Mechanics *) const;
	RecipientPlan prepareRecipient(const Mechanics *, const battle::Unit *, const CastPlan &) const;
	static bool slowSpeed(const Mechanics &);
	bool bindPatch, shieldPatch;
	JsonMap convert(const Mechanics *) const;
	void specialty(const Mechanics *, const battle::Unit *, JsonMap &, double * preparedValue = nullptr) const;
	void recipientPower(const Mechanics *, const battle::Unit *, const std::string &, JsonMap &) const;
	void describe(const nativeEvents::Context &, const battle::Unit *, const std::string &, const JsonMap &) const;
};
}
