const defaults = {pinned: true, compact: true};
function fitBounds(saved, displays, compact = false) {
    const valid = saved && ['x', 'y', 'width', 'height'].every(k => Number.isFinite(saved[k]));
    const display = valid && displays.find(({workArea: a}) => saved.x < a.x + a.width && saved.x + saved.width > a.x && saved.y < a.y + a.height && saved.y + saved.height > a.y) || displays[0];
    const area = display.workArea;
    const bottom = compact ? (display.bounds?.y ?? area.y) + (display.bounds?.height ?? area.height) : area.y + area.height;
    const width = Math.min(area.width, Math.max(compact ? 520 : 840, valid ? saved.width : compact ? 1440 : 1280));
    const height = Math.min(area.height, Math.max(compact ? 250 : 400, valid ? saved.height : compact ? 270 : 860));
    const x = valid ? saved.x : area.x + (area.width - width) / 2;
    const y = valid ? saved.y : compact ? bottom - height : area.y + (area.height - height) / 2;
    return {x: Math.round(Math.max(area.x, Math.min(x, area.x + area.width - width))), y: Math.round(Math.max(area.y, Math.min(y, bottom - height))), width: Math.round(width), height: Math.round(height)};
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
