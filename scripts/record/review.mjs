/*
 * Post-record review: report, keyframes and a ready-to-use review prompt.
 *
 * Everything here is optional except the report; when ffmpeg/ffprobe are missing the recording is
 * still kept and the prompt says the frames/duration were skipped.
 */
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

function probe(video) {
  const result = spawnSync(
    'ffprobe',
    ['-v', 'error', '-show_entries', 'stream=width,height', '-show_entries', 'format=duration', '-of', 'json', video],
    { encoding: 'utf8' },
  );
  if (result.status !== 0) return null;
  try {
    const data = JSON.parse(result.stdout);
    const stream = data.streams?.[0] ?? {};
    return {
      width: stream.width,
      height: stream.height,
      duration: Number(data.format?.duration ?? 0),
    };
  } catch {
    return null;
  }
}

function frame(video, atSeconds, output) {
  const result = spawnSync(
    'ffmpeg',
    ['-y', '-loglevel', 'error', '-ss', String(Math.max(0, atSeconds)), '-i', video, '-frames:v', '1', output],
    { encoding: 'utf8' },
  );
  return result.status === 0;
}

function hash(file) {
  try {
    return createHash('sha256').update(readFileSync(file)).digest('hex');
  } catch {
    return '';
  }
}

export function writeReview({ dir, slug, report, tour, stages, timings }) {
  const video = report.video;
  const media = probe(video);
  const framesDir = join(dir, slug + '.frames');
  const frames = [];

  if (media !== null) {
    mkdirSync(framesDir, { recursive: true });
    for (const timing of timings) {
      const at = ((timing.start + timing.end) / 2000).toFixed(2);
      const file = join(framesDir, `${String(timing.step).padStart(2, '0')}-${timing.title.replace(/[^a-z0-9]+/gi, '-')}.png`);
      if (frame(video, Number(at), file)) frames.push(file);
    }
  }

  report.media = media;
  report.frames = frames;
  report.hash = hash(video);

  const reportPath = join(dir, slug + '.report.json');
  writeFileSync(reportPath, JSON.stringify(report, null, 2));

  const lines = [];
  lines.push(`# Review recording: ${report.tour.name}`);
  lines.push('');
  lines.push(`- Tour: \`${report.tour.id}\``);
  lines.push(`- UI: ${report.url}`);
  lines.push(`- Video: ${video}`);
  lines.push(`- Still: ${report.png}`);
  lines.push(`- Viewport: ${report.viewport.width}x${report.viewport.height}`);
  lines.push(`- Duration: ${media ? media.duration.toFixed(2) + ' s' : '(ffprobe missing)'}`);
  lines.push('');
  lines.push('## What to check');
  lines.push('');
  lines.push('1. The tour overlay never appears.');
  lines.push('2. Every tour step is visible, in order.');
  lines.push('3. Dialog footers (Project / Import) stay on screen.');
  lines.push('4. No dead gap longer than about 1 s.');
  lines.push('5. The last frame shows the expected result.');
  lines.push('');
  if (frames.length > 0) {
    lines.push('## Frames');
    lines.push('');
    for (const file of frames) lines.push(`- ${file}`);
    lines.push('');
  }
  lines.push('## Stages executed');
  lines.push('');
  lines.push('| step | what | start (s) | end (s) |');
  lines.push('| --- | --- | --- | --- |');
  for (const timing of timings) {
    lines.push(
      `| ${timing.step} | ${timing.title} | ${(timing.start / 1000).toFixed(2)} | ${(timing.end / 1000).toFixed(2)} |`,
    );
  }
  lines.push('');
  if (report.warnings.length > 0) {
    lines.push('## Warnings');
    lines.push('');
    for (const warning of report.warnings) lines.push(`- ${warning}`);
    lines.push('');
  }
  if (report.failures.length > 0) {
    lines.push('## Failures');
    lines.push('');
    for (const failure of report.failures) lines.push(`- step ${failure.step} (${failure.title}): ${failure.error}`);
    lines.push('');
  }
  lines.push('## Tour');
  lines.push('');
  lines.push('```json');
  lines.push(JSON.stringify(tour, null, 2));
  lines.push('```');
  lines.push('');
  lines.push('## Run it again');
  lines.push('');
  lines.push('```');
  lines.push(`ndmspc-cli record --tours "${report.tour.name}" --out ${dir}`);
  lines.push('```');
  lines.push('');
  lines.push(
    'If a step is wrong, adjust `overrides.json` (or the tour\'s structured fields) and run the command above again.',
  );

  const reviewPath = join(dir, slug + '.review.md');
  writeFileSync(reviewPath, lines.join('\n') + '\n');
  return { review: reviewPath, report: reportPath, frames: frames.length };
}
