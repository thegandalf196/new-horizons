"""Pure model/static-server tests: no browser, game or original artwork."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / 'tools/new-horizons-ui-editor'

class UiEditorTest(unittest.TestCase):
    def node(self, body):
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node is required for deterministic JS model checks')
        prefix = f"""
import assert from 'node:assert/strict';
import fs from 'node:fs';
import * as m from {json.dumps((EDITOR / 'model.mjs').as_uri())};
const seed = m.parseLayout(fs.readFileSync({json.dumps(str(EDITOR / 'level-up.example.json'))}, 'utf8'));
"""
        result = subprocess.run([node, '--input-type=module', '-e', prefix + body], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_seed_and_roundtrip(self):
        self.node("""
assert.deepEqual(m.parseLayout(m.exportLayout(seed)), seed);
assert.deepEqual(seed.canvas, {width:640,height:568});
assert.deepEqual(m.rect(seed.layers.find(x=>x.id==='portrait'),seed.canvas),{x:297,y:66,w:58,h:64});
for(let i=0;i<2;i++) { const s=seed.layers.find(x=>x.id===`skill-${i}`); assert.deepEqual(m.rect(s,seed.canvas),{x:24,y:294+i*108,w:284,h:96}); }
assert.equal(seed.materials.leather.kind,'tile');
""")

    def test_strict_version_and_references(self):
        self.node("""
for(const ref of ['https://example.test/a.png','data:foo','blob:foo','/private/a.png','../a','private:../a','C:foo','file:abc','private:x//y']) {const d=m.clone(seed);d.layers[2].assetRef=ref;assert.throws(()=>m.validate(d));}
for(const ref of ['core:PortraitsLarge','new-horizons:frame','private:image-1','']) {const d=m.clone(seed);d.layers[2].assetRef=ref;m.validate(d);}
let d=m.clone(seed);d.version=2;assert.throws(()=>m.validate(d));d=m.clone(seed);d.extra=1;assert.throws(()=>m.validate(d));
d=m.clone(seed);d.layers[1].id=d.layers[0].id;assert.throws(()=>m.validate(d));
d=m.clone(seed);d.materials.leather.kind='stretch';assert.throws(()=>m.validate(d));
for(const key of ['id','material']) for(const value of [['leather'],null,42]) {d=m.clone(seed);d.layers[2][key]=value;assert.throws(()=>m.validate(d));}
assert.throws(()=>m.parseLayout(' '.repeat(1024*1024+1)));
""")

    def test_geometry_snap_anchor_and_lock(self):
        self.node("""
let d=m.updateRect(seed,'portrait',{x:303,y:75,w:58,h:64});let l=d.layers.find(x=>x.id==='portrait');assert.deepEqual(m.rect(l,d.canvas),{x:304,y:72,w:58,h:64});
for(const anchor of m.ANCHORS){d=m.reanchor(d,'portrait',anchor);l=d.layers.find(x=>x.id==='portrait');assert.deepEqual(m.rect(l,d.canvas),{x:304,y:72,w:58,h:64});}
d=m.updateRect(d,'portrait',{x:9999,y:-8,w:58,h:64},false);assert.deepEqual(m.rect(d.layers.find(x=>x.id==='portrait'),d.canvas),{x:582,y:0,w:58,h:64});
assert.throws(()=>m.updateRect(seed,'background',{x:0,y:0,w:16,h:16}));assert.throws(()=>m.removeLayer(seed,'background'));assert.throws(()=>m.reorderLayer(seed,'border',1));
""")

    def test_tiling_not_whole_background_resize(self):
        self.node("""
let d=m.editLayer(seed,'skill-0',{x:32,y:296,w:280,h:96});let l=d.layers.find(x=>x.id==='skill-0');const before=m.materialStyle(d,l);
assert.equal(before.backgroundRepeat,'repeat');assert.equal(before.backgroundSize,'64px 64px');assert.equal(before.backgroundPosition,'-32px -296px');
d=m.updateRect(d,l.id,{x:32,y:296,w:400,h:160},false);assert.equal(m.materialStyle(d,d.layers.find(x=>x.id===l.id)).backgroundSize,before.backgroundSize);
d.materials.leather.origin='local';assert.equal(m.materialStyle(d,d.layers.find(x=>x.id===l.id)).backgroundPosition,'0px 0px');assert.throws(()=>m.materialStyle(d,l,'https://example.test/a'));
""")

    def test_history_branch_and_isolation(self):
        self.node("""
