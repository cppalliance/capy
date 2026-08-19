#!/usr/bin/env node
//
// check-no-new-violations.mjs — the "no NEW violations" gate the Task 2
// brief's acceptance criterion describes: a fresh check run is diffed
// against doc/lint/baseline.json (Style Guide Part F.0) per-check
// fingerprint sets, and anything not already in the baseline is reported as
// new. This is what would catch a newly introduced banned word ("utilize")
// or a newly undocumented @param, without re-flagging the existing backlog.
//
// Node built-ins only. Regenerates a fresh snapshot via baseline.mjs into a
// temp file (never overwrites the committed baseline.json) and compares.
//
// Non-blocking by default (Task 2: everything stays warning-mode). Pass
// --strict to exit 1 when new violations are found.
//
// --gate <check>:<rule-regex>  restricts what counts as blocking to a named
// set of (check, rule-regex) pairs — the phase-exit gate-promotion mechanism.
// Repeatable. Each value is split on its FIRST ':' into a check name and a
// regex tested against that check's fingerprints. In gate mode BOTH the exit
// condition and the skip check are filtered: only NEW fingerprints of a gated
// check that match its regex block, and only a skip of a GATED check fails the
// run ("can't verify a gated rule = not a pass"); skips of non-gated checks
// (a11y, mrdocs, vale_docstrings) are reported but do not block. Without
// --gate the comparator stays omnibus (the non-blocking report step).
//
// A gated check that reports ZERO findings against a non-empty baseline also
// fails the gate — a check that silently did not run is indistinguishable from
// one that ran clean, and `skipped` does not catch it. See the rule at the
// bottom of the per-check loop for the reachable case and the reasoning.
//
// Phase-1 exit gate spec (A1/A6/A7/B2/D2):
//   --gate 'doc_lint:^(A1|A6|B2|D2):' --gate 'vale_adoc:Capy\.PartHeadings$'
//   (A1/A6/B2/D2 come from doc_lint; A7 is the Vale rule Capy.PartHeadings.)
//
// Phase-2 exit adds MrDocs-no-warnings to the above (full spec):
//   --gate 'doc_lint:^(A1|A6|B2|D2):' --gate 'vale_adoc:Capy\.PartHeadings$' \
//   --gate 'mrdocs_warnings:.*'
//   mrdocs_warnings:.* gates the whole reference-surface check. E4 (a11y contrast)
//   is NOT gated — it was demoted to Review tier (doc/STYLE_GUIDE.md Part F.0):
//   the gated failures were all color-contrast on shared Antora theme nav chrome,
//   which Capy cannot fix, the same rationale that demoted E2. The a11y scan
//   still runs and is reported non-blocking.
//
// --allow-emptied <check>  suppresses the "gated check reports zero findings
// against a non-empty baseline" failure described below, for one check. Use it
// only when a gated backlog has genuinely closed; it records the decision in
// the run log. Not used by the committed CI invocation.
//
// Usage: node doc/lint/check-no-new-violations.mjs [--strict] [--gate spec ...]
//        [--allow-emptied check ...] [--skip-a11y]
//
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const strict = argv.includes('--strict');

// Parse --gate specs; everything else (except --strict) is passed through to
// baseline.mjs. NB: --gate values must NOT reach baseline.mjs, whose first
// non-flag arg is taken as the output path.
const gateByCheck = new Map(); // check -> [RegExp]
const allowEmptied = new Set(); // checks whose zero is an accepted milestone
const extraArgs = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--strict') continue;
  let allow = null;
  if (a === '--allow-emptied') allow = argv[++i];
  else if (a.startsWith('--allow-emptied=')) allow = a.slice('--allow-emptied='.length);
  if (allow != null) {
    if (!allow) {
      console.error('--allow-emptied expects a check name');
      process.exit(2);
    }
    allowEmptied.add(allow);
    continue;
  }
  let spec = null;
  if (a === '--gate') spec = argv[++i];
  else if (a.startsWith('--gate=')) spec = a.slice('--gate='.length);
  if (spec != null) {
    const idx = spec.indexOf(':');
    if (idx < 0) {
      console.error(`--gate expects <check>:<rule-regex>, got: ${spec}`);
      process.exit(2);
    }
    const check = spec.slice(0, idx);
    const re = new RegExp(spec.slice(idx + 1));
    if (!gateByCheck.has(check)) gateByCheck.set(check, []);
    gateByCheck.get(check).push(re);
    continue;
  }
  extraArgs.push(a);
}
const gated = gateByCheck.size > 0;

const baselinePath = path.join(SCRIPT_DIR, 'baseline.json');
if (!fs.existsSync(baselinePath)) {
  console.log(JSON.stringify({ error: `no baseline.json at ${baselinePath} — run baseline.mjs first` }, null, 2));
  process.exit(0);
}
const baseline = JSON.parse(fs.readFileSync(baselinePath, 'utf8'));

const tmpPath = path.join(os.tmpdir(), `doc-lint-current-${process.pid}.json`);
const r = spawnSync('node', [path.join(SCRIPT_DIR, 'baseline.mjs'), ...extraArgs, tmpPath], { encoding: 'utf8' });
if (r.status !== 0 || !fs.existsSync(tmpPath)) {
  console.log(JSON.stringify({ error: 'failed to generate a current snapshot', stderr: r.stderr }, null, 2));
  process.exit(0);
}
const current = JSON.parse(fs.readFileSync(tmpPath, 'utf8'));
fs.rmSync(tmpPath, { force: true });

