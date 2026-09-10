#!/usr/bin/env node
/**
 * devlog-archive — keep DEVELOPMENT_LOG.md to a bounded window of months.
 *
 * The active log is what every agent reads first (`head`) and what every session
 * prepends to, so its size is a per-session cost and its month window is a
 * documented promise ("current month plus the previous one"). The promise used to
 * be kept by hand and drifted: on 2026-09-10 the active log held June–September,
 * 1.34 MB, 407 entries, while the archive index stopped at May.
 *
 *   node scripts/devlog-archive.mjs            # slice months older than the window into docs/devlog/YYYY-MM.md
 *   node scripts/devlog-archive.mjs --check    # exit 1 if the active log holds an out-of-window month (CI)
 *   node scripts/devlog-archive.mjs --dry-run  # print the plan, write nothing
 *   node scripts/devlog-archive.mjs --keep 3   # widen the window (default 2 = current + previous month)
 *
 * Entry grammar: an entry starts at a line matching /^## (\d{4}-\d{2}-\d{2}) /
 * and runs to the next such line. Any other `## ` heading is an error — the
 * slicer refuses rather than guessing which month a heading belongs to.
 * Archived files keep the active log's newest-first order and merge into an
 * existing month file by date (stable), so re-running is idempotent.
 */
import { readFileSync, writeFileSync, existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ACTIVE = path.join(repoRoot, 'DEVELOPMENT_LOG.md');
const ARCHIVE_DIR = path.join(repoRoot, 'docs', 'devlog');
const README = path.join(ARCHIVE_DIR, 'README.md');
const ENTRY_RE = /^## (\d{4})-(\d{2})-(\d{2}) /;

const args = process.argv.slice(2);
const check = args.includes('--check');
const dryRun = args.includes('--dry-run');
const keepIdx = args.indexOf('--keep');
const keepMonths = keepIdx >= 0 ? Number(args[keepIdx + 1]) : 2;
if (!Number.isInteger(keepMonths) || keepMonths < 1) {
  console.error('devlog-archive: --keep must be a positive integer');
  process.exit(2);
}

function monthKey(date) {
  return `${date.getUTCFullYear()}-${String(date.getUTCMonth() + 1).padStart(2, '0')}`;
}
function monthsBack(date, n) {
  return new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth() - n, 1));
}

/** Split a log body into { preamble, entries[] } where each entry is { month, date, text }. */
function parseLog(text, label) {
  const lines = text.split('\n');
  const entries = [];
  let preamble = [];
  let current = null;
  lines.forEach((line, i) => {
    if (line.startsWith('## ')) {
      const m = ENTRY_RE.exec(line);
      if (!m) {
        throw new Error(`${label}:${i + 1}: H2 heading without a YYYY-MM-DD date — refusing to slice: ${line.slice(0, 80)}`);
      }
      if (current) entries.push(current);
      current = { month: `${m[1]}-${m[2]}`, date: `${m[1]}-${m[2]}-${m[3]}`, lines: [line] };
    } else if (current) {
      current.lines.push(line);
    } else {
      preamble.push(line);
    }
  });
  if (current) entries.push(current);
  return { preamble, entries };
}

function entryText(entry) {
  // Normalize trailing whitespace so a re-slice cannot grow the file.
  return entry.lines.join('\n').replace(/\s+$/, '') + '\n';
}

function monthLabel(month) {
  const [y, m] = month.split('-').map(Number);
  return new Date(Date.UTC(y, m - 1, 1)).toLocaleString('en-US', { month: 'long', year: 'numeric', timeZone: 'UTC' });
}

/**
 * Entries are written in the active log at the repo root, so their relative links
 * resolve from there. Inside docs/devlog/ the same link is two directories deeper;
 * rewrite a root-relative target that exists on disk, and leave everything else
 * (URLs, anchors, already-relative paths, targets that never existed) untouched.
 */
function rewriteRootRelativeLinks(text) {
  return text.replace(/\]\(([^)\s]+)((?:\s[^)]*)?)\)/g, (whole, target, title) => {
    if (/^(?:[a-z][a-z0-9+.-]*:|#|\.\.?\/|\/)/i.test(target)) return whole;
    const bare = target.split('#')[0];
    if (!bare) return whole;
    let decoded = bare;
    try { decoded = decodeURIComponent(bare); } catch { /* keep as-is */ }
    if (!existsSync(path.join(repoRoot, decoded))) return whole;
    return `](../../${target}${title})`;
  });
}

