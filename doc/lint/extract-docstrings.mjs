#!/usr/bin/env node
//
// extract-docstrings.mjs — pulls Doxygen/MrDocs docstring prose out of
// `include/boost/capy/**/*.hpp` into plain .adoc files so Vale (Style Guide
// Part F) can lint it, not only the Antora `.adoc` pages (Style Guide Part F,
// Task 2 Step 4b). Node built-ins only, no dependencies.
//
// For each header with at least one doc comment, writes a mirrored file under
// OUT_DIR (default doc/lint/.docstrings/, gitignored — generated output, not
// source) containing just the comment prose: `@code`/`@endcode`
// samples are dropped (not prose, and full of identifiers/punctuation that
// would drown real findings); `@param`/`@tparam`/`@return`/etc. tags have
// their tag keyword (and, for `@param`/`@tparam`, the parameter name) removed
// but keep their description text, since that's the part C.2/C.9/C.10 apply
// to. The file extension is .adoc so it picks up the same `[*.adoc]` section
// of doc/.vale.ini used for the Antora pages — no separate Vale config needed.
//
// The parameter name is dropped, not re-emitted as a label. Emitting it back
// as `name: description` — which this script did until 2026-07 — made every
// `@param`/`@tparam` in the library trip Google.Colons, whose token is
// `(?<!:[^ ]+?):\s[A-Z]`: a colon, a space, a capital. That accounted for 424
// of 444 Colons alerts on this surface, a false-positive class that grows with
// every parameter documented. A parameter name is an identifier, not prose;
// the description is the prose, and the description is what stays linted.
//
// BOTH Doxygen doc-comment forms are extracted: `/** ... */` blocks and runs of
// consecutive `///` lines. A run of `///` lines is ONE doc comment attached to
// the declaration that follows it, which is how Doxygen and MrDocs read it, and
// MrDocs really does publish that prose — `io_result.hpp:49`'s
// `/// The error code from the operation.` renders on both
// `reference/boost/capy/io_result.html` and `reference/boost/capy/io_result/ec.html`.
// Until 2026-08 only the `/** */` form was extracted, so 86 published doc lines
// across 25 headers were invisible to every Vale rule and to the C2 checker,
// including the four the Phase-4 exit promoted to merge-blocking gates. It was
// latent rather than live (the `///` prose held no violation of any gated rule),
// which is exactly why nothing surfaced it: a bite test's first planted violation
// went into a `///` comment and no gate saw it.
//
// Blocks are emitted in source order, so an extracted file reads in the order a
// reader meets the prose. Fingerprint stability does not depend on that order:
// baseline.mjs keys a Vale finding `file:#N:Check`, where N is a per-(file,Check)
// occurrence counter, so one added alert mints exactly one new key wherever in
// the file it sits.
//
// Usage: node doc/lint/extract-docstrings.mjs [outDir]
//
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SCRIPT_DIR, '../..');
const INCLUDE_ROOT = path.join(REPO_ROOT, 'include/boost/capy');
const OUT_DIR = path.resolve(REPO_ROOT, process.argv[2] || 'doc/lint/.docstrings');

function walk(dir) {
  let out = [];
  for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, ent.name);
    if (ent.isDirectory()) out = out.concat(walk(p));
    else if (ent.name.endsWith('.hpp')) out.push(p);
  }
  return out;
}

// Tags whose keyword (and, for param-like tags, the following identifier)
// is stripped but whose trailing description text is kept as prose.
const NAMED_TAGS = /^@(param|tparam)\s+(?:\[[a-z,]+\]\s*)?(\S+)\s*/;
const BARE_TAGS = /^@(returns?|throws?|pre|post|note|warning|brief|see)\b\s*/;
// `@li` is stripped like the bare tags, but its item is ALSO re-emitted as its
// own paragraph — see cleanBlock(). Doxygen has no other list-item tag in this
// codebase (`grep -c '@arg' include` -> 0), so `li` is the whole set.
const LIST_ITEM = /^@li\b\s*/;
// `@par Some Title` is a SECTION TITLE, not the opening words of the paragraph
// under it, and it carries no terminal punctuation. Left as a bare line it was
// joined into the paragraph's first sentence and inflated its word count — the
// same class of defect as the bold run-in lead, but one no sentence-boundary
// rule can fix, because there is no boundary character to find. Exactly 7 of 202
// C2 findings started on such a line and two of them were not violations
// (`ex/executor_ref.hpp` "Thread Safety" reported 26 for a real 24;
// `io/any_read_stream.hpp` "Immediate Completion" reported 27 for a real 24).
// So the title is emitted as its own paragraph. A bare `@par` with no title is
// Doxygen's plain paragraph break and contributes nothing.
const PAR_TITLE = /^@par\b\s*/;
const INLINE_REFS = /@(ref|p|c)\s+(\S+)/g;

