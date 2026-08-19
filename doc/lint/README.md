<!--
    Copyright (c) 2026 Michael Vandeberg

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

    Official repository: https://github.com/cppalliance/capy
-->
# `doc/lint` — the documentation-quality toolkit

These scripts implement the enforcement tiers in `doc/STYLE_GUIDE.md` Part F.0. They run in
the **Documentation** workflow (`.github/workflows/docs.yml`), in the `antora` job, after the
site build. Node built-ins only, no dependencies of their own.

| Script | What it checks |
|---|---|
| `doc-lint.mjs` | Structural AsciiDoc/nav rules (A1, A6, B2, ANCHOR, D2, …). JSON on stdout. |
| `extract-docstrings.mjs` | Extracts header docstrings into `.docstrings/*.adoc` so Vale can lint them. |
| `sentence-length.mjs` | **The authority for C2** (no sentence over 25 words), over both corpora. JSON on stdout. |
| ↳ | Emits `C2` (hard slice), `advisory-C2` (design essays) and `BACKTICK` (unbalanced code span). |
| `selftest.mjs` | Asserts `sentence-length.mjs` and `doc-lint.mjs`'s B2 and ANCHOR checks still detect what they claim. Exit 1 on regression. |
| `mrdocs-warnings.mjs` | Runs the pinned MrDocs 0.8.0 directly and parses its reference-surface warnings. |
| `run-a11y.mjs` | pa11y-ci contrast/a11y scan over the built site (`doc/build/site`). |
| `baseline.mjs` | Runs every check and snapshots their findings to `baseline.json`. |
| `check-no-new-violations.mjs` | **The gate.** Diffs a fresh run against `baseline.json` and fails on NEW findings. |
| `baseline-diff.mjs` | Explains what replacing `baseline.json` with a candidate would change. |

## How the gate works

`baseline.json` is a snapshot of every finding that already existed when it was taken.
`check-no-new-violations.mjs` runs a fresh scan and reports only fingerprints that are **not**
in the snapshot. Everything in the snapshot is grandfathered.

Which findings actually *block* a merge is the `--gate <check>:<regex>` spec in the blocking
step of `.github/workflows/docs.yml`. Each regex is tested against the **whole** fingerprint.

Fingerprints are built by `baseline.mjs` and their shape is load-bearing:

| Check | Fingerprint |
|---|---|
| `doc_lint` | `rule:file:#N:message` — rule at the **head** |
| `sentence_length` | `C2:file:#N:message` (hard), `advisory-C2:…`, `BACKTICK:…` — rule at the **head** |
| `vale_adoc`, `vale_docstrings` | `file:#N:Check.Name` — check name at the **tail** |
| `mrdocs_warnings` | `file:#N:message` |
| `a11y` | `url:code:selector` |

`#N` is the Nth occurrence of that (head, tail) pair, **not a line number** — so inserting
text above a finding does not rename it. It sits mid-key on purpose: the gate spec
`vale_adoc:Capy\.PartHeadings$` is anchored on the tail, and anything appended after the check
name would make that gate match nothing while still exiting 0.

### `ANCHOR`

`doc-lint.mjs`'s `ANCHOR` rule catches a C++ attribute written as `` `[[nodiscard]]` `` in
prose. Asciidoctor's inline-anchor substitution runs *inside* a backtick span, so that source —
which reads perfectly correctly — renders as an **empty `<code>` element**. Measured:
`` `[[clang::coro_await_elidable]]` `` produced
`<code><a id="clang::coro_await_elidable"></a></code>`. The fix in prose is a passthrough: 
`` `+[[nodiscard]]+` ``.

It is gated (`doc_lint:^(A1|A6|B2|D2|ANCHOR):`), head-anchored because doc_lint fingerprints put
the rule at the head. Verified by planting a violation and confirming the fingerprint
`ANCHOR:<file>:#1:<message>` matches the new spec and **does not** match the previous
`^(A1|A6|B2|D2):` — so the workflow edit is load-bearing, not decorative. Only prose is scanned;
inside a delimited block `[[` is literal and renders fine, and that negative was bite-tested too.

