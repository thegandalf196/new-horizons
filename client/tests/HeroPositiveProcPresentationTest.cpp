/* HeroPositiveProcPresentationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../battle/HeroPositiveProcPresentation.h"

namespace
{
constexpr bool everyFlagCombination()
{
	for(unsigned flags = 0; flags < 16; ++flags)
	{
		const auto cues = heroPositiveProcPresentation::effects(flags & 1, flags & 2, flags & 4, flags & 8);
		if(cues[0] != ((flags & 7) ? EBattleEffect::GOOD_LUCK : EBattleEffect::INVALID)
			|| cues[1] != ((flags & 8) ? EBattleEffect::GOOD_MORALE : EBattleEffect::INVALID))
			return false;
	}
	return true;
}
static_assert(everyFlagCombination());
static_assert(heroPositiveProcPresentation::effects(false, true, true, false)[0] == EBattleEffect::GOOD_LUCK);
static_assert(heroPositiveProcPresentation::effects(true, false, false, true)[1] == EBattleEffect::GOOD_MORALE);

bool firstDrawMarkerIsPerCueAndOnceOnly()
{
	heroPositiveProcPresentation::TraceContext original;
	if(!original.markFirstDrawReturned() || original.markFirstDrawReturned())
		return false;
	heroPositiveProcPresentation::TraceContext next;
	return next.markFirstDrawReturned() && !next.markFirstDrawReturned();
}
// Synthetic summary input: no random sequence fields or eligibility inference.
struct ReceiptSummary
{
	int heroValue;
	int baseThreshold;
	int diceSize;
};

bool receiptEvidenceDefaultsAndAbsence()
{
	heroPositiveProcPresentation::TraceContext context;
	if(context.luckReceiptPresent || context.moraleReceiptPresent
		|| context.luckHeroValue != -1 || context.luckBaseThreshold != -1
		|| context.luckDiceSize != -1 || context.moraleHeroValue != -1
		|| context.moraleBaseThreshold != -1 || context.moraleDiceSize != -1
		|| context.moraleGrantId != 0 || context.luckyDayPolicyScale != -1
		|| context.erraticManaPolicyScale != -1 || context.druidicLuckPolicyScale != -1
		|| context.heroicSpiritPolicyScale != -1)
		return false;
	const ReceiptSummary old{6, 24, 100};
	heroPositiveProcPresentation::captureReceiptEvidence(context, &old, &old, 9);
	heroPositiveProcPresentation::captureReceiptEvidence<ReceiptSummary>(context, nullptr, nullptr, 0);
	return !context.luckReceiptPresent && !context.moraleReceiptPresent
		&& context.luckHeroValue == -1 && context.luckBaseThreshold == -1
		&& context.luckDiceSize == -1 && context.moraleHeroValue == -1
		&& context.moraleBaseThreshold == -1 && context.moraleDiceSize == -1
		&& context.moraleGrantId == 0;
}

bool presentNoProcEvidenceIsCopiedNotAliased()
{
	heroPositiveProcPresentation::TraceContext copied;
	{
		ReceiptSummary luck{6, 24, 100};
		ReceiptSummary morale{5, 15, 100};
		heroPositiveProcPresentation::TraceContext source;
		heroPositiveProcPresentation::captureReceiptEvidence(source, &luck, &morale, 42);
		copied = source;
		luck = {1, 1, 1};
		morale = {2, 2, 2};
	}
	if(!copied.luckReceiptPresent || !copied.moraleReceiptPresent || copied.flags != 0
		|| copied.luckHeroValue != 6 || copied.luckBaseThreshold != 24 || copied.luckDiceSize != 100
		|| copied.moraleHeroValue != 5 || copied.moraleBaseThreshold != 15 || copied.moraleDiceSize != 100
		|| copied.moraleGrantId != 42)
		return false;
	const auto noProc = heroPositiveProcPresentation::effects(false, false, false, false);
	return noProc[0] == EBattleEffect::INVALID && noProc[1] == EBattleEffect::INVALID
		&& everyFlagCombination();
}

bool individualReceiptPresenceRemainsDistinct()
{
	heroPositiveProcPresentation::TraceContext context;
	const ReceiptSummary summary{3, 12, 250};
	heroPositiveProcPresentation::captureReceiptEvidence<ReceiptSummary>(context, &summary, nullptr, 0);
	if(!context.luckReceiptPresent || context.moraleReceiptPresent || context.moraleHeroValue != -1)
		return false;
	heroPositiveProcPresentation::captureReceiptEvidence<ReceiptSummary>(context, nullptr, &summary, 7);
	return !context.luckReceiptPresent && context.luckHeroValue == -1
		&& context.moraleReceiptPresent && context.moraleHeroValue == 3
		&& context.moraleBaseThreshold == 12 && context.moraleDiceSize == 250
		&& context.moraleGrantId == 7 && context.flags == 0;
}
}

int main()
{
	return everyFlagCombination() && firstDrawMarkerIsPerCueAndOnceOnly()
		&& receiptEvidenceDefaultsAndAbsence() && presentNoProcEvidenceIsCopiedNotAliased()
		&& individualReceiptPresenceRemainsDistinct() ? 0 : 1;
}
