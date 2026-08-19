#!/usr/bin/env node
//
// baseline.mjs — runs every check and snapshots current violations to
// doc/lint/baseline.json (Style Guide Part F.0, "no new violations" while the
// backlog is worked down). Node built-ins only, no dependencies.
//
// Each check contributes a `count` and a `fingerprints` array (stable
// per-finding strings) so a later comparator (check-no-new-violations.mjs)
// can diff a fresh run against this snapshot and flag genuinely new
// findings, independent of how many pre-existing ones remain. Fingerprints
// deliberately carry no line number — see occurrenceKey() below.
//
// Usage: node doc/lint/baseline.mjs [--skip-a11y] [outFile]
//   outFile defaults to doc/lint/baseline.json; check-no-new-violations.mjs
//   passes a temp path so a comparison run doesn't clobber the committed one.
//
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const DOC_DIR = path.resolve(SCRIPT_DIR, '..');
const REPO_ROOT = path.resolve(DOC_DIR, '..');
const cliArgs = process.argv.slice(2);
const skipA11y = cliArgs.includes('--skip-a11y');
const outArg = cliArgs.find((a) => !a.startsWith('--'));

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...opts });
  return r;
}

// Fingerprints must survive line shifts. Keying on the reported line number made
// every finding BELOW an insertion point look new: commit df68f9bc, a comment-only
// docstring addition, renamed 27 grandfathered MrDocs findings and red-lined the
// blocking MrDocs-no-warnings gate without introducing a single warning. So the
// line number is replaced by a per-group occurrence index: the Nth finding sharing
// the same (head, tail) pair is keyed `#N`. That keeps multiplicity — the same
// warning appearing one MORE time in the same file is still new — while making the
// key independent of where in the file it appears.
//
// The index goes exactly where the line number was, mid-key. Do not move it to the
// tail: .github/workflows/docs.yml gates on `doc_lint:^(A1|A6|B2|D2):` (head-anchored)
// and `vale_adoc:Capy\.PartHeadings$` (TAIL-anchored), and the regexes are tested
// against the whole fingerprint, so a trailing index would make the PartHeadings
// gate match nothing and fail open.
//
// The counter is per-base-key, never a raw iteration counter, so the resulting key
// multiset is {base:#1 .. base:#k} whatever order the findings arrive in.
function occurrenceKey(seen, head, tail) {
  const base = `${head}\u0000${tail}`; // NUL separator: neither part can contain it
  const n = (seen.get(base) ?? 0) + 1;
  seen.set(base, n);
  return `${head}:#${n}:${tail}`;
}

// Vale is spawned below with cwd=DOC_DIR, so the keys of its JSON output are
// paths relative to DOC_DIR (or absolute). `path.relative(DOC_DIR, file)`
// resolved a *relative* `file` against process.cwd(), NOT against DOC_DIR — so
// regenerating from anywhere other than doc/ prefixed every Vale path with
// `../` and silently renamed all ~3900 Vale fingerprints at once, retiring the
// entire grandfathered Vale backlog and re-minting it under new keys. Resolve
// against DOC_DIR explicitly so the key depends only on the file, never on
// where the generator happened to be invoked from. The separator normalisation
// is a no-op on POSIX (path.sep === '/') and keeps a Windows run from minting a
// parallel backslash-keyed key set.
function valeRelPath(file) {
  return path.relative(DOC_DIR, path.resolve(DOC_DIR, file)).split(path.sep).join('/');
}

function valeFingerprints(target) {
  const r = run('vale', ['--output=JSON', target], { cwd: DOC_DIR });
  if (r.error) {
    return { count: 0, skipped: true, reason: `vale failed to launch: ${r.error.message}`, fingerprints: [] };
  }
  // Vale's own exit codes: 0 = no alerts at MinAlertLevel, 1 = alerts found (the normal,
  // expected case — NOT a failure), 2 = fatal runtime error (e.g. `asciidoctor` off PATH,
  // a broken `vale sync`). On a fatal error Vale writes a single JSON error object to
  // stderr and leaves stdout empty, so a plain JSON.parse(stdout || '{}') silently yields
  // `{}` — indistinguishable from "ran clean, found nothing." Detect that explicitly
  // instead of ever reporting a broken Vale run as `count: 0`.
  let parsed = null;
  try { parsed = r.stdout ? JSON.parse(r.stdout) : null; } catch { parsed = null; }
  const looksLikeFileMap = parsed !== null && typeof parsed === 'object' && !Array.isArray(parsed);
  if (r.status === 2 || !looksLikeFileMap) {
    const tail = (r.stderr || r.stdout || '(no output)').trim().slice(-500);
    return { count: 0, skipped: true, reason: `vale on '${target}' did not produce findings (exit ${r.status}): ${tail}`, fingerprints: [] };
  }
  const fingerprints = [];
  const seen = new Map();
  for (const [file, alerts] of Object.entries(parsed)) {
    for (const a of alerts) fingerprints.push(occurrenceKey(seen, valeRelPath(file), a.Check));
  }
  return { count: fingerprints.length, fingerprints: fingerprints.sort() };
}

