#pragma once

// `~/.desertengine/engines.json` — which engines are installed on this machine, and where.
//
// The direction of this file is the point, and it is the opposite of every other shared format:
// the ENGINE writes it (once, at Editor startup, registering its own root and version) and the
// LAUNCHER reads it. Nothing else may write it.
//
// Why it has to exist even with a single engine installed: the launcher's only route to an engine
// today is the `DESERT_ROOT` environment variable, exported by a run script that lives in the
// ENGINE's repository. After the launcher moves to its own repository (L3) that script is not its
// script and that variable is not set for it — a launcher started from a `.app` would have no way
// at all to find the engine it is supposed to start. A registry file is that way.
//
// It is deliberately NOT an engine PICKER (L2 §2.3 refuses one): with one entry a picker is a
// control with no choice behind it. The launcher uses the newest entry and shows which; a picker
// arrives when a real machine has two installs.
//
// Same-directory include for the same reason as ProjectFormat.hpp — see the note there.
#include "ResultStr.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Common::Engine
{
    // Bumped when a field changes meaning. Readers run `rfl::DefaultIfMissing`, so a field merely
    // ADDED needs no bump: an older file reads with that field's default.
    inline constexpr int kEngineRegistryVersion = 1;

    // One installed engine. Keyed by Root: registering the same root twice updates the entry in
    // place rather than growing the list, which matters because the Editor registers on EVERY
    // start and a developer's tree is one root opened a thousand times.
    struct EngineInstall
    {
        std::string Root;          // absolute path to the engine root (the folder holding Templates/)
        std::string VersionFull;   // display version, e.g. "0.1.316"
        // BUILD NUMBER, AND IT IS OPTIONAL ON PURPOSE. It is the ordering key when two installs share
        // a version — and 0 must never stand in for "not known". The engine emits no commit count at
        // all when git cannot be trusted (a shallow clone, an archive with no history), and a zero
        // there would silently lose every comparison it was never entered into: the install would sort
        // last and a real engine would be passed over for one nobody can identify. Absent means absent.
        std::optional<int> CommitCount;
    };

    struct EngineRegistry
    {
        int                        FileVersion = 0; // stamped by WriteEngineRegistry, never by this default
        std::vector<EngineInstall> Engines;
    };

    [[nodiscard]] Common::ResultStr<EngineRegistry> ReadEngineRegistry( const std::string& json );
    [[nodiscard]] std::string                       WriteEngineRegistry( const EngineRegistry& registry );

    // Inserts or updates `install` by Root, then orders the list newest-build-first. Both hosts go
    // through this instead of writing the vector by hand: "register my root" is the whole of what
    // the engine does to this file, and duplicate-root growth is exactly the defect a second
    // hand-rolled copy of it would reintroduce.
    void RegisterInstall( EngineRegistry& registry, const EngineInstall& install );

    // The engine the launcher will use: the highest CommitCount. nullptr when the registry is
    // empty — a state the launcher has to draw, not a failure to hide.
    [[nodiscard]] const EngineInstall* PreferredInstall( const EngineRegistry& registry );
} // namespace Common::Engine
