const initial = [[0, 4, 8, 10], [4, 12], [0, 2, 4, 6, 8, 10, 12, 14], [0, 3, 7, 10, 14]];
const names = ['Kick', 'Snare', 'Hat', 'Tone'];
const pattern = document.querySelector('#pattern');
const play = document.querySelector('#play');
const status = document.querySelector('#demo-status');
let context, timer, position = 0;
const buttons = [];
for (let lane = 0; lane < 4; lane++) {
  for (let step = 0; step < 16; step++) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'step';
    button.dataset.lane = lane;
    button.setAttribute('aria-label', `${names[lane]}, step ${step + 1}`);
    button.setAttribute('aria-pressed', String(initial[lane].includes(step)));
    button.addEventListener('click', () => button.setAttribute('aria-pressed', String(button.getAttribute('aria-pressed') !== 'true')));
    buttons.push(button);
    pattern.append(button);
  }
}
function sound(lane, step) {
  const now = context.currentTime;
  const gain = context.createGain();
  gain.connect(context.destination);
  const duration = lane === 3 ? .22 : lane === 0 ? .18 : .07;
  gain.gain.setValueAtTime(lane === 2 ? .035 : .12, now);
  gain.gain.exponentialRampToValueAtTime(.001, now + duration);
  let source;
  if (lane === 1 || lane === 2) {
    const buffer = context.createBuffer(1, Math.ceil(context.sampleRate * duration), context.sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    source = context.createBufferSource();
    source.buffer = buffer;
    const filter = context.createBiquadFilter();
    filter.type = 'highpass'; filter.frequency.value = lane === 2 ? 7000 : 1200;
    source.connect(filter); filter.connect(gain);
  } else {
    source = context.createOscillator();
    source.type = 'sine';
    source.frequency.setValueAtTime(lane === 0 ? 140 : [220, 261.63, 329.63, 392][step % 4], now);
    if (lane === 0) source.frequency.exponentialRampToValueAtTime(45, now + duration);
    source.connect(gain);
  }
  source.onended = () => { source.disconnect(); gain.disconnect(); };
  source.start(now); source.stop(now + duration);
}
function tick() {
  buttons.forEach((button, index) => {
    const current = index % 16 === position;
    button.classList.toggle('current', current);
    if (current && button.getAttribute('aria-pressed') === 'true') sound(Math.floor(index / 16), position);
  });
  position = (position + 1) % 16;
}
function stop() {
  clearInterval(timer); timer = null; position = 0;
  play.setAttribute('aria-pressed', 'false');
  play.innerHTML = '<span aria-hidden="true">▶</span> Play pattern';
  buttons.forEach(button => button.classList.remove('current'));
}
play.addEventListener('click', async () => {
  if (timer) { stop(); return; }
  play.disabled = true;
  try {
    context ??= new (window.AudioContext || window.webkitAudioContext)();
    await context.resume();
    if (document.hidden) return;
    play.setAttribute('aria-pressed', 'true');
    play.innerHTML = '<span aria-hidden="true">■</span> Stop pattern';
    tick(); timer = setInterval(tick, 125);
  } catch {
    status.textContent = 'Audio unavailable in this browser. You can still edit the steps.';
  } finally { play.disabled = false; }
});
document.querySelector('#reset').addEventListener('click', () => {
  stop();
  buttons.forEach((button, index) => button.setAttribute('aria-pressed', String(initial[Math.floor(index / 16)].includes(index % 16))));
});
document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
window.addEventListener('pagehide', stop);