const h=new m.History(seed);const external=h.current;external.name='outside';assert.notEqual(h.current.name,'outside');
const a=m.editLayer(seed,'portrait',{label:'a'}), b=m.editLayer(a,'portrait',{label:'b'});h.commit(a);h.commit(b);assert.deepEqual(h.undo(),a);assert.deepEqual(h.redo(),b);h.undo();h.commit(m.editLayer(a,'portrait',{label:'c'}));assert.equal(h.redo().layers.find(x=>x.id==='portrait').label,'c');
for(let i=0;i<60;i++)h.commit(m.editLayer(h.current,'portrait',{label:String(i)}));assert.equal(h.entries.length,50);
""")

    def test_small_canvas_add_and_order(self):
        self.node("""
const small={...m.clone(seed),canvas:{width:16,height:16},layers:[]};const a=m.addLayer(small,'icon');assert.deepEqual(m.rect(a.layers[0],a.canvas),{x:8,y:8,w:8,h:8});let b=m.addLayer(a,'crest');b=m.reorderLayer(b,'crest-1',-1);assert.equal(b.layers[0].id,'crest-1');assert.equal(m.removeLayer(b,'icon-1').layers.length,1);
""")

    def test_png_header_bounds(self):
        self.node("""
const bytes=new Uint8Array(33),v=new DataView(bytes.buffer);bytes.set([137,80,78,71,13,10,26,10]);v.setUint32(8,13);bytes.set([73,72,68,82],12);v.setUint32(16,64);v.setUint32(20,32);assert.deepEqual(m.pngDimensions(bytes.buffer),{width:64,height:32});v.setUint32(16,4097);assert.throws(()=>m.pngDimensions(bytes.buffer));v.setUint32(16,0);assert.throws(()=>m.pngDimensions(bytes.buffer));assert.throws(()=>m.pngDimensions(new ArrayBuffer(32)));
""")

    def test_server_admission_without_launch(self):
        spec = importlib.util.spec_from_file_location('nh_ui_server', EDITOR / 'serve.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.admitted_path('/'), 'index.html')
        self.assertEqual(module.admitted_path('/model.mjs?x=1'), 'model.mjs')
        for path in ['/../license.txt', '/%2e%2e/license.txt', '//model.mjs', '/private.png', '/editor.mjs/']:
            self.assertIsNone(module.admitted_path(path))
        for filename in module.FILES:
            self.assertTrue((EDITOR / filename).is_file())

    def test_frontend_model_event_wiring_without_browser(self):
        self.node(f"""
class Element {{
 constructor() {{this.children=[];this.style={{}};this.dataset={{}};this.elements=[];this.value='';this.checked=false;this.type='';this.className='';this.classList={{add:(...x)=>{{this.className+=' '+x.join(' ')}},toggle:()=>{{}},contains:x=>this.className.split(' ').includes(x)}};}}
 append(x){{this.children.push(x)}} add(x){{this.children.push(x)}} replaceChildren(...x){{this.children=x}} setAttribute(){{}} setPointerCapture(){{}}
}}
const html=fs.readFileSync({json.dumps(str(EDITOR / 'index.html'))},'utf8');
const elements=new Map([...html.matchAll(/id="([^\"]+)"/g)].map(x=>[x[1],new Element()]));
const handlers={{}};
globalThis.document={{getElementById:id=>{{assert(elements.has(id),'Unknown element '+id);return elements.get(id)}},createElement:()=>new Element(),addEventListener:(name,fn)=>handlers[name]=fn}};
globalThis.window={{addEventListener:()=>{{}}}};
globalThis.Option=class {{constructor(text,value){{this.text=text;this.value=value}}}};
globalThis.fetch=async()=>({{ok:true,text:async()=>m.exportLayout(seed)}});
await import({json.dumps((EDITOR / 'editor.mjs').as_uri())});
await new Promise(resolve=>setImmediate(resolve));
assert.equal(elements.get('add-type').children.length,m.TYPES.length);
assert.equal(elements.get('prop-anchor').children.length,m.ANCHORS.length);
assert.equal(elements.get('board').children.length,seed.layers.length);
assert(elements.get('board').children[0].className.includes('preview-leather'));
const portrait=elements.get('board').children.find(x=>x.dataset.id==='portrait');
portrait.onpointerdown({{button:0,preventDefault:()=>{{}},clientX:100,clientY:100,pointerId:1,target:portrait}});
portrait.onpointermove({{clientX:124,clientY:108,shiftKey:false}});portrait.onpointerup();
assert.equal(elements.get('prop-w').value,58);assert.equal(elements.get('prop-x').value,320);
elements.get('undo').onclick();assert.equal(elements.get('prop-x').value,297);
elements.get('redo').onclick();assert.equal(elements.get('prop-x').value,320);
elements.get('add-type').value='crest';elements.get('add').onclick();
assert.equal(elements.get('prop-id').value,'crest-1');assert.equal(elements.get('board').children.length,seed.layers.length+1);
elements.get('delete').onclick();assert.equal(elements.get('board').children.length,seed.layers.length);
assert.equal(typeof handlers.keydown,'function');
""")

    def test_frontend_syntax_bindings_and_schema(self):
        import re
        html = (EDITOR / 'index.html').read_text()
        js = (EDITOR / 'editor.mjs').read_text()
        ids = set(re.findall(r'id="([^"]+)"', html))
        self.assertTrue(set(re.findall(r"\$\('([^']+)'\)", js)).issubset(ids))
        self.assertNotIn('innerHTML', js)
        self.assertNotIn('localStorage', js)
        self.assertIn("connect-src 'self'", html)
        self.assertIn('URL.revokeObjectURL', js)
        self.assertIn('model.TYPES', js)
        self.assertIn('model.ANCHORS', js)
        schema = json.loads((EDITOR / 'layout.schema.json').read_text())
        self.assertEqual(schema['properties']['version']['const'], 1)
        self.assertEqual(schema['properties']['materials']['additionalProperties']['properties']['kind']['const'], 'tile')
        node = shutil.which('node')
        for filename in ['editor.mjs','model.mjs']:
            result = subprocess.run([node,'--check',str(EDITOR / filename)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)

    def asset_environment(self):
        return """
