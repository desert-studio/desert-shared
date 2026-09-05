#pragma once

// The launch protocol between the launcher and the Editor (L2 design §7, D-launch): the fifth
// contract of the same kind as the file formats — about argv, not about files. After the launcher
// moves to its own repository, only these constants (and the tests each host runs against them)
// say how one process starts the other; before this header the flag name was a string literal
// spliced independently on both sides.
//
// Each side's suite asserts its own end of the relation:
//   * the launcher — that the command it composes uses exactly these constants;
//   * the Editor  — that its argument parser accepts kProjectFlag (Tests/Editor/CommandLine);
//   * this repo   — that the constants stay what shipped binaries already expect
//     (Tests/project_format_test.cpp pins the literals).
//
// SCOPE: the constants cover what exists today — the project flag and the two build-configuration
// names the run scripts accept. Editor EXIT CODES are not part of the contract: the Editor does
// not define a stable exit-code table yet, and inventing one here without the Editor honouring it
// would be the exact both-sides-drift this repo exists to end. Formalizing exit codes belongs to
// the task that moves the launcher's spawn from a shell string to argv (L3).

namespace Common::Launch
{
    // `<Editor binary> --project <path/to/.deproj>` — the one flag the launcher passes.
    inline constexpr const char* kProjectFlag = "--project";

    // Build-configuration names as the run scripts (RunEditor.sh / RunEditor.bat) accept them.
    inline constexpr const char* kConfigDebug   = "Debug";
    inline constexpr const char* kConfigRelease = "Release";
} // namespace Common::Launch
