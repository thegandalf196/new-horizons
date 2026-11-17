/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrozenScoringSource.h"
#include "OwnedBonusPayloads.h"
#include "../battle/OwnedUnitCreatureResolver.h"
namespace frozenScoring
{
struct OwnedSource::Data
{
	std::shared_ptr<const ownedBonusPayloads::Captured> owner;
	std::vector<Bonus> stacked;
	std::optional<Records> unstacked;
	bool veteran;
	TConstBonusListPtr query(const CSelector & selector, bool beforeStacking, bool currentVeteran) const
	{
		return frozenScoring::query({stacked, unstacked, currentVeteran}, selector, beforeStacking);
	}
};
OwnedSource::OwnedSource(bonusDependencies::QueryFrameBinding binding, std::shared_ptr<const Data> data)
	: context(std::move(binding)), data(std::move(data)) {}
std::shared_ptr<const OwnedSource> OwnedSource::capture(bonusDependencies::QueryFrameBinding binding,
	std::span<const Bonus> stacked, const std::optional<Records> & unstacked, bool veteran)
{
	if(!binding.profileOwner || binding.count < 0 || stacked.size() > 32768
		|| (unstacked && unstacked->size() > 32768 - stacked.size())) return {};
	try
	{
		(void)binding.profileOwner->creature(binding.creature);
		std::vector<std::shared_ptr<const Bonus>> roots;
		for(const auto & value : stacked)
		{
			if(typeid(value) != typeid(Bonus)) return {};
			roots.push_back(std::make_shared<const Bonus>(value));
		}
		if(unstacked) for(const auto & value : *unstacked) roots.push_back(value);
		auto data = std::make_shared<Data>();
		data->owner = ownedBonusPayloads::copyOwnedValues(roots);
		data->veteran = veteran;
		size_t index = 0;
		for(size_t i = 0; i < stacked.size(); ++i) data->stacked.push_back(*data->owner->roots().at(index++));
		if(unstacked)
		{
			data->unstacked.emplace();
			for(size_t i = 0; i < unstacked->size(); ++i) data->unstacked->push_back(data->owner->roots().at(index++));
		}
		return std::shared_ptr<const OwnedSource>(new OwnedSource(std::move(binding), std::move(data)));
	}
	catch(const std::exception &) { return {}; }
}
bool OwnedSource::matchesProfile(uint32_t recipientId, CreatureID creature) const noexcept
{
	return context.recipientId == recipientId && context.creature == creature;
}
TConstBonusListPtr OwnedSource::query(const CSelector & selector, bool beforeStacking, bool veteran) const
{
	struct Loan { std::shared_ptr<const Data> owner; TConstBonusListPtr list; };
	const auto loan = std::make_shared<Loan>(Loan{data, data->query(selector, beforeStacking, veteran)});
	return TConstBonusListPtr(loan, loan->list.get());
}
TConstBonusListPtr OwnedSource::getAllBonuses(const CSelector & selector, const std::string &) const
{
	return query(selector, false, data->veteran);
}
TConstBonusListPtr OwnedSource::getUnstackedBonuses(const CSelector & selector) const
{
	return query(selector, true, data->veteran);
}
}
