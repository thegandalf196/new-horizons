/* Offline layout prototype, part of VCMI. GPL-2.0-or-later; see license.txt. */
export const FORMAT = 'new-horizons-ui-layout';
export const VERSION = 1;
export const TYPES = ['panel', 'frame', 'banner', 'crest', 'hero', 'icon', 'perk', 'text', 'button', 'image', 'placeholder'];
export const PLACEHOLDER_ROLES = {'skill-icon': 'Skill icon', 'perk-icon': 'Perk icon', 'parent-skill-icon': 'Parent Skill icon'};
export const ANCHORS = ['top-left', 'top-center', 'top-right', 'center-left', 'center', 'center-right', 'bottom-left', 'bottom-center', 'bottom-right'];
const POINTS = [[0, 0], [.5, 0], [1, 0], [0, .5], [.5, .5], [1, .5], [0, 1], [.5, 1], [1, 1]];
const LAYER_KEYS = ['id', 'type', 'label', 'x', 'y', 'w', 'h', 'anchor', 'binding', 'material', 'assetRef', 'visible', 'locked'];
const ID = /^[a-zA-Z][a-zA-Z0-9_-]{0,63}$/;
const REFERENCE = /^(?:[a-zA-Z0-9_-]+:)?[a-zA-Z0-9_./-]*$/;
const forbidden = new Set(['__proto__', 'constructor', 'prototype']);
export const clone = value => JSON.parse(JSON.stringify(value));
function check(test, message) { if(!test) throw new Error(message); }
function object(value, keys, context)
{
	check(value !== null && typeof value === 'object' && !Array.isArray(value), `${context}: object required`);
	check(Object.keys(value).every(key => keys.includes(key)), `${context}: unknown field`);
	check(keys.every(key => Object.hasOwn(value, key)), `${context}: missing field`);
}
function integer(value, min, max, context) { check(Number.isInteger(value) && value >= min && value <= max, `${context}: integer ${min}..${max} required`); }
function text(value, max, context) { check(typeof value === 'string' && value.length <= max, `${context}: bounded text required`); }
function reference(value)
{
	text(value, 160, 'Resource reference');
	const path = value.includes(':') ? value.slice(value.indexOf(':') + 1) : value;
	check(REFERENCE.test(value) && !path.startsWith('/') && !value.includes('//') && !/^(data|https?|file|blob|javascript):/i.test(value) && !/^[a-zA-Z]:/.test(value) && !path.split('/').includes('..'), 'Only portable resource references are allowed (no URLs, data, or absolute paths)');
}
export function rect(layer, canvas)
{
	const point = POINTS[ANCHORS.indexOf(layer.anchor)];
	check(point, 'Unknown anchor');
	return {x: Math.floor(canvas.width * point[0]) + layer.x - Math.floor(layer.w * point[0]),
		y: Math.floor(canvas.height * point[1]) + layer.y - Math.floor(layer.h * point[1]), w: layer.w, h: layer.h};
}
function place(layer, bounds, canvas)
{
	const point = POINTS[ANCHORS.indexOf(layer.anchor)];
	return {...layer, x: bounds.x - Math.floor(canvas.width * point[0]) + Math.floor(bounds.w * point[0]),
		y: bounds.y - Math.floor(canvas.height * point[1]) + Math.floor(bounds.h * point[1]), w: bounds.w, h: bounds.h};
}
function container(document, layer)
{
	const parent = layer.type === 'placeholder' && layer.parentId ? document.layers.find(item => item.id === layer.parentId) : null;
	if(!parent) return {x: 0, y: 0, w: document.canvas.width, h: document.canvas.height};
	check(['panel', 'perk'].includes(parent.type), 'Placeholder parent must be a panel or offer area');
	return rect(parent, document.canvas);
}
export function resolvedRect(document, layer)
{
	const parent = container(document, layer), local = rect(layer, {width: parent.w, height: parent.h});
	return {...local, x: parent.x + local.x, y: parent.y + local.y};
}
export function displayLabel(layer)
{
	return layer.type === 'placeholder' ? `${PLACEHOLDER_ROLES[layer.role]} · ${layer.w}×${layer.h}` : layer.label || layer.binding || layer.type;
}
export function validate(document)
{
	object(document, ['format', 'version', 'status', 'name', 'canvas', 'grid', 'materials', 'layers'], 'Layout');
	check(document.format === FORMAT && document.version === VERSION && document.status === 'prototype', 'Unsupported layout format/version/status');
	text(document.name, 128, 'Layout name');
	object(document.canvas, ['width', 'height'], 'Canvas');
	for(const value of Object.values(document.canvas)) integer(value, 16, 4096, 'Canvas size');
	object(document.grid, ['size', 'snap'], 'Grid');
	integer(document.grid.size, 1, 64, 'Grid size');
	check(typeof document.grid.snap === 'boolean', 'Grid snap must be boolean');
	check(document.materials && typeof document.materials === 'object' && !Array.isArray(document.materials), 'Materials object required');
	check(Object.keys(document.materials).length <= 32, 'At most 32 materials');
	for(const [id, material] of Object.entries(document.materials))
	{
		check(ID.test(id) && !forbidden.has(id), 'Invalid material ID');
		object(material, ['kind', 'resource', 'tileWidth', 'tileHeight', 'origin', 'preview'], 'Material');
		check(material.kind === 'tile', 'Materials must tile; whole-background stretch is not supported');
		reference(material.resource);
		integer(material.tileWidth, 1, 4096, 'Tile width');
		integer(material.tileHeight, 1, 4096, 'Tile height');
		check(['canvas', 'local'].includes(material.origin), 'Invalid tile origin');
		check(['leather', 'stone', 'plain'].includes(material.preview), 'Invalid procedural preview');
	}
	check(Array.isArray(document.layers) && document.layers.length <= 256, 'At most 256 layers');
	const ids = new Set();
	for(const layer of document.layers)
	{
		object(layer, layer.type === 'placeholder' ? [...LAYER_KEYS, 'role', 'parentId'] : LAYER_KEYS, 'Layer');
		text(layer.id, 64, 'Layer ID');
		check(ID.test(layer.id) && !forbidden.has(layer.id) && !ids.has(layer.id), 'Invalid/duplicate layer ID');
		ids.add(layer.id);
		check(TYPES.includes(layer.type) && ANCHORS.includes(layer.anchor), 'Unknown layer type/anchor');
		text(layer.label, 160, 'Layer label');
		text(layer.binding, 160, 'Binding');
		check(/^[a-zA-Z0-9_.\[\]-]*$/.test(layer.binding), 'Binding is a semantic name, not executable code');
		integer(layer.x, -8192, 8192, 'X'); integer(layer.y, -8192, 8192, 'Y');
		integer(layer.w, 8, document.canvas.width, 'Width'); integer(layer.h, 8, document.canvas.height, 'Height');
		text(layer.material, 64, 'Layer material');
		check(layer.material === '' || Object.hasOwn(document.materials, layer.material), 'Unknown material');
		reference(layer.assetRef);
		check(typeof layer.visible === 'boolean' && typeof layer.locked === 'boolean', 'Layer flags must be boolean');
		if(layer.type === 'placeholder')
		{
			check(typeof layer.role === 'string' && Object.hasOwn(PLACEHOLDER_ROLES, layer.role), 'Unknown placeholder role');
			text(layer.parentId, 64, 'Placeholder parent');
			check(!layer.parentId || document.layers.some(item => item.id === layer.parentId && ['panel', 'perk'].includes(item.type)), 'Unknown/non-container placeholder parent');
		}
		const parent = container(document, layer), local = rect(layer, {width: parent.w, height: parent.h});
		check(local.x >= 0 && local.y >= 0 && local.x + local.w <= parent.w && local.y + local.h <= parent.h, 'Layer outside parent');
		const bounds = resolvedRect(document, layer);
		check(bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.w <= document.canvas.width && bounds.y + bounds.h <= document.canvas.height, 'Layer outside canvas');
	}
	return document;
}
export function parseLayout(source)
{
	check(typeof source === 'string' && new TextEncoder().encode(source).length <= 1024 * 1024, 'Layout exceeds 1 MiB');
	return clone(validate(JSON.parse(source)));
}
export function exportLayout(document) { return JSON.stringify(validate(document), null, 2) + '\n'; }
export function updateRect(document, id, bounds, snap = document.grid.snap)
{
	const result = clone(document), index = result.layers.findIndex(layer => layer.id === id);
	check(index >= 0 && !result.layers[index].locked, 'Layer missing or locked');
	const step = snap ? result.grid.size : 1;
	const quantize = value => Math.round(value / step) * step;
	const current = result.layers[index], parent = container(result, current);
	const w = Math.max(8, Math.min(parent.w, bounds.w === current.w ? current.w : quantize(bounds.w)));
	const h = Math.max(8, Math.min(parent.h, bounds.h === current.h ? current.h : quantize(bounds.h)));
	const x = Math.max(parent.x, Math.min(parent.x + parent.w - w, quantize(bounds.x)));
	const y = Math.max(parent.y, Math.min(parent.y + parent.h - h, quantize(bounds.y)));
	result.layers[index] = place(current, {x: x - parent.x, y: y - parent.y, w, h}, {width: parent.w, height: parent.h});
	return validate(result);
}
export function reanchor(document, id, anchor)
{
	const result = clone(document), layer = result.layers.find(item => item.id === id);
	check(layer && ANCHORS.includes(anchor), 'Unknown layer/anchor');
	const parent = container(result, layer), canvas = {width: parent.w, height: parent.h};
	const bounds = rect(layer, canvas);
	Object.assign(layer, place({...layer, anchor}, bounds, canvas));
	return validate(result);
}
export function editLayer(document, id, changes)
{
	const result = clone(document), layer = result.layers.find(item => item.id === id);
	check(layer, 'Unknown layer');
	Object.assign(layer, changes);
	return validate(result);
}
export function addLayer(document, type)
{
	check(TYPES.includes(type), 'Unknown layer type');
	if(type === 'placeholder') return addPlaceholder(document);
	const result = clone(document);
	let number = 1; while(result.layers.some(layer => layer.id === `${type}-${number}`)) ++number;
	const x = Math.min(24, result.canvas.width - 8), y = Math.min(24, result.canvas.height - 8);
	result.layers.push({id: `${type}-${number}`, type, label: type, x, y,
		w: Math.min(128, result.canvas.width - x), h: Math.min(64, result.canvas.height - y),
		anchor: 'top-left', binding: '', material: type === 'panel' || type === 'perk' ? Object.keys(result.materials)[0] ?? '' : '',
		assetRef: '', visible: true, locked: false});
	return validate(result);
}
export function addPlaceholder(document, parentId = '', role = 'skill-icon')
{
	const result = clone(document);
	check(Object.hasOwn(PLACEHOLDER_ROLES, role), 'Unknown placeholder role');
	const parent = parentId ? result.layers.find(item => item.id === parentId && ['panel', 'perk'].includes(item.type)) : null;
	check(!parentId || (parent && !parent.locked), 'Select an unlocked panel or offer area');
	let number = 1; while(result.layers.some(layer => layer.id === `placeholder-${number}`)) ++number;
	const w = Math.min(44, parent?.w ?? result.canvas.width), h = Math.min(44, parent?.h ?? result.canvas.height);
	result.layers.push({id: `placeholder-${number}`, type: 'placeholder', label: PLACEHOLDER_ROLES[role], role, parentId,
		x: Math.min(12, (parent?.w ?? result.canvas.width) - w), y: Math.floor(((parent?.h ?? result.canvas.height) - h) / 2), w, h,
		anchor: 'top-left', binding: '', material: '', assetRef: '', visible: true, locked: false});
	return validate(result);
}
export function removeLayer(document, id)
{
	const result = clone(document), layer = result.layers.find(item => item.id === id);
	check(layer && !layer.locked, 'Layer missing or locked');
	check(!result.layers.some(item => item.parentId === id && item.locked), 'Unlock child placeholders before deleting their parent');
	result.layers = result.layers.filter(item => item.id !== id && item.parentId !== id);
	return validate(result);
}
export function reorderLayer(document, id, direction)
{
	const result = clone(document), index = result.layers.findIndex(item => item.id === id);
	check(index >= 0 && !result.layers[index].locked, 'Layer missing or locked');
	const target = Math.max(0, Math.min(result.layers.length - 1, index + Math.sign(direction)));
	[result.layers[index], result.layers[target]] = [result.layers[target], result.layers[index]];
	return validate(result);
}
export class History
{
	constructor(document) { this.entries = [clone(validate(document))]; this.index = 0; }
	get current() { return clone(this.entries[this.index]); }
	commit(document)
	{
		const next = clone(validate(document));
		if(JSON.stringify(next) === JSON.stringify(this.entries[this.index])) return false;
		this.entries = this.entries.slice(0, this.index + 1); this.entries.push(next);
		if(this.entries.length > 50) this.entries.shift();
		this.index = this.entries.length - 1; return true;
	}
	undo() { if(this.index > 0) --this.index; return this.current; }
	redo() { if(this.index + 1 < this.entries.length) ++this.index; return this.current; }
}
export function materialStyle(document, layer, assetUrl = '')
{
	if(!layer.material) return {};
	const material = document.materials[layer.material], bounds = resolvedRect(document, layer);
	check(!assetUrl || assetUrl.startsWith('blob:'), 'Only session-local imported images may be previewed');
	return {backgroundRepeat: 'repeat', backgroundSize: `${material.tileWidth}px ${material.tileHeight}px`,
		backgroundPosition: material.origin === 'canvas' ? `${-bounds.x}px ${-bounds.y}px` : '0px 0px',
		...(assetUrl ? {backgroundImage: `url("${assetUrl}")`} : {})};
}
export function alignmentGuides(document, id, bounds, resize = false, tolerance = 3)
{
	const layer = document.layers.find(item => item.id === id);
	check(layer, 'Unknown guide layer');
	const targets = [{id: 'canvas', rect: {x: 0, y: 0, w: document.canvas.width, h: document.canvas.height}}];
	if(layer.parentId) targets.push({id: layer.parentId, rect: container(document, layer)});
	for(const sibling of document.layers)
		if(sibling.id !== id && sibling.visible && sibling.parentId !== id
			&& (sibling.parentId || '') === (layer.parentId || '')) targets.push({id: sibling.id, rect: resolvedRect(document, sibling)});
	const guides = [];
	for(const axis of ['x', 'y'])
	{
		const size = axis === 'x' ? 'w' : 'h', positions = resize ? [bounds[axis] + bounds[size]] : [bounds[axis], bounds[axis] + bounds[size] / 2, bounds[axis] + bounds[size]];
		let best;
		for(const target of targets)
			for(const coordinate of [target.rect[axis], target.rect[axis] + target.rect[size] / 2, target.rect[axis] + target.rect[size]])
				for(const position of positions)
				{
					const delta = coordinate - position;
					if(Math.abs(delta) <= tolerance && (!best || Math.abs(delta) < Math.abs(best.delta))) best = {axis, coordinate, delta, target: target.id};
				}
		if(best) guides.push(best);
	}
	return guides;
}
export function gestureLayout(document, id, bounds, {resize = false, snapGrid = document.grid.snap, snapAlignment = false} = {})
{
	let next = updateRect(document, id, bounds, snapGrid);
	let actual = resolvedRect(next, next.layers.find(layer => layer.id === id));
	if(snapAlignment)
	{
		for(const guide of alignmentGuides(document, id, actual, resize))
		{
			if(resize) actual[guide.axis === 'x' ? 'w' : 'h'] += Math.round(guide.delta);
			else actual[guide.axis] += Math.round(guide.delta);
		}
		next = updateRect(document, id, actual, false);
	}
	return next;
}
export function pngDimensions(buffer)
{
	const bytes = new Uint8Array(buffer), view = new DataView(buffer);
	check(bytes.length >= 33 && bytes.length <= 8 * 1024 * 1024, 'PNG must be 33 bytes..8 MiB');
	check([137,80,78,71,13,10,26,10].every((value, index) => bytes[index] === value)
		&& view.getUint32(8) === 13 && [73,72,68,82].every((value, index) => bytes[index + 12] === value), 'PNG signature/IHDR required');
	const width = view.getUint32(16), height = view.getUint32(20);
	integer(width, 1, 4096, 'PNG width'); integer(height, 1, 4096, 'PNG height');
	return {width, height}; // Browser decoder still validates the full PNG.
}
