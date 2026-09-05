# desert-shared

The formats and small code that the Desert **engine** and the Desert **launcher** must understand
identically. Both consume this repository as a **git submodule**. If a change makes the two sides
disagree about a byte on disk or an argument on a command line, it belongs here — nowhere else.

## ⚠ Submodules do NOT update automatically / Сабмодули НЕ обновляются сами

A submodule pointer is **pinned to one commit**. Pushing a new commit to this repository changes
**nothing** in the engine or the launcher: each consumer keeps building the exact commit its
pointer names until someone raises the pointer **by hand, as an explicit commit** in that
consumer's repository:

```bash
cd <consumer-repo>/ThirdParty/desert-shared
git fetch && git checkout <new-sha>
cd - && git add ThirdParty/desert-shared && git commit
```

По-русски и прямо: **обновление этого репозитория не доезжает до движка и лаунчера само.**
Указатель сабмодуля прикреплён к конкретному коммиту; каждый потребитель поднимает его вручную,
отдельным коммитом у себя. Несинхронный подъём безопасен ровно настолько, насколько форматы
миграционно совместимы — поэтому формат меняется **только здесь и только вместе с тестом**.

## What lives here

| Path | Contents |
|---|---|
| `Include/DesertShared/ProjectFormat.hpp` + `Source/ProjectFormat.cpp` | `.deproj` and `projects.json`: the structs ARE the formats (rfl::json reflects member names into the files), one serializer, the standard content-folder census |
| `Include/DesertShared/ResultStr.hpp` | the result type the API is written in (moved verbatim from the engine; namespace stays `Common::`) |
| `Include/DesertShared/LaunchProtocol.hpp` | how the launcher starts the Editor: flag and config-name constants. Exit codes are deliberately NOT here yet — the Editor defines no stable table; formalizing them belongs to the task that moves the spawn to argv (L3) |
| `Tests/project_format_test.cpp` | the conformance suite — gtest, **no `main()`**; every host compiles and runs it in its own test infrastructure |

Planned to move here by later tasks (L2 design §7): `PakFile` (+`PakContentHash`), the
`engines.json` registry format, `VulkanWindow`. They are not here yet; do not cite this README as
evidence they are.

## How a host consumes this repository

This repo carries **no build system and no third parties** — the host supplies both:

1. Add `Include/` to the include path → `#include <DesertShared/ProjectFormat.hpp>`.
2. Compile `Source/ProjectFormat.cpp` into whichever library/binary needs the serializer.
3. Provide on the include path: **reflect-cpp** (`rflcpp/rfl/json.hpp`) and the **fmt** headers
   (the engine takes fmt from its vendored spdlog; a standalone host vendors spdlog or bare fmt).
4. Compile `Tests/project_format_test.cpp` into a gtest runner of your own (it defines no `main`).
   A host that does not run the conformance suite has no contract — only a memory of one.

## Fresh clones and worktrees (engine-repo lesson, paid for)

- A fresh clone of a consumer has an **empty** `ThirdParty/desert-shared` until
  `git submodule update --init ThirdParty/desert-shared` — a recorded gitlink checks out nothing
  by itself, and the build then fails on a missing header that looks like a broken branch.
- Every additional git worktree of a consumer materializes its own copy of initialized
  submodules. This repo is small, so initialize it per worktree; for the engine's big ThirdParty
  tree the house rule remains to repopulate from the shared checkout (rsync) instead — and never
  to commit submodule pointer changes you did not intend
  (`git diff --submodule` before committing).

## Changing a format

1. Change it **here**, with the test that pins the new expectation — in the same commit.
2. On-disk names (JSON field names, census folder names, protocol literals) are contracts with
   every file and binary already out there: renames need a migration story on the consumer side,
   not just a green build.
3. Then raise the submodule pointer in each consumer, explicitly, and run that consumer's full
   suite. Both hosts run the same conformance tests, so drift fails a build instead of a user.
