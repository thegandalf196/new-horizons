/* Offline diagnostic only, part of VCMI. GPL-2.0-or-later. */
import {validate, rect} from './model.mjs';

const PRIMARY = ['attack', 'defense', 'spellPower', 'knowledge'];
const issue = (code, message) => ({code, message});

function referencePosition(binding)
{
	const fixed = {'hero.name': {x: 320, y: 33}, 'hero.level': {x: 320, y: 162},
		'selection.instructions': {x: 320, y: 516}, 'hero.portrait': {x: 297, y: 66},
		'page.previous': {x: 24, y: 532}, 'page.next': {x: 66, y: 532}, 'selection.confirm': {x: 288, y: 532}};
	if(Object.hasOwn(fixed, binding)) return fixed[binding];
	const primary = PRIMARY.findIndex(name => binding === `primary.${name}`);
	return primary >= 0 ? {x: 36 + primary * 148, y: 196} : null;
}

function semanticGeometry(layer, bounds, issues)
{
	const position = {x: bounds.x, y: bounds.y};
	const center = {x: bounds.x + Math.floor(bounds.w / 2), y: bounds.y + Math.floor(bounds.h / 2)};
	const label = ['hero.name', 'hero.level', 'selection.instructions'].includes(layer.binding);
	if(label)
	{
		if(!['text', 'banner'].includes(layer.type)) { issues.push(issue('BINDING_TYPE', 'Centered text role requires a text or banner placeholder.')); return null; }
		issues.push(issue('TEXT_CONTEXT', 'Native translated text, font, alignment and glyph metrics must remain supplied by CLevelWindow; placeholder labels are not game text.'));
		return {consumer: 'CLabel', kind: 'centered-text-position', position: center, maxWidth: bounds.w};
	}
	if(layer.binding === 'hero.portrait')
	{
		if(layer.type !== 'hero') { issues.push(issue('BINDING_TYPE', 'Portrait role requires a hero placeholder.')); return null; }
		if(bounds.w !== 58 || bounds.h !== 64) issues.push(issue('PORTRAIT_SIZE', 'CHeroArea has native fixed 58x64 bounds; editor resizing is not supported by that constructor.'));
		issues.push(issue('HERO_CONTEXT', 'Native hero identity, portrait and popup callback require the existing live/captured context.'));
		return {consumer: 'CHeroArea', kind: 'top-left-position', position, nativeSize: {w: 58, h: 64}};
	}
	const primary = PRIMARY.findIndex(name => layer.binding === `primary.${name}`);
	if(primary >= 0)
	{
		if(layer.type !== 'icon') { issues.push(issue('BINDING_TYPE', 'Primary attribute role requires an icon placeholder.')); return null; }
		issues.push(issue('FIXED_IMAGE', 'Native PSKIL42 frame dimensions and primary-gain branch are engine-owned; editor width/height do not resize CAnimImage.'));
		return {consumer: 'CAnimImage', kind: 'top-left-position', position, nativeResource: {builtin: 'PSKIL42', frame: primary, group: 0}};
	}
	if(PRIMARY.some(name => layer.binding === `primary.${name}.gain`))
	{
		issues.push(issue('COMBINED_GAIN', 'This rectangle combines two native labels with separate baselines (name and numeric gain); it cannot translate to one label.'));
		return {consumer: 'CLabel pair', kind: 'group-envelope-only', rect: bounds};
	}
	if(['page.previous', 'page.next', 'selection.confirm'].includes(layer.binding))
	{
		if(layer.type !== 'button') { issues.push(issue('BINDING_TYPE', 'Navigation/confirmation role requires a button placeholder.')); return null; }
		issues.push(issue('BUTTON_CONTEXT', 'Keep existing selection/paging callback, shortcuts, tooltip and conditional visibility; no callback is generated.'));
		issues.push(issue('BUTTON_SIZE', 'Native animation frames determine button bounds; editor size is not a native resize instruction.'));
		return {consumer: 'CButton', kind: 'top-left-position', position};
	}
	if(/^offers\.(skills|perks)\[[01]\]$/.test(layer.binding))
	{
		if(layer.type !== 'perk') { issues.push(issue('BINDING_TYPE', 'Offer row requires a perk/skill offer placeholder.')); return null; }
		issues.push(issue('OFFER_GROUP', 'Native CComponentBox lays out a variable one/two-row group; a row envelope is not independently configurable.'));
		issues.push(issue('OFFER_CONTEXT', 'Ranks, perk identities, selection exclusivity, paging and keyboard shortcuts must remain native.'));
		return {consumer: 'CComponentBox', kind: 'row-envelope-only', rect: bounds};
	}
	issues.push(issue(layer.binding ? 'UNKNOWN_BINDING' : 'UNBOUND_LAYER', layer.binding ? 'No reviewed level-up mapping exists for this binding.' : 'Unbound decorative geometry has no reviewed native widget mapping.'));
	return null;
}