// A Doxygen `@li` item is a sentence, and the extractor used to hand Vale a run
// of them as consecutive lines with the `@li` keyword still in the text. Two
// defects followed, both fixture-confirmed:
//
//   * Vale's sentence segmenter needs `. ` to break and will not break on
//     `\n@li` (`@` is not a capital), so a run of PERIOD-LESS items collapsed
//     into one pseudo-sentence. Five ten-word items produced exactly one
//     Capy.SentenceLength alert whose Match field was literally 'li'; the same
//     five items with terminal periods produced none. C2 was therefore measuring
//     missing Doxygen punctuation, not sentence length.
//   * The surviving `li` keyword spent a phantom word of the 25-word budget, so
//     a list item's real limit was 24. A hand-counted 25-word item alerted.
//
// Each item is now emitted as its own paragraph (blank-line delimited, which is
// what makes it a separate block to asciidoctor and so a separate sentence
// scope) with the keyword removed and continuation lines folded in.
function cleanBlock(raw) {
  // Drop @code ... @endcode samples entirely — not prose.
  const noCode = raw.replace(/@code\b[\s\S]*?@endcode\b/g, '');
  const lines = noCode.split('\n').map((l) => l.trim());
  const prose = [];
  let item = null; // text of the `@li` item currently being accumulated
  const flush = () => { if (item !== null) { prose.push(item, ''); item = null; } };
  const separate = () => { if (prose.length && prose[prose.length - 1] !== '') prose.push(''); };
  for (let line of lines) {
    if (line === '') {
      // The item's own trailing blank line stands in for this one.
      if (item !== null) flush(); else prose.push('');
      continue;
    }
    const li = LIST_ITEM.exec(line);
    if (li) {
      if (item !== null) flush(); else separate();
      item = line.slice(li[0].length).replace(INLINE_REFS, '$2');
      continue;
    }
    const par = PAR_TITLE.exec(line);
    if (par) {
      flush();
      separate();
      const title = line.slice(par[0].length).replace(INLINE_REFS, '$2').trim();
      if (title) prose.push(title, '');
      continue;
    }
    // A non-blank, non-tag line under an open item is its continuation.
    if (item !== null) {
      if (!line.startsWith('@')) { item += ` ${line.replace(INLINE_REFS, '$2')}`; continue; }
      flush();
    }
    line = line.replace(NAMED_TAGS, '');
    line = line.replace(BARE_TAGS, '');
    line = line.replace(INLINE_REFS, '$2');
    prose.push(line);
  }
  flush();
  return prose.join('\n').trim();
}

// Every `/* ... */` range in the file, so a `///` line sitting INSIDE one is not
// mistaken for a doc comment of its own. There is no such line in the tree today
// (`grep -rn '^[[:space:]]*\*.*///'` finds none), but the cost of being wrong is a
// commented-out doc comment silently entering the linted corpus.
function blockCommentRanges(text) {
  const ranges = [];
  for (const m of text.matchAll(/\/\*[\s\S]*?\*\//g)) ranges.push([m.index, m.index + m[0].length]);
  return ranges;
}

// Doc comments in source order. `/** ... */` blocks come from a direct match; a
// run of consecutive `///` lines is collected into one block, ended by the first
// line that is not a `///` line (blank, code, or anything else) — the same rule
// Doxygen applies. `///<` (trailing member doc) and `////`-style separator rules
// are matched by neither pattern; the tree contains none of either.
function docComments(text) {
  const found = [];
  for (const m of text.matchAll(/\/\*\*([\s\S]*?)\*\//g)) found.push({ at: m.index, raw: m[1] });

  const inBlock = blockCommentRanges(text);
  const covered = (off) => inBlock.some(([a, b]) => off >= a && off < b);
  const lineRe = /^[ \t]*\/\/\/(?!\/)(.*)$/;
  let off = 0;
  let run = null; // { at, lines: [] }
  const flushRun = () => { if (run) { found.push({ at: run.at, raw: run.lines.join('\n') }); run = null; } };
  for (const line of text.split('\n')) {
    const m = lineRe.exec(line);
    if (m && !covered(off)) {
      if (!run) run = { at: off, lines: [] };
      run.lines.push(m[1].replace(/^[ \t]/, ''));
    } else {
      flushRun();
    }
    off += line.length + 1;
  }
  flushRun();

  return found.sort((a, b) => a.at - b.at).map((d) => d.raw);
}

let written = 0;
for (const file of walk(INCLUDE_ROOT)) {
  const text = fs.readFileSync(file, 'utf8');
  const blocks = docComments(text).map(cleanBlock).filter(Boolean);
  if (blocks.length === 0) continue;

  const rel = path.relative(INCLUDE_ROOT, file);
  const outPath = path.join(OUT_DIR, `${rel}.adoc`);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, blocks.join('\n\n') + '\n');
  written++;
}

console.log(JSON.stringify({ headersWithDocs: written, outDir: path.relative(REPO_ROOT, OUT_DIR) }, null, 2));