function renderMonthFile(month, entries) {
  const header = [
    `# AgentDeck Development Log — ${month}`,
    '',
    `Archived from the active [DEVELOPMENT_LOG.md](../../DEVELOPMENT_LOG.md). Entries are ordered newest date first; search this file directly when investigating ${monthLabel(month)} work.`,
    '',
    '---',
    '',
  ];
  return header.join('\n') + rewriteRootRelativeLinks(entries.map(entryText).join('\n'));
}

function renderReadme(counts) {
  const months = Object.keys(counts).sort().reverse();
  const rows = months.map((m) => `| ${m} | ${counts[m]} | [${m}.md](${m}.md) |`);
  return [
    '# Development Log Archive',
    '',
    'Older `DEVELOPMENT_LOG.md` entries are sliced by month to keep the active log small and targeted lookups cheap. Newest entries stay in the [active log](../../DEVELOPMENT_LOG.md), which holds the current month and the previous one (`pnpm devlog:check` fails CI when an older month is still there; `pnpm devlog:archive` moves it here). Search one month file at a time — never load the whole directory.',
    '',
    '| Month | Entries | File |',
    '|---|---|---|',
    ...rows,
    '',
  ].join('\n');
}

const now = new Date();
const cutoff = monthsBack(now, keepMonths - 1); // first day of the oldest month that stays
const cutoffKey = monthKey(cutoff);

const active = parseLog(readFileSync(ACTIVE, 'utf8'), 'DEVELOPMENT_LOG.md');
const stale = active.entries.filter((e) => e.month < cutoffKey);
const staleMonths = [...new Set(stale.map((e) => e.month))].sort();

if (stale.length === 0) {
  console.log(`devlog-archive: active log holds ${active.entries.length} entries, all within ${cutoffKey}..${monthKey(now)} — nothing to archive.`);
  process.exit(0);
}

const summary = staleMonths
  .map((m) => `${m} (${stale.filter((e) => e.month === m).length} entries)`)
  .join(', ');

if (check) {
  console.error(`devlog-archive: DEVELOPMENT_LOG.md holds ${stale.length} entries older than ${cutoffKey}: ${summary}.`);
  console.error('  Run `pnpm devlog:archive` (or node scripts/devlog-archive.mjs) and commit the result.');
  process.exit(1);
}

console.log(`devlog-archive: archiving ${stale.length} entries — ${summary}`);
if (dryRun) process.exit(0);

// Write month files (merge into an existing file by date, newest first, stable).
for (const month of staleMonths) {
  const file = path.join(ARCHIVE_DIR, `${month}.md`);
  let existing = [];
  if (existsSync(file)) {
    existing = parseLog(readFileSync(file, 'utf8'), `docs/devlog/${month}.md`).entries;
  }
  const incoming = stale.filter((e) => e.month === month);
  const merged = [...incoming, ...existing]
    .map((e, i) => ({ e, i }))
    .sort((a, b) => (a.e.date < b.e.date ? 1 : a.e.date > b.e.date ? -1 : a.i - b.i))
    .map((x) => x.e);
  writeFileSync(file, renderMonthFile(month, merged));
  console.log(`  wrote docs/devlog/${month}.md (${merged.length} entries)`);
}

// Rewrite the active log with only in-window entries, preserving their order.
const kept = active.entries.filter((e) => e.month >= cutoffKey);
const preamble = active.preamble.join('\n').replace(/\s+$/, '');
writeFileSync(ACTIVE, `${preamble}\n\n${kept.map(entryText).join('\n')}`);
console.log(`  DEVELOPMENT_LOG.md now holds ${kept.length} entries (${cutoffKey}..${monthKey(now)})`);

// Regenerate the archive index from the files on disk.
const counts = {};
for (const name of readdirSync(ARCHIVE_DIR)) {
  const m = /^(\d{4}-\d{2})\.md$/.exec(name);
  if (!m) continue;
  counts[m[1]] = parseLog(readFileSync(path.join(ARCHIVE_DIR, name), 'utf8'), name).entries.length;
}
writeFileSync(README, renderReadme(counts));
console.log(`  docs/devlog/README.md indexes ${Object.keys(counts).length} months`);
