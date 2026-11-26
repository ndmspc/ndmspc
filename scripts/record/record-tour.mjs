#!/usr/bin/env node
/*
 * Record one tool tour with agent-browser.
 *
 * Invoked by `ndmspc-cli record`; can also be run directly:
 *   node record-tour.mjs --url … --tour-json tour.json --tour-id … --tour-name … \
 *        --slug … --out ./videos [--size 1920x1080] [--dry-run]
 *
 * The tour JSON comes from `tools/list`; `stages.mjs` turns it (plus `overrides.json`) into the
 * in-page actions, and `lib/driver.js` supplies the cursor and helpers.
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { buildStages, describeStages, loadOverrides } from './stages.mjs';
import { writeReview } from './review.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const DRIVER = readFileSync(join(HERE, 'lib', 'driver.js'), 'utf8');
const CLEANUP = readFileSync(join(HERE, 'lib', 'cleanup.js'), 'utf8');

function parseArgs(argv) {
  const options = {
    url: 'http://localhost:8080',
    tourJson: '',
    tourId: '',
    tourName: '',
    slug: 'tour',
    out: './ndmspc-recordings',
    size: '1920x1080',
    session: 'ndmspc-record',
    review: true,
    dryRun: false,
    writeStages: '',
    // Pacing. The clip's rhythm is the pauses between actions, not the actions: a click lands in
    // milliseconds, so what a viewer sees is how long the frame rests on each state. `speed` scales
    // all of them at once (2 is twice as fast, 0.5 twice as slow); the two explicit pauses override
    // the scaled defaults for callers who want one part quicker than the rest.
    speed: 1,
    stepPause: null,
    clickPause: null,
    tail: null,
    // How long the clip holds on the first state before the first stage runs. The first action lands
    // in milliseconds, so without this the video opens on an already-changed screen and the viewer
    // never sees what the step started from.
    startDelay: null,
    // Where the synthetic cursor sits before the first move: "center" (the default) or "x,y".
    cursorStart: 'center',
    cursorTravel: null,
    // The cards that open and close the clip. Empty says "use what the tour declares": the title
    // card takes the tour's name and description, and the outro is a fixed thank-you.
    title: null,
    subtitle: null,
    outro: null,
    credit: null,
    cardHold: null,
    noCards: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => {
      const next = argv[++i];
      if (next === undefined) throw new Error(`${arg} needs a value`);
      return next;
    };
    if (arg === '--url') options.url = value();
    else if (arg === '--tour-json') options.tourJson = value();
    else if (arg === '--tour-id') options.tourId = value();
    else if (arg === '--tour-name') options.tourName = value();
    else if (arg === '--slug') options.slug = value();
    else if (arg === '--out') options.out = value();
    else if (arg === '--size') options.size = value();
    else if (arg === '--session') options.session = value();
    else if (arg === '--write-stages') options.writeStages = value();
    else if (arg === '--speed') options.speed = positive(value(), '--speed');
    else if (arg === '--step-pause') options.stepPause = millis(value(), '--step-pause');
    else if (arg === '--click-pause') options.clickPause = millis(value(), '--click-pause');
    else if (arg === '--tail') options.tail = millis(value(), '--tail');
    else if (arg === '--start-delay') options.startDelay = millis(value(), '--start-delay');
    else if (arg === '--cursor-start') options.cursorStart = value();
    else if (arg === '--cursor-travel') options.cursorTravel = millis(value(), '--cursor-travel');
    else if (arg === '--title') options.title = value();
    else if (arg === '--subtitle') options.subtitle = value();
    else if (arg === '--outro') options.outro = value();
    else if (arg === '--credit') options.credit = value();
    else if (arg === '--card-hold') options.cardHold = millis(value(), '--card-hold');
    else if (arg === '--no-cards') options.noCards = true;
    else if (arg === '--no-review') options.review = false;
    else if (arg === '--dry-run') options.dryRun = true;
    else throw new Error(`unknown option: ${arg}`);
  }
  if (!options.tourJson) throw new Error('--tour-json is required');
  return options;
}

/** A positive number from the command line (`--speed 0.5`). */
function positive(text, flag) {
  const value = Number(text);
  if (!Number.isFinite(value) || value <= 0) throw new Error(`${flag} must be a positive number`);
  return value;
}

