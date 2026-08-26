# Capy / Corosio Documentation Style Guide

**Audience:** human editors and AI agents writing or editing the documentation.

This guide is a **checkable contract**: every rule is phrased so an agent can apply it, and
most can be checked automatically. Enforcement falls into three tiers — a CI **gate** that
blocks a merge, a CI **warning** that flags candidates for a human to judge, or **review**
via the PR checklist when no tool can decide (see Part F for the per-rule mapping). It is
organized by five documentation axes — Structure, Accuracy, Wording, Completeness,
Presentation — plus the cross-cutting concern that motivates all of them: **drift**.

> **The prime directive — prevent drift.** Prose and generated reference tend to diverge
> over time; hand-copied signatures and pasted examples rot silently. Every rule below
> exists to make the docs *self-correcting*: single-sourced, compiled, and linted. When a
> rule trades elegance for drift-resistance, drift-resistance wins.

---

## Part A — Structure (Diátaxis)

Follow **[Diátaxis](https://diataxis.fr)**. Every page is exactly one of four modes, and
**modes must not mix**:

| Mode | Purpose | Answers |
|---|---|---|
| **Tutorial** | learning, by doing | "teach me" |
| **How-to** | a single task, start→finish | "how do I X?" |
| **Reference** | information, dry and complete | "what is the signature of X?" |
| **Explanation** | understanding, rationale | "why is it this way?" |

**Rules:**
- **A1.** Each page declares its mode; an edit keeps content within that mode.
- **A2. Reference belongs in the reference.** Exposition pages never reproduce full
  signatures or concept definitions — they *link* (Part B). This is the highest-value
  structural rule.
- **A3. Rationale belongs in Explanation.** Use interleaved admonitions (`[NOTE]`/`[TIP]`)
  for *local* rationale on a how-to/tutorial page; reserve dedicated design/explanation
  pages for *cross-cutting* rationale. Do not run multiple parallel rationale channels for
  the same material.
- **A4. One concept, one home.** Before adding a page, find where the concept already
  lives. If two pages teach the same thing, merge them.
- **A5. Ordering follows dependency.** A page may not rely on a concept introduced only on
  a *later* page. "Advanced" material comes after the basics it builds on.
- **A6. Quick-start / getting-started content sits near the top of the navigation**, not
  buried near the reference.
- **A7. Headings describe content, not ceremony.** No numbered "Part N" mega-headings for
  short sections; use plain descriptive headings.

## Part B — Single-source-of-truth (anti-drift core)

The Antora pipeline provides two mechanisms; use them instead of hand-authoring:

- **B1. Never hand-type an API signature in prose.** Reference a symbol with the `cpp:`
  macro so it links to the generated reference and cannot drift
  (e.g. `cpp:boost::capy::run_async[]`). To describe what a function does, link it; do not
  restate its declaration.
- **B2. Never paste example code.** Every code block is an `include::example$...[tag=...]`
  of a compiled source file. New examples are written as compiled sources with tagged
  regions, not typed into the page.
- **B3. Intentionally-non-compiling blocks are tagged**, not silently pasted: use a
  pseudocode role for sketches/rejected designs and an external role for other-library
  comparisons, so the compile gate knows to skip them. These two apply to a `[source,*]`
  block only — role=pseudocode/external on a bare listing does nothing (B2 doesn't look for
  them there).
  A bare listing (`----` with no `[source,*]` attribute, or `....`) that holds literal
  program output or a hand-drawn figure — never code — is tagged `[role=output]` or
  `[role=figure]` respectively, so B2 does not mistake it for an unmarked code block. This is
  the one constraint that makes the design safe, and it is load-bearing: **role=output/
  role=figure exempt only a bare listing, never a `[source,*]` block** — a block that
  actually compiles is tagged `[source,*]` and cleared through pseudocode/external, full
  stop, however output-shaped its content looks. Confusing the two would let `role=output`
  launder real code past B2. `doc-lint.mjs`'s SHAPE check runs a content heuristic over every
  role=output/role=figure block and reports (non-gated, advisory) any whose content looks
  like code, so a wrong tag is not silently permanent — see the check's header comment.
  Note `role=` on a bare listing emits a real CSS class (`class="listingblock output"` /
  `...figure`) with no stylesheet rule behind it today; a future UI bundle that styles
  `.output`/`.figure` will change how every such block renders, project-wide, in one step.
- **B4. A brief describes behavior, not identity — classes included.** A reference brief
  says what the entity *does*, not what it *is*, and never restates its declaration or claims
  parameters it does not take. This binds class briefs as much as function briefs. *(Reversal:
  both documentation audits read identity-shaped class briefs — "A test utility for…", "Result
  type for…", "Recycling memory resource with size-class buckets" — as house convention,
  attested across 17+ headers, and on that reading dropped roughly 230 findings apiece. The
  maintainer ruled that B4 binds them anyway: describing what a class *is* is not a licensed
  house style, it is the defect B4 exists to catch. Treat both audits' B4 dispositions on class
  briefs as superseded; this is what authorizes re-opening that finding set.)*
- **B5. Doc code uses the namespace alias, never a using-directive.** Every file under
  `test/doc/`, and every code block authored inline in a `.adoc` page, spells library names
  against `namespace capy = boost::capy;` — `capy::task`, `capy::test::fuse`,
  `capy::this_coro::executor` — even on a page that never declares the alias itself. A reader
  should be able to tell which names in an example come from Capy without knowing the library.
  The exception: a block that verbatim-quotes library-internal source keeps that source's own
  unqualified spelling. `using namespace boost::capy;` is not used in doc code. Warning
  suppressions live in `test/doc/doc_warnings.hpp`, included once per file outside every tag; a
  warning that fires in one fragment stays in that fragment with a comment saying why. B5 is
  enforced by review only — no script checks it.

## Part C — Wording (pragmatic Simplified Technical English)

Apply an **ASD-STE100–derived subset**: STE's *spirit* — short sentences, active voice, one
idea per sentence, simple tense, one term per concept — **not** its strict word-ban. Enforce
**hard** in reference briefs and how-to steps; **relax** to spirit-only in tutorials and
design essays.

- **C1. One idea per sentence.** Split compounds joined by "and/but/;/—".
- **C2. Length.** ≤ 20 words for instructions, ≤ 25 for descriptive text. Hard in API docs;
  soft in essays.
- **C3. Active voice; name the actor.** "The task receives the executor," not "the executor
  is received."
- **C4. Present simple.** Avoid needless "will" and "has been".
- **C5. No unnecessary negatives.** Write "This is X," not "This is X. Not Y. Not Z."
- **C6. No decorative figurative language.** At most one analogy per page, only when it
  carries real explanatory weight. Cut clichés and metaphors the reader must
  reverse-engineer.
- **C7. Define terms before use.** No expression enters the text without a definition or a
  glossary link.
- **C8. Keep articles.** "the task", "a coroutine" — never drop *the/a* to save words.
- **C9. Plain words.** *use* (not utilize/leverage), *to* (not in order to), *before* (not
  prior to), *because* (not due to the fact that); delete
  *simply/basically/obviously/of course/note that*.
- **C10. One term per concept** (Part C.1). Never alternate synonyms.
- **C11. One documentation command per concept — docstring tags included.** Doxygen offers
  both `@pre` and `@par Preconditions` for the same concept, a precondition. Use `@pre`; never
  `@par Preconditions`. *(Placement: this is C10's "one term per concept" applied to command
  choice rather than word choice, not a drift risk, so it sits in Part C rather than Part B —
  neither tag can go stale relative to the code; they only differ in which markup an author
  reaches for. It is deliberately not a C.1 table row: C.1 governs English words chosen while
  writing prose, enforced by matching that prose after docstring extraction; these are Doxygen
  commands consumed *by* the extractor itself, and the two do not even survive extraction in
  the same shape — `@par Preconditions` re-emits as a bare "Preconditions" prose line, `@pre`
  re-emits with no label at all — so a C.1-style substitution rule could not enforce this as
  written. Evidence: the docstring corpus was split exactly 17/17 between the two forms when
  this was ruled — a genuine tie, not two conventions living in different files; `thread_pool.hpp`
  alone contains both (`@pre` once, `@par Preconditions` twice). There was no house rule to
  preserve; the maintainer broke the tie in favor of `@pre`. Treat `@par Preconditions` as the
  form to replace wherever a docstring is touched.)*

### C.1 Terminology table (controlled vocabulary)

Use the **Use** column everywhere; never the **Avoid** synonyms. API identifiers are
technical names and never change. *(This table is the one part of the guide expected to grow
as vocabulary is added — extend it rather than letting synonyms drift.)*

| Concept | Use | Avoid |
|---|---|---|
| begin executing a coroutine | **start** | launch, spawn, fire off, kick off, run (verb) |
| the `co_await` operation | **await** | wait on, waiting for |
| value a coroutine yields at completion | **result** | return value (except naming the C++ type) |
| object that schedules work | **executor** | scheduler (reserve for P2300) |
| context owning threads/executors | **execution context** | context (bare), backend context |
| concrete I/O impl behind a type-erased type | **I/O backend** | backend, provider, engine |
| type that hides its concrete type | **type-erased** | erased, opaque, boxed |
| `stop_token`-based cancellation | **stop token** / **cancellation** | cancel token, cancellation token |
| callback passed to `run_async` | **completion handler** | handler (bare), callback |
| a `task<T>` value | **task** | coroutine (the language feature), coro |
| the C++20 language feature | **coroutine** | coro, async function |
| awaitable satisfying `IoAwaitable` | **I/O awaitable** | awaitable (bare, when the concept is meant) |

Approved technical names (need no paraphrase): coroutine, task, promise, awaiter,
awaitable, executor, execution context, strand, thread pool, allocator, frame, buffer,
buffer sequence, stream, stop token, sender, receiver, scheduler, mutex, event, waker.
Where a name here also appears in the Avoid column above, the table row governs: use
it only in the sense the row names. **scheduler** is approved only in its P2300 sense
(the `scheduler` concept); it is never a synonym for **executor**.

## Part D — Completeness & Pedagogy

- **D1. Goal-oriented, not syntax-first.** Open a concept with a use case ("you want to
  X"), then introduce the machinery that achieves it. Do not enumerate syntax before
  motivation.
- **D2. Every concept page has a runnable example** of the library's *own* type — not only
  of the standard-library types it resembles. A page introducing a type shows that type in
  use, actually running. *(Primer carve-out: the "library's own type" clause does not bind a
  section that declares itself background material rather than a Capy concept page.
  `doc/modules/ROOT/pages/3.concurrency/3a.foundations.adoc` through `3d.patterns.adoc` each
  carry several runnable examples, all deliberately of standard-library types, because
  `3.intro.adoc` frames the whole section as first-principles concurrency taught before Capy is
  introduced, not as an introduction to a Capy type — this carve-out is what excuses `3a`–`3d`
  from D2, not from having examples at all. `3.intro.adoc` itself has no `include::example$` of
  any kind and is not covered by this carve-out; its D2 finding is a separate, already-baselined
  gap (a prose-only introduction page), not evidence for "deliberately standard-library." Both
  prior audits flagged `3a`–`3d`'s std-lib examples as a D2 violation and both were wrong to; it
  does not recur, and this note exists only to stop a third pass from re-filing it. No page
  changes follow from this carve-out.)*
  *(Landing-page carve-out: every `*.intro.adoc` — all nine, `:page-mode: explanation` — is a
  motivating essay plus a mechanical child list, never an introduction of a Capy type. `3.intro`'s
  gap above is one instance of this general case, not a special case of its own. `doc-lint.mjs`'s
  D2 check is scoped to `CONCEPT_DIRS` (a pedagogical category — "this chapter teaches
  progressively") and deliberately does **not** read `:page-mode:` at all, so a page cannot leave
  D2's scope by declaring a mode, correct or not — the six chapter-intro pages
  (`2`–`7.intro.adoc`) stay in D2's scope and keep failing it for the same reason `3.intro.adoc`
  always did: they introduce no type, so there is no example to add. That six-finding count is a
  documented, intentional consequence of D2's own scope, not a regression to chase to zero by
  adding decorative `include::example$` blocks to pages that don't need one.)*
- **D3. Every non-obvious design choice states its rationale** (or links to the explanation
  page that does). "Because it is" is not documentation.
- **D4. Document thread-safety *and* executor affinity** at the class level where relevant.
- **D5. No unexplained qualifiers.** Hedges like "even on X" or "where available" either get
  explained or get cut.

## Part E — Presentation & Tooling

- **E1.** Prose links to the reference via `cpp:` (Part B1) so a first-time reader can see a
  type inline.
- **E2.** A right-rail table of contents is enabled; long pages are split at natural mode
  boundaries. *(Review tier: verify by eye, do not gate.)* The ToC is switched on by the
  `page-toc` attribute in `doc/antora.yml`, not by the theme.
- **E3.** The reference is grouped by functionality where the generator allows; operators are
  documented with their types; asynchronous operations are distinguishable from synchronous.
- **E4.** The theme passes a contrast check in both light and dark mode. *(Review tier: the
  gated failures were all `color-contrast` on shared Antora theme nav chrome — an external UI
  bundle Capy cannot fix, the same rationale that demoted E2; scan runs non-blocking, verify by
  eye, do not gate.)* The same reasoning covers the other rule classes the scan reports, checked
  against the built HTML at the 2026-08-25 reseed: `link-name` is Asciidoctor's empty
  `<a class="anchor">` before every section heading, `link-in-text-block` is the MrDocs reference
  title's auto-linked namespace segment, and `scrollable-region-focusable` is the `ui-bundle`
  stylesheet putting `overflow-x:auto` on `.listingblock pre` without a `tabindex`. All three are
  generator or theme output, not authored content, so they are grandfathered on the same terms —
  **but only for those shapes.** An a11y finding on markup Capy actually writes is a defect and
  this carve-out does not reach it.

## Part F — Enforcement (makes this guide checkable)

The guide is only anti-drift if CI checks it. Add **[Vale](https://vale.sh)** and wire it
into the CI documentation job.

### F.0 Enforcement tier by rule

Not every rule is machine-checkable. Each rule sits in one of three tiers:

- **Gate** — CI blocks the merge. Checked by Vale, the snippet-compile job, a small custom
  AsciiDoc/nav lint script, or an accessibility scan.
- **Warning** — CI flags candidates, a human decides. Heuristic checks with real
  false-positive/negative rates; never block on these.
- **Review** — no tool can judge; enforced by the PR checklist (F3).

| Tier | Rules |
|---|---|
| **Gate** | A1, A6, A7, B2, B3, C2, C4, C9, C10, D2 |
| **Warning** | A2, B1, C1, C3, C5, C6, D4, D5, E1 |
| **Review** | A3, A4, A5, B4, B5, C7, C8, C11, D1, D3, E2, E3, E4 |

The accuracy gates (B2, B3, D2 correctness) are enforced by the snippet-compile job, not by
Vale — that job is what makes examples unable to drift.

`doc/.vale.ini`:
```ini
StylesPath = .vale/styles
MinAlertLevel = warning
Packages = Google
[*.adoc]
BasedOnStyles = Vale, Google, Capy
; AsciiDoc source/callout blocks are code, not prose:
BlockIgnores = (?s) *(\[source.*?----.*?----)
TokenIgnores = (\x60[^\x60]+\x60)
```

`doc/.vale/styles/Capy/Terminology.yml` (enforces Part C.1):
```yaml
extends: substitution
message: "Use '%s' for one-term-per-concept consistency (style guide C.1)."
level: warning
ignorecase: false
swap:
  '\b(launch|spawn|fire off|kick off)\b': start
  '\bcancellation token\b': stop token
  '\bcancel token\b': stop token
  '\bboxed\b': type-erased
```

`doc/.vale/styles/Capy/NoFluff.yml` (enforces C5/C9):
```yaml
extends: existence
message: "Filler/fluff — delete or rewrite (style guide C5/C9): '%s'."
level: warning
ignorecase: true
tokens:
  - simply
  - basically
  - essentially
  - obviously
  - of course
  - note that
  - in order to
  - due to the fact that
  - utilize
```

`doc/.vale/styles/Capy/SentenceLength.yml` (retired to `suggestion`; does not enforce C2 —
see F1):
```yaml
extends: occurrence
message: "Sentence over 25 words — split it (style guide C1/C2)."
level: suggestion
scope: sentence
token: \b(\w+)\b
max: 25
```

- **F1.** CI runs `vale doc/modules` and fails on `error`-level findings, except **C2**: its
  authority is `doc/lint/sentence-length.mjs` (`doc/lint/README.md`), hard on docstrings and
  non-essay `.adoc` pages, advisory on `9.design/` and `A.specification-methods/`.
  `Capy.SentenceLength` is `level: suggestion` and enforces nothing.
- **F2.** The snippet-compile job is the accuracy gate; keep every example sourced from a
  compiled file (Part B2).
- **F3.** Doc PR checklist: mode declared (A1)? no hand-typed signatures (B1)? example
  compiled (B2)? namespace alias, not a using-directive (B5)? terminology clean (`vale`)?
  rationale present (D3)?

### F4 — A check is not adopted until a planted violation has failed it

**The rule: before promoting a rule to a gate — or believing a gate you just wired — plant a
violation of that exact rule and watch the check fail. A green run is not evidence.** Twelve times
during the documentation-improvement work a check looked healthy while checking less than it
appeared to, and every one of them read as a pass. Three, all recoverable from this repository's
history:

- **A rule that could not match any input.** `Capy/PartHeadings.yml` (A7) was written
  `scope: heading` with the pattern `^==+\s+Part\s+\d+`. Vale's heading scope hands the rule the
  heading *text*, with the `==` markers already stripped, so the anchor guaranteed zero matches.
  It reported clean over a corpus full of `== Part 3:` headings until `b54fe6c8` fixed it.
- **A gate spec that matched no fingerprint.**
  `--gate 'vale_adoc:^(Capy\.SimpleTense|Capy\.NoFluff|Capy\.Terminology)$'` reports
  `gated: true, gatedNew: 0` at exit 0, because the check name lives at the *tail* of a Vale
  fingerprint and the leading `^` anchors to the file name. The un-anchored form fails on the same
  input. Found twice, the second time by bite-testing rather than by reading.
- **A gated check that collapsed to zero without being marked skipped.** A crashing check emitted
  no findings and reported `count: 0, skipped: false`; the comparator then computed *zero new
  violations* from an empty current set and passed. Zero looks exactly like success. Both
  comparators now carry an explicit fail-closed rule for it — a gated check with zero findings
  against a non-empty baseline is fatal unless `--allow-emptied` names it, in
  `doc/lint/baseline-diff.mjs` (reseed candidates) and in
  `doc/lint/check-no-new-violations.mjs` (the blocking gate). The reachable case that motivated
  the second one, and the reason it is whole-check rather than per-gate-regex, is recorded at the
  rule itself: `cd doc && vale --output=JSON lint/.nonexistent-corpus` prints `{}` and exits 0.
  `doc/lint/baseline.mjs` additionally checks `extract-docstrings.mjs`'s exit status, because the
  docstring corpus is generated and the generator never clears its output directory, so a crashed
  extractor used to leave a stale corpus that linted clean.

The shared shape is that all three failures are **silent and reassuring**: the machinery reports
success, and the only way to distinguish "nothing is wrong" from "nothing is being checked" is to
introduce something wrong and confirm it is caught. `doc/lint/selftest.mjs` exists for the same
reason, and `doc/lint/README.md` records the fingerprint shapes a gate spec has to match.

---

### How an agent uses this guide
1. Identify the page's Diátaxis mode; keep edits in-mode (A1).
2. Never type a signature or paste code — link (B1) or include a compiled snippet (B2).
3. Run Vale locally over both corpora, from `doc/`, before proposing the change, and fix all
   `error`s. Vale must run from `doc/` with `node_modules/.bin` on `PATH` — this project's Vale
   needs `asciidoctor` (the asciidoctor.js build under `node_modules`, not a Ruby install; there
   is no Ruby on a stock dev machine here) to parse AsciiDoc, and without both of those it exits
   2 having printed nothing, which greps identical to a clean run and has already misled two
   audit sub-agents this way:
   ```
   cd doc && export PATH="$PWD/node_modules/.bin:$PATH"
   vale --output=JSON modules
   node lint/extract-docstrings.mjs && vale --output=JSON lint/.docstrings
   ```
   A `0` in the output is not evidence of a clean run by itself — it is at least as often
   evidence the run never happened (F.4's silent-and-reassuring failures are exactly this
   shape). Confirm a non-zero total somewhere before trusting a zero. Vale does not enforce
   C2 (sentence length) either way — its authority is `doc/lint/sentence-length.mjs`, not Vale
   (F1); do not look to `vale`'s exit code for C2.
4. For every new claim, either link the rationale or add it (D3).