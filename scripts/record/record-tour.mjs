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
import { dirname, join } from 'node:path';
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
    else if (arg === '--no-review') options.review = false;
    else if (arg === '--dry-run') options.dryRun = true;
    else throw new Error(`unknown option: ${arg}`);
  }
  if (!options.tourJson) throw new Error('--tour-json is required');
  return options;
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
  browser.call(['wait', '2500']);
  browser.eval(DRIVER);
  // Off camera: the app's consent banner and any tour card that a first visit still raised.
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

  if (/import/i.test(options.tourName)) {
    try {
      browser.eval(CLEANUP);
    } catch {
      // A plain server has no Storage; the import itself does not need cleanup there.
    }
  }

  const toolName = options.tourId.startsWith('ndmspc/ngnt') ? 'NGNT explorer' : 'Root browser';
  browser.eval(`(async () => {
    const N = window.ND;
    await N.waitFor(() => N.combos().length > 1, 15000);
    await N.tool(${JSON.stringify(toolName)});
    await N.waitFor(() => document.querySelector('#ndmspc-field-file') !== null, 15000);
    return 'ready';
  })()`);
  await new Promise((resolve) => setTimeout(resolve, 500));

  const video = join(options.out, options.slug + '.webm');
  const png = join(options.out, options.slug + '.png');
  const timings = [];
  const failures = [];

  browser.call(['record', 'start', video]);
  const started = now();
  for (const stage of stages) {
    const at = now() - started;
    try {
      browser.eval(stage.code);
      timings.push({ step: stage.step, title: stage.title, start: at, end: now() - started });
    } catch (error) {
      failures.push({ step: stage.step, title: stage.title, error: String(error.message || error) });
    }
    await new Promise((resolve) => setTimeout(resolve, 350));
  }
  await new Promise((resolve) => setTimeout(resolve, 1200));
  browser.call(['screenshot', png]);
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