/** A duration in milliseconds from the command line (`--step-pause 500`). */
function millis(text, flag) {
  const value = Number(text);
  if (!Number.isFinite(value) || value < 0) throw new Error(`${flag} must be a duration in ms`);
  return value;
}

function run(command, args, { input } = {}) {
  const result = spawnSync(command, args, {
    input,
    encoding: 'utf8',
    timeout: 180000,
    maxBuffer: 16 * 1024 * 1024,
  });
  return result;
}

class Browser {
  constructor(session) {
    this.session = session;
  }
  call(args, { input, timeout = 180000 } = {}) {
    const result = spawnSync('agent-browser', ['--session', this.session, ...args], {
      input,
      encoding: 'utf8',
      timeout,
      maxBuffer: 16 * 1024 * 1024,
    });
    if (result.status !== 0) {
      throw new Error(`agent-browser ${args.join(' ')} failed: ${result.stderr || result.stdout}`);
    }
    return (result.stdout || '').trim();
  }
  eval(code) {
    return this.call(['eval', '--stdin'], { input: code });
  }
}

function parseSize(size) {
  const match = /^(\d+)x(\d+)$/.exec(size);
  if (!match) throw new Error(`invalid --size '${size}' (expected WxH, e.g. 1920x1080)`);
  return [Number(match[1]), Number(match[2])];
}

/**
 * A progress bar on stderr — the terminal the recorder inherits — so a long tour shows how far it
 * has got instead of printing nothing until the end. Redrawn on the same line with a carriage
 * return; on a pipe (not a TTY) it prints one line per stage instead, because `\r` would otherwise
 * pile up in a log file.
 */
function progress(total, label) {
  const tty = process.stderr.isTTY === true;
  const width = 28;
  let last = -1;
  return (done, note = '') => {
    const stage = Math.min(done, total);
    if (stage === last) return;
    last = stage;
    const filled = total === 0 ? width : Math.round((stage / total) * width);
    const bar = '#'.repeat(filled) + '-'.repeat(width - filled);
    const pct = String(total === 0 ? 100 : Math.round((stage / total) * 100)).padStart(3);
    const line = `${label} [${bar}] ${pct}% (${stage}/${total})${note === '' ? '' : ' ' + note}`;
    if (tty) {
      process.stderr.write('\r' + line + (stage === total ? '\n' : '\x1b[K'));
    } else {
      process.stderr.write(line + '\n');
    }
  };
}

/**
 * Where the cursor starts, as `{x, y}` or `{}` for the middle of the viewport.
 *
 * `--cursor-start center` (the default) opens the clip with the pointer already in the middle of the
 * screen; `--cursor-start 40,900` puts it in a corner, which is where a person's pointer usually is
 * before they reach for something. An unreadable value is reported rather than silently ignored.
 */
function parseCursorStart(text) {
  const value = String(text ?? '').trim().toLowerCase();
  if (value === '' || value === 'center' || value === 'middle') return {};
  const match = /^(-?\d+)\s*,\s*(-?\d+)$/.exec(value);
  if (match === null) throw new Error(`--cursor-start must be 'center' or 'x,y', not '${text}'`);
  return { x: Number(match[1]), y: Number(match[2]) };
}

/**
 * The markup of a card: a title, a subtitle and a footer, on the room's own background.
 *
 * The card is drawn **in the page**, so it is part of the video the browser records — no second
 * file to stitch on, and no ffmpeg to depend on. It is styled here rather than in the app because it
 * belongs to the recording, not to the tool.
 *
 * @param {{title: string, subtitle: string, footer: string}} lines What to draw; empty lines are left out.
 * @returns {string} The card's inner HTML.
 */
