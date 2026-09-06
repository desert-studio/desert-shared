#pragma once

// The ONE definition of the on-disk project formats, shared by every process that touches them:
//
//   * the ENGINE (desert-engine) — Editor and Runtime open a `.deproj` through
//     Engine/Project/ProjectContext and maintain the recent-projects registry
//     `~/.desertengine/projects.json`; the GamePackager regenerates the `.deproj` it ships;
//   * the LAUNCHER (Project Hub, desert-launcher after L3) — creates `.deproj` files, reads and
//     writes the registry.
//
// History, so the shape makes sense: these formats once existed as FOUR independent copies inside
// the engine repo — reflected structs on the reading side, and three hand-spliced JSON writers
// held to them by a comment asking everyone to "keep the field name in sync". A typo on any
// writing side produced a project the engine silently refused to open, and the hand-rolled
// writers could not escape a quote in a project name at all. Now the struct IS the format
// (rfl::json reflects the member names into the file) and every producer and consumer goes
// through the same two functions. Tests/project_format_test.cpp pins the on-disk field names, so
// renaming a member here (which would orphan every .deproj already on disk) fails BOTH hosts'
// suites instead of failing users.
//
// This header knows nothing about any host: no engine constants, no logger, no paths. The engine
// asserts the relation between the folder census below and its own Constants::Path globals in its
// own suite (Desert/Tests/Engine/ProjectFormat).
//
// Host-supplied dependencies: reflect-cpp (the serializer in Source/ProjectFormat.cpp) and the
// fmt headers (via ResultStr.hpp).

// Same-directory include ON PURPOSE: consumers reach this header through host-side redirect
// headers whose projects do not all carry an include path for this repo; a quoted same-dir
// include resolves relative to THIS file and works for every one of them.
#include "ResultStr.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Common::Project
{
    // <project>/<Name>.deproj — the project descriptor. Member names are the file format.
    struct ProjectFile
    {
        std::string Name;
        std::string AssetsRoot   = "Assets";
        std::string DefaultScene = ""; // relative to the project directory; "" = no startup scene
    };

    // ~/.desertengine/projects.json — the recent-projects registry (most recent first, .deproj paths).
    struct ProjectsRegistry
    {
        std::vector<std::string> Projects;
    };

    // The standard content folders every project owns, RELATIVE to its assets root — what a
    // creator scaffolds on disk and the engine re-creates on open. These names are an ON-DISK
    // contract exactly like the field names above: projects already created carry them, so the
    // rows are pinned by the shared tests. The census used to exist as three independent literal
    // lists (the hub's, ProjectContext::Open's, SetProjectRoot's implicit one) and only luck kept
    // them equal; the engine's Clouds/* folders are deliberately absent — they are made on demand
    // by the bake panels that produce their content.
    inline constexpr std::array<std::string_view, 7> StandardContentFolders = {
         "Meshes/", "Materials/", "Textures/", "Scenes/", "Prefabs/", "Scripts/", "Collections/",
    };

    // Serialization — implemented once, over rfl::json, in Source/ProjectFormat.cpp. Readers
    // return the parse error VERBATIM so the caller can say why a file was refused instead of
    // refusing quietly.
    [[nodiscard]] Common::ResultStr<ProjectFile>      ReadProjectFile( const std::string& json );
    [[nodiscard]] std::string                         WriteProjectFile( const ProjectFile& file );
    [[nodiscard]] Common::ResultStr<ProjectsRegistry> ReadProjectsRegistry( const std::string& json );
    [[nodiscard]] std::string                         WriteProjectsRegistry( const ProjectsRegistry& registry );
} // namespace Common::Project
