const defaults = {pinned: true, compact: true};
function fitBounds(saved, displays, compact = false) {
    const valid = saved && ['x', 'y', 'width', 'height'].every(k => Number.isFinite(saved[k])) && saved.width > 0 && saved.height > 0;
    if (!displays.length) throw Error('No connected displays');
    // Use physical overlap: the Dock reservation is not an off-screen region.
    // Prefer the monitor containing most of the window, not enumeration order.
    const overlap = display => {
        const a = display.bounds || display.workArea;
        return valid ? Math.max(0, Math.min(saved.x + saved.width, a.x + a.width) - Math.max(saved.x, a.x)) *
            Math.max(0, Math.min(saved.y + saved.height, a.y + a.height) - Math.max(saved.y, a.y)) : 0;
    };
    const display = displays.reduce((best, candidate) => overlap(candidate) > overlap(best) ? candidate : best);
    const area = display.workArea;
    const physical = display.bounds || area;
    // Keep the title bar below the menu bar. Compact strips may deliberately
    // overlap bottom/side Dock reservations; workshop windows use the work area.
    const limits = compact ? {x: physical.x, y: area.y, width: physical.width,
        height: physical.y + physical.height - area.y} : area;
    const bottom = limits.y + limits.height;
    const width = Math.min(limits.width, Math.max(compact ? 520 : 840, valid ? saved.width : compact ? 1440 : 1280));
    const height = Math.min(limits.height, Math.max(compact ? 250 : 400, valid ? saved.height : compact ? 270 : 860));
    const x = valid ? saved.x : area.x + (area.width - width) / 2;
    const y = valid ? saved.y : compact ? bottom - height : area.y + (area.height - height) / 2;
    return {x: Math.round(Math.max(limits.x, Math.min(x, limits.x + limits.width - width))), y: Math.round(Math.max(area.y, Math.min(y, bottom - height))), width: Math.round(width), height: Math.round(height)};
}
function normalizeState(value) {
    // Restore the DAW-friendly footprint once; later manual sizes remain untouched.
    let performance = value?.performance;
    if (value?.layoutVersion !== 3) performance = undefined;
    return {layoutVersion: 3,pinned: typeof value?.pinned === 'boolean' ? value.pinned : defaults.pinned, compact: typeof value?.compact === 'boolean' ? value.compact : defaults.compact, workshop: value?.workshop, performance};
}
function isAppURL(value, origin) {
    try { const u = new URL(value); return u.origin === origin && !u.username && !u.password && ['/ui/', '/ui/index.html'].includes(u.pathname); } catch { return false; }
}
module.exports = {fitBounds, normalizeState, isAppURL};