// A crashed check must report as SKIPPED, never as zero findings. doc-lint.mjs and
// mrdocs-warnings.mjs have no error path for an *uncaught throw*: they die with a
// non-zero status and an empty stdout. Defaulting that to an empty findings object
// yielded `count: 0, skipped: false` — a crashed check indistinguishable from a
// clean one. Both are GATED, and the comparator only treats a `skipped` check as
// unverifiable, so the zero sailed through as "backlog empty": the reseed reporter
// called it "0 added, none gated" and the merge gate called it "0 new". That is the
// fail-open shape this toolchain exists to prevent, so the exit status is now
// checked before the output is believed. (mrdocs-warnings.mjs's *designed*
// failures — no binary, version-pin miss, MrDocs itself failing — already emit
// `{error}` on exit 0 and are handled below; this only covers crashes.)
function crashed(r, script) {
  if (!r.error && r.status === 0) return null;
  const tail = (r.stderr || r.error?.message || r.stdout || '(no output)').trim().slice(-500);
  return { count: 0, skipped: true, reason: `${script} failed (exit ${r.status}): ${tail}`, fingerprints: [] };
}

function docLintFingerprints() {
  const r = run('node', [path.join(SCRIPT_DIR, 'doc-lint.mjs')]);
  const bad = crashed(r, 'doc-lint.mjs');
  if (bad) return bad;
  let parsed;
  try {
    parsed = JSON.parse(r.stdout || '');
  } catch {
    return {
      count: 0, skipped: true, fingerprints: [],
      reason: `doc-lint.mjs produced unparseable output: ${(r.stdout || '(empty)').trim().slice(-500)}`,
    };
  }
  const fingerprints = [];
  const seen = new Map();
  for (const [check, items] of Object.entries(parsed.findings || {})) {
    // SHAPE is advisory-only and never gated (see doc-lint.mjs's header
    // comment); folding it into doc_lint's fingerprint set let a single
    // advisory SHAPE finding keep `currentSet.length` non-zero in
    // check-no-new-violations.mjs even when A1/A6/B2/D2 — the checks the
    // gate spec `doc_lint:^(A1|A6|B2|D2):` actually cares about — report
    // zero, silently disarming the "gated check reports 0 against a
    // non-empty baseline" backstop (check-no-new-violations.mjs:187).
    if (check === 'SHAPE') continue;
    for (const it of items) fingerprints.push(occurrenceKey(seen, `${check}:${it.file}`, it.message));
  }
  return { count: fingerprints.length, byRule: parsed.summary, fingerprints: fingerprints.sort() };
}

// C2 (sentence length) is checked by our own script, not by Vale — see the
// header of sentence-length.mjs and the comment in Capy/SentenceLength.yml for
// why. The finding shape is doc-lint.mjs's, so the fingerprint is the doc_lint
// shape (`rule:file:#N:message`, rule at the HEAD) and a future gate spec reads
// `--gate 'sentence_length:^C2:'`. Note this check has no entry in the
// committed baseline.json yet, so every finding reports as NEW until the
// maintainer reseeds; it is deliberately NOT in the gate spec, so that cannot
// block a merge.
function sentenceLengthFingerprints() {
  const r = run('node', [path.join(SCRIPT_DIR, 'sentence-length.mjs')], { cwd: DOC_DIR });
  const bad = crashed(r, 'sentence-length.mjs');
  if (bad) return bad;
  let parsed;
  try {
    parsed = JSON.parse(r.stdout || '');
  } catch {
    return {
      count: 0, skipped: true, fingerprints: [],
      reason: `sentence-length.mjs produced unparseable output: ${(r.stdout || '(empty)').trim().slice(-500)}`,
    };
  }
  const fingerprints = [];
  const seen = new Map();
  for (const [check, items] of Object.entries(parsed.findings || {})) {
    for (const it of items) fingerprints.push(occurrenceKey(seen, `${check}:${it.file}`, it.message));
  }
  return { count: fingerprints.length, byRule: parsed.summary, fingerprints: fingerprints.sort() };
}

