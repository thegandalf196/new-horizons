/* Adventure map button construction. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "AdventureMapWidget.h"
#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "render/Colors.h"
#include "render/AssetGenerator.h"
#include "render/IRenderHandler.h"
#include "../../lib/json/JsonNode.h"

std::shared_ptr<CIntObject> AdventureMapWidget::buildMapButton(const JsonNode & input)
{
	auto position = readTargetArea(input["area"]);
	auto image = AnimationPath::fromJson(input["image"]);
	auto help = readHintText(input["help"]);
	bool playerColored = input["playerColored"].Bool();
	// Store default image path for buttonLayerOther (used to restore when no custom icon is set)
	if (input["name"].String() == "buttonLayerOther")
	{
		defaultLayerOtherImage = input["image"].String();
		defaultLayerOtherGenerateSmall = input["generateSmall"].Bool();
	}
	if(!input["generateFromBaseImage"].isNull())
	{
		bool small = input["generateSmall"].Bool();
		auto assetGenerator = ENGINE->renderHandler().getAssetGenerator();
		auto layout = assetGenerator->createAdventureMapButton(ImagePath::fromJson(input["generateFromBaseImage"]), small);
		assetGenerator->addAnimationFile(AnimationPath::builtin("SPRITES/" + input["image"].String()), layout);
		ENGINE->renderHandler().updateGeneratedAssets();
	}

	auto button = std::make_shared<CButton>(position.topLeft(), image, help, 0, EShortcut::NONE, playerColored);

	loadButtonBorderColor(button, input["borderColor"]);
	loadButtonHotkey(button, input["hotkey"]);
	loadButtonCallback(button, input["callback"]);
	if(input["name"].String() == "bugReportButton")
	{
		button->setHelp(CButton::tooltip("Report a bug", "Open a public GitHub report draft. Diagnostic logs are optional and reviewed first."));
	}

	return button;
}
