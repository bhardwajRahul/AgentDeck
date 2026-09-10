#!/usr/bin/env node

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const markdownFiles = execFileSync('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z', '*.md'], {
  cwd: repoRoot,
  encoding: 'utf8',
})
  .split('\0')
  .filter(Boolean);

const failures = [];
const anchorCache = new Map();

function lineNumber(text, index) {
  return text.slice(0, index).split('\n').length;
}

function stripCode(text) {
  return text
    .replace(/```[\s\S]*?```/g, (block) => '\n'.repeat((block.match(/\n/g) || []).length))
    .replace(/`[^`\n]*`/g, '');
}

function githubSlug(raw) {
  return raw
    .replace(/!?\[([^\]]+)\]\([^)]+\)/g, '$1')
    .replace(/<[^>]+>/g, '')
    .replace(/[`*_~]/g, '')
    .toLowerCase()
    .trim()
    .replace(/[^\p{L}\p{N}\s_-]/gu, '')
    .replace(/\s+/g, '-');
}

function anchorsFor(relativeFile) {
  if (anchorCache.has(relativeFile)) return anchorCache.get(relativeFile);

  const text = stripCode(readFileSync(path.join(repoRoot, relativeFile), 'utf8'));
  const anchors = new Set();
  const duplicateCounts = new Map();

  for (const line of text.split('\n')) {
    const heading = line.match(/^#{1,6}\s+(.+?)\s*#*$/);
    if (!heading) continue;

    const base = githubSlug(heading[1]);
    const duplicateIndex = duplicateCounts.get(base) || 0;
    duplicateCounts.set(base, duplicateIndex + 1);
    anchors.add(duplicateIndex === 0 ? base : `${base}-${duplicateIndex}`);
  }

  for (const match of text.matchAll(/<(?:a|div|span)[^>]+(?:id|name)=["']([^"']+)["']/gi)) {
    anchors.add(match[1]);
  }

  anchorCache.set(relativeFile, anchors);
  return anchors;
}

function decodeDestination(value) {
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}

function checkDestination(sourceFile, sourceText, rawDestination, index) {
  let destination = rawDestination.trim().replace(/^<|>$/g, '');
  if (!destination || /^(?:https?:|mailto:|data:|app:)/i.test(destination)) return;

  const line = lineNumber(sourceText, index);
  if (/^file:/i.test(destination) || /^\/Users\//.test(destination) || /^[A-Za-z]:\\/.test(destination)) {
    failures.push(`${sourceFile}:${line}: machine-local link is not portable: ${destination}`);
    return;
  }

  // A leading slash is a published-site route, not a repository file path.
  if (destination.startsWith('/')) return;

  const hashIndex = destination.indexOf('#');
  const rawPath = hashIndex === -1 ? destination : destination.slice(0, hashIndex);
  const rawAnchor = hashIndex === -1 ? '' : destination.slice(hashIndex + 1);
  const cleanPath = decodeDestination(rawPath.split('?')[0]);
  const target = cleanPath
    ? path.resolve(repoRoot, path.dirname(sourceFile), cleanPath)
    : path.resolve(repoRoot, sourceFile);
  const relativeTarget = path.relative(repoRoot, target);

  if (relativeTarget === '..' || relativeTarget.startsWith(`..${path.sep}`) || path.isAbsolute(relativeTarget)) {
    failures.push(`${sourceFile}:${line}: local target escapes the repository: ${rawPath}`);
    return;
  }

  if (!existsSync(target)) {
    failures.push(`${sourceFile}:${line}: missing local target: ${rawPath}`);
    return;
  }

  if (!rawAnchor || path.extname(target).toLowerCase() !== '.md') return;

  const expectedAnchor = decodeDestination(rawAnchor).toLowerCase();
  if (!anchorsFor(relativeTarget).has(expectedAnchor)) {
    failures.push(`${sourceFile}:${line}: missing Markdown anchor #${rawAnchor} in ${relativeTarget}`);
  }
}

// Development-log entries (docs/devlog/entries/*.md) are authored with links relative
// to the REPOSITORY ROOT, because they are only ever read through the generated
// aggregates (DEVELOPMENT_LOG.md at the root, docs/devlog/YYYY-MM.md two levels down),
// which scripts/devlog-build.mjs rewrites per depth and which this checker validates.
// Validating the same links against the entry's own directory would fail every one.
const ROOT_RELATIVE_LINK_DIRS = ['docs/devlog/entries/'];

for (const relativeFile of markdownFiles) {
  const source = readFileSync(path.join(repoRoot, relativeFile), 'utf8');
  const text = stripCode(source);

  if (relativeFile === 'README.md' || relativeFile.startsWith('docs/')) {
    const h1Count = text.split('\n').filter((line) => /^#\s+/.test(line)).length;
    if (h1Count !== 1) {
      failures.push(`${relativeFile}: expected exactly one H1 outside code blocks; found ${h1Count}`);
    }
  }

  if (ROOT_RELATIVE_LINK_DIRS.some((dir) => relativeFile.startsWith(dir))) continue;

  for (const match of text.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)) {
    checkDestination(relativeFile, text, match[1], match.index);
  }
  for (const match of text.matchAll(/(?:href|src)=["']([^"']+)["']/g)) {
    checkDestination(relativeFile, text, match[1], match.index);
  }
}

if (failures.length > 0) {
  console.error(`Documentation check failed with ${failures.length} issue(s):`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exit(1);
}

console.log(
  `Documentation check passed: ${markdownFiles.length} Markdown files, local targets/anchors, and H1 structure verified.`,
);