`doc-lint.mjs` also emits a `SHAPE` rule alongside `A1`/`A6`/`B2`/`ANCHOR`/`D2` — deliberately outside the
gate spec's `^(A1|A6|B2|D2|ANCHOR):` alternation, the same way `advisory-C2` sits outside
`sentence_length`'s `^C2:`. SHAPE is a content-shape heuristic over every `role=output`/
`role=figure` bare listing (doc/STYLE_GUIDE.md B3): those roles are a permanent B2 exemption, so a
block wrongly tagged non-code would otherwise be permanently invisible. A SHAPE finding never
blocks a merge — it is a signal for a human to re-check the tag, not a defect in itself.

**A Vale gate spec must never carry a leading `^`.** The regex is tested against the whole
fingerprint, and for the Vale checks the check name is at the **tail**, so `^Capy\.NoFluff$`
matches nothing and the comparator then reports `gated: true, gatedNew: 0` — a gate that
announces it is gating while checking nothing, at exit 0. Measured twice on this branch. The
head-anchored shape is correct only for the two rule-at-the-head checks (`doc_lint`,
`sentence_length`), which is why the live spec mixes `^(A1|A6|B2|D2|ANCHOR):` and `^C2:` with
`Capy\.PartHeadings$` and `(Capy\.SimpleTense|Capy\.NoFluff|Capy\.Terminology)$`. When you add
a gate, plant a violation of that exact rule and watch the step fail before you believe it.

**Never hand-edit `baseline.json`.**

### C2: gate the script, not the Vale rule

`Capy.SentenceLength` is `level: suggestion` (see the comment in
`.vale/styles/Capy/SentenceLength.yml`), so it never enters a Vale fingerprint set. The
obvious-looking spec by analogy with `vale_adoc:Capy\.PartHeadings$` is therefore **vacuous** —
measured: `--gate 'vale_adoc:Capy\.SentenceLength$'` gives `gated: true, gatedNew: 0`, exit 0,
while checking nothing. Gate the script:

```
--gate 'sentence_length:^C2:'      # live in docs.yml since Phase-4 exit
                                   # re-measured at the Phase-4 final fix wave: exit 1, gatedNew 2
```

`^C2:` binds the **hard** slice only — the `include/**` docstrings plus every `.adoc` page outside
`modules/ROOT/pages/9.design/` and `modules/ROOT/pages/A.specification-methods/`. The design-essay
findings are keyed `advisory-C2` (`doc/STYLE_GUIDE.md` Part C2 makes the limit soft in essays), and
that key deliberately does not begin with `C2` so a mis-written `^C2` cannot reach them. Verified:
the gated slice contains 0 `advisory-C2` fingerprints.

### Run `selftest.mjs` after touching `sentence-length.mjs` or `doc-lint.mjs`

```
node lint/selftest.mjs      # exit 0 = 35 assertions hold; exit 1 names what broke
```

It exercises the properties whose failure is **silent**, because nothing in the real corpus
reaches them: the unbalanced-backtick guard (both halves — the diagnostic *and* the count
correction), the two under-reporting cases (mid-sentence ellipsis, parenthesised abbreviation),
the bold run-in lead, reader word counting, code blocks not being linted as prose, and the
hard/advisory partition including a `9.designish/` look-alike that must stay hard. Two plausible
refactors of the backtick rule were demonstrated to break it while every corpus-level number still
looked reasonable; both fail this file. Fixtures live in `lint/fixtures/` and are not part of
either linted corpus.

It also asserts that `extract-docstrings.mjs` extracts **both** Doxygen comment forms, which is the
same class of silent failure: `///` runs went unextracted until 2026-08, so 86 published doc lines
were outside every gate, and losing that branch again would just make the corpus smaller and every
count lower. The expectation is derived from the header tree rather than written down — the headers
whose *only* doc comments are `///` runs must each produce an output file — so it cannot go stale.