function cardHtml(lines) {
  const escape = (text) =>
    String(text ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  const part = (text, style) => (String(text ?? '') === '' ? '' : `<div style="${style}">${escape(text)}</div>`);
  return (
    '<div style="position:fixed;inset:0;z-index:2147483647;display:flex;flex-direction:column;' +
    'align-items:center;justify-content:center;gap:18px;background:linear-gradient(135deg,#eef4ff,#dbe7fb);' +
    'font-family:Montserrat,system-ui,sans-serif;color:#0f172a;text-align:center;padding:6vw;' +
    'transition:opacity .45s ease;">' +
    part(lines.title, 'font-size:3.4em;font-weight:600;line-height:1.15;') +
    part(lines.subtitle, 'font-size:1.5em;opacity:.72;max-width:26em;line-height:1.4;') +
    part(lines.footer, 'font-size:1.05em;opacity:.55;margin-top:1.2em;letter-spacing:.02em;') +
    '</div>'
  );
}

/**
 * An eval that shows a card for `hold` ms, fades it out, and resolves.
 *
 * @param {{title: string, subtitle: string, footer: string}} lines The card's lines.
 * @param {number} hold Milliseconds to keep it on screen.
 * @returns {string} The stage code the recorder evaluates.
 */
function cardStage(lines, hold, { fadeOut = true } = {}) {
  return `(async () => {
    const N = window.ND;
    const card = document.createElement('div');
    card.innerHTML = ${JSON.stringify(cardHtml(lines))};
    const box = card.firstElementChild;
    document.body.appendChild(box);
    await N.sleep(${hold});
    ${fadeOut
      ? `box.style.opacity = '0';
    await N.sleep(500);
    box.remove();`
      : `// Left on screen: the closing card is the last frame, so it must not fade back to the page.`}
    return 'card-done';
  })()`;
}

/**
 * Whether a card has anything to say: a title is enough, and all three empty means no card at all.
 *
 * @param {{title: string, subtitle: string, footer: string}} lines The card's lines.
 * @returns {boolean} True when at least one line is set.
 */
function hasCard(lines) {
  return [lines.title, lines.subtitle, lines.footer].some((line) => String(line ?? '').trim() !== '');
}

/**
 * Wait for an in-page condition, polling from here rather than from the page.
 *
 * A single `eval` that sleeps is what breaks: agent-browser caps one CDP `Runtime.evaluate` at about
 * ten seconds, so an in-page `waitFor(..., 15000)` is killed before its own timeout and surfaces as
 * "CDP command timed out" — telling nobody what was actually being waited for. Each poll here is a
 * quick eval, and the deadline is kept on this side.
 *
 * @param {Browser} browser The session to poll.
 * @param {string} expression A JS expression that is truthy when the condition holds.
 * @param {number} timeoutMs How long to keep trying.
 * @param {string} what What is being waited for, for the error message.
 * @param {number} pollMs How often to look.
 * @returns {Promise<void>} Resolves when the condition holds; throws with `what` when it does not.
 */
async function waitInPage(browser, expression, timeoutMs, what, pollMs = 250) {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    let ok = false;
    try {
      ok = browser.eval(`(() => !!(${expression}))()`) === 'true';
    } catch {
      ok = false; // A page mid-navigation is not ready: keep waiting until the deadline.
    }
    if (ok) return;
    if (Date.now() >= deadline) throw new Error(`timed out after ${timeoutMs} ms waiting for ${what}`);
    await new Promise((resolve) => setTimeout(resolve, pollMs));
  }
}

function writeStageDump(dir, stages) {
  mkdirSync(dir, { recursive: true });
  stages.forEach((stage, index) => {
    const name = String(index + 1).padStart(2, '0') + '-step-' + stage.step + '.js';
    writeFileSync(join(dir, name), stage.code + '\n');
  });
}

async function clearRoom(url) {
  const parsed = new URL(url);
  const room = parsed.searchParams.get('room');
  if (!room) return 'no-room';
  const token = parsed.searchParams.get('token');
  const body = JSON.stringify({ room, ...(token ? { token } : {}) });
  try {
    const response = await fetch(`${parsed.origin}/api/ndmspc/room/clear`, {
      method: 'DELETE',
      headers: { 'Content-Type': 'application/json' },
      body,
    });
    return response.ok ? 'cleared' : `clear-${response.status}`;
  } catch (error) {
    return `clear-failed: ${error.message}`;
  }
}

function now() {
  return Number(process.hrtime.bigint() / 1000000n);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const tour = JSON.parse(readFileSync(options.tourJson, 'utf8'));
  const overrides = loadOverrides();
  const { stages, warnings } = buildStages(tour, { id: options.tourId, overrides });

  if (stages.length === 0) {
    throw new Error('the tour has no actionable steps (add overrides.json entries)');
  }
  if (options.writeStages) writeStageDump(options.writeStages, stages);

  if (options.dryRun) {
    console.log(`Tour: ${options.tourName} (${options.tourId})`);
    console.log(`Stages: ${stages.length}`);
    console.log(describeStages(stages));
    if (warnings.length > 0) console.log('\nWarnings:\n- ' + warnings.join('\n- '));
    return 0;
  }

  const browser = new Browser(options.session);
  const [width, height] = parseSize(options.size);
  // Absolute, because the paths are handed to **agent-browser**, whose daemon resolves them against
  // its own working directory rather than this one: `--out ./recordings` reached it as a relative path
  // and the screenshot failed with "No such file or directory" even though the directory existed.
  options.out = resolve(options.out);
  mkdirSync(options.out, { recursive: true });

  await clearRoom(options.url);

  const origin = new URL(options.url).origin + '/';
  browser.call(['set', 'viewport', String(width), String(height)]);
  browser.call(['open', origin]);
  browser.eval(
    [
      "localStorage.setItem('ndmspc:tour:1:done','1');",
      "localStorage.setItem('ndmspc:tour:help-tip:done','1');",
      "localStorage.setItem('ndmspc:tour:help-pointer:done','1');",
      "'ok'",
    ].join('\n'),
  );
  browser.call(['open', options.url]);
  // Off camera: the app's consent banner and any tour card that a first visit still raised. Before
  // the driver, because the card would otherwise sit on top of the room while it connects.
  browser.call(['wait', '1200']);
  browser.eval(`(() => {
    const click = (label) => {
      const b = [...document.querySelectorAll('button')].find((x) => x.textContent.trim() === label);
      if (b) b.click();
    };
    click('Decline');
    click('Skip the tour');
    click('Done');
    return 'dismissed';
  })()`);
  // The room view starts **disconnected** (its URL field is editable until the user presses
  // Connect), so the first thing a recording must do is press it — otherwise every stage runs
  // against an empty page: no tools, no pads, nothing to film.
  // The URL field starts on the app's **default** (http://localhost:8080/api), not on the address the
  // page was served from — so pressing Connect without setting it opens whatever else runs on 8080.
  // `--url` is the address to record, so it is typed in first.
  const apiUrl = new URL('/api', options.url).toString();
  browser.eval(`(() => {
    const field = [...document.querySelectorAll('input')].find(
      (x) => x.getAttribute('aria-label') === 'Room API URL');
    if (field) {
      const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
      setter.call(field, ${JSON.stringify(apiUrl)});
      field.dispatchEvent(new Event('input', { bubbles: true }));
    }
    const b = [...document.querySelectorAll('button')].find((x) => x.textContent.trim() === 'Connect');
    if (b) { b.click(); return 'connecting'; }
    return 'already-connected';
  })()`);
  // The driver's own pauses: what the recorder was asked for, scaled by `--speed` when the caller
  // gave no explicit figure. Set before the driver reads it.
  const clickPause = options.clickPause ?? Math.round(700 / options.speed);
  browser.eval(
    `window.__abPace = ${JSON.stringify({
      click: clickPause,
      hover: Math.round(220 / options.speed),
      travel: options.cursorTravel ?? Math.round(450 / options.speed),
      save: Math.round(1200 / options.speed),
      type: Math.round(60 / options.speed),
      cursor: parseCursorStart(options.cursorStart),
    })}; 'pace'`,
  );
  browser.eval(DRIVER);
  // Waited from here, not in the page: an in-page wait of this length is what the CDP cap kills.
  await waitInPage(browser, `window.ND && window.ND.combos().length > 0`, 30000, 'the room to answer');

  if (/import/i.test(options.tourName)) {
    try {
      browser.eval(CLEANUP);
    } catch {
      // A plain server has no Storage; the import itself does not need cleanup there.
    }
  }

  // A tour of one's own names no built-in tool. The two built-in tours are picked by their label
  // ("NGNT explorer"); any other tour is picked by its **group**, the action's own prefix
  // (`myname/gaus`), which is what the room's picker lists. It used to be hard-coded to "Root
  // browser" and then to wait for `#ndmspc-field-file` — the browser's own field — so any other
  // tool's tour waited 30 s for something that would never appear and the eval timed out.
  const builtin = options.tourId.startsWith('ndmspc/ngnt')
    ? 'NGNT explorer'
    : options.tourId.startsWith('ndmspc/browser')
      ? 'Root browser'
      : '';
  const groupName = options.tourId.includes('/')
    ? options.tourId.slice(0, options.tourId.lastIndexOf('/'))
    : '';
  const pick = builtin === '' ? groupName : builtin;
  // Ready means the step's form is on screen — `Save & Run` is on every step, whatever the tool,
  // so this waits for the tour's own tool rather than for the browser's file field.
  // The room lists its tools once, when the client connects — and a **deferred** tool macro is only
  // loaded by the first `tools/list`, which the client has already made by then. So a server whose
  // macro is deferred hands the page an empty list and never refreshes it: the tool is there, the
  // page just does not know. A reload after the macro is loaded makes the two agree, so a short first
  // attempt and one reload is the order, not a rare repair.
  const readyCheck = `window.ND && window.ND.visibleSaves().length > 0`;
  try {
    await waitInPage(browser, readyCheck, 6000, 'the tool\'s form');
  } catch {
    process.stderr.write('no tool form after connecting — reloading once, so the page sees the loaded macro\n');
    browser.call(['open', options.url]);
    browser.call(['wait', '1500']);
    browser.eval(DRIVER);
    await waitInPage(browser, readyCheck, 20000, 'the tool\'s form (after reload)');
  }
  if (pick !== '') {
    // Which tool the room is on, once its list is there: a picker with more than one group.
    try {
      browser.eval(`window.ND.tool(${JSON.stringify(pick)})`);
      await waitInPage(browser, `window.ND.visibleSaves().length > 0`, 6000, `the ${pick} tool`);
    } catch {
      // A single-group room has no picker, and is already on its one tool.
    }
  }
  await new Promise((resolve) => setTimeout(resolve, 500));

  // The clip's pacing, resolved once: `speed` scales the defaults, and an explicit pause wins over
  // the scaled one. A step's own pause is what the viewer sees between two states, so it is the
  // figure worth exposing; there is deliberately no "type slower" — the driver already types.
  const stepPause = options.stepPause ?? Math.round(900 / options.speed);
  const tail = options.tail ?? Math.round(2500 / options.speed);
  const startDelay = options.startDelay ?? Math.round(2500 / options.speed);

  // The cards: a title before the tool is shown, a thank-you after the last stage. Both are drawn in
  // the page, so they are part of the same recording.
  const cardHold = options.cardHold ?? Math.round(3000 / options.speed);
  const intro = {
    title: options.title ?? options.tourName,
    subtitle: options.subtitle ?? tour.description ?? '',
    footer: options.credit ?? 'made with ndmspc',
  };
  const outro = {
    title: options.outro ?? 'Thanks for watching',
    subtitle: '',
    footer: options.credit ?? 'made with ndmspc',
  };
  const cards = options.noCards !== true;

  const video = join(options.out, options.slug + '.webm');
  const png = join(options.out, options.slug + '.png');
  const timings = [];
  const failures = [];

  // Said before the first frame: opening the page, connecting and picking the tool take a moment,
  // and a recorder that says nothing for that long reads as hung.
  process.stderr.write(`recording ${options.tourName}: opening ${options.url}, ${stages.length} stage(s)\n`);
  // A recording left running by an earlier attempt makes this one fail outright ("Recording already
  // active") — agent-browser keeps it per session, and a session reused after a crash still holds it.
  // Stopping first is harmless when nothing is recording, and the difference between "the last run
  // died" and "this run is broken" otherwise reads exactly the same.
  try {
    browser.call(['record', 'stop'], { timeout: 60000 });
    process.stderr.write('stopped a recording left over from an earlier run in this session\n');
  } catch {
    // Nothing was recording: the usual case.
  }
  // The bar counts everything that holds the recording: the opening card, the tour's stages, and the
  // closing card. A bar that only counts stages sits at 0% through the whole title card, which reads
  // as "nothing is happening" exactly when the recorder is busiest.
  const withIntro = cards && hasCard(intro);
  const withOutro = cards && hasCard(outro);
  const total = stages.length + (withIntro ? 1 : 0) + (withOutro ? 1 : 0);
  const tick = progress(total, options.tourName);
  let done = 0;
  tick(done, withIntro ? 'title card' : '');

  browser.call(['record', 'start', video]);
  // The opening card is laid **over** the room, which is already prepared underneath: it holds, then
  // fades away to reveal the tool. Nothing is reloaded, so the tour starts exactly where it was set up.
  if (withIntro) {
    browser.eval(cardStage(intro, cardHold));
    tick(++done, 'title card');
  }
  const started = now();
  // Hold on the opening state: the tool's own form, before anything has been run.
  if (startDelay > 0) await new Promise((resolve) => setTimeout(resolve, startDelay));
  for (const stage of stages) {
    const at = now() - started;
    try {
      browser.eval(stage.code);
      timings.push({ step: stage.step, title: stage.title, start: at, end: now() - started });
    } catch (error) {
      failures.push({ step: stage.step, title: stage.title, error: String(error.message || error) });
    }
    await new Promise((resolve) => setTimeout(resolve, stepPause));
    tick(++done, stage.title === undefined ? '' : `step ${stage.step} ${stage.title}`);
  }
  await new Promise((resolve) => setTimeout(resolve, tail));
  // The still is the result, taken **before** the closing card rather than after it: a thumbnail of
  // a thank-you slide says nothing about what the tour does.
  browser.call(['screenshot', png]);
  // Closing card: the last thing on the clip. It does **not** fade out — fading would hand the last
  // frames back to the page, which is exactly the "webpage after the thanks" it is there to replace.
  if (withOutro) {
    // Announced before it holds rather than after: the card is on screen the moment the eval starts,
    // and the bar should say what is being waited for while it waits.
    tick(++done, 'closing card');
    browser.eval(cardStage(outro, cardHold, { fadeOut: false }));
  }
  browser.call(['record', 'stop'], { timeout: 240000 });

  const report = {
    tour: { id: options.tourId, name: options.tourName },
    url: options.url,
    viewport: { width, height },
    video,
    png,
    stages,
    timings,
    warnings,
    failures,
    startedAt: new Date().toISOString(),
  };

  if (options.review) {
    const review = writeReview({ dir: options.out, slug: options.slug, report, tour, stages, timings });
    console.log(`Recorded ${video}`);
    console.log(`Review:   ${review.review}`);
    console.log(`Report:   ${review.report}`);
    if (review.frames > 0) console.log(`Frames:   ${review.frames}`);
  } else {
    writeFileSync(join(options.out, options.slug + '.report.json'), JSON.stringify(report, null, 2));
    console.log(`Recorded ${video}`);
  }

  if (failures.length > 0) {
    console.error(`${failures.length} stage(s) failed:`);
    for (const failure of failures) console.error(`  step ${failure.step} (${failure.title}): ${failure.error}`);
    return 4;
  }
  return 0;
}

main().then(
  (code) => process.exit(code),
  (error) => {
    console.error(`record-tour: ${error.message || error}`);
    process.exit(4);
  },
);
