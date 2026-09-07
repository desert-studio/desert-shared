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
    // The version stamped into a `.deproj` this build writes. Scenes have carried a version since
    // they existed; the descriptor did not, so a field added to it could only ever be detected by
    // its absence — which stops being enough the moment a field changes MEANING rather than
    // appearing. One integer now, bumped when a migration is needed, is the whole mechanism.
    //
    // 1 — Name / AssetsRoot / DefaultScene / Description / EngineVersion.
    inline constexpr int kProjectFileVersion = 1;

    // <project>/<Name>.deproj — the project descriptor. Member names are the file format.
    //
    // Every reader below runs `rfl::DefaultIfMissing`, so a descriptor written before a field
    // existed reads as that field's default rather than as a corrupt file. That is the migration
    // for ADDED fields.
    //
    // FileVersion therefore defaults to 0 and is STAMPED BY THE WRITER, not by this initializer.
    // The two are not interchangeable and the difference is the whole point: a file that predates
    // versioning has no version key, DefaultIfMissing hands back this default, and if that default
    // were `kProjectFileVersion` an unversioned descriptor would arrive claiming to be a current
    // one — the version field would then be unable to detect the exact case it exists for.
    struct ProjectFile
    {
        int         FileVersion = 0; // 0 = written before .deproj carried a version; see above
        std::string Name;
        std::string AssetsRoot   = "Assets";
        std::string DefaultScene = ""; // relative to the project directory; "" = no startup scene
        // Free text shown on the launcher's project tile and on its settings screen. "" = none.
        std::string Description = "";
        // The engine version that last wrote this descriptor, for diagnosis and for the collection
        // compatibility check — NOT for choosing an engine (L2 §2.3 refuses a per-project picker).
        std::string EngineVersion = "";
    };

    // One line of the recent-projects registry.
    //
    // The registry used to be a flat list of paths, and a launcher cannot draw "2 hours ago" from a
    // path. LastOpened is Unix seconds UTC — an integer rather than a formatted date on purpose:
    // both hosts have to parse it, and neither `std::chrono::parse` nor a hand-rolled ISO-8601
    // reader is something two repositories should have to agree on twice.
    //
    // 0 = unknown, which is what every entry migrated from the flat list carries. It is a real
    // state, not a sentinel to be hidden: the launcher shows nothing rather than inventing a date.
    struct ProjectRecord
    {
        std::string Path;           // the .deproj path, verbatim
        long long   LastOpened = 0; // Unix seconds UTC; 0 = never recorded
    };

    // ~/.desertengine/projects.json — the recent-projects registry.
    //
    // ORDER, not LastOpened, is the recency: the vector is most-recent-first and stays that way.
    // The two would be the same fact if every entry had a time, but migrated entries do not, and
    // sorting by a timestamp that is 0 for the whole legacy list would scramble the one piece of
    // ordering information the old format did carry. LastOpened is what the tile SHOWS; position is
    // what the list MEANS.
    //
    // There is no cap. There used to be one, of ten, silently dropping the eleventh — removed in
    // both writers (this registry has two: the launcher and the engine's ProjectContext).
    struct ProjectsRegistry
    {
        int                        FileVersion = 0; // stamped by the writer — see ProjectFile::FileVersion
        std::vector<ProjectRecord> Projects;
    };

    // <engine-root>/Templates/<Id>/template.json — one starter template, as DATA.
    //
    // The launcher used to carry its templates as C++ structs, which made the set of templates a
    // property of the LAUNCHER BUILD rather than of the engine install it launches: adding one
    // meant a recompile, and a template could not carry content because the launcher links no
    // engine code and cannot author a scene. A folder with a manifest, a thumbnail and a
    // byte-copied payload has neither problem.
    //
    // `Id` is the folder name and is deliberately not repeated in the file. The thumbnail is a
    // CONVENTION — Media/Thumbnail.png — not a field: the file is either there or the tile draws
    // its placeholder, and a path field would only add a second way to be wrong.
    struct TemplateManifest
    {
        std::string DisplayName;
        std::string Description = "";
        std::string Category    = ""; // "" = no category tabs; tabs arrive as data, when there are enough
        int         SortKey     = 0;  // ascending; Blank sorts first without being alphabetically first
        std::string DefaultScene = ""; // project-relative, e.g. "Assets/Scenes/Main.desce"; "" = none
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
    [[nodiscard]] Common::ResultStr<ProjectFile> ReadProjectFile( const std::string& json );
    [[nodiscard]] std::string                    WriteProjectFile( const ProjectFile& file );

    // Reads BOTH registry shapes: the current one, and the flat `{"Projects": ["a", "b"]}` written
    // before LastOpened existed. A migrated entry keeps its position — the order was the only
    // recency the old format had — and carries LastOpened = 0. Writing always produces the current
    // shape, so the file migrates the first time anything touches it.
    [[nodiscard]] Common::ResultStr<ProjectsRegistry> ReadProjectsRegistry( const std::string& json );
    [[nodiscard]] std::string                         WriteProjectsRegistry( const ProjectsRegistry& registry );

    [[nodiscard]] Common::ResultStr<TemplateManifest> ReadTemplateManifest( const std::string& json );
    [[nodiscard]] std::string                         WriteTemplateManifest( const TemplateManifest& manifest );

    // Moves `deprojPath` to the front of the list, keeping it unique, and stamps its LastOpened.
    //
    // THE one implementation. This policy used to exist twice — `Hub::PromoteRecent` and
    // `ProjectContext::RegisterRecent` — over a file both processes write, which meant every change
    // to it had to be made in two repositories at once or the second writer would undo the first.
    // That is not hypothetical: both copies carried a silent cap of ten, and lifting only one would
    // have had the engine erase everything the launcher kept. There is no cap here either, and now
    // there is only one place that could grow one.
    //
    // `nowUnixSeconds` is passed in rather than read from the clock so the function is a pure
    // rewrite of a value — the hosts own their clocks, and a test owns its own time.
    void PromoteRecent( ProjectsRegistry& registry, const std::string& deprojPath, long long nowUnixSeconds );

    // Seconds since the epoch, UTC — the one spelling of "now" both hosts stamp LastOpened with.
    [[nodiscard]] long long UnixNow();
} // namespace Common::Project
