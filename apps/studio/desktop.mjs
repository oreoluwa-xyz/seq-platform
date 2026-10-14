const desktop = window.seqDesktop;
if (desktop) {
    document.body.classList.add('desktop-app');
    const bar = document.createElement('div');
    bar.className = 'desktop-toolbar';
    bar.setAttribute('aria-label', 'Desktop window controls');
    const label = document.createElement('span'); label.textContent = 'SEQ / DESKTOP';
    const pin = document.createElement('button'); pin.id = 'desktop-pin'; pin.title = 'Keep SEQ above your DAW · ⌘/Ctrl + Shift + P';
    const compact = document.createElement('button'); compact.id = 'desktop-compact'; compact.title = 'Switch between the workshop and a compact live view · ⌘/Ctrl + Shift + M';
    const hide = document.createElement('button'); hide.textContent = 'Hide'; hide.id = 'desktop-hide';
    const moved = ['connection','manual-open','connect','live-temp'].map(id=>{
        const node=document.getElementById(id),anchor=document.createComment('desktop '+id);
        node.before(anchor);return {node,anchor};
    });
    const apply = state => {
        for(const {node,anchor} of moved) {
            if(state.compact) bar.append(node); else anchor.after(node);
        }
        hide.title = state.shortcutAvailable ? 'Hide / show SEQ · Ctrl + Backslash (\\) · works in sequencer mode with updated firmware' : 'Hide SEQ · Ctrl + Backslash unavailable; reopen from the Dock';
        label.textContent=state.compact?'seq':'SEQ / DESKTOP';
        document.querySelector('#live-content').append(document.querySelector('#cc-dock'));
        document.querySelector('#manual-open').title='Keyboard guide and manual · follows the hardware mode';
        document.body.classList.toggle('desktop-performance', state.compact);
        pin.textContent = state.pinned ? 'Pinned' : 'Pin'; pin.setAttribute('aria-pressed', String(state.pinned));
        compact.textContent = state.compact ? 'Workshop ↗' : 'Performance view'; compact.setAttribute('aria-pressed', String(state.compact));
    };
    const showError = error => { label.textContent = error.message; };
    pin.onclick = () => desktop.setPinned(pin.getAttribute('aria-pressed') !== 'true').then(apply).catch(showError);
    compact.onclick = () => desktop.setCompact(compact.getAttribute('aria-pressed') !== 'true').then(apply).catch(showError);
    hide.onclick = () => desktop.hide().catch(showError);
    bar.append(label, pin, compact, hide); document.body.prepend(bar);
    desktop.onState(apply);
    desktop.getState().then(apply).catch(showError);
}