export function translationReport(document)
{
	validate(document); // Actual editor validator: integer bounds, references and version.
	const globalIssues = [issue('NO_RUNTIME_ADAPTER', 'CLevelWindow is constructed in C++; it does not consume this layout or report.'),
		issue('DRAW_ORDER', 'Editor layer order/visibility/locking are authoring metadata, not a replacement for native widget construction and dynamic visibility.')];
	if(document.canvas.width !== 640 || document.canvas.height !== 568)
		globalIssues.push(issue('CANVAS_MISMATCH', 'Reviewed level-up coordinates use a 640x568 canvas; other canvases require a separate design review.'));
	const bound = new Set();
	const layers = document.layers.map(layer =>
	{
		const bounds = rect(layer, document.canvas), issues = [];
		const geometryProposal = semanticGeometry(layer, bounds, issues);
		if(geometryProposal?.position)
		{
			geometryProposal.approvedReferencePosition = referencePosition(layer.binding);
			geometryProposal.matchesApprovedPosition = geometryProposal.position.x === geometryProposal.approvedReferencePosition.x
				&& geometryProposal.position.y === geometryProposal.approvedReferencePosition.y;
		}
		if(layer.binding && bound.has(layer.binding)) issues.push(issue('DUPLICATE_BINDING', 'More than one layer proposes the same semantic role; no automatic resolution is allowed.'));
		if(layer.binding) bound.add(layer.binding);
		if(layer.assetRef) issues.push(issue(layer.assetRef.startsWith('private:') ? 'PRIVATE_IMAGE' : 'IMAGE_RESOLUTION', 'Image references require resource/frame resolution and native sizing; no image bytes are transferred.'));
		if(['frame', 'banner', 'crest'].includes(layer.type)) issues.push(issue('ORNAMENT_ART', 'Procedural borders/banner/crest previews are not approved runtime artwork.'));
		let materialProposal = null;
		if(layer.material)
		{
			const material = document.materials[layer.material];
			materialProposal = {consumer: 'CFilledTexture', rect: bounds, resourceReference: material.resource,
				requestedTile: {w: material.tileWidth, h: material.tileHeight}, requestedOrigin: material.origin};
			issues.push(issue('NATIVE_TILE_SIZE', 'CFilledTexture uses loaded native image cell dimensions; editor tile dimensions cannot be assumed or silently rescaled.'));
			issues.push(issue('MATERIAL_RESOLUTION', 'Procedural material preview is not runtime art; resource lookup and image format require separate verification.'));
			if(material.origin === 'canvas') issues.push(issue('TILE_ORIGIN', 'The configurable texture builder restarts tiling at each panel; it cannot preserve canvas-origin phase.'));
			if(material.resource.startsWith('private:')) issues.push(issue('PRIVATE_MATERIAL', 'Session-private PNG references are not packaged engine resources.'));
		}
		return {id: layer.id, binding: layer.binding, visible: layer.visible, editorAnchor: layer.anchor,
			resolvedRect: bounds, geometryProposal, materialProposal, blockers: issues};
	});
	return {format: 'new-horizons-ui-translation-report', version: 1, runtimeReady: false,
		target: 'CLevelWindow', status: 'offline-geometry-proposal', canvas: {...document.canvas},
		sourceReferences: ['client/windows/GUIClasses.cpp:createLevelUpControls/createSkillBox',
			'client/gui/InterfaceObjectConfigurable.cpp:buildTexture/buildImage/buildLabel/buildButton (reference contracts only)',
			'client/widgets/Images.cpp:CFilledTexture::showAll'],
		globalBlockers: globalIssues, layers,
		summary: {layers: layers.length, geometryProposals: layers.filter(layer => layer.geometryProposal).length,
			layerBlockers: layers.reduce((count, layer) => count + layer.blockers.length, 0), globalBlockers: globalIssues.length}};
}

export function exportTranslationReport(document) { return JSON.stringify(translationReport(document), null, 2) + '\n'; }
