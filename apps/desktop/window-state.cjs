const defaults = {pinned: true, compact: false};
function fitBounds(saved, displays, compact = false) {
    const fallback = displays[0].workArea;
    const valid = saved && ['x', 'y', 'width', 'height'].every(k => Number.isFinite(saved[k]));
    const area = valid && displays.find(({workArea: a}) => saved.x < a.x + a.width && saved.x + saved.width > a.x && saved.y < a.y + a.height && saved.y + saved.height > a.y)?.workArea || fallback;
    const width = Math.min(area.width, Math.max(compact ? 520 : 840, valid ? saved.width : compact ? 780 : 1280));
    const height = Math.min(area.height, Math.max(400, valid ? saved.height : compact ? 720 : 860));
    const x = valid ? saved.x : area.x + (area.width - width) / 2;
    const y = valid ? saved.y : area.y + (area.height - height) / 2;
    return {x: Math.round(Math.max(area.x, Math.min(x, area.x + area.width - width))), y: Math.round(Math.max(area.y, Math.min(y, area.y + area.height - height))), width: Math.round(width), height: Math.round(height)};
}
function normalizeState(value) {
    return {pinned: typeof value?.pinned === 'boolean' ? value.pinned : defaults.pinned, compact: typeof value?.compact === 'boolean' ? value.compact : defaults.compact, workshop: value?.workshop, performance: value?.performance};
}
function isAppURL(value, origin) {
    try { const u = new URL(value); return u.origin === origin && !u.username && !u.password && ['/ui/', '/ui/index.html'].includes(u.pathname); } catch { return false; }
}
module.exports = {fitBounds, normalizeState, isAppURL};
