# Research: gating docstring `@code` examples

**Status:** research only, no code/config change. Produced during the documentation
improvement work, Task 5.

## Problem

Header docstrings under `include/boost/capy/**` contain hand-typed `@code`/`@par Example`
blocks. No gate compiles them today. `test/doc/CMakeLists.txt` (target
`boost_capy_doc_tests`, run via `./b2 libs/capy/test`) compiles the `.adoc` pages' examples —
commit `aa1a38c7` ("docs: compile every documentation code block") converted all ~480
hand-typed blocks on the 45 `.adoc` pages into compiled includes precisely because a hand-typed
example can silently rot. Docstring `@code` blocks were out of scope for that commit and remain
unprotected.

## Inventory

```
$ grep -rlE '@code|@par Example' include/boost/capy/ | wc -l
52
$ grep -roE '@endcode' -r include/boost/capy/ | wc -l
103
```

52 files, 103 `@code`/`@endcode` block pairs (`@par Example` headings always wrap a `@code`
block in this codebase; no bare-prose "Example" sections exist without one). For scale: the
`.adoc`-page effort in `aa1a38c7` covered ~480 blocks across 45 pages; this corpus is ~21% of
that by block count, spread across 52 files instead of 45 pages.

Representative blocks (chosen to show the range, not cherry-picked for compilability):

```cpp
// include/boost/capy/when_any.hpp
@code
task<void> example()
{
    std::vector<io_task<size_t>> reads;
    for (auto& buf : buffers)
        reads.push_back(stream.read_some(buf));
    auto result = co_await when_any(std::move(reads));
    ...
}
@endcode
```

```cpp
// include/boost/capy/ex/strand.hpp
@code
thread_pool pool(4);
strand strand(pool.get_executor());  // CTAD deduces the executor type
continuation c1{h1}, c2{h2}, c3{h3};
strand.post(c1);
...
@endcode
```

```cpp
// include/boost/capy/ex/run_async.hpp
@code
// Correct usage - wrapper is temporary
run_async(ex)(my_task());

// Compile error - cannot call operator() on lvalue
auto w = run_async(ex);
w(my_task());  // Error: operator() requires rvalue
@endcode
```

Two findings that size the effort:

1. **Almost none of the 103 blocks are self-contained.** `stream`, `buffers`, `h1`/`h2`/`h3`
   are never declared in scope; the blocks are teaching fragments, not compilable programs.
   Bringing them under a compile gate needs per-block scaffolding (declare the ambient names),
   the same kind of work `test/doc/snippets/*.cpp` already does for the `.adoc` pages.
2. **Some blocks are intentionally non-compiling** (the `run_async.hpp` example above
   demonstrates a *rejected* overload on purpose). `aa1a38c7` hit the identical problem on the
   `.adoc` side and solved it with an explicit exemption convention: `role=pseudocode` (21
   blocks) and `role=external` (13 blocks). Doxygen `@code` has no attribute slot equivalent to
   an Asciidoc role, so an equivalent marker would have to be invented (e.g. a sentinel first
   line inside the block, stripped before compiling).

## MrDocs version actually in use

MrDocs is not a standalone install; `@cppalliance/antora-cpp-reference-extension` downloads and
runs it. `doc/lint/mrdocs-warnings.mjs` (built in Task 2) is the existing tool that invokes the
binary directly, mirroring the extension's own resolution: check `PATH`, else search
`~/.cache/antora/reference-collector/mrdocs/<platform>/<tag>/bin/mrdocs`.

Two tags are cached locally (`master` and `develop`); the script's cache search (DFS, PATH
directories then the cache tree) resolves to **`master`**. Running it directly:

```
$ /home/michael/.cache/antora/reference-collector/mrdocs/linux/master/bin/mrdocs --version
MrDocs version 0.8.0+e9f847d8acfd
Built with LLVM 22.0.0git
Build SHA: e9f847d8acfd0a5d8381d44685d8779414a17437
Target: x86_64-unknown-linux-gnu
```