It also covers `doc-lint.mjs`'s B2 check, against a throwaway fixture tree built at run time (not
`lint/fixtures/`, which only `sentence-length.mjs` reads): every language token B2 must reach (not
just `cpp`/`c++`), a bare `----`/`....` listing holding real code with no role marker, a 5-dash
(`-----`) listing (closer length must match opener length, not just be `>=4`), and — load-bearing —
that `role=output`/`role=figure` clears only a *bare* listing and never a `[source,*]` block. A
mutation that ORs the clearing-role and non-code-role checks together, ignoring which kind of block
it is, makes `role=output` a blanket exemption for real C++; this file catches it. It also asserts
the SHAPE advisory sidecar (doc/STYLE_GUIDE.md B3) fires on a role=output/role=figure block whose
content looks like code and stays silent on a genuine one.

### `sentence_length` has no baseline entry — so the C2 gate is RED, on purpose

**Read this before you try to make the Documentation job green.** `sentence_length` was added
after the committed `baseline.json` was taken, so **nothing in its slice is grandfathered** and
every one of its findings reports as new. Phase-4 exit gated it anyway
(`--gate 'sentence_length:^C2:'`), which means the blocking step **exits 1** on the whole hard
slice. Measured at 620fdf2c, that slice is exactly **two** findings, both in
`include/boost/capy/when_any.hpp` (`lint/.docstrings/when_any.hpp.adoc`) — a 27-word and a
31-word sentence of the shape *"If at least one child await-returned a zero `ec`, the result
holds …, unless producing the winner's payload threw, in which case that exception is
rethrown."* **Zero `.adoc` fingerprints remain under `^C2:`.**

Those two are **accepted refusals, not defects.** A Phase-4 rewrite that split them made a false
claim against the code and was reverted verbatim; the maintainer's content review carries that
exact text. The maintainer declined an in-source refusal marker and chose **visible debt over new
machinery**, so:

* **Do not** add a suppression mechanism, and **do not** widen or head-trim the gate spec.
* **Do not** reseed `baseline.json` locally — a local run grandfathers ~357 local-vs-CI drift
  fingerprints (see below).
* The fix is the **post-merge `workflow_dispatch` reseed** documented in the next section. Until
  it lands, the Documentation job is red on one step with two known findings.

By contrast the **C4/C9/C10** gates — `Capy.SimpleTense` / `Capy.NoFluff` / `Capy.Terminology`
over *both* surfaces — are **green today and need no reseed**. Their three residual `.adoc`
findings sit inside two verbatim third-party quoted passages and are already in
`baseline.json`, with fingerprints verified to describe those same sentences.

One further consequence of gating this check: a **crash** of `sentence-length.mjs` is now fatal.
It is reported as `SKIPPED` on stderr, and a skip of a *gated* check fails the gate.

---

## Reseeding `baseline.json`

### When you need to

The snapshot goes stale in one direction: when you **fix** findings they disappear from the
scan but stay in the snapshot. Those stale entries are dead grandfather clauses — the finding
can be reintroduced later and the gate will still pass, because the snapshot says it is a
known issue. Reseed when a batch of fixes has landed and you want the gate to start protecting
them.

**Never reseed to make a red gate go green** — *with exactly one pre-authorised exception, the
C2 case described under "`sentence_length` has no baseline entry", above.* Outside that case, a
red gate means something new was introduced: fix that instead. The exception exists because the
C2 gate is red for findings that were never new and were never defects, and grandfathering them
is the ruled resolution rather than a way around a real regression.

### Why not just run `baseline.mjs` locally

Because a local run differs from a CI run by hundreds of fingerprints — a different MrDocs
0.8.0 build hash, `chromium` instead of the runner's `google-chrome`, different
file-processing order, a different `asciidoctor` implementation. Committing that grandfathers
your machine's quirks as if they were the project's backlog. Measured during Phase 3: a local
run adds **357** fingerprints a CI run would not.