let totalNew = 0;        // omnibus: new findings across ALL checks (report semantics)
let anySkipped = false;  // any check skipped at all
let gatedNew = 0;        // new findings in gated checks matching a gate regex
let gatedSkipped = false; // a GATED check was skipped (can't verify => gate fails)
let gatedEmptied = false; // a GATED check reported ZERO findings against a non-empty baseline
const gatedFindings = []; // the specific gated new fingerprints (named in the log)
const emptiedGated = [];  // the checks that tripped the emptiness rule
const report = {};
for (const [check, currentCheck] of Object.entries(current.checks)) {
  const gateRes = gateByCheck.get(check) || null;
  // A skipped check (Vale broken, MrDocs/a11y couldn't run, ...) is NOT a clean pass — it
  // means no comparison happened at all. Surface it loudly (stderr, outside the JSON blob)
  // so it can't be mistaken for "0 new" in a log that only skims the summary line, and
  // record it distinctly (newCount: null, not 0) in the JSON report too. A skip of a GATED
  // check additionally fails the gate: an unverifiable gated rule is not a pass.
  if (currentCheck.skipped) {
    anySkipped = true;
    if (gateRes) gatedSkipped = true;
    console.error(`SKIPPED: ${check} (${currentCheck.reason}) — no-new-violations comparison NOT performed for this check.${gateRes ? ' [GATED — fails the gate]' : ''}`);
    report[check] = {
      skipped: true, reason: currentCheck.reason, gated: !!gateRes,
      baselineCount: baseline.checks[check]?.count ?? 0, currentCount: currentCheck.count,
      newCount: null, newFindings: [],
    };
    continue;
  }
  const baseSet = new Set(baseline.checks[check]?.fingerprints || []);
  const currentSet = currentCheck.fingerprints || [];
  const newOnes = currentSet.filter((fp) => !baseSet.has(fp));
  totalNew += newOnes.length;
  const entry = { baselineCount: baseline.checks[check]?.count ?? 0, currentCount: currentCheck.count, newCount: newOnes.length, newFindings: newOnes };
  if (gateRes) {
    const gatedOnes = newOnes.filter((fp) => gateRes.some((re) => re.test(fp)));
    entry.gated = true;
    entry.gatedNewCount = gatedOnes.length;
    entry.gatedNewFindings = gatedOnes;
    gatedNew += gatedOnes.length;
    for (const fp of gatedOnes) gatedFindings.push(`${check} :: ${fp}`);

    // A GATED check that reports ZERO findings where the committed baseline has
    // some is treated as a check that did not run, until proven otherwise. This
    // is the fail-open the `skipped` flag does NOT catch, and it is reachable:
    //
    //   $ cd doc && vale --output=JSON lint/.nonexistent-corpus
    //   {}
    //   $ echo $?
    //   0
    //
    // baseline.mjs marks a Vale check skipped only on exit 2 or a non-object
    // parse, so exit 0 plus `{}` yields `{count: 0, skipped: false}` — and this
    // comparator then computes "zero new" from an empty current set and reports
    // `gated: true, gatedNew: 0`, i.e. a gate that says it is gating while
    // measuring nothing. Any renamed corpus path, crashed extractor, or
    // `.vale.ini` edit that stops matching the corpus lands here.
    //
    // The rule is the same one baseline-diff.mjs applies to a reseed candidate,
    // for the same reasons: emptiness rather than a removal-fraction threshold
    // (a check that did not run produces exactly zero, never 40% fewer), and
    // scoped to GATED checks, whose zero is the one that decides a merge.
    //
    // Deliberately WHOLE-CHECK, not per-gate-regex. The gated SLICE of
    // vale_docstrings is legitimately empty today — zero Capy.SimpleTense /
    // NoFluff / Terminology on the docstring corpus is exactly what Phase 4
    // delivered — so a per-slice rule would fail the committed invocation on
    // the phase's own success state. A whole-check zero cannot be produced by
    // wording work: the residual Vale.Spelling/Google backlog on both corpora
    // is not going to zero, so only a broken run gets there.
    //
    // The one legitimate whole-check zero, a gated backlog genuinely closing,
    // is a milestone worth an explicit --allow-emptied <check>.
    if (currentSet.length === 0 && baseSet.size > 0 && !allowEmptied.has(check)) {
      entry.gatedEmptied = true;
      gatedEmptied = true;
      emptiedGated.push(check);
    } else if (currentSet.length === 0 && baseSet.size > 0) {
      entry.gatedEmptiedAllowed = true;
    }
  }
  report[check] = entry;
}

if (anySkipped) {
  console.error(`SKIPPED checks present — totalNew (${totalNew}) is only valid for the checks that actually ran.`);
}

// The blocking condition: gated slice when --gate is present, omnibus otherwise.
const blockingNew = gated ? gatedNew : totalNew;
const blockingSkip = gated ? gatedSkipped : anySkipped;
if (gated && blockingNew > 0) {
  console.error(`GATE: ${blockingNew} new gated violation(s):`);
  for (const f of gatedFindings) console.error(`  - ${f}`);
}
for (const check of emptiedGated) {
  console.error(`GATE: gated check '${check}' reports 0 findings but the committed baseline has ` +
    `${baseline.checks[check]?.fingerprints?.length ?? 0} — a check that did not run looks exactly ` +
    `like this. Verify it really ran; if the backlog is genuinely closed, re-run with ` +
    `--allow-emptied ${check}.`);
}

console.log(JSON.stringify({
  totalNew, anySkipped, strict,
  gated, gatedNew: gated ? gatedNew : undefined, gatedSkipped: gated ? gatedSkipped : undefined,
  gatedEmptied: gated ? gatedEmptied : undefined,
  emptiedGated: gated ? emptiedGated : undefined,
  gatedFindings: gated ? gatedFindings : undefined,
  checks: report,
}, null, 2));
process.exit(strict && (blockingNew > 0 || blockingSkip || (gated && gatedEmptied)) ? 1 : 0);
