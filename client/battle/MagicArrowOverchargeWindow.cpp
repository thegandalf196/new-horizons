/*
 * MagicArrowOverchargeWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "MagicArrowOverchargeWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"

namespace
{
constexpr int WINDOW_WIDTH = 420;
constexpr int WINDOW_HEIGHT = 340;

std::string signedMana(int value)
{
	return value >= 0 ? "+" + std::to_string(value) : std::to_string(value);
}
}

MagicArrowOverchargeWindow::MagicArrowOverchargeWindow(MagicArrowOverchargeContext context_)
	: CWindowObject(SHADOW_DISABLED), context(std::move(context_))
{
	pos.w = WINDOW_WIDTH;
	pos.h = WINDOW_HEIGHT;

	// Keep the allocation decision in a predictable place. In particular, do
	// not position it next to the selected stack: on crowded battlefields that
	// made the modal look like a tooltip and could hide it near screen edges.
	center();

	OBJECT_CONSTRUCTION;
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
		ColorRGBA(24, 29, 35, 255), ColorRGBA(156, 132, 85, 255)));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 23, FONT_BIG,
		ETextAlignment::CENTER, Colors::YELLOW, "Magic Arrow"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 46, FONT_SMALL,
		ETextAlignment::CENTER, Colors::WHITE, "Choose optional Overcharge after selecting a target"));

	targetLabel = std::make_shared<CMultiLineLabel>(Rect(16, 55, WINDOW_WIDTH - 32, 45),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	overchargeLabel = std::make_shared<CLabel>(WINDOW_WIDTH / 2, 108, FONT_MEDIUM,
		ETextAlignment::CENTER, Colors::YELLOW, "");

	minus = std::make_shared<CButton>(Point(18, 98), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Reduce Overcharge", "Spend one less optional Mana on Magic Arrow."),
		[this] { setOvercharge((slider ? slider->getValue() : 0) - 1); });
	minus->setTextOverlay("-", FONT_BIG, Colors::WHITE);
	minus->setHoverable(true);

	plus = std::make_shared<CButton>(Point(WINDOW_WIDTH - 98, 98), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Increase Overcharge", "Spend one more optional Mana on Magic Arrow."),
		[this] { setOvercharge((slider ? slider->getValue() : 0) + 1); });
	plus->setTextOverlay("+", FONT_BIG, Colors::WHITE);
	plus->setHoverable(true);

	const int maximum = std::max(0, context.initial.maximumOvercharge);
	slider = std::make_shared<CSlider>(Point(101, 115), WINDOW_WIDTH - 202,
		[this](int value) { setOvercharge(value); }, 1, maximum + 1,
		std::clamp(context.initial.overcharge, 0, maximum), Orientation::HORIZONTAL, CSlider::BLUE);

	costLabel = std::make_shared<CMultiLineLabel>(Rect(16, 147, WINDOW_WIDTH - 32, 38), FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "");
	damageLabel = std::make_shared<CMultiLineLabel>(Rect(16, 190, WINDOW_WIDTH - 32, 55), FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "");
	stateLabel = std::make_shared<CMultiLineLabel>(Rect(16, 251, WINDOW_WIDTH - 32, 35), FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "");

	confirmButton = std::make_shared<CButton>(Point(WINDOW_WIDTH - 194, 292), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm Magic Arrow", "Submit the selected target and Overcharge through the normal hero spell request."),
		[this] { confirm(); });
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);

	cancelButton = std::make_shared<CButton>(Point(WINDOW_WIDTH - 98, 292), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Discard the target and Overcharge choice without spending mana or the Hero Action."),
		[this] { cancel(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);

	refresh();
}

MagicArrowOverchargeValues MagicArrowOverchargeWindow::valuesFor(int overcharge) const
{
	const int maximum = std::max(0, context.initial.maximumOvercharge);
	overcharge = std::clamp(overcharge, 0, maximum);
	if(context.evaluate)
		return context.evaluate(overcharge);

	// Without a target-aware core evaluator, never invent a damage projection.
	MagicArrowOverchargeValues values = context.initial;
	values.overcharge = overcharge;
	values.previewAvailable = false;
	values.additionalMana = overcharge;
	values.totalMana = values.baseMana + values.additionalMana;
	values.affordable = values.totalMana <= values.availableMana;
	return values;
}

void MagicArrowOverchargeWindow::setOvercharge(int overcharge)
{
	const int maximum = std::max(0, context.initial.maximumOvercharge);
	overcharge = std::clamp(overcharge, 0, maximum);
	if(slider && slider->getValue() != overcharge)
		slider->scrollTo(overcharge, false);
	refresh();
}

void MagicArrowOverchargeWindow::refresh()
{
	const int selected = slider ? slider->getValue() : context.initial.overcharge;
	const auto values = valuesFor(selected);

	const std::string targetSummary = values.targetDescription + "\nMagic resistance: "
		+ std::to_string(values.magicResistancePercent) + "% chance; estimates assume the spell lands.";
	if(targetLabel->getText() != targetSummary)
		targetLabel->setText(targetSummary);
	overchargeLabel->setText("Overcharge " + std::to_string(values.overcharge) + " / " + std::to_string(values.maximumOvercharge));
	costLabel->setText("Mana: base " + std::to_string(values.baseMana) + "  surcharge " + signedMana(values.additionalMana)
		+ "\nTotal: " + std::to_string(values.totalMana) + " / available " + std::to_string(values.availableMana));
	if(values.previewAvailable)
		damageLabel->setText("No Overcharge: " + std::to_string(values.baseDamage) + " damage, " + std::to_string(values.baseKills)
			+ " estimated kills\nWith Overcharge " + std::to_string(values.overcharge) + ": "
			+ std::to_string(values.projectedDamage) + " damage, " + std::to_string(values.projectedKills) + " estimated kills");
	else
		damageLabel->setText("No Overcharge: -- damage, -- estimated kills\nWith Overcharge " + std::to_string(values.overcharge)
			+ ": -- damage, -- estimated kills");

	const std::string state = !values.legal ? "Target is no longer legal for Magic Arrow. Cancel to return."
		: !values.affordable ? "Not enough Mana for this Overcharge value."
		: !values.previewAvailable ? "Forecast unavailable; estimate omitted. Confirm to cast or Cancel to return."
		: "Optional overcharge is affordable. Confirm to cast or Cancel to return.";
	if(stateLabel->getText() != state)
		stateLabel->setText(state);

	minus->block(values.overcharge <= 0);
	plus->block(values.overcharge >= values.maximumOvercharge || !values.affordable);
	confirmButton->block(!values.legal || !values.affordable);
}

void MagicArrowOverchargeWindow::confirm()
{
	const int selected = slider ? slider->getValue() : context.initial.overcharge;
	const auto values = valuesFor(selected);
	if(!values.legal || !values.affordable)
	{
		refresh();
		return;
	}

	if(!context.confirm || context.confirm(selected))
		close();
	else
	{
		// The authority rejected the stale selection.  Keep the panel visible so
		// the user can cancel cleanly or choose a newly affordable value.
		context.initial.availableMana = values.availableMana;
		refresh();
	}
}

void MagicArrowOverchargeWindow::cancel()
{
	if(context.cancel)
		context.cancel();
	close();
}

void MagicArrowOverchargeWindow::show(Canvas & to)
{
	// Painting must remain side-effect free.  CLabel::setText and CButton::block
	// request a redraw, so refreshing from show/showAll would recurse through
	// CIntObject::redraw -> showAll until the stack overflows.  State changes
	// are refreshed by the constructor and input callbacks instead.
	CWindowObject::show(to);
}

void MagicArrowOverchargeWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
}
