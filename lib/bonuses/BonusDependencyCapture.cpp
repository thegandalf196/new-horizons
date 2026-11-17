/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyCapture.h"
#include "BonusDependencyOverlay.h"
#include "BonusDependencyTerrain.h"
#include <map>
#include <set>
#include <stdexcept>

namespace bonusDependencies
{
namespace
{
struct Occurrence
{
	std::shared_ptr<const Bonus> raw;
	std::shared_ptr<Bonus> output;
	const CBonusSystemNode * exporter;
	std::vector<Stage> stages;
};
class CaptureObserver final : public BonusCollectionObserver
{
	const CBonusSystemNode & recipient;
	std::map<const BonusList *, std::vector<Occurrence>> rows;
	std::vector<Occurrence> suppressed;
public:
	explicit CaptureObserver(const CBonusSystemNode & recipient) : recipient(recipient) {}
	void local(const BonusList & destination, const BonusList & records, const CBonusSystemNode & exporter) override
	{
		for(const auto & raw : records) rows[&destination].push_back({raw, raw, &exporter, {}});
	}
	void updated(const BonusList & input, size_t index, const BonusList & output,
		const std::shared_ptr<Bonus> & value, const CBonusSystemNode & node,
		const BonusEvaluationContext & context, bool primary, bool retained) override
	{
		auto occurrence = rows.at(&input).at(index);
		occurrence.output = value;
		occurrence.stages.push_back({context, &node == &recipient, primary});
		if(retained) rows[&output].push_back(std::move(occurrence));
		else suppressed.push_back(std::move(occurrence));
	}
	void retire(const BonusList & temporary) override { rows.erase(&temporary); }
	std::vector<Occurrence> take(const BonusList & output)
	{
		auto result = std::move(rows[&output]);
		for(auto & occurrence : suppressed) result.push_back(std::move(occurrence));
		return result;
	}
};
bool countFieldSupported(BonusType type)
{
	// Only fields consumed by the shared physical-damage binder. Count-derived
	// movement, initiative, ammo/activation counters and health need other owners.
	switch(type)
	{
	case BonusType::PRIMARY_SKILL: case BonusType::CREATURE_DAMAGE:
	case BonusType::PHYSICAL_DEFENSE: case BonusType::ENEMY_DEFENCE_REDUCTION:
	case BonusType::ENEMY_ATTACK_REDUCTION: case BonusType::GENERAL_ATTACK_REDUCTION:
	case BonusType::PERCENTAGE_DAMAGE_BOOST: case BonusType::GENERAL_DAMAGE_REDUCTION:
	case BonusType::GENERAL_DAMAGE_PREMY: case BonusType::DAMAGE_RECEIVED_CAP:
	case BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE: return true;
	default: return false;
	}
}


bool validateGraph(const BonusGraphQuery & graph, const CBonusSystemNode & root,
	const detail::TerrainCaptureCertificate * terrain)
{
	std::set<const CBonusSystemNode *> active;
	size_t records = 0, edges = 0, visits = 0;
	bool peerMana = false;
	const auto inspect = [&](auto && self, const CBonusSystemNode & node, size_t depth) -> void
	{
		if(depth > 64 || ++visits > 1024 || !active.insert(&node).second)
			throw std::invalid_argument("Unbounded/cyclic modifier source graph");
		for(const auto & raw : graph.local(node))
		{
			if(++records > 8192 || !raw || raw->type == BonusType::TERRAIN_NATIVE
				|| !(terrain ? terrain->classifyMaterialized(*raw) : classifyMaterializedLocal(*raw)).supported)
				throw std::invalid_argument("Unsupported raw modifier source");
			if(detail::heroBaseManaRegeneration(*raw))
			{
				if(node.getNodeType() != BonusNodeType::HERO)
					throw std::invalid_argument("Hero base recovery requires hero exporter");
				peerMana = true;
			}
			if(intersects(classifyMaterializedLocal(*raw).dependencies, Change::Count) && !countFieldSupported(raw->type))
				throw std::invalid_argument("Count-derived field requires its native transition owner");
		}
		const auto & parents = graph.parents(node);
		if(parents.size() > 4096 - edges) throw std::invalid_argument("Oversized modifier source graph");
		edges += parents.size();
		for(const auto * parent : parents)
		{
			if(!parent) throw std::invalid_argument("Missing modifier source parent");
			self(self, *parent, depth + 1);
		}
		active.erase(&node);
	};
	inspect(inspect, root, 0);
	return peerMana;
}
}
CapturedModifiers captureNativeModifiers(const BonusGraphQuery & graph, const CBonusSystemNode & recipient)
{
	return captureNativeOverlayModifiers(graph, recipient, {});
}
CapturedModifiers captureNativeOverlayModifiers(const BonusGraphQuery & graph, const CBonusSystemNode & recipient,
	std::span<const recipientBonusOverlay::Layer> layers)
{
	std::unique_ptr<const detail::TerrainCaptureCertificate> terrain;
	if(recipient.getNodeType() == BonusNodeType::STACK_BATTLE
		&& graph.evaluation(recipient).getNodeType() == BonusNodeType::STACK_BATTLE)
		terrain.reset(new detail::TerrainCaptureCertificate);
	const bool peerMana = validateGraph(graph, recipient, terrain.get()); // Before executing any raw modifier.
	if(layers.size() > 512) throw std::invalid_argument("Oversized certified terrain overlay lineage");
	size_t overlayRecords = 0;
	for(const auto & layer : layers)
	{
		for(const auto count : {layer.added.size(), layer.updated.size(), layer.removed.size(),
			layer.stackedSnapshot ? layer.stackedSnapshot->size() : size_t{0},
			layer.unstackedSnapshot ? layer.unstackedSnapshot->size() : size_t{0}})
		{
			if(count > 8192 - overlayRecords) throw std::invalid_argument("Oversized certified terrain overlay records");
			overlayRecords += count;
		}
		const auto reject = [peerMana](const auto & row)
		{
			if(peerMana && row.type == BonusType::MANA_REGENERATION)
				throw std::invalid_argument("Hero recovery overlay needs fresh native peer collection");
			if(row.type == BonusType::TERRAIN_NATIVE)
				throw std::invalid_argument("Native-ground overlay needs full peer limitation");
		};
		for(const auto * rows : {&layer.added, &layer.updated})
			for(const auto & row : *rows) if(row) reject(*row);
		for(const auto & row : layer.removed) if(row) reject(*row);
		if(layer.stackedSnapshot) for(const auto & row : *layer.stackedSnapshot) reject(row);
		if(layer.unstackedSnapshot) for(const auto & row : *layer.unstackedSnapshot) if(row) reject(*row);
	}
	BonusList output;
	CaptureObserver observer(recipient);
	collectBonusGraphObserved(graph, recipient, output, observer);
	auto occurrences = observer.take(output);
	std::vector<std::shared_ptr<const Bonus>> roots;
	for(const auto & occurrence : occurrences) roots.push_back(occurrence.raw);
	const std::shared_ptr<const ownedBonusPayloads::Captured> owner = ownedBonusPayloads::copyOwnedValues(roots);
	CapturedModifiers result;
	auto recipes = std::make_shared<std::vector<Recipe>>();
	auto identities = std::make_shared<std::vector<OccurrenceIdentity>>();
	std::map<const Bonus *, uint32_t> aliases, rawIds;
	std::map<const CBonusSystemNode *, uint32_t> exporters;
	for(size_t i = 0; i < occurrences.size(); ++i)
	{
		const auto & occurrence = occurrences[i];
		recipes->push_back(detail::captureCertified(owner, i, occurrence.stages, graph.evaluation(recipient), true, terrain.get(), &output, occurrence.output.get()));
		const auto [alias, inserted] = aliases.emplace(occurrence.output.get(), static_cast<uint32_t>(aliases.size()));
		const auto exporter = exporters.emplace(occurrence.exporter, static_cast<uint32_t>(exporters.size())).first->second;
		const auto rawId = rawIds.emplace(occurrence.raw.get(), static_cast<uint32_t>(rawIds.size())).first->second;
		identities->push_back({exporter, rawId, alias->second, occurrence.raw->type});
	}
	result.recipes = std::move(recipes);
	result.identities = std::move(identities);
	if(!layers.empty())
	{
		OutputOrigins origins;
		for(const auto & [row, alias] : aliases) origins.emplace(row, OutputOrigin{result.rawOriginIdentity, alias});
		result.overlays.push_back(std::make_shared<const OwnedModifierOverlay>(layers, origins, nullptr, true));
	}
	return result; // All native graph/context identities retire here.
}
bool CapturedModifiers::canRefresh(Change changes) const noexcept
{
	if(intersects(changes, Change::Profile | Change::Controller | Change::Round | Change::Roster | Change::ActiveEffects | Change::TerrainContext)) return false;
	return std::ranges::all_of(*recipes, [&](const auto & recipe) { return recipe.canRefresh(changes); });
}
}
