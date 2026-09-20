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
constexpr int WINDOW_WIDTH = 360;
constexpr int WINDOW_HEIGHT = 244;

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

	// Place the panel beside the current target/card, then keep it on screen at
	// small resolutions.  `anchor` is supplied by the targeting controller in
	// screen coordinates; no battlefield geometry is reconstructed here.
	moveTo(context.anchor + Point(18, 18));
	fitToScreen(4);

	OBJECT_CONSTRUCTION;
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
		ColorRGBA(24, 29, 35, 255), ColorRGBA(156, 132, 85, 255)));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 23, FONT_BIG,
		ETextAlignment::CENTER, Colors::YELLOW, "Magic Arrow"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 46, FONT_SMALL,
		ETextAlignment::CENTER, Colors::WHITE, "Choose optional Overcharge after selecting a target"));

	targetLabel = std::make_shared<CMultiLineLabel>(Rect(16, 57, WINDOW_WIDTH - 32, 28),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	overchargeLabel = std::make_shared<CLabel>(WINDOW_WIDTH / 2, 98, FONT_MEDIUM,
		ETextAlignment::CENTER, Colors::YELLOW, "");

	minus = std::make_shared<CButton>(Point(18, 88), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Reduce Overcharge", "Spend one less optional Mana on Magic Arrow."),
		[this] { setOvercharge((slider ? slider->getValue() : 0) - 1); });
	minus->setTextOverlay("-", FONT_BIG, Colors::WHITE);
	minus->setHoverable(true);

	plus = std::make_shared<CButton>(Point(WINDOW_WIDTH - 98, 88), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Increase Overcharge", "Spend one more optional Mana on Magic Arrow."),
		[this] { setOvercharge((slider ? slider->getValue() : 0) + 1); });
	plus->setTextOverlay("+", FONT_BIG, Colors::WHITE);
	plus->setHoverable(true);

	const int maximum = std::max(0, context.initial.maximumOvercharge);
	slider = std::make_shared<CSlider>(Point(91, 105), WINDOW_WIDTH - 182,
		[this](int value) { setOvercharge(value); }, 1, maximum + 1,
		std::clamp(context.initial.overcharge, 0, maximum), Orientation::HORIZONTAL, CSlider::BLUE);

	costLabel = std::make_shared<CLabel>(16, 139, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "");
	damageLabel = std::make_shared<CLabel>(16, 158, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "");
	stateLabel = std::make_shared<CLabel>(16, 183, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "");

	confirmButton = std::make_shared<CButton>(Point(WINDOW_WIDTH - 194, 204), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm Magic Arrow", "Submit the selected target and Overcharge through the normal hero spell request."),
		[this] { confirm(); });
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);

	cancelButton = std::make_shared<CButton>(Point(WINDOW_WIDTH - 98, 204), AnimationPath::builtin("settingsWindow/button80"),
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

	// This fallback is only for a standalone UI consumer (for example a
	// layout test).  The live battle path always supplies `evaluate`, so the
	// authoritative Wisdom, resistance and target formulas stay server-owned.
	MagicArrowOverchargeValues values = context.initial;
	values.overcharge = overcharge;
	values.additionalMana = overcharge;
	values.totalMana = values.baseMana + values.additionalMana;
	values.projectedDamage = static_cast<int>(static_cast<int64_t>(values.baseDamage) * (100 + 15 * overcharge) / 100);
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

	if(targetLabel->getText() != values.targetDescription)
		targetLabel->setText(values.targetDescription);
	overchargeLabel->setText("Overcharge " + std::to_string(values.overcharge) + " / " + std::to_string(values.maximumOvercharge));
	costLabel->setText("Mana: base " + std::to_string(values.baseMana) + "  surcharge " + signedMana(values.additionalMana)
		+ "  total " + std::to_string(values.totalMana) + " / " + std::to_string(values.availableMana));
	damageLabel->setText("Projected damage: " + std::to_string(values.baseDamage) + " -> " + std::to_string(values.projectedDamage));

	const std::string state = !values.legal ? "Target is no longer legal for Magic Arrow. Cancel to return."
		: values.affordable ? "Optional overcharge is affordable. Confirm to cast or Cancel to return."
		: "Not enough Mana for this Overcharge value.";
	if(stateLabel->getText() != state)
		stateLabel->setText(state);

	minus->block(values.overcharge <= 0);
	plus->block(values.overcharge >= values.maximumOvercharge || !values.affordable);
	confirmButton->block(!values.legal || !values.affordable);

	// A context can become stale while this modal is open (for example when a
	// battle ends or another request changes Mana).  The next redraw reevaluates
	// the same value and disables confirmation instead of silently submitting.
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