Confirmed this is also the binary `mrdocs-warnings.mjs` runs in practice: invoking the script
produced 216 findings against this same binary/config.

**Pinned version: MrDocs 0.8.0 (build `e9f847d8acfd`, matches upstream tag `v0.8.0`, released
2025-10-30).** Feasibility below is evaluated against this build's actual shipped headers and
`--help` output, not the newest upstream commit.

## Options evaluated

### (a) MrDocs `@snippet`/include directive pulling from a compiled `test/doc/` source

**Not feasible at the pinned version — the command does not exist.**

- `mrdocs --help` (run against the pinned binary) lists every CLI flag; nothing resembling
  `--snippet` or file inclusion for doc comments.
- The doc-comment block-command surface is a closed, exhaustively enumerated set, straight from
  the shipped header (`include/mrdocs/Metadata/DocComment/Block/BlockKind.hpp` in the cached
  install):
  `Admonition, Brief, Code, Heading, Paragraph, List, DefinitionList, Quote, ThematicBreak,
  FootnoteDefinition, Table, Math, Param, Postcondition, Precondition, Returns, See, Throws,
  TParam`. No `Snippet` or `Include` kind.
- `CodeBlock.hpp` confirms `@code`/`@endcode` stores only a `literal` string captured verbatim
  from the comment — there is no file-reference field.
- Upstream docs corroborate this design intent, not just the shipped binary: the MrDocs
  `commands/blocks.adoc` page (cppalliance/mrdocs, `docs/modules/ROOT/pages/commands/blocks.adoc`)
  states plainly: "A code block reproduces source verbatim. Fence it with `@code` and
  `@endcode`... `@verbatim`/`@endverbatim` are the same idea without highlighting." No mention
  of file inclusion anywhere in the command reference (`commands/reference.adoc`).
- Confirmed independently via GitHub issue search: **cppalliance/mrdocs#620**, "Compile code
  snippets in javadocs," opened 2024-06-05, still **open**, last comment 2025-12-16 from the
  maintainer (`alandefreitas`) comparing it to how Rustdoc concatenates all snippets into one
  file for compilation — i.e. upstream has thought about exactly this feature and has not
  built it. It's tracked under an umbrella issue (`#1113`, "Explore unknowns," last updated
  2026-06-25 — active).

Effort/round-trip are moot: the mechanism this option assumes does not exist in the pinned
MrDocs.

### (b) A preprocessor that extracts `@code` blocks into a generated TU compiled in CI

**Feasible, but real effort — comparable in kind to `aa1a38c7`'s `.adoc` work, scaled to ~21%
of its block count.**

- Extraction itself is cheap and has working prior art in this repo:
  `doc/lint/extract-docstrings.mjs` already isolates `@code`/`@endcode` spans with
  `raw.replace(/@code\b[\s\S]*?@endcode\b/g, '')` (it currently *discards* them for the Vale
  prose gate — the inverse of what a compile gate needs, but the extraction regex is a known
  starting point).
- The hard cost is not extraction, it's compilability. As shown in Inventory, most blocks
  reference ambient names not declared in the block. Each needs a small scaffold (declare
  `stream`, `buffers`, etc.) — the same category of work `test/doc/snippets/*.cpp` already does
  for `.adoc` pages, block-by-block, not a bulk automatable transform.
- Non-compiling-by-design blocks (at least the `run_async.hpp` case, likely more) need an
  exemption convention. `aa1a38c7` solved this with Asciidoc `role=pseudocode` /
  `role=external`, an attribute slot `@code` doesn't have; a docstring-side equivalent has to be
  invented and is not a drop-in port of that convention.
- **Round-trip is clean only if designed carefully.** Since MrDocs cannot pull `@code` content
  from a file (per option a), the header's hand-typed text stays the single rendered source of
  truth; a gate can only *verify* that text (by wrapping the literal block in a scaffold and
  compiling it), not *generate* it from an external snippet. That's an achievable design (no
  divergence between what's rendered and what's compiled, because they're the same literal
  text) but it means the CI target compiles ad hoc per-block scaffolding, not the tidy
  `include::example$...[tag=...]` mechanism used on the `.adoc` side.
