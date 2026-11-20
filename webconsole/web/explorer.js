/* Dual-pane file explorer for the DISKMODE Web Console. */
'use strict';

const Explorer = (() => {
  const panes = [newPane(), newPane()];
  let active = 0, root = null, phoneShown = 0, clipboard = null, lastJobStates = {};
  const canWrite = () => ['file_manager', 'admin'].includes(S.role);

  function newPane() { return { volume: null, path: '', entries: [], total: 0, truncated: false, selected: new Set(), cursor: -1, history: [], hIdx: -1, sort: 'name', desc: false, filter: '', kind: '', hidden: false, writable: false, el: null, loading: false, error: '' }; }
  const other = i => panes[1 - i];
  const vinfo = id => S.volumes.find(v => v.id === id);
  const key = (p, e) => e.name;

  // ---------------------------------------------------------------- mounting
  function mount(container) {
    root = container;
    const phone = h('div', { class: 'row phone-switch', style: 'margin-bottom:8px' }, h('button', { class: phoneShown === 0 ? 'primary' : '', text: 'Pane A', onclick: () => { phoneShown = 0; active = 0; renderAll(); } }), h('button', { class: phoneShown === 1 ? 'primary' : '', text: 'Pane B', onclick: () => { phoneShown = 1; active = 1; renderAll(); } }),
      h('span', { class: 'muted', text: 'Source → destination: use Copy/Move to the other pane.' }));
    const wrap = h('div', { class: 'explorer' });
    panes.forEach((p, i) => { p.el = h('div', { class: 'pane', tabindex: -1 }); wrap.append(p.el); });
    const details = h('div', { id: 'details' });
    root.replaceChildren(phone, wrap, details);
    root.style.display = 'flex'; root.style.flexDirection = 'column'; wrap.style.flex = '1'; wrap.style.minHeight = '320px';
    document.addEventListener('keydown', onKey);
    $('#topbar-actions').replaceChildren(h('button', { text: 'Refresh', onclick: refreshAll }), h('span', { class: 'muted', text: 'Drag files between panes · right-click for actions' }));
    if (!panes[0].volume) {
      const first = S.volumes.find(v => v.mounted && v.ownership === 'device');
      if (first) { panes[0].volume = first.id; panes[1].volume = (S.volumes.find(v => v.mounted && v.writable && v.id !== first.id) || first).id; }
    }
    renderAll();
    panes.forEach((p, i) => { if (p.volume && !p.entries.length) load(i); });
  }
  function detach() { document.removeEventListener('keydown', onKey); root = null; }
  function onVolumes(vols) { if (root) { if (!panes[0].volume) mount(root); else renderAll(); } }
  function refreshAll() { panes.forEach((p, i) => p.volume && load(i, true)); }
  function onJobsChanged(jobs) {
    let changed = false;
    for (const j of jobs) {
      const terminal = !['queued', 'running'].includes(j.state);
      // A job we saw running has finished, or a job finished so fast that the
      // first poll already shows it terminal.
      if (terminal && ((lastJobStates[j.id] && lastJobStates[j.id] !== j.state) || (!lastJobStates[j.id] && Date.now() - j.finished < 15000))) changed = true;
      lastJobStates[j.id] = j.state;
    }
    if (changed) { refreshAll(); if (S.view === 'inbox' || S.view === 'storage') render(); }
  }
  async function openIn(i, volume, path, selectName) {
    panes[i].volume = volume; navigate(i, path, true);
    if (S.view !== 'files') { S.view = 'files'; render(); }
    if (selectName) { await load(i); const idx = panes[i].entries.findIndex(e => e.name === selectName); if (idx >= 0) { panes[i].selected = new Set([selectName]); panes[i].cursor = idx; renderPane(i); showDetails(i); scrollCursor(i); } }
    active = i; phoneShown = i; renderAll();
  }

  // ---------------------------------------------------------------- loading
  async function load(i, keepSel) {
    const p = panes[i]; if (!p.volume) return;
    p.loading = true; p.error = ''; renderPane(i);
    try {
      const q = new URLSearchParams({ volume: p.volume, path: p.path, sort: p.sort, desc: p.desc ? 1 : 0, filter: p.filter, kind: p.kind, hidden: p.hidden ? 1 : 0, limit: 2000 });
      const r = await api('GET', '/files?' + q);
      p.entries = r.entries; p.total = r.total; p.truncated = r.truncated; p.writable = r.writable;
      if (keepSel) { const names = new Set(p.entries.map(e => e.name)); p.selected = new Set([...p.selected].filter(n => names.has(n))); } else { p.selected.clear(); }
      if (p.cursor >= p.entries.length) p.cursor = p.entries.length - 1;
    } catch (e) {
      p.entries = []; p.total = 0; p.error = e.message;
      if (e.status === 503 || e.status === 404) { p.path = ''; }
    }
    p.loading = false; renderPane(i); if (i === active) showDetails(i);
  }
  function navigate(i, path, push = true) {
    const p = panes[i];
    if (push) { p.history = p.history.slice(0, p.hIdx + 1); p.history.push({ volume: p.volume, path }); p.hIdx = p.history.length - 1; }
    p.path = path; p.cursor = -1; p.filter = ''; load(i);
  }
  function goHistory(i, d) { const p = panes[i]; const n = p.hIdx + d; if (n < 0 || n >= p.history.length) return; p.hIdx = n; p.volume = p.history[n].volume; p.path = p.history[n].path; p.cursor = -1; load(i); }

  // ---------------------------------------------------------------- rendering
  function renderAll() { panes.forEach((p, i) => renderPane(i)); }
  function renderPane(i) {
    const p = panes[i]; if (!p.el) return;
    const v = vinfo(p.volume);
    p.el.className = 'pane' + (i === active ? ' active' : '') + (window.innerWidth <= 1000 && i !== phoneShown ? ' inactive-pane' : '');
    p.el.onpointerdown = () => { if (active !== i) { active = i; renderAll(); showDetails(i); } };
    const volSel = h('select', { 'aria-label': 'Volume', onchange: e => { p.volume = e.target.value; navigate(i, ''); } }, ...S.volumes.map(x => h('option', { value: x.id, text: x.name + (x.mounted ? '' : ' (not mounted)') + (x.ownership === 'usb-host' ? ' (USB host)' : ''), selected: x.id === p.volume })));
    const filter = h('input', { type: 'search', placeholder: 'Filter', value: p.filter, 'aria-label': 'Filter files', style: 'width:110px', oninput: e => { p.filter = e.target.value; clearTimeout(p.ft); p.ft = setTimeout(() => load(i, true), 200); } });
    const sort = h('select', { 'aria-label': 'Sort', onchange: e => { const [s, d] = e.target.value.split(':'); p.sort = s; p.desc = d === '1'; load(i, true); } }, ...[['name:0', 'Name ↑'], ['name:1', 'Name ↓'], ['mtime:1', 'Newest'], ['mtime:0', 'Oldest'], ['size:1', 'Largest'], ['size:0', 'Smallest'], ['type:0', 'Type']].map(([val, t]) => h('option', { value: val, text: t, selected: val === p.sort + ':' + (p.desc ? 1 : 0) })));
    const kind = h('select', { 'aria-label': 'Type filter', onchange: e => { p.kind = e.target.value; load(i, true); } }, ...[['', 'All'], ['dir', 'Folders'], ['audio', 'Audio'], ['project', 'Projects'], ['other', 'Other']].map(([val, t]) => h('option', { value: val, text: t, selected: val === p.kind })));
    const upload = h('input', { type: 'file', multiple: true, class: 'hidden', onchange: e => { uploadFiles([...e.target.files], { volume: p.volume, dir: p.path }); e.target.value = ''; } });
    const bar = h('div', { class: 'pane-bar' },
      h('button', { class: 'icon', title: 'Back', 'aria-label': 'Back', text: '◀', disabled: p.hIdx <= 0, onclick: () => goHistory(i, -1) }),
      h('button', { class: 'icon', title: 'Forward', 'aria-label': 'Forward', text: '▶', disabled: p.hIdx >= p.history.length - 1, onclick: () => goHistory(i, 1) }),
      h('button', { class: 'icon', title: 'Up', 'aria-label': 'Parent folder', text: '↑', disabled: !p.path, onclick: () => navigate(i, parent(p.path)) }),
      volSel, filter, sort, kind,
      h('button', { class: 'icon', title: 'New folder', 'aria-label': 'New folder', text: '＋', disabled: !p.writable || !canWrite(), onclick: () => newFolder(i) }),
      h('button', { class: 'icon', title: 'Upload files', 'aria-label': 'Upload', text: '⤒', disabled: !p.writable || !canWrite(), onclick: () => upload.click() }), upload,
      h('button', { class: 'icon', title: p.hidden ? 'Hide hidden files' : 'Show hidden files', text: p.hidden ? '◉' : '◎', onclick: () => { p.hidden = !p.hidden; load(i, true); } }),
      h('button', { class: 'icon', title: 'Refresh', 'aria-label': 'Refresh', text: '⟳', onclick: () => load(i, true) }));
    const parts = p.path.split('/').filter(Boolean);
    const crumb = (label, path) => h('button', { text: label, onclick: () => navigate(i, path), ondragover: e => { if (dragOk(e)) { e.preventDefault(); e.currentTarget.classList.add('drop-target'); } }, ondragleave: e => e.currentTarget.classList.remove('drop-target'), ondrop: e => { e.currentTarget.classList.remove('drop-target'); onDrop(e, i, path); } });
    const crumbs = h('div', { class: 'crumbs' }, crumb(v ? v.name : '–', ''), ...parts.map((s, k) => [h('span', { class: 'sep', text: '›' }), crumb(s, parts.slice(0, k + 1).join('/'))]).flat());
    const list = h('div', { class: 'filelist', tabindex: 0, role: 'listbox', 'aria-multiselectable': 'true', 'aria-label': 'Files',
      ondragover: e => { if (dragOk(e)) { e.preventDefault(); e.dataTransfer.dropEffect = e.dataTransfer.types.includes('Files') ? 'copy' : (e.altKey ? 'copy' : 'move'); p.el.classList.add('dropping'); } }, ondragleave: e => { if (!p.el.contains(e.relatedTarget)) p.el.classList.remove('dropping'); },
      ondrop: e => { p.el.classList.remove('dropping'); onDrop(e, i, p.path); }, oncontextmenu: e => { if (e.target === list) { e.preventDefault(); menuFor(i, null, e.clientX, e.clientY); } } });
    if (p.loading && !p.entries.length) list.append(h('div', { class: 'empty', text: 'Loading…' }));
    else if (p.error) list.append(h('div', { class: 'empty' }, h('div', { class: 'notice err', text: p.error })));
    else if (!p.entries.length) list.append(h('div', { class: 'empty', text: p.filter ? 'No matches' : 'Empty folder' + (p.writable && canWrite() ? ' – drop files here to upload' : '') }));
    else p.entries.forEach((e, idx) => list.append(renderItem(i, e, idx)));
    const selSize = [...p.selected].reduce((a, n) => { const e = p.entries.find(x => x.name === n); return a + (e ? e.size : 0); }, 0);
    const foot = h('div', { class: 'pane-foot' }, h('span', { text: p.total + ' item' + (p.total === 1 ? '' : 's') + (p.truncated ? ' (list truncated)' : '') }), p.selected.size ? h('span', { text: '· ' + p.selected.size + ' selected (' + fmtBytes(selSize) + ')' }) : null, h('span', { class: 'grow' }),
      v && v.mounted ? h('span', { text: fmtBytes(v.free) + ' free' }) : null, v && !p.writable ? h('span', { class: 'badge warn', text: v.ownership === 'usb-host' ? 'USB host owns card' : 'read-only' }) : null);
    const sel = [...p.selected];
    const acts = h('div', { class: 'actions' },
      h('button', { text: 'Copy →' + (window.innerWidth <= 1000 ? ' ' + (i ? 'A' : 'B') : ''), disabled: !sel.length || !other(i).writable || !canWrite(), title: 'Copy selection to the other pane', onclick: () => transfer('copy', i, sel, 1 - i, other(i).path) }),
      h('button', { text: 'Move →', disabled: !sel.length || !p.writable || !other(i).writable || !canWrite(), onclick: () => transfer('move', i, sel, 1 - i, other(i).path) }),
      h('button', { text: 'Rename', disabled: sel.length !== 1 || !p.writable || !canWrite(), onclick: () => rename(i, sel[0]) }),
      h('button', { text: 'Download', disabled: sel.length !== 1 || !isFile(p, sel[0]), onclick: () => download(i, sel[0]) }),
      h('button', { class: 'danger', text: 'Delete', disabled: !sel.length || !p.writable || !canWrite(), onclick: () => remove(i, sel) }),
      h('button', { text: 'More…', disabled: !sel.length, onclick: e => { const r = e.currentTarget.getBoundingClientRect(); menuFor(i, sel, r.left, r.bottom); } }));
    p.el.replaceChildren(bar, crumbs, list, foot, acts);
    p.list = list;
  }
  const isFile = (p, n) => (p.entries.find(e => e.name === n) || {}).type === 'file';
  function renderItem(i, e, idx) {
    const p = panes[i];
    const k = e.type === 'dir' ? 'dir' : kindOf(e.name);
    const el = h('div', { class: 'fitem' + (p.selected.has(e.name) ? ' selected' : '') + (p.cursor === idx ? ' cursor' : ''), role: 'option', 'aria-selected': p.selected.has(e.name) ? 'true' : 'false', draggable: 'true', dataset: { name: e.name },
      onclick: ev => { ev.stopPropagation(); select(i, idx, ev); },
      ondblclick: () => open(i, e),
      oncontextmenu: ev => { ev.preventDefault(); if (!p.selected.has(e.name)) { p.selected = new Set([e.name]); p.cursor = idx; renderPane(i); showDetails(i); } menuFor(i, [...p.selected], ev.clientX, ev.clientY); },
      ondragstart: ev => { if (!p.selected.has(e.name)) { p.selected = new Set([e.name]); renderPane(i); } ev.dataTransfer.setData('application/x-diskmode-items', JSON.stringify({ pane: i, volume: p.volume, dir: p.path, names: [...p.selected] })); ev.dataTransfer.effectAllowed = 'copyMove'; },
      ondragover: ev => { if (e.type === 'dir' && dragOk(ev)) { ev.preventDefault(); ev.stopPropagation(); el.classList.add('drop-target'); } }, ondragleave: () => el.classList.remove('drop-target'),
      ondrop: ev => { el.classList.remove('drop-target'); if (e.type === 'dir') { ev.stopPropagation(); onDrop(ev, i, join(p.path, e.name)); } } },
      h('span', { class: 'ic ' + k, text: e.type === 'dir' ? '▸' : k === 'audio' ? '♪' : k === 'project' ? 'P' : '·', title: 'Toggle selection', onclick: ev => { ev.stopPropagation(); toggle(i, idx); } }),
      h('span', { class: 'name', text: e.name }, e.protected ? h('span', { class: 'lock', title: 'Protected: cannot be changed from the console', text: '🔒' }) : null),
      h('span', { class: 'size', text: e.type === 'dir' ? '' : fmtBytes(e.size) }), h('span', { class: 'date', text: fmtDate(e.mtime) }));
    return el;
  }
  function scrollCursor(i) { const p = panes[i]; const el = p.list && p.list.children[p.cursor]; if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' }); }

  // ---------------------------------------------------------------- selection and navigation
  function select(i, idx, ev) {
    const p = panes[i]; const name = p.entries[idx].name; active = i;
    if (ev.shiftKey && p.cursor >= 0) { const [a, b] = [Math.min(p.cursor, idx), Math.max(p.cursor, idx)]; for (let k = a; k <= b; k++) p.selected.add(p.entries[k].name); }
    else if (ev.ctrlKey || ev.metaKey) { p.selected.has(name) ? p.selected.delete(name) : p.selected.add(name); p.cursor = idx; }
    else { p.selected = new Set([name]); p.cursor = idx; }
    renderAll(); showDetails(i);
  }
  function toggle(i, idx) { const p = panes[i]; const n = p.entries[idx].name; p.selected.has(n) ? p.selected.delete(n) : p.selected.add(n); p.cursor = idx; active = i; renderAll(); showDetails(i); }
  function open(i, e) { const p = panes[i]; if (e.type === 'dir') navigate(i, join(p.path, e.name)); else { p.selected = new Set([e.name]); renderPane(i); showDetails(i); } }
  function onKey(ev) {
    if (S.view !== 'files' || ev.target.matches('input, select, textarea') || document.querySelector('dialog[open]')) return;
    const p = panes[active]; const mod = ev.ctrlKey || ev.metaKey;
    const act = {
      Tab: () => { active = 1 - active; phoneShown = active; renderAll(); showDetails(active); },
      ArrowDown: () => { p.cursor = Math.min(p.entries.length - 1, p.cursor + 1); if (!ev.shiftKey) p.selected = new Set(); if (p.cursor >= 0) p.selected.add(p.entries[p.cursor].name); renderPane(active); scrollCursor(active); showDetails(active); },
      ArrowUp: () => { p.cursor = Math.max(0, p.cursor - 1); if (!ev.shiftKey) p.selected = new Set(); if (p.cursor >= 0 && p.entries[p.cursor]) p.selected.add(p.entries[p.cursor].name); renderPane(active); scrollCursor(active); showDetails(active); },
      Enter: () => { if (p.cursor >= 0) open(active, p.entries[p.cursor]); },
      Backspace: () => { if (p.path) navigate(active, parent(p.path)); },
      ' ': () => { if (p.cursor >= 0) toggle(active, p.cursor); },
      F2: () => { if (p.selected.size === 1 && canWrite()) rename(active, [...p.selected][0]); },
      F5: () => load(active, true),
      Delete: () => { if (p.selected.size && canWrite()) remove(active, [...p.selected]); },
      Escape: () => { p.selected.clear(); renderPane(active); showDetails(active); },
    };
    if (mod && ev.key.toLowerCase() === 'a') { p.selected = new Set(p.entries.map(e => e.name)); renderPane(active); ev.preventDefault(); return; }
    if (mod && ev.key.toLowerCase() === 'c' && p.selected.size) { clipboard = { op: 'copy', volume: p.volume, dir: p.path, names: [...p.selected] }; toast('Copied ' + p.selected.size + ' item(s) to the clipboard'); ev.preventDefault(); return; }
    if (mod && ev.key.toLowerCase() === 'x' && p.selected.size) { clipboard = { op: 'move', volume: p.volume, dir: p.path, names: [...p.selected] }; toast('Cut ' + p.selected.size + ' item(s)'); ev.preventDefault(); return; }
    if (mod && ev.key.toLowerCase() === 'v' && clipboard) { submit(clipboard.op, clipboard.volume, clipboard.dir, clipboard.names, p.volume, p.path); if (clipboard.op === 'move') clipboard = null; ev.preventDefault(); return; }
    if (act[ev.key]) { act[ev.key](); ev.preventDefault(); }
  }

  // ---------------------------------------------------------------- details panel
  let detailsReq = 0;
  async function showDetails(i) {
    const box = $('#details'); if (!box) return;
    const p = panes[i]; const sel = [...p.selected];
    if (sel.length !== 1) { box.replaceChildren(sel.length > 1 ? h('div', { class: 'details muted', text: sel.length + ' items selected' }) : ''); return; }
    const name = sel[0]; const e = p.entries.find(x => x.name === name); if (!e) return;
    const req = ++detailsReq;
    const path = join(p.path, name);
    const dl = h('dl', {}, h('dt', { text: 'Name' }), h('dd', { text: name }), h('dt', { text: 'Location' }), h('dd', { text: (vinfo(p.volume) || {}).name + ':/' + p.path }), h('dt', { text: 'Type' }), h('dd', { text: e.type === 'dir' ? 'Folder' : kindOf(name) + ' file' }),
      e.type !== 'dir' && h('dt', { text: 'Size' }), e.type !== 'dir' && h('dd', { text: fmtBytes(e.size) + ' (' + e.size + ' bytes)' }), h('dt', { text: 'Modified' }), h('dd', { text: fmtDate(e.mtime) }));
    const extra = h('div', {});
    const favBtn = h('button', { text: '☆ Favorite', onclick: async () => { const on = !favBtn.dataset.on; try { await api('POST', '/favorites', { volume: p.volume, path, on }); favBtn.dataset.on = on ? '1' : ''; favBtn.textContent = (on ? '★' : '☆') + ' Favorite'; } catch (err) { toast(err.message, 'err'); } } });
    api('GET', '/favorites').then(r => { if (r.favorites.some(f => f.volume === p.volume && f.path === path)) { favBtn.dataset.on = '1'; favBtn.textContent = '★ Favorite'; } }).catch(() => { });
    box.replaceChildren(h('div', { class: 'details' }, h('div', { class: 'row' }, h('b', { class: 'grow', text: name }), favBtn, e.type === 'file' && h('button', { text: 'Download', onclick: () => download(i, name) }), e.type === 'file' && h('button', { text: 'Checksum', onclick: () => checksum(i, name) })), dl, extra));
    if (e.type === 'file') {
      try {
        const st = await api('GET', '/files/stat?' + new URLSearchParams({ volume: p.volume, path }));
        if (req !== detailsReq) return;
        const tags = h('input', { type: 'text', value: (st.tags || []).join(', '), placeholder: 'tags, comma separated', 'aria-label': 'Tags', onchange: () => api('POST', '/tags', { volume: p.volume, path, tags: tags.value.split(',').map(s => s.trim()).filter(Boolean) }).then(() => toast('Tags saved', 'ok')).catch(err => toast(err.message, 'err')) });
        extra.append(h('div', { class: 'row' }, h('span', { class: 'muted', text: 'Tags' }), h('div', { class: 'grow' }, tags)));
        if (st.wav && st.wav.valid) {
          const w = st.wav;
          extra.append(h('div', { class: 'muted', style: 'margin:6px 0', text: [w.channels === 1 ? 'mono' : w.channels === 2 ? 'stereo' : w.channels + ' ch', w.sample_rate + ' Hz', w.bits + '-bit' + (w.format === 3 ? ' float' : ''), w.seconds.toFixed(2) + ' s'].join(' · ') + (w.supported ? '' : ' · ' + w.note) }));
          if (w.supported) { const cv = h('canvas', { class: 'wave', width: 800, height: 80 }); extra.append(cv); api('GET', '/files/peaks?' + new URLSearchParams({ volume: p.volume, path, buckets: 400 })).then(r => drawPeaks(cv, r.peaks)).catch(() => { }); }
          extra.append(h('audio', { controls: true, preload: 'none', src: API + '/files/download?' + new URLSearchParams({ volume: p.volume, path, inline: 1 }), style: 'width:100%; margin-top:6px' }), h('div', { class: 'muted', style: 'font-size:12px', text: 'Preview plays in this browser, not on the MPC.' }));
        } else if (kindOf(name) === 'audio') extra.append(h('div', { class: 'muted', text: 'Preview is available for WAV files only.' }));
      } catch (err) { if (req === detailsReq) extra.append(h('div', { class: 'notice err', text: err.message })); }
    } else {
      api('GET', '/storage/usage?' + new URLSearchParams({ volume: p.volume, path })).then(r => { if (req === detailsReq) extra.append(h('div', { class: 'muted', text: r.files + ' files, ' + r.folders + ' folders, ' + fmtBytes(r.bytes) })); }).catch(() => { });
    }
  }

  // ---------------------------------------------------------------- operations
  async function newFolder(i) {
    const p = panes[i]; const n = await promptDlg('New folder', 'Folder name', 'New Folder', nameProblem); if (!n) return;
    try { await api('POST', '/files/mkdir', { volume: p.volume, path: join(p.path, n) }); await load(i, true); p.selected = new Set([n]); p.cursor = p.entries.findIndex(e => e.name === n); renderPane(i); } catch (e) { toast(e.message, 'err'); }
  }
  async function rename(i, name) {
    const p = panes[i]; const n = await promptDlg('Rename', 'New name', name, nameProblem); if (!n || n === name) return;
    try { await api('POST', '/files/rename', { volume: p.volume, path: join(p.path, name), name: n }); await load(i, true); p.selected = new Set([n]); renderPane(i); showDetails(i); } catch (e) { toast(e.message, 'err'); }
  }
  async function remove(i, names) {
    const p = panes[i];
    const ok = await dialog({ title: 'Move to trash', body: h('div', {}, h('p', { text: names.length === 1 ? 'Move "' + names[0] + '" to the trash?' : 'Move ' + names.length + ' items to the trash?' }), h('p', { class: 'muted', text: 'Items can be restored from SD Utilities → Trash until they are purged.' })), buttons: [{ label: 'Cancel', value: false }, { label: 'Move to trash', class: 'danger', value: true }] });
    if (!ok) return;
    try { const r = await api('POST', '/files/delete', { items: names.map(n => ({ volume: p.volume, path: join(p.path, n) })) }); toast('Deleting ' + names.length + ' item(s)…'); refreshJobs(); } catch (e) { toast(e.message, 'err'); }
  }
  function download(i, name) {
    const p = panes[i]; const a = h('a', { href: API + '/files/download?' + new URLSearchParams({ volume: p.volume, path: join(p.path, name) }), download: name }); document.body.append(a); a.click(); a.remove();
  }
  async function checksum(i, name) {
    const p = panes[i];
    try { const r = await api('GET', '/files/hash?' + new URLSearchParams({ volume: p.volume, path: join(p.path, name) }));
      if (r.job) { toast('Computing checksum in the background…'); const j = await waitJob(r.job); if (j.state !== 'done') throw new Error(j.error || 'failed'); r.sha256 = j.detail.sha256; }
      dialog({ title: 'SHA-256', body: h('div', {}, h('div', { text: name }), h('code', { class: 'mono', style: 'word-break:break-all', text: r.sha256 })), buttons: [{ label: 'Close', value: true }] });
    } catch (e) { toast(e.message, 'err'); }
  }
  async function transfer(op, fromPane, names, toPane, destPath) { const p = panes[fromPane]; const d = panes[toPane]; await submit(op, p.volume, p.path, names, d.volume, destPath); }
  async function submit(op, volume, dir, names, destVolume, destPath) {
    if (!canWrite()) { toast('Read-only session', 'err'); return; }
    const dv = vinfo(destVolume); if (!dv || !dv.writable) { toast('Destination is not writable', 'err'); return; }
    if (volume === destVolume && dir === destPath) { toast(op === 'copy' ? 'Source and destination are the same folder.' : 'Already there.'); return; }
    if (volume === destVolume) for (const n of names) { const src = join(dir, n); if (destPath === src || destPath.startsWith(src + '/')) { toast('Cannot ' + op + ' a folder into itself', 'err'); return; } }
    let conflict = 'fail', items = names;
    try {
      const existing = await api('GET', '/files?' + new URLSearchParams({ volume: destVolume, path: destPath, limit: 2000, hidden: 1 }));
      const taken = new Set(existing.entries.map(e => e.name));
      const clashes = names.filter(n => taken.has(n));
      if (clashes.length) {
        const pol = await conflictDialog(clashes, dv.name + ':/' + destPath); if (!pol) return;
        if (pol === 'fail') items = names.filter(n => !taken.has(n)); else conflict = pol;
        if (!items.length) { toast('Nothing to ' + op + ': all items were skipped.'); return; }
      }
      await api('POST', '/files/' + op, { items: items.map(n => ({ volume, path: join(dir, n) })), dest: { volume: destVolume, path: destPath }, conflict });
      toast((op === 'copy' ? 'Copying ' : 'Moving ') + items.length + ' item(s) to ' + dv.name + ':/' + destPath); refreshJobs();
    } catch (e) { toast(e.message, 'err'); }
  }

  // ---------------------------------------------------------------- drag and drop
  function dragOk(ev) { return ev.dataTransfer && (ev.dataTransfer.types.includes('application/x-diskmode-items') || ev.dataTransfer.types.includes('Files')); }
  function onDrop(ev, paneIdx, destPath) {
    ev.preventDefault(); const p = panes[paneIdx]; p.el.classList.remove('dropping');
    if (!canWrite() || !p.writable) { toast('This folder is read-only', 'err'); return; }
    if (ev.dataTransfer.files && ev.dataTransfer.files.length) { uploadFiles([...ev.dataTransfer.files], { volume: p.volume, dir: destPath }); return; }
    const raw = ev.dataTransfer.getData('application/x-diskmode-items'); if (!raw) return;
    const d = JSON.parse(raw);
    const op = (ev.altKey || ev.ctrlKey) ? 'copy' : (d.volume === p.volume ? 'move' : 'copy');   // same card: move; across volumes: copy (safer default)
    submit(op, d.volume, d.dir, d.names, p.volume, destPath);
  }

  // ---------------------------------------------------------------- context menu
  function menuFor(i, sel, x, y) {
    const p = panes[i]; const w = p.writable && canWrite(); const one = sel && sel.length === 1; const entry = one && p.entries.find(e => e.name === sel[0]);
    const items = sel ? [
      { label: 'Open', disabled: !one || !entry || entry.type !== 'dir', action: () => open(i, entry) },
      { label: 'Download', disabled: !one || !entry || entry.type !== 'file', action: () => download(i, sel[0]) },
      '-',
      { label: 'Copy to other pane', disabled: !other(i).writable || !canWrite(), action: () => transfer('copy', i, sel, 1 - i, other(i).path) },
      { label: 'Move to other pane', disabled: !w || !other(i).writable, action: () => transfer('move', i, sel, 1 - i, other(i).path) },
      { label: 'Copy to folder…', disabled: !canWrite(), action: async () => { const t = await pickFolder('Copy to'); if (t) submit('copy', p.volume, p.path, sel, t.volume, t.path); } },
      { label: 'Move to folder…', disabled: !w, action: async () => { const t = await pickFolder('Move to'); if (t) submit('move', p.volume, p.path, sel, t.volume, t.path); } },
      '-',
      { label: 'Rename', disabled: !one || !w, action: () => rename(i, sel[0]) },
      { label: 'Batch rename…', disabled: sel.length < 2 || !w, action: () => batchRename(i, sel) },
      { label: 'Add to collection…', action: () => addToCollection(i, sel) },
      { label: 'Checksum (SHA-256)', disabled: !one || !entry || entry.type !== 'file', action: () => checksum(i, sel[0]) },
      '-',
      { label: 'Move to trash', disabled: !w, action: () => remove(i, sel) },
    ] : [
      { label: 'New folder', disabled: !w, action: () => newFolder(i) }, { label: 'Paste', disabled: !clipboard || !w, action: () => { submit(clipboard.op, clipboard.volume, clipboard.dir, clipboard.names, p.volume, p.path); if (clipboard.op === 'move') clipboard = null; } }, { label: 'Select all', action: () => { p.selected = new Set(p.entries.map(e => e.name)); renderPane(i); } }, { label: 'Refresh', action: () => load(i, true) }];
    contextMenu(x, y, items);
  }
  async function addToCollection(i, sel) {
    const p = panes[i];
    let cols; try { cols = (await api('GET', '/collections')).collections; } catch (e) { toast(e.message, 'err'); return; }
    const s = h('select', {}, h('option', { value: '__new', text: '+ New collection…' }), ...cols.map(c => h('option', { value: c.id, text: c.name })));
    if (cols.length) s.value = cols[0].id;
    const id = await dialog({ title: 'Add to collection', body: s, buttons: [{ label: 'Cancel', value: null }, { label: 'Add', class: 'primary', value: () => s.value }] }); if (!id) return;
    let cid = id;
    try {
      if (id === '__new') { const n = await promptDlg('New collection', 'Name', '', v => v ? '' : 'Name is required'); if (!n) return; cid = (await api('POST', '/collections', { name: n })).id; }
      for (const n of sel) await api('POST', '/collections/' + cid + '/items', { volume: p.volume, path: join(p.path, n) });
      toast('Added ' + sel.length + ' item(s) to the collection', 'ok');
    } catch (e) { toast(e.message, 'err'); }
  }
  async function batchRename(i, sel) {
    const p = panes[i];
    const pat = h('input', { type: 'text', value: '{name}{ext}' }); const start = h('input', { type: 'number', value: 1, min: 0, style: 'width:90px' });
    const preview = h('div', { style: 'max-height:30vh; overflow:auto; margin-top:8px; font-size:13px' });
    let plan = [];
    const upd = async () => { try { plan = (await api('POST', '/files/batch_rename/plan', { volume: p.volume, dir: p.path, names: sel, pattern: pat.value, start: +start.value })).plan; preview.replaceChildren(...plan.map(x => h('div', { class: 'row' }, h('span', { class: 'muted', text: x.from }), '→', h('b', { text: x.to }), x.problem ? h('span', { class: 'badge err', text: x.problem }) : null))); } catch (e) { preview.textContent = e.message; } };
    pat.oninput = start.oninput = () => { clearTimeout(pat.t); pat.t = setTimeout(upd, 200); }; upd();
    const ok = await dialog({ title: 'Batch rename ' + sel.length + ' items', body: h('div', {}, h('label', { text: 'Pattern: {name} stem · {ext} extension · {n} / {n2} / {n3} counter · {date}' }), h('div', { class: 'row' }, h('div', { class: 'grow' }, pat), h('span', { class: 'muted', text: 'start' }), start), preview), buttons: [{ label: 'Cancel', value: false }, { label: 'Rename', class: 'primary', value: true }] });
    if (!ok) return;
    try { const r = await api('POST', '/files/batch_rename/apply', { volume: p.volume, dir: p.path, names: sel, pattern: pat.value, start: +start.value }); const failed = r.results.filter(x => x.state === 'failed'); toast(failed.length ? failed.length + ' rename(s) failed: ' + failed[0].error : 'Renamed ' + r.results.filter(x => x.state === 'done').length + ' items', failed.length ? 'err' : 'ok'); load(i); } catch (e) { toast(e.message, 'err'); }
  }

  window.addEventListener('resize', () => { if (root) renderAll(); });
  return { mount, detach, onVolumes, onJobsChanged, refreshAll, openIn };
})();
