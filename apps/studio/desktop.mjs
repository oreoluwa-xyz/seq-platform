const desktop = window.seqDesktop;
if (desktop) {
    document.body.classList.add('desktop-app');
    const bar = document.createElement('div');
    bar.className = 'desktop-toolbar';
    bar.setAttribute('aria-label', 'Desktop window controls');
    const label = document.createElement('span'); label.textContent = 'SEQ / DESKTOP';
    const pin = document.createElement('button'); pin.id = 'desktop-pin'; pin.title = 'Keep SEQ above your DAW · ⌘/Ctrl + Shift + P';
    const compact = document.createElement('button'); compact.id = 'desktop-compact'; compact.title = 'Switch between the workshop and a compact live view · ⌘/Ctrl + Shift + M';
    const apply = state => {
        document.body.classList.toggle('desktop-performance', state.compact);
        pin.textContent = state.pinned ? 'Pinned on top' : 'Pin on top'; pin.setAttribute('aria-pressed', String(state.pinned));
        compact.textContent = state.compact ? 'Open workshop ↗' : 'Performance view'; compact.setAttribute('aria-pressed', String(state.compact));
    };
    const showError = error => { label.textContent = error.message; };
    pin.onclick = () => desktop.setPinned(pin.getAttribute('aria-pressed') !== 'true').then(apply).catch(showError);
    compact.onclick = () => desktop.setCompact(compact.getAttribute('aria-pressed') !== 'true').then(apply).catch(showError);
    bar.append(label, pin, compact); document.body.prepend(bar);
    desktop.onState(apply);
    desktop.getState().then(apply).catch(showError);
}