function mrdocsFingerprints() {
  const r = run('node', [path.join(SCRIPT_DIR, 'mrdocs-warnings.mjs')]);
  const bad = crashed(r, 'mrdocs-warnings.mjs');
  if (bad) return bad;
  let parsed = {};
  try { parsed = JSON.parse(r.stdout || '{}'); } catch { /* fall through */ }
  if (parsed.error) return { count: 0, skipped: true, reason: parsed.error, fingerprints: [] };
  const seen = new Map();
  const fingerprints = (parsed.findings || []).map((f) => occurrenceKey(seen, f.file ?? '?', f.message));
  return { count: fingerprints.length, fingerprints: fingerprints.sort() };
}

function a11yFingerprints() {
  const r = run('node', [path.join(SCRIPT_DIR, 'run-a11y.mjs')]);
  let parsed = {};
  try { parsed = JSON.parse(r.stdout || '{}'); } catch { /* fall through */ }
  if (parsed.error) return { count: 0, skipped: true, reason: parsed.error, fingerprints: [] };
  const fingerprints = (parsed.findings || []).map((f) => `${f.url}:${f.code}:${f.selector}`);
  return {
    count: fingerprints.length,
    contrastCount: parsed.summary?.contrast ?? fingerprints.filter((f) => f.includes(':color-contrast:')).length,
    fingerprints: fingerprints.sort(),
  };
}

const results = {};
results.vale_adoc = valeFingerprints('modules');

// The docstring corpus is GENERATED, so the generator's exit status is part of
// the measurement. It used to be discarded: extract-docstrings.mjs never clears
// OUT_DIR, so a crash left whatever the last successful run wrote — a stale
// corpus that Vale lints happily and reports `skipped: false` over. Worse, if
// the crash happened before anything was ever written (fresh clone, renamed
// path), Vale over an absent directory exits 0 with `{}`, which valeFingerprints
// reads as `count: 0, skipped: false` — a vacuous clean for the C4/C9/C10
// docstring gates and for sentence_length's docstring half. Both dependent
// checks are therefore marked SKIPPED, which the comparator treats as a gate
// failure, instead of being believed.
const extract = run('node', [path.join(SCRIPT_DIR, 'extract-docstrings.mjs')]);
const extractBad = crashed(extract, 'extract-docstrings.mjs');
if (extractBad) {
  const reason = `docstring corpus not regenerated: ${extractBad.reason}`;
  results.vale_docstrings = { count: 0, skipped: true, reason, fingerprints: [] };
  results.sentence_length = { count: 0, skipped: true, reason, fingerprints: [] };
} else {
  results.vale_docstrings = valeFingerprints('lint/.docstrings');

  // After extract-docstrings.mjs above: sentence-length.mjs lints BOTH corpora and
  // exits non-zero rather than reporting a clean zero for one it could not read.
  results.sentence_length = sentenceLengthFingerprints();
}

results.doc_lint = docLintFingerprints();
results.mrdocs_warnings = mrdocsFingerprints();
results.a11y = skipA11y ? { count: 0, skipped: true, reason: '--skip-a11y' } : a11yFingerprints();

const baseline = {
  generatedAt: new Date().toISOString(),
  note: 'Snapshot of current violations (Task 2, Style Guide Part F.0). Non-blocking: ' +
        'this records the backlog so a future comparator can flag NEW findings without ' +
        'failing on the ones already known about. Fingerprints are line-insensitive: ' +
        'the `#N` component is the Nth occurrence of that (file, message) pair, NOT a ' +
        'line number, so inserting text above a finding does not rename it.',
  checks: Object.fromEntries(Object.entries(results).map(([k, v]) => [k, {
    count: v.count, skipped: v.skipped || false, reason: v.reason,
    byRule: v.byRule, contrastCount: v.contrastCount,
    fingerprints: v.fingerprints,
  }])),
};

const outPath = outArg ? path.resolve(outArg) : path.join(SCRIPT_DIR, 'baseline.json');
fs.writeFileSync(outPath, JSON.stringify(baseline, null, 2) + '\n');
console.log(JSON.stringify({
  written: path.relative(REPO_ROOT, outPath),
  summary: Object.fromEntries(Object.entries(baseline.checks).map(([k, v]) => [k, v.skipped ? 'skipped' : v.count])),
}, null, 2));
