import {setupKeyboard} from '../ui/keyboard.mjs';
import {manualSections, renderSection} from '../ui/manual.mjs';

const root = document.querySelector('#keyboard-guide');
root.querySelector('#keyboard-live').checked = false;
root.querySelector('.keyboard-caption').textContent = 'Select a key to explore its gesture. Use arrow keys to move around the map. F1–F12 identify physical positions, including the Mac media row.';
const paletteReference = document.querySelector('#reference-palette');
const keyboard = setupKeyboard({getReferencePalette:()=>({chordSupported:true,mode:paletteReference.value==='notes'?'palette':'chords',editDigit:paletteReference.value==='quality'?1:null})});
const search = document.querySelector('#guide-search');
const nav = document.querySelector('#guide-sections');
const content = document.querySelector('#guide-content');
let sections = [], applying = false;
const slug = title => title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
function route() {
  const [view, query = ''] = location.hash.slice(1).split('?');
  return {view: view === 'guide' ? 'guide' : 'keyboard', params: new URLSearchParams(query)};
}
function renderGuide() {
  const {params} = route();
  const query = search.value.trim().toLowerCase();
  const matches = sections.filter(s => `${s.title}\n${s.lines.join('\n')}`.toLowerCase().includes(query));
  const selected = matches.find(s => s.id === params.get('section')) || matches[0];
  nav.replaceChildren(...matches.map(section => {
    const a = document.createElement('a');
    a.textContent = section.title;
    const params = new URLSearchParams({section: section.id});
    if (query) params.set('q', search.value);
    a.href = `#guide?${params}`;
    a.setAttribute('aria-current', section === selected ? 'page' : 'false');
    a.addEventListener('click', () => requestAnimationFrame(() => {content.focus();}));
    return a;
  }));
  document.querySelector('#guide-count').textContent = `${matches.length} ${query ? 'matching ' : ''}sections`;
  content.replaceChildren(selected ? renderSection(selected) : document.createTextNode('No matching sections. Try “recording”, “chord”, or a key name.'));
}
function applyRoute() {
  applying = true;
  const {view, params} = route();
  document.querySelector('#map-view').hidden = view !== 'keyboard';
  document.querySelector('#written-view').hidden = view !== 'guide';
  document.querySelector('#map-tab').setAttribute('aria-current', view === 'keyboard' ? 'page' : 'false');
  document.querySelector('#guide-tab').setAttribute('aria-current', view === 'guide' ? 'page' : 'false');
  if (view === 'keyboard') {
    paletteReference.value = ['notes','chords','quality'].includes(params.get('palette')) ? params.get('palette') : 'notes';
    const os = root.querySelector('#keyboard-os');
    os.value = params.get('os') === 'win' ? 'win' : 'mac';
    root.querySelector('#keyboard-grid').checked = params.get('grid') === '4';
    os.dispatchEvent(new Event('change'));
    const mode = [...root.querySelectorAll('[data-mode]')].find(b => b.dataset.mode === params.get('mode')) || root.querySelector('[data-mode="drum"]');
    mode.click();
    const key = [...root.querySelectorAll('[data-key]')].find(b => b.dataset.key === params.get('key')) || root.querySelector('[data-key="F1"]');
    key.click();
  } else {
    search.value = params.get('q') || '';
    if (sections.length) renderGuide();
  }
  applying = false;
}
function saveKeyboard() {
  if (applying) return;
  const params = new URLSearchParams({mode: root.querySelector('[data-mode][aria-pressed="true"]').dataset.mode, key: root.querySelector('[data-key][aria-pressed="true"]').dataset.key, os: root.querySelector('#keyboard-os').value});
  if (root.querySelector('#keyboard-grid').checked) params.set('grid', '4');
  params.set('palette', paletteReference.value);
  history.replaceState(null, '', `#keyboard?${params}`);
}
paletteReference.addEventListener('change', () => {root.querySelector('[data-mode="euclid"]').click();keyboard.refreshReference();saveKeyboard();});
root.addEventListener('click', saveKeyboard);
root.addEventListener('change', saveKeyboard);
root.addEventListener('keydown', event => {if (['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(event.key)) saveKeyboard();});
search.addEventListener('input', () => {
  const {params} = route();
  if (search.value) params.set('q', search.value); else params.delete('q');
  history.replaceState(null, '', `#guide?${params}`);
  renderGuide();
});
document.querySelector('#copy-link').addEventListener('click', async () => {
  const status = document.querySelector('#copy-status');
  try { await navigator.clipboard.writeText(location.href); status.textContent = 'Link copied'; }
  catch { status.textContent = 'Copy the URL from your address bar.'; }
});
window.addEventListener('hashchange', applyRoute);
applyRoute();
async function loadGuide() {
  try {
    const response = await fetch('../MANUAL.md');
    if (!response.ok) throw Error('Manual unavailable');
    sections = manualSections(await response.text()).map((section, i, all) => {
      const base = slug(section.title) || 'section';
      const count = all.slice(0, i).filter(s => slug(s.title) === slug(section.title)).length;
      return {...section, id: count ? `${base}-${count + 1}` : base};
    });
    if (route().view === 'guide') renderGuide();
  } catch {
    const message = document.createElement('p'); message.textContent = 'The written guide could not load. You can retry or read the Markdown source below.';
    const retry = document.createElement('button'); retry.textContent = 'Retry'; retry.onclick = loadGuide;
    content.replaceChildren(message, retry);
  }
}
loadGuide();
