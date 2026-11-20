/* DISKMODE Web Console frontend. Vanilla JS, no build step, no external code.
   Everything renders on the client; the MPC only serves JSON and bytes. */
'use strict';

const API = '/api/v1';
const S = {                      // application state
  auth: null, role: 'none', csrf: null, trusted: true,
  view: 'files', volumes: [], jobs: [], uploads: {}, pollTimer: null,
  screen: { running: false, timer: null, scale: 2, fps: 5, status: null },
  drawerOpen: true,
};

// ------------------------------------------------------------ helpers
const $ = (sel, root = document) => root.querySelector(sel);
const h = (tag, attrs = {}, ...children) => {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'html') el.innerHTML = v;              // only used with trusted, app-generated markup
    else if (k === 'style') el.style.cssText = v;         // CSSOM, allowed under style-src 'self' (no inline attributes)
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (v === true) el.setAttribute(k, '');
    else el.setAttribute(k, v);
  }
  for (const c of children.flat()) if (c != null && c !== false) el.append(c.nodeType ? c : document.createTextNode(String(c)));
  return el;
};
const fmtBytes = n => { if (n == null) return '–'; const u = ['B', 'KB', 'MB', 'GB', 'TB']; let i = 0, v = Number(n); while (v >= 1024 && i < 4) { v /= 1024; i++; } return (i ? v.toFixed(1) : v) + ' ' + u[i]; };
const fmtDate = s => s ? new Date(s * 1000).toLocaleString(undefined, { dateStyle: 'short', timeStyle: 'short' }) : '';
const fmtTs = ms => ms ? new Date(ms).toLocaleTimeString() : '';
const basename = p => p.split('/').pop();
const parent = p => p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '';
const join = (a, b) => a ? a + '/' + b : b;
const kindOf = n => { const e = (n.split('.').pop() || '').toLowerCase(); if (['wav', 'aif', 'aiff', 'flac', 'mp3', 'ogg', 'm4a'].includes(e)) return 'audio'; if (['xpj', 'xpm', 'xpl', 'sxq', 'mid', 'midi', 'xal'].includes(e)) return 'project'; return 'other'; };

function toast(msg, kind = '') {
  let box = $('.toasts'); if (!box) { box = h('div', { class: 'toasts', role: 'status' }); document.body.append(box); }
  const t = h('div', { class: 'toast ' + kind, text: msg }); box.append(t);
  setTimeout(() => t.remove(), kind === 'err' ? 7000 : 3500);
}

async function api(method, path, body, opts = {}) {
  const headers = {}; let payload;
  if (body !== undefined) { if (opts.raw) { payload = body; headers['Content-Type'] = 'application/octet-stream'; } else { payload = JSON.stringify(body); headers['Content-Type'] = 'application/json'; } }
  if (method !== 'GET' && S.csrf) headers['X-DM-CSRF'] = S.csrf;
  const res = await fetch(API + path, { method, headers, body: payload, credentials: 'same-origin', signal: opts.signal });
  const ct = res.headers.get('content-type') || '';
  const data = ct.includes('application/json') ? await res.json() : (opts.binary ? await res.arrayBuffer() : await res.text());
  if (res.status === 401 && S.auth) { S.auth = false; render(); throw new Error('Session ended. Please pair again.'); }
  if (!res.ok && !opts.allowError) throw Object.assign(new Error((data && data.message) || res.statusText || 'request failed'), { status: res.status, data });
  return opts.withResponse ? { res, data } : data;
}

