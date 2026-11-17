/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyOverlay.h"
#include "BonusDependencyFacts.h"
#include "BonusList.h"
#include <set>
#include <stdexcept>
namespace bonusDependencies
{
bool supportsRecipientRecord(const Bonus & value)
{
	return value.type != BonusType::TERRAIN_NATIVE
		&& !value.propagator && !value.propagationUpdater && classify(value).supported;
}

OwnedModifierOverlay::OwnedModifierOverlay(std::span<const recipientBonusOverlay::Layer> source,
	const OutputOrigins & currentOrigins, const OwnedModifierOverlay * previousOwnLeaf, bool coldSourceProjection)
{
	if(source.size() > 512) throw std::invalid_argument("Oversized modifier overlay lineage");
	std::vector<std::shared_ptr<const Bonus>> roots;
	std::vector<std::vector<size_t>> handles, localRemovals;
	std::set<const Bonus *> earlier;
	for(const auto & layer : source)
	{
		if(layer.veteranCohesionEarned) throw std::invalid_argument("Opaque recipient postprocessor");
		handles.emplace_back();
		sourceRemovals.emplace_back();
		localRemovals.emplace_back();
		const auto add = [&](const std::shared_ptr<Bonus> & record)
		{
			if(roots.size() >= 8192 || !record || !supportsRecipientRecord(*record)) throw std::invalid_argument("Unsupported recipient overlay row");
			handles.back().push_back(roots.size());
			roots.push_back(record);
		};
		for(const auto & record : layer.added)
		{
			if(currentOrigins.contains(record.get())) throw std::invalid_argument("Current answer aliased as recipient addition");
			add(record);
			earlier.insert(record.get());
		}
		for(const auto & record : layer.updated)
		{
			add(record);
			earlier.insert(record.get());
		}
		for(const auto & record : layer.removed)
		{
			add(record);
			const auto found = currentOrigins.find(record.get());
			const auto origin = found != currentOrigins.end() ? std::optional(found->second)
				: previousOwnLeaf ? previousOwnLeaf->sourceRemoval(record) : std::nullopt;
			if(origin)
			{
				sourceRemovals.back().push_back(*origin);
				removalReceipts.push_back({record,*origin});
			}
			else if(earlier.contains(record.get())) localRemovals.back().push_back(roots.size()-1);
			else if(!(coldSourceProjection && layer.stackedSnapshot && layer.unstackedSnapshot
				&& recipientBonusOverlay::isCapturedProjectionEffect(record.get(),layer.capturedGroups)))
				throw std::invalid_argument("Unresolved current recipient removal identity");
		}
		if(layer.stackedSnapshot)
			for(const auto & value : *layer.stackedSnapshot) add(std::make_shared<Bonus>(value));
		if(layer.unstackedSnapshot)
			for(const auto & record : *layer.unstackedSnapshot)
			{
				add(record);
				earlier.insert(record.get());
			}
	}
	owner = ownedBonusPayloads::copyOwnedValues(roots);
	bindOwnedLayers(source, handles, localRemovals, roots);
}
void OwnedModifierOverlay::bindOwnedLayers(std::span<const recipientBonusOverlay::Layer> source,
	const std::vector<std::vector<size_t>> & handles, const std::vector<std::vector<size_t>> & localRemovals,
	const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	for(size_t i = 0; i < owner->roots().size(); ++i)
	{
		const auto & row = owner->roots()[i];
		ownedOrigins.try_emplace(row.get(), OutputOrigin{originOwner, static_cast<uint32_t>(i)});
		const auto type = static_cast<size_t>(row->type);
		if(type < rawTypes.size()) rawTypes[type] = true;
	}
	for(size_t i = 0; i < source.size(); ++i)
	{
		layers.emplace_back(source[i].view());
		stackedOrigins.emplace_back();
		auto & layer = layers.back();
		size_t cursor = 0;
		const auto next = [&]() { return owner->roots().at(handles[i].at(cursor++)); };
		for(auto & record : layer.added) record = next();
		for(auto & record : layer.updated) record = next();
		std::set<std::shared_ptr<Bonus>> removed;
		for(size_t j = 0; j < layer.removed.size(); ++j) removed.insert(next());
		layer.removed.swap(removed);
		if(layer.stackedSnapshot)
			for(auto & value : *layer.stackedSnapshot)
			{
				const auto record = next();
				value = *record;
				stackedOrigins.back().push_back(ownedOrigins.at(record.get()));
			}
		if(layer.unstackedSnapshot)
			for(auto & record : *layer.unstackedSnapshot) record = next();
		for(const auto root : localRemovals[i])
		{
			const auto origin = ownedOrigins.at(owner->roots().at(root).get());
			sourceRemovals[i].push_back(origin);
			removalReceipts.push_back({roots[root], origin});
		}
	}
}

std::optional<OutputOrigin> OwnedModifierOverlay::sourceRemoval(const std::shared_ptr<Bonus> & record) const
{
	for(const auto & receipt : removalReceipts)
	{
		const auto identity = receipt.source.lock();
		if(identity && identity.get() == record.get() && !identity.owner_before(record) && !record.owner_before(identity))
			return receipt.origin;
	}
	return {};
}
bool OwnedModifierOverlay::mayHaveBonusType(BonusType type) const
{
	const auto index = static_cast<size_t>(type);
	return index >= rawTypes.size() || rawTypes[index];
}
TConstBonusListPtr OwnedModifierOverlay::apply(const BonusList & inherited,
	OutputOrigins & origins, bool unstacked) const
{
	class Trace final : public recipientBonusOverlay::OutputObserver
	{
		OutputOrigins & origins;
	public:
		explicit Trace(OutputOrigins & origins) : origins(origins) {}
		void cloned(const Bonus & source, const std::shared_ptr<Bonus> & output) override
		{
			origins.insert_or_assign(output.get(), origins.at(&source));
		}
	} trace(origins);
	for(const auto & [row, origin] : ownedOrigins) origins.insert_or_assign(row, origin);
	TConstBonusListPtr current = std::make_shared<BonusList>(inherited);
	for(size_t i = 0; i < layers.size(); ++i)
	{
		auto layer = layers[i];
		if(layer.stackedSnapshot)
			for(size_t j = 0; j < layer.stackedSnapshot->size(); ++j)
				origins.insert_or_assign(&(*layer.stackedSnapshot)[j], stackedOrigins[i][j]);
		for(const auto & row : *current)
			if(std::ranges::find(sourceRemovals[i], origins.at(row.get())) != sourceRemovals[i].end()) layer.removed.insert(row);
		auto next = std::make_shared<BonusList>();
		recipientBonusOverlay::copyInherited(*next, *current, layer.view(), unstacked);
		recipientBonusOverlay::appendObserved(*next, layer.view(), Selector::all, unstacked, trace);
		current = recipientBonusOverlay::finish(next, layer.view(), Selector::all);
		// Materialized recipient values: no updater/limiter or extra stacking.
		for(const auto & row : *current)
			if(!origins.contains(row.get()))
				throw std::invalid_argument("Opaque native recipient postprocessor output");
	}
	return current;
}
}
