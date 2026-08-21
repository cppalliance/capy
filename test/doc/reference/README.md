# Reference examples

The examples shown in the MrDocs reference live here, as ordinary C++ compiled by
`boost_capy_doc_tests` alongside the page snippets in `test/doc/snippets/`. An
example in the reference is therefore one the build has already checked.

Compilation and rendering are independent. CI compiles these files whether or
not the docs build; the docs build injects them whether or not they compile.
Neither waits on the other, and MrDocs stays a dependency of the docs workflow
only.

## Adding an example

1. Write the example in the file named after the symbol that documents it:

   ```text
   test/doc/reference/<scope>.<kind>.cpp
   ```

   `<scope>` is the symbol's qualified name with `boost::capy::` stripped and
   `::` replaced by `__`; `<kind>` is what MrDocs calls it (`record`,
   `function`, `namespace`, `concept`, `typedef`, `variable`, `enum`). So
   `boost::capy::work_guard`, a class, is `work_guard.record.cpp`, and
   `boost::capy::test::fuse` is `test__fuse.record.cpp`. The name *is* the
   mapping: the transform opens that path, because MrDocs' Lua sandbox has no
   directory listing.

2. Put the code the reference should show inside a tagged region:

   ```cpp
   namespace ex_1 {
   // tag::example[]
   void keep_alive_while_setting_up()
   {
       ...
   }
   // end::example[]
   } // namespace ex_1
   ```

   Only what is between the tags renders. Includes, warning suppressions and
   the namespaces stay outside them, so the reader sees the example and not the
   scaffolding. Each region gets its own namespace inside a file-level
   anonymous one: ten examples across the library define `example()`, and this
   is what keeps them from colliding with each other or across files.

3. Add `@par Example` to the docstring where the example belongs:

   ```text
       @par Example
   ```

   The transform inserts the code directly after that heading, which is how the
   example lands where the docstring says rather than at the bottom. A docstring
   with `@par Example` and no example, and no file to supply one, fails the docs
   build.

## Several examples for one symbol

Add more tagged regions to the same file, each in its own namespace. They are
injected in file order.

## Symbols that share a file

Overload sets and duplicated records can share a `<scope>.<kind>` name. Give
each region a declaration naming the symbols it belongs to:

```cpp
// mrdocs::for buffer_param-0e buffer_param-0c
```

The names are MrDocs anchors, which you can read from the generated reference.
Without this, every symbol sharing the file takes every example in it, and an
overload page shows its siblings' examples.

## Checks

- `boost_capy_doc_tests` compiles these files with `-Wall -Wextra -Werror`.
- The docs workflow asserts the number of injected examples, because a MrDocs
  without the extension installed ignores the script and renders the reference
  with no examples at all while still reporting success.
- Five `@code` blocks remain in headers: MrDocs does not publish those symbols,
  so there is nothing to inject them into.

## Requirements

Injection needs MrDocs' extension API, which is not in any tagged release --
corpus transforms arrived in cppalliance/mrdocs#1196 and script-driven
generators in #1218, both after v0.8.0.

`doc/build_antora.sh` downloads a develop build, copies
`doc/addons/extensions/*.lua` into `<install>/share/mrdocs/addons/extensions/`,
and exports `MRDOCS_ROOT` so the Antora reference extension uses it instead of
downloading its own. Doing it there rather than in CI means every caller gets
it: this repository's docs workflow, the C++ Alliance doc build, and a plain
local run.

Set `MRDOCS_ROOT` yourself to build against a specific MrDocs; the extension is
copied into whatever install is used. Note that setting a `version` for the
reference extension in `doc/local-playbook.yml` makes it reject a local install
and download its own, which silently produces a reference with no examples.