- Sizing: `aa1a38c7` converted ~480 blocks across 45 pages (a dedicated commit/task). This
  corpus is 103 blocks across 52 files — meaningfully smaller by count, but each block still
  needs individual scaffolding judgment, so it is not scaled down proportionally in time; the
  need to invent a new non-compiling-block convention (not portable from Asciidoc roles) adds
  further one-off design cost this task did not have to pay.

### (c) Rely on `doc-sync` to catch drift at change-time, no standing gate

**Already partially wired, weaker than a compiler check, no marginal implementation cost.**

`doc/prompts/doc-sync.md` (Step 1) makes the co-located docstring (`own_docstring` hit) a
**mandatory, unconditional** hit for every changed public symbol — it is always inspected, not
opt-in. Step 4 (Verify) requires, for `edit_kind=docstring`: "the header still compiles and
every `@param` name still matches the declaration."

This is real protection, but it does not compile the `@code` example itself — comments are
preprocessed away before the header compiles, so "the header still compiles" says nothing about
whether the example body inside the comment is still valid C++. The check is a human/agent
review comparing the example's prose against the new declaration, not a compiler-verified fact.
It catches signature drift (a renamed parameter, a changed return type) reliably because that's
literally what Step 4 asks the reviewer to check; it does not catch e.g. a valid-looking example
that no longer compiles for a subtler reason (removed overload, changed constraint) unless the
reviewing agent happens to notice.

### (d) MrDocs-native example verification, if the pinned version supports it

**Not supported — same evidence as (a).** `--help` has no `--warn-example`/`--check-example`/
similar flag; the Warnings section of `mrdocs --help` is exhaustively: `--concurrency`,
`--ignore-failures`, `--ignore-map-errors`, `--log-level`, `--report`, `--verbose`,
`--warn-as-error`, `--warn-broken-ref`, `--warn-if-doc-error`, `--warn-if-undoc-enum-val`,
`--warn-if-undocumented`, `--warn-no-paramdoc`, `--warn-unnamed-param`, `--warnings` — nothing
inspects `@code` content. This collapses into the same open upstream issue (#620) as option (a).

## Recommendation: DEFER

Do not build a standing compile gate for docstring `@code` blocks in this phase.

**Why:** Options (a) and (d) — the two that would give a clean, low-maintenance round-trip —
are blocked on a MrDocs feature that does not exist at the pinned version and is an
18-month-old open upstream issue with no committed timeline. Option (b) is feasible but is a
real, separately-sized effort (bespoke per-block scaffolding for ~103 non-self-contained
examples, plus inventing a non-compiling-block convention `aa1a38c7` didn't have to invent for
Asciidoc) — disproportionate to bolt on inside a Phase 0 *research* task, and better sized as
its own follow-up once scoped concretely.

**Interim protection (option c), already in place, no new work:** `doc-sync`'s mandatory
`own_docstring` hit plus its Step 4 requirement that a docstring edit's `@param`s still match
the new declaration. This catches the drift class that matters most (signature/behavior drift
following a code change) at PR time, via human/agent review — just not via a compiler.

**Concrete follow-up, if/when taken up** (sketch only, not implemented here):

- A new script, e.g. `doc/lint/check-docstring-examples.mjs`, reusing
  `extract-docstrings.mjs`'s `@code`/`@endcode` extraction regex (inverted: keep the code,
  not discard it).
- An exemption marker for intentionally non-compiling blocks — e.g. a sentinel first line
  (`// doc-example: no-compile`) inside the block, stripped before compiling, analogous in
  intent to `aa1a38c7`'s `role=pseudocode`/`role=external` but expressed in a form `@code` can
  carry.
