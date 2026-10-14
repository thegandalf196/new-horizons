/* Offline layout prototype, part of VCMI. GPL-2.0-or-later; see license.txt. */
export const FORMAT = 'new-horizons-ui-layout';
export const VERSION = 1;
export const TYPES = ['panel', 'frame', 'banner', 'crest', 'hero', 'icon', 'perk', 'text', 'button', 'image'];
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
		object(layer, LAYER_KEYS, 'Layer');
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
		const bounds = rect(layer, document.canvas);
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
	const current = result.layers[index];
	const w = Math.max(8, Math.min(result.canvas.width, bounds.w === current.w ? current.w : quantize(bounds.w)));
	const h = Math.max(8, Math.min(result.canvas.height, bounds.h === current.h ? current.h : quantize(bounds.h)));
	const x = Math.max(0, Math.min(result.canvas.width - w, quantize(bounds.x)));
	const y = Math.max(0, Math.min(result.canvas.height - h, quantize(bounds.y)));
	result.layers[index] = place(result.layers[index], {x, y, w, h}, result.canvas);
	return validate(result);
}
export function reanchor(document, id, anchor)
{
	const result = clone(document), layer = result.layers.find(item => item.id === id);
	check(layer && ANCHORS.includes(anchor), 'Unknown layer/anchor');
	const bounds = rect(layer, result.canvas);
	Object.assign(layer, place({...layer, anchor}, bounds, result.canvas));
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
	const result = clone(document);
	let number = 1; while(result.layers.some(layer => layer.id === `${type}-${number}`)) ++number;
	const x = Math.min(24, result.canvas.width - 8), y = Math.min(24, result.canvas.height - 8);
	result.layers.push({id: `${type}-${number}`, type, label: type, x, y,
		w: Math.min(128, result.canvas.width - x), h: Math.min(64, result.canvas.height - y),
		anchor: 'top-left', binding: '', material: type === 'panel' || type === 'perk' ? Object.keys(result.materials)[0] ?? '' : '',
		assetRef: '', visible: true, locked: false});
	return validate(result);
}
export function removeLayer(document, id)
{
	const result = clone(document), layer = result.layers.find(item => item.id === id);
	check(layer && !layer.locked, 'Layer missing or locked');
	result.layers = result.layers.filter(item => item.id !== id);
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
	const material = document.materials[layer.material], bounds = rect(layer, document.canvas);
	check(!assetUrl || assetUrl.startsWith('blob:'), 'Only session-local imported images may be previewed');
	return {backgroundRepeat: 'repeat', backgroundSize: `${material.tileWidth}px ${material.tileHeight}px`,
		backgroundPosition: material.origin === 'canvas' ? `${-bounds.x}px ${-bounds.y}px` : '0px 0px',
		...(assetUrl ? {backgroundImage: `url("${assetUrl}")`} : {})};
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
