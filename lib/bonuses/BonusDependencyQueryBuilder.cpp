/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyCapture.h"
#include "BonusDependencyOverlay.h"
#include "../battle/OwnedUnitCreatureResolver.h"
namespace bonusDependencies
{
namespace
{
std::shared_ptr<const ownedBonusPayloads::Captured> ownSpells(std::span<const Bonus> source)
{
	if(source.size() > 8192) throw std::invalid_argument("Oversized exact source-local spell program");
	std::vector<std::shared_ptr<const Bonus>> roots;
	for(const auto & value : source)
	{
		if(value.source != BonusSource::SPELL_EFFECT || !classifyMaterializedLocal(value).supported)
			throw std::invalid_argument("Unsupported exact source-local spell program");
		roots.push_back(std::make_shared<const Bonus>(value));
	}
	return ownedBonusPayloads::copyOwnedValues(roots);
}
std::vector<Bonus> spellValues(const ownedBonusPayloads::Captured & source)
{
	std::vector<Bonus> result;
	for(const auto & row : source.roots()) result.push_back(*row);
	return result;
}
bool sameProfile(QueryFrameBinding a, QueryFrameBinding b)
{
	a.count = b.count = 0;
	return a == b;
}
}
std::shared_ptr<const CapturedModifiers> CapturedModifiers::bindQueries(const QueryFrameBinding & binding,
	std::span<const Bonus> exactLocalSpells) const
{
	if(queryBinding || binding.count < 0 || !binding.profileOwner) return {};
	try
	{
		(void)binding.profileOwner->creature(binding.creature);
		auto result = std::make_shared<CapturedModifiers>(*this);
		result->queryBinding = binding;
		result->localSpellOwner = ownSpells(exactLocalSpells);
		result->localSpells = spellValues(*result->localSpellOwner);
		return result;
	}
	catch(const std::exception &) { return {}; }
}
bool CapturedModifiers::ownsQueryFrame(const OwnedBonusQueryFrame & frame) const noexcept
{
	return queryBinding && captureIdentity == frame.captureIdentity && sameProfile(*queryBinding, frame.binding());
}
std::shared_ptr<const RecipientLayerSource> CapturedModifiers::bindRecipientLayerSource(
	std::shared_ptr<const OwnedBonusQueryFrame> source) const
{
	if(!source || !ownsQueryFrame(*source)) return {};
	const auto actualOwner = source->weak_from_this().lock();
	if(!actualOwner || actualOwner.owner_before(source) || source.owner_before(actualOwner)) return {};
	return std::shared_ptr<const RecipientLayerSource>(new RecipientLayerSource(captureIdentity, std::move(source)));
}
bool CapturedModifiers::ownsRecipientLayerSource(const RecipientLayerSource & source) const noexcept
{
	return captureIdentity == source.baseIdentity && source.frame && ownsQueryFrame(*source.frame);
}
std::shared_ptr<const CapturedModifiers> CapturedModifiers::appendRecipientLayer(const recipientBonusOverlay::Layer & layer,
	std::span<const Bonus> exactLocalSpells, const OwnedBonusQueryFrame & exactBefore,
	const RecipientLayerSource * fixedSource) const
{
	if(!queryBinding || !sameProfile(*queryBinding, exactBefore.binding())
		|| (fixedSource && !ownsRecipientLayerSource(*fixedSource))
		|| (captureIdentity != exactBefore.captureIdentity && captureIdentity != exactBefore.leafBaseIdentity)) return {};
	try
	{
		OutputOrigins allowedOrigins = exactBefore.outputOrigins;
		if(fixedSource)
			for(const auto & [row, origin] : fixedSource->frame->outputOrigins) allowedOrigins.emplace(row, origin);
		const auto ownsRow = [](const OwnedBonusQueryFrame & frame, const auto & row)
		{
			const auto & roots = frame.outputOwner->roots();
			const auto found = std::ranges::find_if(roots, [&](const auto & owned) { return owned.get() == row.get(); });
			return found != roots.end() && !found->owner_before(row) && !row.owner_before(*found);
		};
		for(const auto & row : layer.removed)
		{
			if(exactBefore.outputOrigins.contains(row.get()))
			{
				if(!ownsRow(exactBefore, row)) return {};
			}
			else if(fixedSource && fixedSource->frame->outputOrigins.contains(row.get()))
			{
				if(!ownsRow(*fixedSource->frame, row)) return {};

			}
		}

		auto result = std::make_shared<CapturedModifiers>(*this);
		result->captureIdentity = std::make_shared<const uint8_t>(0);
		result->leafBaseIdentity = captureIdentity;
		result->queryBinding = exactBefore.binding();
		result->localSpellOwner = ownSpells(exactLocalSpells);
		result->localSpells = spellValues(*result->localSpellOwner);
		const auto leaf = std::make_shared<const OwnedModifierOverlay>(
			std::span<const recipientBonusOverlay::Layer>(&layer, 1), allowedOrigins,
			exactBefore.leafBaseIdentity == captureIdentity ? exactBefore.ownLeafSource.get() : nullptr);
		size_t records = leaf->recordCount();
		if(overlays.size() >= 512) return {};
		for(const auto & ancestor : overlays)
		{
			if(ancestor->recordCount() > 8192 - records) return {};
			records += ancestor->recordCount();
		}
		result->overlays.push_back(leaf);
		return result;
	}
	catch(const std::exception &) { return {}; }
}
std::shared_ptr<const OwnedBonusQueryFrame> CapturedModifiers::refreshQueries(const QueryFrameBinding & binding,
	Change changes, std::shared_ptr<const OwnedBonusQueryFrame> previous) const
{
	if(!queryBinding || !sameProfile(*queryBinding, binding) || binding.count < 0 || !canRefresh(changes)) return {};
	if(previous && (!ownsQueryFrame(*previous) || !sameProfile(previous->binding(), binding))) return {};
	const auto priorCount = previous ? previous->binding().count : queryBinding->count;
	if(priorCount != binding.count && !intersects(changes, Change::Count)) return {};
	if(previous && previous->binding() == binding) return previous;
	auto current = materialize(binding.count, changes, previous ? &previous->scalar : nullptr);
	if(!current) return {};
	try
	{
		auto result = std::make_shared<OwnedBonusQueryFrame>();
		result->captureIdentity = captureIdentity;
		result->leafBaseIdentity = leafBaseIdentity;
		result->context = binding;
		result->scalar = std::move(current->scalar);
		result->localSpellOwner = localSpellOwner;
		result->localSpells = localSpells;
		std::vector<std::shared_ptr<const Bonus>> roots;
		for(const auto & row : *current->stacked) roots.push_back(row);
		for(const auto & row : *current->unstacked) roots.push_back(row);

		result->outputOwner = ownedBonusPayloads::copyOwnedValues(roots);
		auto stacked = std::make_shared<BonusList>(), unstacked = std::make_shared<BonusList>();
		size_t index = 0;
		const auto copyLane = [&](const BonusList & input, BonusList & output)
		{
			for(const auto & row : input)
			{
				const auto & copy = result->outputOwner->roots().at(index++);
				result->outputOrigins.emplace(copy.get(), current->origins.at(row.get()));
				output.push_back(copy);
			}
		};
		copyLane(*current->stacked, *stacked);
		copyLane(*current->unstacked, *unstacked);
		if(leafBaseIdentity && !overlays.empty()) result->ownLeafSource = overlays.back();
		result->stacked = std::move(stacked);
		result->unstacked = std::move(unstacked);
		for(const auto & recipe : *recipes)
		{
			const auto type = static_cast<size_t>(recipe.rawRecord()->type);
			if(type < result->rawTypes.size()) result->rawTypes[type] = true;
		}
		for(size_t type = 0; type < result->rawTypes.size(); ++type)
			if(!result->rawTypes[type]) result->rawTypes[type] = std::ranges::any_of(overlays, [type](const auto & layer) {
				return layer->mayHaveBonusType(static_cast<BonusType>(type));
			});
		return result;
	}
	catch(const std::exception &) { return {}; }
}
}