- Per-block scaffold files under a new `test/doc/docstrings/<header>/<n>.cpp` tree (mirroring
  `test/doc/snippets/`), authored by hand the first time each block is touched — realistically
  folded into the Phase 1 "Structure" pass that is already rewriting each header's docstrings
  file-by-file, rather than done as one big upfront sweep.
- A new CMake target (`boost_capy_docstring_doc_tests`, modeled on `test/doc/CMakeLists.txt`'s
  `boost_capy_doc_tests`) added to `tests` once a meaningful fraction of the 52 files have
  scaffolds.

**Revisit triggers:**

1. cppalliance/mrdocs#620 ("Compile code snippets in javadocs") or its umbrella #1113 ships
   native `@snippet`/example-compile support — re-run this research against the new version;
   options (a)/(d) would likely become the better long-term answer at that point.
2. A docstring `@code` example is found broken in review despite doc-sync running — evidence
   the interim protection (c) is insufficient in practice, not just in theory.
3. Phase 1's per-file Structure pass finishes touching all 52 headers — natural checkpoint to
   size a dedicated follow-up task, since every docstring will have just been freshly rewritten
   and reviewed anyway.

## Audit evidence for revisit trigger #2 (added post-hoc, DEFER still stands)

A later reference-surface audit of the docstrings exercised trigger #2 above:
it read all 103 blocks and, for several, actually compiled the corrected form with
`g++ -std=c++20 -fsyntax-only`. Result: **12 of the 103 blocks were broken** — about 12%,
not a hypothetical risk. Two of the twelve were not drift but active misinformation:

- `run_async_wrapper`'s `@warning` and `test::run_blocking_wrapper`'s copy of it both asserted
  that storing the wrapper (`auto w = run_async(ex);`) "does not compile." Compiled and run:
  it does compile — C++17 guaranteed copy elision constructs `w` directly from the prvalue, so
  the deleted copy/move constructors are never considered. What actually fails to compile is
  calling through the stored lvalue (`w(my_task())`); `std::move(w)(my_task())` compiles and
  runs. Both docstrings have been corrected to state the real guarantee — the rvalue
  ref-qualifier rejects calls on an lvalue — instead of the false one.
- The other ten were compile failures or semantic drift from the API they document: a context
  passed where an `Executor` is required (`work_guard`), a CTAD default that silently produces
  the wrong buffer const-ness (`buffer_param`'s Virtual Interface Pattern), a closure passed
  where `post` requires a `continuation&` (`ExecutionContext`), a duplicate function definition
  in one TU (`async_mutex`), a comment-only `if` body followed by `else` (`cond`, plain syntax
  error), `.data()` called on a value that has no such member for the sequence concepts in play
  (`test::buffer_to_string`), symbols (`route_params`, `route::next`) that exist in none of
  capy, corosio, or burl (`io_task`), a mock object constructed outside the `fuse::armed` retry
  loop so state leaked across rounds and the trailing comment went stale (`test::stream`,
  `test::write_stream`), a discarded partial-write result under a concept that documents
  partial writes as normal (`Stream`), and a redeclared local plus undeclared placeholder names
  (`any_read_stream`, `any_write_stream`).

None of this changes the recommendation. The DEFER rationale was about mechanism (no MrDocs
snippet-compile support, bespoke per-block scaffolding, a non-compiling-block convention to
invent) — a real defect rate does not make that mechanism appear. What it does is retire the
"reasonable without evidence" caveat the original recommendation carried: the deferred risk
was real, not just plausible, and this pass fixed the twelve found so far by hand rather than
by gate. If a future pass finds a similar rate again, that is the point to weigh option (b)'s
cost against a third hand-fix pass instead of re-deriving this analysis from scratch.

Not fixed as part of this pass, and worth folding into the next one: `any_stream.hpp`'s
example has the same redeclared-local/undeclared-placeholder shape as `any_read_stream` and
`any_write_stream` above, but it was outside the audited list (R2 covers 12 blocks, not this
one) and so was left alone here to keep the fixed count matched to the audited evidence.