// ------------------------------------------------------------ dialogs
function dialog({ title, body, buttons, onOpen }) {
  return new Promise(resolve => {
    const dlg = h('dialog', { 'aria-label': title });
    const close = v => { dlg.close(); dlg.remove(); resolve(v); };
    const foot = h('div', { class: 'dlg-foot' });
    for (const b of buttons) foot.append(h('button', { class: b.class || '', text: b.label, onclick: () => { const v = b.value instanceof Function ? b.value() : b.value; if (v !== undefined) close(v); } }));
    dlg.append(h('div', { class: 'dlg-head', text: title }), h('div', { class: 'dlg-body' }, body), foot);
    dlg.addEventListener('cancel', e => { e.preventDefault(); close(null); });
    document.body.append(dlg); dlg.showModal(); if (onOpen) onOpen(dlg);
  });
}
const confirmDlg = (title, text, label = 'OK', cls = 'primary') => dialog({ title, body: h('p', { text }), buttons: [{ label: 'Cancel', value: false }, { label, value: true, class: cls }] });
function promptDlg(title, label, value = '', validate) {
  const input = h('input', { type: 'text', value, autocomplete: 'off', spellcheck: 'false' });
  const err = h('div', { class: 'muted', style: 'min-height:18px;margin-top:4px' });
  const check = () => { const p = validate ? validate(input.value) : ''; err.textContent = p || ''; return !p; };
  input.addEventListener('input', check);
  return dialog({ title, body: h('div', {}, h('label', { text: label }), input, err), onOpen: () => { input.focus(); input.select(); },
    buttons: [{ label: 'Cancel', value: null }, { label: 'OK', class: 'primary', value: () => check() ? input.value : undefined }] });
}
const nameProblem = n => !n ? 'Name is required' : /[\/\\<>:"|?*\x00-\x1f]/.test(n) ? 'Characters / \\ < > : " | ? * are not allowed' : (n === '.' || n === '..' || /[. ]$/.test(n)) ? 'Invalid name' : n.length > 255 ? 'Name too long' : '';

function contextMenu(x, y, items) {
  closeMenu();
  const m = h('div', { class: 'menu', role: 'menu' });
  for (const it of items) { if (it === '-') { m.append(h('hr')); continue; } m.append(h('button', { role: 'menuitem', text: it.label, disabled: it.disabled, onclick: () => { closeMenu(); it.action(); } })); }
  document.body.append(m);
  const r = m.getBoundingClientRect(); m.style.left = Math.min(x, innerWidth - r.width - 8) + 'px'; m.style.top = Math.min(y, innerHeight - r.height - 8) + 'px';
  setTimeout(() => document.addEventListener('pointerdown', outsideMenu), 0);
  document.addEventListener('keydown', escMenu);
}
function outsideMenu(e) { if (!e.target.closest || !e.target.closest('.menu')) closeMenu(); }
function closeMenu() { document.querySelectorAll('.menu').forEach(m => m.remove()); document.removeEventListener('keydown', escMenu); document.removeEventListener('pointerdown', outsideMenu); }
function escMenu(e) { if (e.key === 'Escape') closeMenu(); }

// ------------------------------------------------------------ jobs / uploads drawer
async function refreshJobs() {
  try { const j = await api('GET', '/jobs'); S.jobs = j.jobs; } catch (e) { /* offline */ }
  renderDrawer();
  const active = S.jobs.some(j => j.state === 'queued' || j.state === 'running') || Object.values(S.uploads).some(u => u.state === 'uploading');
  if (typeof Explorer !== 'undefined') Explorer.onJobsChanged(S.jobs);
  clearTimeout(S.pollTimer); S.pollTimer = setTimeout(refreshJobs, active ? 700 : 4000);
}
async function waitJob(id) {
  for (;;) { const j = await api('GET', '/jobs/' + id); if (!['queued', 'running'].includes(j.state)) return j; await new Promise(r => setTimeout(r, 300)); }
}
function jobLabel(j) { return { copy: 'Copy', move: 'Move', delete: 'Delete', hash: 'Checksum', index: 'Index', duplicates: 'Find duplicates' }[j.type] || j.type; }
function renderDrawer() {
  const body = $('#drawer-body'); if (!body) return;
  const head = $('#drawer-count');
  const items = [];
  for (const u of Object.values(S.uploads)) {
    const pct = u.size ? Math.round(u.sent / u.size * 100) : 0;
    items.push(h('div', { class: 'job' }, h('div', {}, h('b', { text: 'Upload ' }), u.name, ' → ', h('span', { class: 'muted', text: u.inbox ? 'Smart Inbox' : u.volume + ':' + (u.dir || '/') })),
      h('div', { class: 'row' }, h('span', { class: 'badge ' + (u.state === 'failed' ? 'err' : u.state === 'done' ? 'ok' : ''), text: u.state + (u.state === 'uploading' ? ' ' + pct + '%' : '') }),
        u.state === 'uploading' && h('button', { class: 'icon', title: 'Cancel', text: '✕', onclick: () => u.abort() })),
      h('div', { class: 'meter' }, h('i', { style: 'width:' + pct + '%' })), u.error && h('div', { class: 'items' }, h('span', { class: 'err', text: u.error }))));
  }
  for (const j of S.jobs) {
    const pct = j.total ? Math.min(100, Math.round(j.done / j.total * 100)) : (j.state === 'done' ? 100 : 0);
    const badge = { done: 'ok', failed: 'err', partial: 'warn', cancelled: 'warn', running: 'info', queued: '' }[j.state] || '';
    const failed = j.items.filter(i => i.state === 'failed');
    items.push(h('div', { class: 'job' },
      h('div', {}, h('b', { text: jobLabel(j) + ' ' }), j.items.length ? j.items.length + ' item' + (j.items.length > 1 ? 's' : '') : '', j.items[0] && h('span', { class: 'muted', text: '  ' + j.items[0].source + (j.items[0].destination ? ' → ' + parent(j.items[0].destination.split(':')[1] || '') : '') })),
      h('div', { class: 'row' }, h('span', { class: 'badge ' + badge, text: j.state }), ['queued', 'running'].includes(j.state) && h('button', { class: 'icon', title: 'Cancel', text: '✕', onclick: () => api('POST', '/jobs/' + j.id + '/cancel').then(refreshJobs).catch(e => toast(e.message, 'err')) })),
      ['running', 'queued'].includes(j.state) && h('div', { class: 'meter' }, h('i', { style: 'width:' + pct + '%' })),
      (failed.length || j.error) && h('div', { class: 'items' }, j.error && h('span', { class: 'err', text: j.error + ' ' }), failed.slice(0, 5).map(i => h('div', {}, h('span', { class: 'err', text: '✕ ' }), i.source + ': ' + i.error)), failed.length > 5 && h('div', { text: '… and ' + (failed.length - 5) + ' more' }))));
  }
  body.replaceChildren(...(items.length ? items : [h('div', { class: 'muted', style: 'padding:8px 0', text: 'No transfers or operations yet.' })]));
  const active = S.jobs.filter(j => ['queued', 'running'].includes(j.state)).length + Object.values(S.uploads).filter(u => u.state === 'uploading').length;
  if (head) head.textContent = active ? active + ' active' : (S.jobs.length + Object.keys(S.uploads).length) + ' total';
}

// ------------------------------------------------------------ uploads (chunked, resumable)
async function uploadFiles(files, target) {   // target: {volume, dir} or {inbox:true}
  for (const f of files) uploadOne(f, target);
}
async function sha256Hex(file) {
  if (!crypto.subtle || file.size > 256 * 1024 * 1024) return '';
  try { return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', await file.arrayBuffer()))).map(b => b.toString(16).padStart(2, '0')).join(''); } catch { return ''; }
}
async function uploadOne(file, target) {
  const key = Math.random().toString(36).slice(2);
  const ctl = new AbortController();
  const u = S.uploads[key] = { name: file.name, size: file.size, sent: 0, state: 'uploading', volume: target.volume, dir: target.dir, inbox: !!target.inbox, abort: () => { ctl.abort(); u.state = 'cancelled'; renderDrawer(); } };
  renderDrawer();
  try {
    if (nameProblem(file.name)) throw new Error('File name not allowed: ' + file.name);
    const sha = await sha256Hex(file);
    const init = await api('POST', '/transfers/upload', { volume: target.volume, dir: target.dir, name: file.name, size: file.size, sha256: sha, inbox: !!target.inbox }, { signal: ctl.signal });
    const chunk = Math.min(init.chunk_max, 4 * 1024 * 1024);
    let off = 0, attempts = 0;
    while (off < file.size) {
      try {
        const r = await api('PUT', '/transfers/upload/' + init.id + '?offset=' + off, file.slice(off, off + chunk), { raw: true, signal: ctl.signal });
        off = r.upload.received; u.sent = off; renderDrawer(); attempts = 0;
      } catch (e) {
        if (ctl.signal.aborted) throw e;
        if (e.data && e.data.upload) { off = e.data.upload.received; continue; }          // resume where the server is
        if (++attempts > 5) throw e;
        await new Promise(r => setTimeout(r, 1000 * attempts));                           // network hiccup: retry
      }
    }
    let r = await api('POST', '/transfers/upload/' + init.id + '/commit', {}, { allowError: true, withResponse: true, signal: ctl.signal });
    if (r.res.status === 409 && !target.inbox) {
      const policy = await conflictDialog([file.name], target.volume + ':' + (target.dir || '/'));
      if (!policy) { await api('DELETE', '/transfers/upload/' + init.id); u.state = 'cancelled'; renderDrawer(); return; }
      r = await api('POST', '/transfers/upload/' + init.id + '/commit', { conflict: policy }, { withResponse: true, allowError: true });
    }
    if (!r.res.ok) throw new Error(r.data.message || 'commit failed');
    u.state = 'done'; u.sent = file.size;
    toast(target.inbox ? 'Added to Smart Inbox: ' + file.name : 'Uploaded ' + (r.data.path || file.name), 'ok');
    if (Explorer) Explorer.refreshAll();
    if (S.view === 'inbox') render();
  } catch (e) {
    if (u.state !== 'cancelled') { u.state = 'failed'; u.error = e.message; toast('Upload failed: ' + e.message, 'err'); }
  }
  renderDrawer();
}
function conflictDialog(names, dest) {
  return dialog({ title: 'Name conflict', body: h('div', {}, h('p', { text: (names.length === 1 ? '"' + names[0] + '" already exists' : names.length + ' items already exist') + ' in ' + dest + '.' }),
    h('p', { class: 'muted', text: 'Keep both renames the new item ("name 2.wav"). Overwrite replaces existing files and cannot be undone.' })),
    buttons: [{ label: 'Cancel', value: null }, { label: 'Skip existing', value: 'fail' }, { label: 'Keep both', value: 'rename', class: 'primary' }, { label: 'Overwrite', value: 'overwrite', class: 'danger' }] });
}

// ------------------------------------------------------------ views
const VIEWS = {
  dashboard: { label: 'Dashboard', icon: '▦' }, files: { label: 'Files', icon: '▤' }, inbox: { label: 'Smart Inbox', icon: '⤓' },
  search: { label: 'Search', icon: '⌕' }, collections: { label: 'Collections', icon: '✦' }, screen: { label: 'Screen Bridge', icon: '▭' },
  storage: { label: 'SD Utilities', icon: '◫' }, settings: { label: 'Settings', icon: '⚙' },
};

function render() {
  const app = $('#app');
  if (!S.auth) { app.replaceChildren(renderLogin()); return; }
  if (!$('.shell')) {
    app.replaceChildren(h('div', { class: 'shell' },
      h('aside', { class: 'sidebar' }, h('div', { class: 'brand', html: 'DISK<span>MODE</span>' }), h('nav', { class: 'nav', id: 'nav' }),
        h('div', { class: 'foot' }, h('span', { id: 'foot-role' }), h('span', { id: 'foot-conn' }))),
      h('section', { class: 'main' }, h('div', { class: 'topbar' }, h('h2', { id: 'title' }), h('div', { id: 'topbar-actions', class: 'row' })), h('div', { class: 'content', id: 'content' })),
      h('div', { class: 'drawer' + (S.drawerOpen ? '' : ' collapsed'), id: 'drawer' },
        h('div', { class: 'drawer-head', onclick: () => { S.drawerOpen = !S.drawerOpen; $('#drawer').classList.toggle('collapsed', !S.drawerOpen); } }, h('b', { text: 'Transfers & operations' }), h('span', { class: 'badge', id: 'drawer-count' }), h('span', { class: 'grow' }), h('span', { class: 'muted', text: '▴▾' })),
        h('div', { class: 'drawer-body', id: 'drawer-body' }))));
    refreshJobs();
  }
  const nav = $('#nav');
  nav.replaceChildren(...Object.entries(VIEWS).map(([k, v]) => h('button', { class: S.view === k ? 'active' : '', 'aria-current': S.view === k ? 'page' : null, onclick: () => { S.view = k; location.hash = k; render(); } }, h('span', { text: v.icon, 'aria-hidden': 'true' }), h('span', { class: 'label', text: v.label }), k === 'inbox' && S.inboxCount ? h('span', { class: 'n', text: S.inboxCount }) : null)));
  $('#title').textContent = VIEWS[S.view].label;
  $('#foot-role').replaceChildren(h('span', { class: 'badge ' + (S.role === 'admin' ? 'warn' : ''), text: S.role.replace('_', ' ') }), ' ', S.trusted ? h('span', { class: 'badge ok', text: location.protocol === 'https:' ? 'HTTPS' : 'local' }) : h('span', { class: 'badge err', text: 'insecure' }));
  $('#topbar-actions').replaceChildren();
  const content = $('#content');
  if (S.view !== 'files' && Explorer) Explorer.detach();
  ({ dashboard: renderDashboard, files: () => Explorer.mount(content), inbox: renderInbox, search: renderSearch, collections: renderCollections, screen: renderScreen, storage: renderStorage, settings: renderSettings })[S.view](content);
  if (S.view !== 'screen') stopScreen();
}

function renderLogin() {
  const code = h('input', { type: 'password', autocomplete: 'one-time-code', inputmode: 'text', 'aria-label': 'Pairing code' });
  const role = h('select', {}, h('option', { value: 'file_manager', text: 'File Manager (browse, upload, organise)' }), h('option', { value: 'viewer', text: 'Viewer (read-only)' }));
  const msg = h('div', {});
  const submit = async e => {
    e.preventDefault();
    try { const r = await api('POST', '/auth/pair', { code: code.value, role: role.value }); S.auth = true; S.role = r.role; S.csrf = r.csrf; S.trusted = r.trusted_transport; S.view = (location.hash || '#files').slice(1); if (!VIEWS[S.view]) S.view = 'files'; render(); loadVolumes(); }
    catch (err) { msg.replaceChildren(h('div', { class: 'notice err', text: err.message })); }
  };
  const form = h('form', { onsubmit: submit }, h('label', { text: 'Pairing code (shown by the device)' }), code, h('label', { text: 'Access level' }), role, h('div', { style: 'margin-top:14px' }, h('button', { class: 'primary', type: 'submit', text: 'Pair', style: 'width:100%' })), msg);
  const card = h('div', { class: 'card' }, h('h1', { html: 'DISK<span>MODE</span>' }), h('div', { class: 'muted', text: 'Web Console for the Akai MPC Sample' }), form);
  api('GET', '/auth/status').then(st => {
    if (!st.trusted_transport) msg.append(h('div', { class: 'notice warn', text: st.warning || 'Not an HTTPS connection: only read-only viewing is possible.' }));
    if (st.authenticated) { S.auth = true; S.role = st.role; S.csrf = st.csrf; S.trusted = st.trusted_transport; S.view = (location.hash || '#files').slice(1); if (!VIEWS[S.view]) S.view = 'files'; render(); loadVolumes(); }
  }).catch(() => msg.append(h('div', { class: 'notice err', text: 'Cannot reach the device. Check the USB-C connection.' })));
  return h('div', { class: 'login' }, card);
}

async function loadVolumes() {
  try { const r = await api('GET', '/volumes'); S.volumes = r.volumes; if (Explorer) Explorer.onVolumes(S.volumes); } catch (e) { toast(e.message, 'err'); }
  try { const r = await api('GET', '/inbox'); S.inboxCount = r.items.length; } catch { }
  if (S.auth) { const n = $('#nav'); if (n) render(); }
}
const vol = id => S.volumes.find(v => v.id === id);

// ------------------------------------------------------------ dashboard
async function renderDashboard(c) {
  c.replaceChildren(h('div', { class: 'muted', text: 'Loading…' }));
  let st; try { st = await api('GET', '/storage/status'); } catch (e) { c.replaceChildren(h('div', { class: 'notice err', text: e.message })); return; }
  S.volumes = st.volumes;
  const cards = st.volumes.map(v => {
    const used = v.capacity ? Math.round(v.used / v.capacity * 100) : 0;
    return h('div', { class: 'card' }, h('div', { class: 'row' }, h('h3', { class: 'grow', text: v.name }), h('span', { class: 'badge', text: v.kind }),
      v.mounted ? h('span', { class: 'badge ' + (v.writable ? 'ok' : 'warn'), text: v.writable ? 'read/write' : v.ownership === 'usb-host' ? 'USB host owns card' : 'read-only' }) : h('span', { class: 'badge err', text: 'not mounted' })),
      v.mounted && h('div', { class: 'meter ' + (used > 95 ? 'err' : used > 85 ? 'warn' : '') }, h('i', { style: 'width:' + used + '%' })),
      h('div', { class: 'muted', style: 'margin-top:6px', text: v.mounted ? fmtBytes(v.used) + ' used of ' + fmtBytes(v.capacity) + ' · ' + fmtBytes(v.free) + ' free · ' + v.fs_type : 'No filesystem is available for this volume.' }),
      v.counts && v.counts.indexed && h('div', { class: 'muted', text: v.counts.files + ' files · ' + v.counts.folders + ' folders · audio ' + ((v.counts.by_kind.audio || {}).files || 0) + ' · projects ' + ((v.counts.by_kind.project || {}).files || 0) }),
      v.mounted && h('div', { class: 'row', style: 'margin-top:8px' }, h('button', { text: 'Browse', onclick: () => { S.view = 'files'; render(); Explorer.openIn(0, v.id, ''); } }),
        !(v.counts && v.counts.indexed) && h('button', { text: 'Index for search', onclick: () => api('POST', '/search/index', { volume: v.id }).then(() => { toast('Indexing ' + v.name); refreshJobs(); }).catch(e => toast(e.message, 'err')) })));
  });
  const warn = st.warnings.map(w => h('div', { class: 'notice ' + (w.level === 'warn' ? 'warn' : ''), text: w.message }));
  const errs = st.recent_errors.length ? h('div', { class: 'card' }, h('h3', { text: 'Recent operation problems' }), h('table', { class: 'list' }, h('tbody', {}, st.recent_errors.slice(0, 10).map(e => h('tr', {}, h('td', { text: e.ts.replace('T', ' ').replace('Z', '') }), h('td', { text: e.op }), h('td', { text: e.source }), h('td', {}, h('span', { class: 'badge err', text: e.state })), h('td', { class: 'muted', text: e.detail })))))) : null;
  const srv = st.server;
  c.replaceChildren(h('div', { class: 'grid' }, ...cards), h('div', { style: 'margin-top:12px; display:grid; gap:12px' }, ...warn, errs,
    h('div', { class: 'card' }, h('h3', { text: 'Server' }), h('div', { class: 'muted', text: 'Version ' + srv.version + ' · up ' + Math.round(srv.uptime_s / 60) + ' min · RSS ' + fmtBytes(srv.rss_kb * 1024) + ' · screen source: ' + srv.screen_source + (srv.screen_simulated ? ' (simulated)' : '') + ' · formatting: simulation only' }),
      h('div', { class: 'muted', text: st.media_health.note }))));
}

// ------------------------------------------------------------ inbox
async function renderInbox(c) {
  c.replaceChildren(h('div', { class: 'muted', text: 'Loading…' }));
  const canWrite = ['file_manager', 'admin'].includes(S.role);
  const input = h('input', { type: 'file', multiple: true, accept: 'audio/*,.wav,.aif,.aiff,.flac,.mp3', class: 'hidden', onchange: e => { uploadFiles([...e.target.files], { inbox: true }); e.target.value = ''; } });
  $('#topbar-actions').replaceChildren(input, h('button', { class: 'primary', text: 'Add samples…', disabled: !canWrite, onclick: () => input.click() }));
  let r; try { r = await api('GET', '/inbox'); } catch (e) { c.replaceChildren(h('div', { class: 'notice err', text: e.message })); return; }
  S.inboxCount = r.items.length;
  const drop = h('div', { class: 'card', style: 'text-align:center; border-style:dashed', text: canWrite ? 'Drop audio files here (or use "Add samples…"). Files are staged first and only land in a sample folder when you commit them.' : 'Pair as File Manager to add samples.',
    ondragover: e => { e.preventDefault(); drop.style.borderColor = 'var(--accent)'; }, ondragleave: () => drop.style.borderColor = '', ondrop: e => { e.preventDefault(); drop.style.borderColor = ''; if (canWrite) uploadFiles([...e.dataTransfer.files], { inbox: true }); } });
  const items = r.items.map(it => {
    const canvas = h('canvas', { class: 'wave', width: 800, height: 80 });
    if (it.wav && it.wav.supported) api('GET', '/inbox/' + it.id + '/peaks?buckets=400').then(p => drawPeaks(canvas, p.peaks)).catch(() => { });
    const audio = h('audio', { controls: true, preload: 'none', src: API + '/inbox/' + it.id + '/audio', style: 'width:100%; margin-top:6px' });
    const dup = it.duplicates.length ? h('div', { class: 'notice warn', text: 'Identical file already on the device: ' + it.duplicates.map(d => d.volume + ':' + d.path).join(', ') }) : null;
    const meta = it.wav && it.wav.valid ? [it.wav.channels === 1 ? 'mono' : it.wav.channels === 2 ? 'stereo' : it.wav.channels + ' ch', it.wav.sample_rate + ' Hz', it.wav.bits + '-bit', it.wav.seconds.toFixed(2) + ' s'].join(' · ') : (it.note || '');
    return h('div', { class: 'card inbox-item' },
      h('div', {}, h('b', { text: it.name }), h('div', { class: 'muted', text: fmtBytes(it.size) + ' · ' + meta }), h('div', { class: 'muted mono', style: 'font-size:11px', text: 'sha256 ' + it.sha256.slice(0, 16) + '…' })),
      h('div', { class: 'row' }, h('button', { text: 'Rename', disabled: !canWrite, onclick: async () => { const n = await promptDlg('Rename sample', 'New name', it.name, nameProblem); if (n) api('POST', '/inbox/' + it.id + '/rename', { name: n }).then(() => render()).catch(e => toast(e.message, 'err')); } }),
        h('button', { class: 'primary', text: 'Move to folder…', disabled: !canWrite, onclick: () => commitInbox(it) }),
        h('button', { class: 'danger', text: 'Discard', disabled: !canWrite, onclick: async () => { if (await confirmDlg('Discard staged sample', 'Remove "' + it.name + '" from the inbox? It was never written to a sample folder.', 'Discard', 'danger')) api('DELETE', '/inbox/' + it.id).then(() => render()).catch(e => toast(e.message, 'err')); } })),
      canvas, dup, it.wav && it.wav.valid ? audio : h('div', { class: 'muted', text: 'Audition is available for WAV files only.' }));
  });
  c.replaceChildren(drop, h('div', { class: 'muted', style: 'margin:10px 0', text: 'Audition plays in this browser (not through the MPC outputs). Device-side audition is not available.' }), ...(items.length ? items : [h('div', { class: 'empty', text: 'The inbox is empty.' })]));
}
async function commitInbox(it) {
  const target = await pickFolder('Move "' + it.name + '" to');
  if (!target) return;
  try {
    const r = await api('POST', '/inbox/' + it.id + '/commit', { volume: target.volume, path: target.path }, { allowError: true, withResponse: true });
    if (r.res.status === 409) { const pol = await conflictDialog([it.name], target.volume + ':' + (target.path || '/')); if (!pol) return; const r2 = await api('POST', '/inbox/' + it.id + '/commit', { volume: target.volume, path: target.path, conflict: pol }); toast('Moved to ' + r2.volume + ':' + r2.path, 'ok'); }
    else if (!r.res.ok) throw new Error(r.data.message);
    else toast('Moved to ' + r.data.volume + ':' + r.data.path, 'ok');
    render(); if (Explorer) Explorer.refreshAll();
  } catch (e) { toast(e.message, 'err'); }
}
function drawPeaks(canvas, peaks) {
  const ctx = canvas.getContext('2d'), W = canvas.width, H = canvas.height, n = peaks.length / 2;
  ctx.clearRect(0, 0, W, H); ctx.fillStyle = '#5cb3ff';
  for (let i = 0; i < n; i++) { const lo = peaks[i * 2], hi = peaks[i * 2 + 1]; const y1 = H / 2 - hi * H / 2, y2 = H / 2 - lo * H / 2; ctx.fillRect(i * W / n, Math.min(y1, y2), Math.max(1, W / n - 1), Math.max(1, Math.abs(y2 - y1))); }
  ctx.fillStyle = '#2b323d'; ctx.fillRect(0, H / 2, W, 1);
}
// Folder picker: a small browser over writable volumes.
function pickFolder(title) {
  return new Promise(resolve => {
    const writable = S.volumes.filter(v => v.writable);
    if (!writable.length) { toast('No writable volume is available.', 'err'); resolve(null); return; }
    let cur = { volume: writable[0].id, path: '' };
    const sel = h('select', {}, ...writable.map(v => h('option', { value: v.id, text: v.name })));
    const crumbs = h('div', { class: 'crumbs' }); const list = h('div', { class: 'filelist', style: 'max-height:40vh; border:1px solid var(--line); border-radius:8px' });
    const load = async () => {
      const r = await api('GET', '/files?volume=' + encodeURIComponent(cur.volume) + '&path=' + encodeURIComponent(cur.path) + '&kind=dir');
      crumbs.replaceChildren(h('button', { text: vol(cur.volume).name, onclick: () => { cur.path = ''; load(); } }), ...cur.path.split('/').filter(Boolean).map((p, i, a) => [h('span', { class: 'sep', text: '›' }), h('button', { text: p, onclick: () => { cur.path = a.slice(0, i + 1).join('/'); load(); } })]).flat());
      list.replaceChildren(...(r.entries.length ? r.entries.map(e => h('div', { class: 'fitem', style: 'grid-template-columns:28px 1fr', onclick: () => { cur.path = join(cur.path, e.name); load(); } }, h('span', { class: 'ic dir', text: '▸' }), h('span', { class: 'name', text: e.name + (e.protected ? '  🔒' : '') }))) : [h('div', { class: 'empty', text: 'No subfolders' })]));
    };
    sel.onchange = () => { cur = { volume: sel.value, path: '' }; load(); };
    load().catch(e => toast(e.message, 'err'));
    dialog({ title, body: h('div', {}, sel, crumbs, list, h('div', { class: 'muted', style: 'margin-top:6px', text: 'The folder shown in the path above is the destination.' })),
      buttons: [{ label: 'Cancel', value: null }, { label: 'Use this folder', class: 'primary', value: () => ({ ...cur }) }] }).then(resolve);
  });
}

// ------------------------------------------------------------ search
function renderSearch(c) {
  const q = h('input', { type: 'search', placeholder: 'Search file names…', 'aria-label': 'Search' });
  const kind = h('select', {}, h('option', { value: '', text: 'All types' }), h('option', { value: 'audio', text: 'Audio' }), h('option', { value: 'project', text: 'Projects' }), h('option', { value: 'other', text: 'Other' }));
  const volSel = h('select', {}, h('option', { value: '', text: 'All volumes' }), ...S.volumes.map(v => h('option', { value: v.id, text: v.name })));
  const sort = h('select', {}, h('option', { value: 'name', text: 'Name' }), h('option', { value: 'mtime', text: 'Modified' }), h('option', { value: 'size', text: 'Size' }));
  const results = h('div', {}); const status = h('div', { class: 'muted', style: 'margin:8px 0' });
  let t;
  const run = async () => {
    try {
      const r = await api('GET', '/search?q=' + encodeURIComponent(q.value) + '&kind=' + kind.value + '&volume=' + volSel.value + '&sort=' + sort.value + '&desc=' + (sort.value === 'mtime' ? 1 : 0) + '&limit=300');
      status.textContent = r.total + ' result' + (r.total === 1 ? '' : 's') + (r.total > 300 ? ' (showing 300)' : '');
      results.replaceChildren(...(r.results.length ? [h('table', { class: 'list' }, h('thead', {}, h('tr', {}, h('th', { text: 'Name' }), h('th', { text: 'Folder' }), h('th', { text: 'Volume' }), h('th', { text: 'Size' }), h('th', { text: 'Modified' }), h('th'))),
        h('tbody', {}, r.results.map(f => h('tr', {}, h('td', {}, h('span', { class: 'ic ' + f.kind, style: 'display:inline-grid; margin-right:6px', text: f.kind[0].toUpperCase() }), f.name), h('td', { class: 'muted', text: parent(f.path) || '/' }), h('td', { class: 'muted', text: (vol(f.volume) || {}).name || f.volume }), h('td', { text: fmtBytes(f.size) }), h('td', { class: 'muted', text: fmtDate(f.mtime) }),
          h('td', {}, h('button', { text: 'Show', onclick: () => { S.view = 'files'; render(); Explorer.openIn(0, f.volume, parent(f.path), f.name); } }))))))] : [h('div', { class: 'empty', text: q.value ? 'Nothing found. Is the volume indexed?' : 'Type to search the index.' })]));
    } catch (e) { status.textContent = e.message; }
  };
  const debounce = () => { clearTimeout(t); t = setTimeout(run, 200); };
  [q, kind, volSel, sort].forEach(el => el.addEventListener('input', debounce));
  const idx = h('div', { class: 'row' });
  api('GET', '/search/status').then(r => idx.replaceChildren(h('span', { class: 'muted', text: 'Indexed: ' + (r.indexes.length ? r.indexes.map(i => i.volume + ' (' + i.files + ' files, ' + (i.indexed_at ? new Date(i.indexed_at).toLocaleString() : 'never') + ')').join(', ') : 'nothing yet') }))).catch(() => { });
  const indexBtn = h('button', { text: 'Re-index…', onclick: async () => { const v = await pickVolume('Index which volume?', S.volumes.filter(v => v.mounted && v.ownership === 'device')); if (v) api('POST', '/search/index', { volume: v }).then(() => { toast('Indexing started'); refreshJobs(); }).catch(e => toast(e.message, 'err')); } });
  const dupBtn = h('button', { text: 'Find duplicates…', onclick: async () => { const v = await pickVolume('Find duplicates on', S.volumes.filter(v => v.mounted)); if (!v) return; try { const j = await api('POST', '/search/duplicates', { volume: v }); toast('Scanning for duplicates…'); refreshJobs(); const done = await waitJob(j.job); showDuplicates(done.detail, v); } catch (e) { toast(e.message, 'err'); } } });
  $('#topbar-actions').replaceChildren(indexBtn, dupBtn);
  c.replaceChildren(h('div', { class: 'row' }, h('div', { class: 'grow' }, q), kind, volSel, sort), idx, status, results);
  q.focus();
}
function pickVolume(title, list) {
  const sel = h('select', {}, ...list.map(v => h('option', { value: v.id, text: v.name })));
  if (!list.length) { toast('No suitable volume.', 'err'); return Promise.resolve(null); }
  return dialog({ title, body: sel, buttons: [{ label: 'Cancel', value: null }, { label: 'Start', class: 'primary', value: () => sel.value }] });
}
function showDuplicates(d, volume) {
  const groups = (d && d.groups) || [];
  const body = h('div', {}, h('p', { class: 'muted', text: groups.length + ' group' + (groups.length === 1 ? '' : 's') + ' of identical files (same size and SHA-256).' + (d.truncated ? ' Not every candidate was hashed yet; run again for more.' : '') }),
    ...groups.map(g => h('div', { class: 'card', style: 'margin-bottom:8px' }, h('div', { class: 'muted', text: fmtBytes(g.size) + ' · ' + g.sha256.slice(0, 12) + '…' }), ...g.files.map(f => h('div', { class: 'row', style: 'padding:4px 0' }, h('span', { class: 'grow', text: f.path }), h('button', { text: 'Show', onclick: () => { document.querySelector('dialog').close(); document.querySelector('dialog').remove(); S.view = 'files'; render(); Explorer.openIn(0, volume, parent(f.path), f.name); } }))))));
  dialog({ title: 'Duplicate files', body, buttons: [{ label: 'Close', value: true }] });
}

// ------------------------------------------------------------ collections / favorites / recents
async function renderCollections(c) {
  c.replaceChildren(h('div', { class: 'muted', text: 'Loading…' }));
  let cols, favs, rec;
  try { [cols, favs, rec] = await Promise.all([api('GET', '/collections'), api('GET', '/favorites'), api('GET', '/recents')]); } catch (e) { c.replaceChildren(h('div', { class: 'notice err', text: e.message })); return; }
  $('#topbar-actions').replaceChildren(h('button', { class: 'primary', text: 'New collection', onclick: async () => { const n = await promptDlg('New collection', 'Name', '', v => v ? '' : 'Name is required'); if (n) api('POST', '/collections', { name: n }).then(() => render()).catch(e => toast(e.message, 'err')); } }));
  const show = (v, p) => { S.view = 'files'; render(); Explorer.openIn(0, v, parent(p), basename(p)); };
  const item = (it, extra) => h('div', { class: 'row', style: 'padding:5px 0; border-bottom:1px solid var(--line)' }, h('span', { class: 'ic ' + kindOf(basename(it.path)), text: kindOf(basename(it.path))[0].toUpperCase() }), h('span', { class: 'grow', text: basename(it.path) }), h('span', { class: 'muted', text: (vol(it.volume) || {}).name || it.volume }), h('button', { text: 'Show', onclick: () => show(it.volume, it.path) }), extra);
  const colCards = cols.collections.map(col => h('div', { class: 'card' }, h('div', { class: 'row' }, h('h3', { class: 'grow', text: col.name }), h('span', { class: 'badge', text: col.items.length + ' items' }),
    h('button', { class: 'icon', title: 'Rename', text: '✎', onclick: async () => { const n = await promptDlg('Rename collection', 'Name', col.name); if (n) api('POST', '/collections/' + col.id + '/rename', { name: n }).then(() => render()).catch(e => toast(e.message, 'err')); } }),
    h('button', { class: 'icon danger', title: 'Delete collection', text: '🗑', onclick: async () => { if (await confirmDlg('Delete collection', 'Delete "' + col.name + '"? The files themselves are not touched.', 'Delete', 'danger')) api('DELETE', '/collections/' + col.id).then(() => render()).catch(e => toast(e.message, 'err')); } })),
    h('div', { class: 'muted', style: 'margin-bottom:6px', text: 'A virtual collection: files stay where they are.' }),
    ...(col.items.length ? col.items.map(it => item(it, h('button', { class: 'icon', title: 'Remove from collection', text: '✕', onclick: () => api('POST', '/collections/' + col.id + '/remove', { volume: it.volume, path: it.path }).then(() => render()) }))) : [h('div', { class: 'muted', text: 'Empty. Right-click a file in Files → Add to collection.' })])));
  c.replaceChildren(h('div', { class: 'grid' }, ...colCards,
    h('div', { class: 'card' }, h('h3', { text: 'Favorites' }), ...(favs.favorites.length ? favs.favorites.map(f => item(f, h('button', { class: 'icon', title: 'Unfavorite', text: '★', onclick: () => api('POST', '/favorites', { ...f, on: false }).then(() => render()) }))) : [h('div', { class: 'muted', text: 'No favorites yet.' })])),
    h('div', { class: 'card' }, h('h3', { text: 'Recent files' }), ...(rec.recents.length ? rec.recents.slice(0, 20).map(r => item(r, h('span', { class: 'muted', text: fmtTs(r.ts) }))) : [h('div', { class: 'muted', text: 'Nothing downloaded or previewed yet.' })]))));
}

// ------------------------------------------------------------ screen bridge
async function renderScreen(c) {
  const canvas = h('canvas', { width: 320, height: 240, 'aria-label': 'MPC Sample display' });
  const stamp = h('span', { class: 'stamp badge sim hidden', text: 'SIMULATED' });
  const status = h('div', { class: 'muted' });
  const frame = h('div', { class: 'screen-frame' }, canvas, stamp);
  const scale = h('select', { onchange: () => { S.screen.scale = +scale.value; apply(); } }, ...[1, 2, 3, 4].map(s => h('option', { value: s, text: s + '×', selected: s === S.screen.scale })));
  const fps = h('select', { onchange: () => { S.screen.fps = +fps.value; } }, ...[1, 2, 5, 10].map(f => h('option', { value: f, text: f + ' fps', selected: f === S.screen.fps })));
  const toggle = h('button', { class: 'primary', text: S.screen.running ? 'Stop' : 'Start viewing', onclick: () => { S.screen.running ? stopScreen() : startScreen(); toggle.textContent = S.screen.running ? 'Stop' : 'Start viewing'; } });
  const full = h('button', { text: 'Fullscreen', onclick: () => frame.requestFullscreen && frame.requestFullscreen() });
  const apply = () => { canvas.style.width = 320 * S.screen.scale + 'px'; canvas.style.height = 240 * S.screen.scale + 'px'; };
  apply();
  S.screen.canvas = canvas; S.screen.stamp = stamp; S.screen.statusEl = status;
  let st; try { st = await api('GET', '/screen/status'); } catch (e) { status.textContent = e.message; }
  S.screen.status = st;
  const info = st ? (st.available ? (st.simulated ? 'Frame source: SIMULATED test pattern. These frames are NOT captured from the device.' : 'Frame source: ' + st.source + '. Frames come from an external capture file written by a verified pipeline.') : 'No live frames: ' + st.reason) : '';
  c.replaceChildren(h('div', { class: 'screen-wrap' }, h('div', { class: 'row' }, toggle, h('label', { text: 'Scale' }), scale, h('label', { text: 'Rate' }), fps, full), frame,
    h('div', { class: 'notice ' + (st && st.simulated ? 'warn' : ''), text: info }), status,
    h('div', { class: 'card', style: 'max-width:640px' }, h('h3', { text: 'Remote control' }), h('div', { class: 'muted', text: 'Disabled. Physical pads, buttons and the encoder keep working while you view the screen. Remote input will only be offered once the native command-handling path is integrated and verified on hardware.' }))));
  if (S.screen.running) startScreen();
}
function startScreen() {
  if (!S.screen.canvas) return;
  S.screen.running = true;
  const tick = async () => {
    if (!S.screen.running || S.view !== 'screen') return;
    const t0 = performance.now();
    try {
      const { res, data } = await api('GET', '/screen/frame', undefined, { binary: true, withResponse: true, allowError: true });
      if (res.ok) {
        const sim = res.headers.get('X-DM-Frame-Simulated') === '1';
        S.screen.stamp.classList.toggle('hidden', !sim);
        drawRGB565(S.screen.canvas, new Uint8Array(data));
        S.screen.statusEl.textContent = 'Connected · frame ' + res.headers.get('X-DM-Frame-Seq') + ' · source ' + res.headers.get('X-DM-Frame-Source') + ' · ' + Math.round(performance.now() - t0) + ' ms';
      } else if (res.status !== 429) S.screen.statusEl.textContent = 'Stream problem: ' + ((data && data.message) || res.status) + ' (retrying)';
    } catch (e) { S.screen.statusEl.textContent = 'Disconnected, reconnecting… ' + e.message; }
    S.screen.timer = setTimeout(tick, Math.max(50, 1000 / S.screen.fps - (performance.now() - t0)));
  };
  tick();
}
function stopScreen() { S.screen.running = false; clearTimeout(S.screen.timer); if (S.screen.statusEl) S.screen.statusEl.textContent = 'Stopped.'; }
function drawRGB565(canvas, bytes) {
  const ctx = canvas.getContext('2d'); const img = ctx.createImageData(320, 240); const d = img.data;
  for (let i = 0, p = 0; i < 320 * 240; i++, p += 4) { const v = bytes[i * 2] | (bytes[i * 2 + 1] << 8); d[p] = ((v >> 11) & 31) * 255 / 31; d[p + 1] = ((v >> 5) & 63) * 255 / 63; d[p + 2] = (v & 31) * 255 / 31; d[p + 3] = 255; }
  ctx.putImageData(img, 0, 0);
}

// ------------------------------------------------------------ storage utilities (dashboard details, trash, format simulator)
async function renderStorage(c) {
  c.replaceChildren(h('div', { class: 'muted', text: 'Loading…' }));
  let st, net; try { [st, net] = await Promise.all([api('GET', '/storage/status'), api('GET', '/storage/network')]); } catch (e) { c.replaceChildren(h('div', { class: 'notice err', text: e.message })); return; }
  const isAdmin = S.role === 'admin';
  const trashCard = h('div', { class: 'card' }, h('h3', { text: 'Trash' }), h('div', { class: 'muted', text: 'Deleted items are kept in a hidden folder on the same card until you restore or purge them.' }));
  for (const v of st.volumes.filter(v => v.mounted)) {
    const box = h('div', { style: 'margin-top:8px' }, h('b', { text: v.name }));
    api('GET', '/files/trash?volume=' + v.id).then(r => {
      box.append(...(r.items.length ? r.items.map(t => h('div', { class: 'row', style: 'padding:4px 0; border-bottom:1px solid var(--line)' }, h('span', { class: 'grow', text: t.original_path }), h('span', { class: 'muted', text: fmtBytes(t.size) + ' · ' + t.deleted_at.replace('T', ' ').replace('Z', '') }),
        h('button', { text: 'Restore', disabled: !v.writable, onclick: () => api('POST', '/files/trash/restore', { volume: v.id, id: t.id }).then(r => { toast('Restored ' + r.path, 'ok'); render(); if (Explorer) Explorer.refreshAll(); }).catch(e => toast(e.message, 'err')) }),
        h('button', { class: 'danger', text: 'Purge', disabled: !v.writable, onclick: async () => { if (await confirmDlg('Purge from trash', 'Permanently delete "' + t.name + '"? This cannot be undone.', 'Purge', 'danger')) api('POST', '/files/trash/purge', { volume: v.id, id: t.id }).then(() => render()).catch(e => toast(e.message, 'err')); } }))) : [h('span', { class: 'muted', text: ' – empty' })]));
    }).catch(() => box.append(h('span', { class: 'muted', text: ' – unavailable' })));
    trashCard.append(box);
  }
  const netCard = h('div', { class: 'card' }, h('h3', { text: 'USB-C network' }), h('div', { class: 'muted', text: net.note }),
    h('table', { class: 'list' }, h('tbody', {}, ...net.interfaces.filter(i => i.name !== 'lo').map(i => h('tr', {}, h('td', {}, h('b', { text: i.name }), i.usb_like ? h('span', { class: 'badge ok', style: 'margin-left:6px', text: 'USB' }) : null), h('td', { class: 'mono', text: [...i.ipv4, ...i.ipv6.filter(a => !a.startsWith('fe80'))].join(' ') || '–' }), h('td', {}, h('span', { class: 'badge ' + (i.running ? 'ok' : 'warn'), text: i.running ? 'up' : 'down' })))))),
    h('div', { class: 'muted', text: 'Reachable at: ' + (net.urls.join('  ') || 'no address yet') + ' · you are ' + net.client + ' · ' + (net.tls ? 'HTTPS' : 'plain HTTP') }));
  const fmtCard = h('div', { class: 'card' }, h('div', { class: 'row' }, h('h3', { class: 'grow', text: 'Format microSD card' }), h('span', { class: 'badge sim', text: 'SIMULATION ONLY' })),
    h('div', { class: 'muted', text: 'Formatting erases every file on the card. In this build the final step is a non-destructive simulation: all safety checks run for real, no formatting utility is ever invoked. Real formatting stays disabled until the device-level safety gates have been verified on hardware.' }),
    h('div', { class: 'row', style: 'margin-top:10px' }, isAdmin ? h('button', { class: 'danger', text: 'Start format wizard…', onclick: () => formatWizard(st.volumes) }) : h('span', { class: 'muted', text: 'Administrator access is required (Settings → Administrator).' })));
  const jcard = h('div', { class: 'card' }, h('h3', { text: 'Operation journal' }), h('div', { id: 'journal', class: 'muted', text: 'Loading…' }));
  api('GET', '/storage/journal').then(r => $('#journal').replaceChildren(h('table', { class: 'list' }, h('tbody', {}, ...r.entries.slice(0, 40).map(e => h('tr', {}, h('td', { class: 'muted', text: e.ts.replace('T', ' ').replace('Z', '') }), h('td', { text: e.op }), h('td', { text: e.source }), h('td', { text: e.destination }), h('td', {}, h('span', { class: 'badge ' + ({ done: 'ok', failed: 'err', interrupted: 'err', partial: 'warn', cancelled: 'warn' }[e.state] || ''), text: e.state })), h('td', { class: 'muted', text: e.detail })))))));
  c.replaceChildren(h('div', { style: 'display:grid; gap:12px' }, trashCard, fmtCard, netCard, jcard));
}
async function formatWizard(volumes) {
  const candidates = volumes.filter(v => v.format_candidate);
  if (!candidates.length) { toast('No volume is eligible for formatting by policy.', 'err'); return; }
  const sel = h('select', {}, ...candidates.map(v => h('option', { value: v.id, text: v.name + (v.mounted ? ' (' + v.fs_type + ', ' + fmtBytes(v.capacity) + ')' : ' (not mounted)') })));
  const v = await dialog({ title: 'Step 1 of 3 · Select the card', body: h('div', {}, h('p', { text: 'Only removable microSD cards allowed by policy are listed. Internal storage can never be formatted.' }), sel), buttons: [{ label: 'Cancel', value: null }, { label: 'Run safety checks', class: 'primary', value: () => sel.value }] });
  if (!v) return;
  let pre; try { pre = await api('POST', '/admin/format/preflight', { volume: v }); } catch (e) { toast(e.message, 'err'); return; }
  const list = h('ul', { class: 'checks' }, ...pre.checks.map(ck => h('li', {}, h('b', { class: ck.pass ? 'pass' : 'fail', text: ck.pass ? '✓' : '✕' }), h('span', {}, h('b', { text: ck.name.replaceAll('_', ' ') }), h('div', { class: 'muted', text: ck.detail })))));
  const info = h('dl', { style: 'display:grid; grid-template-columns:140px 1fr; gap:4px 10px' }, h('dt', { class: 'muted', text: 'Volume' }), h('dd', { text: (pre.volume_info || {}).name || v }), h('dt', { class: 'muted', text: 'Device' }), h('dd', { class: 'mono', text: pre.device || 'not identified' }), h('dt', { class: 'muted', text: 'Capacity' }), h('dd', { text: fmtBytes(pre.size_bytes) }), h('dt', { class: 'muted', text: 'Filesystem' }), h('dd', { text: (pre.volume_info || {}).fs_type || '–' }));
  const ok = await dialog({ title: 'Step 2 of 3 · Safety checks ' + (pre.eligible ? 'passed' : 'FAILED'), body: h('div', {}, info, list, !pre.eligible && h('div', { class: 'notice err', text: 'Formatting is refused because at least one prerequisite failed. Nothing was changed.' })),
    buttons: pre.eligible ? [{ label: 'Cancel', value: false }, { label: 'Continue', class: 'danger', value: true }] : [{ label: 'Close', value: false }] });
  if (!ok) return;
  const label = h('input', { type: 'text', maxlength: 11, placeholder: 'SAMPLES' });
  const chk = h('input', { type: 'checkbox', id: 'fmt-ack' });
  const fs = h('select', {}, ...pre.supported_fs.map(f => h('option', { value: f, text: f })));
  const go = await dialog({ title: 'Step 3 of 3 · Confirm erase', body: h('div', {}, h('div', { class: 'notice err', text: 'ALL DATA on ' + ((pre.volume_info || {}).name || v) + ' (' + pre.device + ', ' + fmtBytes(pre.size_bytes) + ') will be erased. Back up your samples and projects first.' }),
    h('label', { text: 'Filesystem' }), fs, h('label', { text: 'Volume label (optional, max 11 characters)' }), label,
    h('label', { style: 'display:flex; gap:8px; align-items:center; margin-top:14px' }, chk, 'I selected the right card and understand that all its data will be erased.'),
    h('p', { class: 'muted', text: 'This build performs a simulation: the privileged helper is not invoked.' })),
    buttons: [{ label: 'Cancel', value: null }, { label: 'Format (simulated)', class: 'danger', value: () => chk.checked ? true : (toast('Tick the confirmation first.', 'err'), undefined) }] });
  if (!go) return;
  try {
    const r = await api('POST', '/admin/format/confirm', { token: pre.token, volume: v, device: pre.device, fs_type: fs.value, label: label.value, confirm_erase: true }, { allowError: true });
    dialog({ title: r.ok ? 'Simulated format completed' : 'Format refused', body: h('div', {}, h('div', { class: 'notice ' + (r.ok ? 'ok' : 'err'), text: r.ok ? (r.result.note || 'ok') : r.message }), r.result && h('pre', { class: 'mono', style: 'white-space:pre-wrap; font-size:12px', text: JSON.stringify(r.result, null, 2) }), r.ok && h('div', { class: 'muted', text: 'Remounted: ' + r.remounted + ' · filesystem afterwards: ' + r.filesystem_after })), buttons: [{ label: 'Close', value: true }] });
  } catch (e) { toast(e.message, 'err'); }
}

// ------------------------------------------------------------ settings
async function renderSettings(c) {
  let me; try { me = await api('GET', '/session'); } catch (e) { c.replaceChildren(h('div', { class: 'notice err', text: e.message })); return; }
  const s = me.session;
  const adminCard = h('div', { class: 'card' }, h('h3', { text: 'Administrator' }));
  if (S.role === 'admin') {
    adminCard.append(h('div', { class: 'muted', text: 'This session has administrator rights (session management, guarded formatting).' }));
    const list = h('div', {}); adminCard.append(list);
    api('GET', '/session/list').then(r => list.replaceChildren(h('table', { class: 'list' }, h('thead', {}, h('tr', {}, h('th', { text: 'Client' }), h('th', { text: 'Role' }), h('th', { text: 'Last seen' }), h('th', { text: 'State' }), h('th'))), h('tbody', {}, ...r.sessions.map(x => h('tr', {}, h('td', { text: x.client + (x.current ? ' (this)' : '') }), h('td', { text: x.role }), h('td', { class: 'muted', text: fmtTs(x.last_seen) }), h('td', {}, h('span', { class: 'badge ' + (x.revoked ? 'err' : 'ok'), text: x.revoked ? 'revoked' : 'active' })), h('td', {}, !x.current && !x.revoked && h('button', { text: 'Revoke', onclick: () => api('POST', '/session/revoke', { handle: x.handle }).then(() => render()).catch(e => toast(e.message, 'err')) })))))),
      h('div', { class: 'row', style: 'margin-top:8px' }, h('button', { class: 'danger', text: 'Revoke all other sessions', onclick: async () => { if (await confirmDlg('Revoke sessions', 'Sign out every other paired browser?', 'Revoke', 'danger')) api('POST', '/session/revoke_others').then(() => render()); } })))).catch(e => list.textContent = e.message);
  } else {
    const pw = h('input', { type: 'password', autocomplete: 'current-password', 'aria-label': 'Administrator password' });
    adminCard.append(h('div', { class: 'muted', text: 'Administrator access is separate from file access and is needed for session management and the format wizard.' }),
      h('form', { class: 'row', style: 'margin-top:8px', onsubmit: async e => { e.preventDefault(); try { const r = await api('POST', '/auth/elevate', { password: pw.value }); S.role = r.role; toast('Administrator access granted', 'ok'); render(); } catch (err) { toast(err.message, 'err'); } } }, pw, h('button', { class: 'primary', type: 'submit', text: 'Unlock' })));
    if (!S.trusted) adminCard.append(h('div', { class: 'notice warn', text: 'Administrator access requires HTTPS.' }));
  }
  c.replaceChildren(h('div', { class: 'grid' },
    h('div', { class: 'card' }, h('h3', { text: 'This session' }), h('dl', { style: 'display:grid; grid-template-columns:120px 1fr; gap:4px 10px' }, h('dt', { class: 'muted', text: 'Role' }), h('dd', { text: s.role }), h('dt', { class: 'muted', text: 'Client' }), h('dd', { text: s.client }), h('dt', { class: 'muted', text: 'Paired' }), h('dd', { text: new Date(s.created).toLocaleString() }), h('dt', { class: 'muted', text: 'Expires' }), h('dd', { text: new Date(s.expires_absolute).toLocaleString() + ' (or after 30 min idle)' }), h('dt', { class: 'muted', text: 'Transport' }), h('dd', { text: s.trusted_transport ? 'trusted (HTTPS or local)' : 'untrusted plain HTTP' })),
      h('div', { class: 'row', style: 'margin-top:8px' }, h('button', { text: 'Sign out', onclick: () => api('POST', '/auth/logout').then(() => { S.auth = false; S.role = 'none'; S.csrf = null; render(); }) })),
      h('h3', { style: 'margin-top:14px', text: 'Activity' }), h('div', { class: 'muted', style: 'font-size:12px' }, ...s.activity.slice(-10).reverse().map(a => h('div', { text: fmtTs(a.ts) + '  ' + a.event })))),
    adminCard,
    h('div', { class: 'card' }, h('h3', { text: 'Keyboard' }), h('div', { style: 'display:grid; gap:4px; font-size:13px' }, ...[['Tab', 'switch pane'], ['↑ ↓', 'move'], ['Enter', 'open folder / preview'], ['Backspace', 'parent folder'], ['Space', 'toggle selection'], ['Ctrl/⌘ A', 'select all'], ['F2', 'rename'], ['Delete', 'move to trash'], ['Ctrl/⌘ C / X / V', 'copy / move via clipboard'], ['F5', 'refresh']].map(([k, d]) => h('div', { class: 'row' }, h('kbd', { text: k }), h('span', { class: 'muted', text: d }))))),
    h('div', { class: 'card' }, h('h3', { text: 'About' }), h('div', { class: 'muted', text: 'DISKMODE Web Console runs on the MPC Sample and is reached over the USB-C network. No cloud, no account, no internet connection. Features marked SIMULATED do not touch hardware.' }))));
}

// ------------------------------------------------------------ boot
window.addEventListener('hashchange', () => { const v = location.hash.slice(1); if (VIEWS[v] && S.auth) { S.view = v; render(); } });
document.addEventListener('DOMContentLoaded', () => { S.view = VIEWS[location.hash.slice(1)] ? location.hash.slice(1) : 'files'; render(); });
window.addEventListener('online', () => toast('Connection restored', 'ok'));
window.addEventListener('offline', () => toast('Connection lost. Waiting for the USB-C link…', 'err'));
