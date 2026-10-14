/* Offline layout prototype, part of VCMI. GPL-2.0-or-later. */
import * as model from './model.mjs';
const $ = id => document.getElementById(id);
let history, selected = '', zoom = 1, gesture = null, layoutEpoch = 0;
const assets = new Map();
let assetBytes = 0, assetPixels = 0, assetNumber = 0;
let pendingAssets = 0, pendingBytes = 0, pendingPixels = 0;
const status = message => { $('status').textContent = message; };
for(const type of model.TYPES) $('add-type').add(new Option(type, type));
for(const anchor of model.ANCHORS) $('prop-anchor').add(new Option(anchor, anchor));
$('properties').onsubmit = event => event.preventDefault();
function attempt(action) { try { action(); } catch(error) { status(error.message); } }
function commit(layout) { history.commit(layout); render(); }
function boundsStyle(node, bounds) { Object.assign(node.style, {left: `${bounds.x}px`, top: `${bounds.y}px`, width: `${bounds.w}px`, height: `${bounds.h}px`}); }
function render()
{
	const layout = history.current;
	if(!layout.layers.some(layer => layer.id === selected)) selected = '';
	$('board').replaceChildren(); $('layers').replaceChildren();
	Object.assign($('board').style, {width: `${layout.canvas.width}px`, height: `${layout.canvas.height}px`, transform: `scale(${zoom})`, transformOrigin: 'top left', '--grid': `${layout.grid.size}px`});
	Object.assign($('scaled-board').style, {width: `${layout.canvas.width * zoom}px`, height: `${layout.canvas.height * zoom}px`});
	$('board').classList.toggle('grid', $('grid-visible').checked);
	for(const layer of layout.layers)
	{
		const item = document.createElement('button'); item.textContent = `${layer.visible ? '' : '○ '}${layer.locked ? '🔒 ' : ''}${layer.id}`;
		item.className = 'layer-row'; item.setAttribute('aria-pressed', String(layer.id === selected)); item.onclick = () => { selected = layer.id; render(); }; $('layers').append(item);
		if(!layer.visible) continue;
		const node = document.createElement('div'); node.dataset.id = layer.id; node.className = `layer type-${layer.type}${layer.locked ? ' locked' : ''}${layer.id === selected ? ' selected' : ''}`;
		boundsStyle(node, model.rect(layer, layout.canvas));
		if(layer.material) { node.classList.add(`preview-${layout.materials[layer.material].preview}`); Object.assign(node.style, model.materialStyle(layout, layer, assets.get(layout.materials[layer.material].resource)?.url)); }
		const asset = assets.get(layer.assetRef);
		if(asset) { const img = document.createElement('img'); img.src = asset.url; img.alt = layer.label; img.draggable = false; node.append(img); }
		else { const label = document.createElement('span'); label.className = 'placeholder'; label.textContent = layer.label || layer.binding || layer.type; node.append(label); }
		if(layer.id === selected && !layer.locked) { const handle = document.createElement('div'); handle.className = 'resize'; node.append(handle); }
		node.onpointerdown = event => beginGesture(event, node, layer); $('board').append(node);
	}
	$('undo').disabled = history.index === 0; $('redo').disabled = history.index + 1 === history.entries.length;
	$('grid-size').value = layout.grid.size; $('snap').checked = layout.grid.snap;
	const layer = layout.layers.find(item => item.id === selected);
	for(const element of $('properties').elements) element.disabled = !layer;
	$('prop-material').replaceChildren(new Option('None', ''), ...Object.keys(layout.materials).map(id => new Option(id, id)));
	if(layer) for(const key of ['id','label','binding','anchor','x','y','w','h','material','assetRef','visible','locked']) { const input = $(`prop-${key}`); if(input.type === 'checkbox') input.checked = layer[key]; else input.value = layer[key]; }
	const material = layer && layout.materials[layer.material]; $('material-properties').hidden = !material;
	if(material) { $('tile-resource').value = material.resource; $('tile-width').value = material.tileWidth; $('tile-height').value = material.tileHeight; $('tile-origin').value = material.origin; }
}
function beginGesture(event, node, layer)
{
	if(event.button !== 0) return;
	event.preventDefault(); selected = layer.id;
	if(layer.locked) { render(); return; }
	gesture = {doc: history.current, bounds: model.rect(layer, history.current.canvas), x: event.clientX, y: event.clientY, resize: event.target.classList.contains('resize'), id: layer.id, next: history.current};
	node.classList.add('selected'); node.setPointerCapture(event.pointerId);
	node.onpointermove = move => attempt(() => { if(!gesture) return; const dx = (move.clientX - gesture.x) / zoom, dy = (move.clientY - gesture.y) / zoom; const b = gesture.bounds;
		gesture.next = model.updateRect(gesture.doc, gesture.id, gesture.resize ? {...b,w: b.w + dx,h: b.h + dy} : {...b,x: b.x + dx,y: b.y + dy}, gesture.doc.grid.snap && !move.shiftKey);
		const current = gesture.next.layers.find(item => item.id === gesture.id); boundsStyle(node, model.rect(current, gesture.next.canvas)); Object.assign(node.style, model.materialStyle(gesture.next, current, assets.get(gesture.next.materials[current.material]?.resource)?.url)); });
	node.onpointerup = () => { const next = gesture?.next; gesture = null; if(next) commit(next); };
	node.onpointercancel = () => { gesture = null; render(); };
}
async function loadSeed() { const epoch = ++layoutEpoch; gesture = null; const response = await fetch('./level-up.example.json'); if(!response.ok) throw new Error('Cannot load seed'); const layout = model.parseLayout(await response.text()); if(epoch !== layoutEpoch) return; history = new model.History(layout); selected = ''; render(); status('Native level-up geometry; procedural prototype materials, not game artwork.'); }
$('seed').onclick = () => { if(confirm('Replace this layout? Export first to keep it.')) loadSeed().catch(error => status(error.message)); };
$('undo').onclick = () => { history.undo(); render(); }; $('redo').onclick = () => { history.redo(); render(); };
$('zoom').onchange = () => { zoom = Number($('zoom').value); render(); };
$('grid-visible').onchange = render;
for(const id of ['grid-size','snap']) $(id).onchange = () => attempt(() => { const next = history.current; next.grid = {size: Number($('grid-size').value), snap: $('snap').checked}; commit(model.validate(next)); });
for(const key of ['label','binding','x','y','w','h','material','assetRef','visible','locked']) $(`prop-${key}`).onchange = () => attempt(() => { const input = $(`prop-${key}`); commit(model.editLayer(history.current, selected, {[key]: input.type === 'checkbox' ? input.checked : ['x','y','w','h'].includes(key) ? Number(input.value) : input.value})); });
$('prop-anchor').onchange = () => attempt(() => commit(model.reanchor(history.current, selected, $('prop-anchor').value)));
for(const id of ['tile-resource','tile-width','tile-height','tile-origin']) $(id).onchange = () => attempt(() => { const next = history.current, layer = next.layers.find(item => item.id === selected), m = next.materials[layer.material]; Object.assign(m,{resource:$('tile-resource').value,tileWidth:Number($('tile-width').value),tileHeight:Number($('tile-height').value),origin:$('tile-origin').value}); commit(model.validate(next)); });
$('add').onclick = () => attempt(() => { const next = model.addLayer(history.current, $('add-type').value); selected = next.layers.at(-1).id; commit(next); });
$('delete').onclick = () => attempt(() => commit(model.removeLayer(history.current, selected)));
$('back').onclick = () => attempt(() => commit(model.reorderLayer(history.current, selected, -1)));
$('front').onclick = () => attempt(() => commit(model.reorderLayer(history.current, selected, 1)));
$('import').onclick = () => $('layout-file').click();
$('layout-file').onchange = async () => { const file = $('layout-file').files[0]; try { if(!file) return; const epoch = ++layoutEpoch; gesture = null; if(file.size > 1024 * 1024) throw new Error('Layout exceeds 1 MiB'); const layout = model.parseLayout(await file.text()); if(epoch !== layoutEpoch) return; history = new model.History(layout); selected = ''; render(); status('Imported references need private PNGs rebound in this session.'); } catch(error) { status(error.message); } finally { $('layout-file').value = ''; } };
$('export').onclick = () => attempt(() => { const url = URL.createObjectURL(new Blob([model.exportLayout(history.current)],{type:'application/json'})); const a = document.createElement('a'); a.href = url; a.download = 'layout.nh-ui.json'; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000); status('Exported layout and references only; no PNG bytes or local filenames.'); });
$('asset-import').onclick = () => $('asset-file').click();
$('asset-file').onchange = async () =>
{
	let url = '';
	let reservedBytes = 0, reservedPixels = 0, reserved = false;
	try {
		const file = $('asset-file').files[0]; if(!file) return;
		const epoch = layoutEpoch, targetId = selected, kind = $('asset-kind').value;
		const initialLayer = history.current.layers.find(item => item.id === targetId); if(!initialLayer || initialLayer.locked) throw new Error('Select an unlocked layer first');
		if(file.size > 8 * 1024 * 1024) throw new Error('PNG exceeds 8 MiB');
		if(assets.size + pendingAssets >= 32 || assetBytes + pendingBytes + file.size > 32 * 1024 * 1024) throw new Error('Session image budget: 32 images / 32 MiB');
		++pendingAssets; pendingBytes += file.size; reservedBytes = file.size; reserved = true;
		const dimensions = model.pngDimensions(await file.arrayBuffer());
		if(epoch !== layoutEpoch) throw new Error('Layout replaced during PNG import; image not committed');
		const pixels = dimensions.width * dimensions.height;
		if(assetPixels + pendingPixels + pixels > 16 * 1024 * 1024) throw new Error('Session decoded-image budget: 16 megapixels');
		pendingPixels += pixels; reservedPixels = pixels;
		url = URL.createObjectURL(file); const image = new Image(); image.src = url; await image.decode();
		if(image.naturalWidth !== dimensions.width || image.naturalHeight !== dimensions.height) throw new Error('PNG dimensions disagree');
		if(epoch !== layoutEpoch) throw new Error('Layout replaced during PNG import; image not committed');
		if(gesture) throw new Error('Finish the active drag before importing an image');
		// No awaits below: merge into current state and admit against current budgets.
		const next = history.current, layer = next.layers.find(item => item.id === targetId);
		if(!layer || layer.locked || layer.type !== initialLayer.type) throw new Error('Import target removed, locked or changed');
		if(assets.size >= 32 || assetBytes + file.size > 32 * 1024 * 1024) throw new Error('Session image budget: 32 images / 32 MiB');
		if(assetPixels + pixels > 16 * 1024 * 1024) throw new Error('Session decoded-image budget: 16 megapixels');
		const ref = `private:image-${++assetNumber}`;
		if(kind === 'material') { const id = `imported-${assetNumber}`; next.materials[id] = {kind:'tile',resource:ref,tileWidth:dimensions.width,tileHeight:dimensions.height,origin:'canvas',preview:'plain'}; layer.material = id; }
		else { if(!['image','hero','icon','banner','crest'].includes(layer.type)) throw new Error('Ornaments require an image, hero, icon, banner or crest layer'); layer.assetRef = ref; }
		model.validate(next); assets.set(ref,{url,size:file.size}); assetBytes += file.size; assetPixels += pixels; url = ''; commit(next); status(`Private PNG retained in memory as ${ref}; tile repeats at its cell size. Rebind references after reopening.`);
	} catch(error) { status(error.message); } finally { if(url) URL.revokeObjectURL(url); if(reserved) { --pendingAssets; pendingBytes -= reservedBytes; pendingPixels -= reservedPixels; } $('asset-file').value = ''; }
};
window.addEventListener('beforeunload', () => { for(const asset of assets.values()) URL.revokeObjectURL(asset.url); });
document.addEventListener('keydown', event => { if(['INPUT','TEXTAREA','SELECT'].includes(event.target.tagName) || !history) return; attempt(() => {
	if((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') { event.preventDefault(); event.shiftKey ? history.redo() : history.undo(); render(); }
	else if(event.key === 'Delete' && selected) { event.preventDefault(); commit(model.removeLayer(history.current, selected)); }
	else if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(event.key) && selected) { event.preventDefault(); const doc = history.current, layer = doc.layers.find(item => item.id === selected), b = model.rect(layer,doc.canvas), step = event.shiftKey ? 1 : doc.grid.snap ? doc.grid.size : 1; b.x += event.key === 'ArrowLeft' ? -step : event.key === 'ArrowRight' ? step : 0; b.y += event.key === 'ArrowUp' ? -step : event.key === 'ArrowDown' ? step : 0; commit(model.updateRect(doc,selected,b,false)); }
}); });
loadSeed().catch(error => status(error.message));