So the candidate is authored by CI, in the CI environment, and a human reviews and commits it.

### Prerequisite

GitHub only offers the `workflow_dispatch` trigger for a workflow whose file is present **on
the repository's default branch**. If `.github/workflows/docs.yml` on the default branch has
no `workflow_dispatch:` in its `on:` block, neither the "Run workflow" button nor
`gh workflow run` will find it, and you must merge that change first. Once it is on the
default branch you can dispatch against any ref.

### 1. Run the job

Web UI: **Actions → Documentation → Run workflow →** pick the branch → **Run workflow**.

```bash
gh workflow run docs.yml --ref <branch-you-want-a-baseline-for>
gh run list --workflow=docs.yml    # then: gh run watch <run-id>
```

The run does everything a normal docs run does — including the blocking gate against the
*currently committed* baseline — and then produces a candidate baseline, **only** because you
dispatched it manually. A push or a pull request never produces one; an automatic reseed would
absorb real regressions into the grandfathered backlog.

### 2. Read the report before downloading anything

Open the finished run and read its **Summary** page, under "Candidate
doc/lint/baseline.json". The report ends in a `RESULT:` line; **if it does not say
`candidate retires … none gated`, the reseed step has failed and you must not commit the
file.**

> **The one pre-authorised exception, for the first reseed after the doc-improvement branch
> merges.** The point of that reseed is to grandfather the two accepted `when_any.hpp` C2
> refusals described above — and a gated addition is exactly what the report treats as fatal.
> So the report **will** exit 1 with `RESULT: candidate must not be committed until its gated
> additions are justified`, and it **will** list precisely:
>
> ```
>   - sentence_length :: C2:lint/.docstrings/when_any.hpp.adoc:#1:sentence over 25 words
>   - sentence_length :: C2:lint/.docstrings/when_any.hpp.adoc:#2:sentence over 25 words
> ```
>
> Because that step ends in `exit "$status"` (`.github/workflows/docs.yml:406`), **the
> "report what the candidate would change" step shows as FAILED in the Actions UI.** That is
> expected here and is not an infra problem. **The candidate is still retrievable:** the upload
> step is guarded `if: always()` (`docs.yml:408-414`), so the
> `doc-lint-baseline-candidate` artifact is attached to the run even though the report step went
> red. Download it as usual.
>
> Those **two fingerprints, and nothing else**, are the justification. There is no
> `--allow-gated` flag today; accepting them is a maintainer decision made by hand and recorded
> in the reseed commit message. **Anything else in that list is a real regression — go fix the
> documentation instead.**
>
> **Delete this blockquote once the reseed is committed.** It documents a one-shot transition;
> left in place afterwards it becomes a standing exception to a rule that should have none.

Then check four things, in this order:

1. **A `SKIPPED` cell in the per-check table.** Stop if you see one. A skipped check means a
   tool could not run (most often Ruby `asciidoctor` missing, which breaks the `vale_adoc`
   slice; or a MrDocs cache miss). The candidate would wipe that check's entire grandfathered
   backlog. Fix the environment, re-run, discard this candidate.

   The same applies to a **gated check showing `0` findings** against a non-zero committed
   count: a check that crashes reports zero, and zero looks like success. That is also fatal.
   If a gated backlog has *genuinely* reached zero — a milestone, not an accident — confirm
   the check really ran and re-run the comparator with `--allow-emptied <check>`.

2. **"ADDED fingerprints that the merge gate WOULD have blocked."** If it says `(none — …)`,
   good. Anything listed is either **a real regression** you are about to grandfather away —
   go fix the documentation instead — or **an environment difference**. Do not accept a
   candidate with unexplained entries here; this section is the entire safety mechanism. Two
   checks that settle which it is:

   - **Does the fingerprint's file and rule correspond to something that changed at the ref
     you dispatched?** `git log --oneline <default-branch>..<ref> -- <that file>`. A real
     regression has a change behind it; drift does not.
   - **Re-dispatch against the default branch and see whether the same fingerprint appears.**
     Environment drift reproduces on a ref that contains none of your work. A regression
     introduced at your ref does not.