class Element {
 constructor(){this.children=[];this.style={};this.dataset={};this.elements=[];this.value='';this.checked=false;this.type='';this.className='';this.classList={add:(...x)=>this.className+=' '+x.join(' '),toggle:()=>{},contains:x=>this.className.split(' ').includes(x)};}
 append(x){this.children.push(x)} add(x){this.children.push(x)} replaceChildren(...x){this.children=x} setAttribute(){} setPointerCapture(){} click(){}
}
const html=fs.readFileSync(HTML_PATH,'utf8');
const elements=new Map([...html.matchAll(/id="([^"]+)"/g)].map(x=>[x[1],new Element()]));
globalThis.document={getElementById:id=>elements.get(id),createElement:()=>new Element(),addEventListener:()=>{}};
globalThis.window={addEventListener:()=>{}};
globalThis.Option=class {constructor(text,value){this.text=text;this.value=value}};
globalThis.fetch=async()=>({ok:true,text:async()=>m.exportLayout(seed)});
globalThis.confirm=()=>true;
const revoked=[], exports=[], decoders=[];let urls=0;
globalThis.URL={createObjectURL:blob=>{if(blob instanceof Blob)exports.push(blob);return `blob:test-${++urls}`},revokeObjectURL:url=>revoked.push(url)};
let width=1,height=1;
globalThis.Image=class {constructor(){this.naturalWidth=width;this.naturalHeight=height}decode(){return new Promise(resolve=>decoders.push(resolve))}};
await import(EDITOR_URL);
const flush=()=>new Promise(resolve=>setImmediate(resolve));await flush();
const select=id=>elements.get('layers').children.find(x=>x.textContent===id).onclick();
const exportDoc=async()=>{elements.get('export').onclick();return JSON.parse(await exports.at(-1).text())};
function beginImport(size=1,w=1,h=1,kind='ornament') {
 width=w;height=h;const bytes=new Uint8Array(33),v=new DataView(bytes.buffer);bytes.set([137,80,78,71,13,10,26,10]);v.setUint32(8,13);bytes.set([73,72,68,82],12);v.setUint32(16,w);v.setUint32(20,h);
 elements.get('asset-kind').value=kind;elements.get('asset-file').files=[{size,arrayBuffer:async()=>bytes.buffer}];return elements.get('asset-file').onchange();
}
function finishDecode(){assert(decoders.length);decoders.shift()();}
select('portrait');
""".replace('HTML_PATH', json.dumps(str(EDITOR / 'index.html'))).replace('EDITOR_URL', json.dumps((EDITOR / 'editor.mjs').as_uri()))

    def test_delayed_asset_preserves_current_edit_and_reset_refuses(self):
        self.node(self.asset_environment() + """
const pending=beginImport();await flush();
const node=elements.get('board').children.find(x=>x.dataset.id==='portrait');
node.onpointerdown({button:0,preventDefault:()=>{},clientX:100,clientY:100,pointerId:1,target:node});node.onpointermove({clientX:124,clientY:108,shiftKey:false});node.onpointerup();
elements.get('prop-label').value='Edited during decode';elements.get('prop-label').onchange();
finishDecode();await pending;
let doc=await exportDoc(), layer=doc.layers.find(x=>x.id==='portrait');assert.equal(layer.x,320);assert.equal(layer.label,'Edited during decode');assert.equal(layer.assetRef,'private:image-1');
const resetPending=beginImport();await flush();const rejectedUrl=`blob:test-${urls}`;
elements.get('seed').onclick();await flush();finishDecode();await resetPending;
assert.match(elements.get('status').textContent,/Layout replaced/);doc=await exportDoc();layer=doc.layers.find(x=>x.id==='portrait');assert.equal(layer.x,297);assert.equal(layer.assetRef,'');assert(revoked.includes(rejectedUrl));
// A JSON replacement invalidates pending imports even when IDs are identical.
select('portrait');const jsonPending=beginImport();await flush();const jsonUrl=`blob:test-${urls}`;
const replacement=m.clone(seed);replacement.name='Replacement';elements.get('layout-file').files=[{size:100,text:async()=>m.exportLayout(replacement)}];await elements.get('layout-file').onchange();finishDecode();await jsonPending;
assert.match(elements.get('status').textContent,/Layout replaced/);doc=await exportDoc();assert.equal(doc.name,'Replacement');assert.equal(doc.layers.find(x=>x.id==='portrait').assetRef,'');assert(revoked.includes(jsonUrl));
""")

    def test_asset_size_and_target_admission(self):
        self.node(self.asset_environment() + """
let reads=0;elements.get('asset-file').files=[{size:8*1024*1024+1,arrayBuffer:async()=>{reads++;throw new Error('must not read')}}];await elements.get('asset-file').onchange();assert.equal(reads,0);assert.equal(decoders.length,0);assert.equal(urls,0);
let pending=beginImport();await flush();const lockedUrl=`blob:test-${urls}`;elements.get('prop-locked').type='checkbox';elements.get('prop-locked').checked=true;elements.get('prop-locked').onchange();finishDecode();await pending;assert.match(elements.get('status').textContent,/locked/);assert(revoked.includes(lockedUrl));
elements.get('prop-locked').checked=false;elements.get('prop-locked').onchange();pending=beginImport();await flush();const dragUrl=`blob:test-${urls}`;
const node=elements.get('board').children.find(x=>x.dataset.id==='portrait');node.onpointerdown({button:0,preventDefault:()=>{},clientX:100,clientY:100,pointerId:1,target:node});node.onpointermove({clientX:124,clientY:108,shiftKey:false});finishDecode();await pending;assert.match(elements.get('status').textContent,/active drag/);assert(revoked.includes(dragUrl));node.onpointerup();
const doc=await exportDoc();assert.equal(doc.layers.find(x=>x.id==='portrait').x,320);assert.equal(doc.layers.find(x=>x.id==='portrait').assetRef,'');
pending=beginImport();await flush();const removedUrl=`blob:test-${urls}`;elements.get('delete').onclick();finishDecode();await pending;assert.match(elements.get('status').textContent,/removed/);assert(revoked.includes(removedUrl));assert(!(await exportDoc()).layers.some(x=>x.id==='portrait'));
""")

    def test_concurrent_asset_budgets_checked_at_commit(self):
        for size, width, height, count, admitted in [(8*1024*1024,1,1,5,4), (1,2048,4096,3,2), (1,1,1,33,32)]:
            with self.subTest(size=size, width=width, count=count):
                self.node(self.asset_environment() + f"""
const pending=[];for(let i=0;i<{count};i++){{pending.push(beginImport({size},{width},{height}));await flush();}}
assert.equal(decoders.length,{admitted}); // pending imports are reserved before allocating decoded images.
for(let i=0;i<{admitted};i++){{finishDecode();await pending[i];}}await Promise.all(pending);
const doc=await exportDoc();assert.equal(doc.layers.find(x=>x.id==='portrait').assetRef,'private:image-{admitted}');
assert.equal(urls,{admitted}+1); // rejected pre-decode imports allocate no Blob URL.
assert.equal(revoked.length,0); // accepted image URLs remain session-owned.
""")

    def test_canceled_pending_asset_reservations_are_released(self):
        self.node(self.asset_environment() + """
const pending=[];for(let i=0;i<4;i++){pending.push(beginImport(8*1024*1024));await flush();}
assert.equal(decoders.length,4);elements.get('seed').onclick();await flush();
for(const promise of pending){finishDecode();await promise;}assert.equal(revoked.length,4);
select('portrait');const retry=beginImport(8*1024*1024);await flush();assert.equal(decoders.length,1);finishDecode();await retry;
assert.equal((await exportDoc()).layers.find(x=>x.id==='portrait').assetRef,'private:image-1');
""")

if __name__ == '__main__':
    unittest.main()
