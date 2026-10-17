/*
 * ProjectedMeleeExpiry.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include <map>

class HypotheticBattle;
class StackWithBonuses;
class CStack;
class InitialExportBattleSubject;

struct MeleeBonusSource
{
	const CStack * native = nullptr;
	std::vector<std::shared_ptr<Bonus>> localExports;
};

/// Sequence-local identity adapter; native exported/descendant/unpropagation
/// semantics remain in ProjectedBonusGraph. Never commits or alters live nodes.
class ProjectedMeleeExpiry final
{
	ProjectedBonusGraph graph;
	HypotheticBattle * branch = nullptr;
	std::shared_ptr<InitialExportBattleSubject> ownedSubject;
	std::map<uint32_t, MeleeBonusSource> sources;
	std::map<uint32_t, std::shared_ptr<StackWithBonuses>> units;
	std::map<uint32_t, TConstBonusListPtr> views();
	void publishRemoved(const std::map<uint32_t, TConstBonusListPtr> & before);
public:
	bool capture(HypotheticBattle & battle);
	/// Separate recipient-model producer only: actual canonical owned bindings
	/// and recorded import/branch inputs, never a fabricated native export graph.
	bool captureRecipientLayers(HypotheticBattle & battle);
	void expire(uint32_t actorId, const CSelector & selector);
	void expireSelected(uint32_t actorId, const std::vector<Bonus> & selected);
};