3. **The ADDED / REMOVED breakdown by check and rule.** ADDED becomes permanently
   grandfathered; additions in ungated slices are tolerable while those slices are still being
   worked down, but read the rule names and confirm they are the classes you expect.

4. **REMOVED is what you came for — so confirm it is what you did.** The rule breakdown should
   match the work that has actually landed since the last reseed. A retirement far larger than
   the work, or spread across rules nobody touched, means a tool linted less than it should
   have rather than that the backlog shrank. (Precedent: a malformed code span once made
   `asciidoctor` swallow real prose and collapsed the linted surface from 1576 alerts to 412,
   with no error anywhere.)

### 3. Accept it

```bash
cd "$(git rev-parse --show-toplevel)"          # paths below are repo-root-relative
gh run download <run-id> -n doc-lint-baseline-candidate -D /tmp/cand
cp /tmp/cand/baseline.json doc/lint/baseline.json
git diff --stat doc/lint/baseline.json
```

The artifact also contains `baseline-diff.txt` (the report you just read) and
`baseline.json.diff` (a `diff -u` against the committed baseline, the only place a single
changed fingerprint is visible verbatim).

Commit on a branch, naming the run that produced it and what the ADDED entries were, so the
next person can audit the decision:

```
ci: reseed doc/lint/baseline.json from CI run <run-id>

Retires <N> stale fingerprints (<breakdown>). Adds <M>, all in non-gated
slices (<breakdown>); no added fingerprint matches the blocking gate spec.
```

Open a PR. The Documentation workflow on that PR runs the gate against your new baseline —
**that run is the real proof.** If the candidate was authored in a drifted environment,
`mrdocs_warnings` (gated as a whole check) red-lines immediately. It fails closed, not open.

### Reading a candidate by hand (diagnosis only)

`baseline-diff.mjs` compares any two snapshots. The CI step derives its `--gate` values from
the blocking step in `.github/workflows/docs.yml` so the two cannot rot apart; when you run it
by hand, copy them from that step — a report run with a stale or missing gate spec prints
`none gated` and means nothing.

```bash
cd doc
node lint/baseline-diff.mjs lint/baseline.json /path/to/candidate.json \
  --gate 'doc_lint:^(A1|A6|B2|D2):' \
  --gate 'vale_adoc:Capy\.PartHeadings$' \
  --gate 'mrdocs_warnings:.*' \
  --gate 'sentence_length:^C2:' \
  --gate 'vale_adoc:(Capy\.SimpleTense|Capy\.NoFluff|Capy\.Terminology)$' \
  --gate 'vale_docstrings:(Capy\.SimpleTense|Capy\.NoFluff|Capy\.Terminology)$'
```

Six specs as of Phase-4 exit. The CI step extracts them, so this hand-run copy is the one that
can rot — check it against the blocking step before trusting a `none gated` result.

Exit 0 means the candidate is explainable (it may still add *ungated* findings — read the
report). Exit 1 means it must not be committed as-is, for one of three reasons, all named in
the `RESULT:` line: a check is `skipped`, a gated check collapsed to zero findings, or an added
fingerprint matches the gate spec.

### Running the checks locally

`vale_adoc` and `vale_docstrings` need an `asciidoctor` on `PATH` (Vale shells out to it for
`.adoc`), and `run-a11y.mjs` needs `doc/build/site` plus a browser:

```bash
cd doc
export PATH="$PWD/node_modules/.bin:$PATH"
node lint/baseline.mjs /tmp/local-snapshot.json      # never commit this as baseline.json
```

`baseline.mjs` output is working-directory-independent, so it is safe to run from anywhere.
