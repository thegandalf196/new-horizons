/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedFrameRawCapture.h"
#include "OwnedUnitBonusFrameBatch.h"
class CStack;
namespace battle::ownedFrameCapture
{
OwnedUnitBonusFrame::Identity identity(const CStack &);
struct Builder
{
	std::shared_ptr<const OwnedUnitCreatureResolver> creatures;
	std::vector<const CCreature *> visibleForms; // Immediate capture lease only.
	std::map<CreatureID, size_t> traitGraphs;
	std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs;
	std::vector<std::vector<OwnedRecipientBonusGraph::SourceRelation>> relations;
	std::vector<OwnedUnitBonusFrameBatch::Recipe> recipes;
	explicit Builder(const std::vector<const CStack *> & visible);
	void recipient(const CStack &, const std::vector<const CStack *> & visible, OwnedUnitEnvironment::Facts, size_t position);
private:
	size_t variant(const CStack & main, const CStack & query, CreatureID form, bool native, bool peer);
};
}
