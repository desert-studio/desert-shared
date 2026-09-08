// The RELATION under test: what one process writes about a project, another process reads back
// unchanged. The engine and the launcher live in different repositories and share only this
// submodule, so this file is the replacement for the verbal "keep the field names in sync"
// agreement — BOTH hosts compile and run it in their own suites (no main() here; the host's
// runner provides one).
//
// Two axes of drift are pinned:
//   1. producer vs consumer — round trips through the ONE serializer both sides call;
//   2. binary vs history — golden JSON literals and pinned census rows / protocol literals: a
//      .deproj written last month must still open, a project scaffolded last month must still
//      have the folders the engine looks for, and a launcher built last month must still call the
//      flag today's Editor parses. Renaming any of them turns this file red in both hosts
//      instead of orphaning what is already on disk.

#include <gtest/gtest.h>

#include <DesertShared/EngineRegistry.hpp>
#include <DesertShared/LaunchProtocol.hpp>
#include <DesertShared/ProjectFormat.hpp>

#include <string>

using namespace Common::Project;

namespace
{
    ProjectFile ReadOk( const std::string& json )
    {
        auto result = ReadProjectFile( json );
        EXPECT_TRUE( result.IsSuccess() ) << "expected a parse, got: " << result.GetError();
        return result.ExtractValue();
    }
} // namespace

// ── producer vs consumer ─────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, WhatTheLauncherWritesTheEngineReads )
{
    // Exactly what the launcher's New Project flow produces, including the characters the old
    // string-splice could not survive: a quote and a backslash in the project name.
    ProjectFile written;
    written.Name          = "Neo \"Dune\" \\ 2";
    written.DefaultScene  = written.AssetsRoot + "/Scenes/Main.desce";
    written.Description   = "Third-person prototype. Sand dunes, one vehicle, no HUD yet.";
    written.EngineVersion = "0.1.316+db43fdb";

    const ProjectFile read = ReadOk( WriteProjectFile( written ) );
    EXPECT_EQ( read.FileVersion, kProjectFileVersion ); // stamped on the way out, not copied
    EXPECT_EQ( read.Name, written.Name );
    EXPECT_EQ( read.AssetsRoot, written.AssetsRoot );
    EXPECT_EQ( read.DefaultScene, written.DefaultScene );
    EXPECT_EQ( read.Description, written.Description );
    EXPECT_EQ( read.EngineVersion, written.EngineVersion );
}

TEST( ProjectFormat, RegistryRoundTripsAWindowsPathAndTheTimeBesideIt )
{
    // The launcher's old reader was a naive quoted-string scanner: it returned the JSON escapes
    // RAW, so every backslash the Editor had escaped came back doubled and the path pointed
    // nowhere.
    ProjectsRegistry written;
    written.Projects = { { "C:\\Users\\dev\\My Game\\MyGame.deproj", 1757203200 },
                         { "/Users/dev/Other/Other.deproj", 0 } };

    auto read = ReadProjectsRegistry( WriteProjectsRegistry( written ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    ASSERT_EQ( read.GetValue().Projects.size(), written.Projects.size() );
    for ( size_t i = 0; i < written.Projects.size(); ++i )
    {
        EXPECT_EQ( read.GetValue().Projects[i].Path, written.Projects[i].Path );
        EXPECT_EQ( read.GetValue().Projects[i].LastOpened, written.Projects[i].LastOpened )
             << "the time the tile draws did not survive the file";
    }
}

// ── migrations: what is already on this machine must still open ──────────────────────────────────

TEST( ProjectFormat, ADescriptorWrittenBeforeTheNewFieldsExistedStillOpens )
{
    // Byte-for-byte what every `.deproj` on disk before this change contains. It has no
    // FileVersion, no Description and no EngineVersion, and refusing it would have made this
    // change delete every existing project from every launcher and editor at once.
    const ProjectFile read =
         ReadOk( R"({"Name":"MyGame","AssetsRoot":"Assets","DefaultScene":"Assets/Scenes/MyGame.desce"})" );
    EXPECT_EQ( read.Name, "MyGame" );
    EXPECT_EQ( read.DefaultScene, "Assets/Scenes/MyGame.desce" );
    // 0, not kProjectFileVersion: the file genuinely predates versioning and must not claim to be
    // something this build wrote. Everything else defaults to "absent", which is what it is.
    EXPECT_EQ( read.FileVersion, 0 ) << "an unversioned descriptor is claiming a version it never had";
    EXPECT_EQ( read.Description, "" );
    EXPECT_EQ( read.EngineVersion, "" );
}

TEST( ProjectFormat, ARegistryFromBeforeLastOpenedKeepsEveryProjectAndItsOrder )
{
    // The live `~/.desertengine/projects.json` on every machine that has ever run this engine is a
    // flat array of strings. Position was the ONLY recency it carried, so the migration has to
    // preserve it exactly — and must not invent a timestamp it does not have.
    auto read = ReadProjectsRegistry(
         R"({"Projects":["/a/A.deproj","C:\\dev\\B\\B.deproj","/c/C.deproj"]})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    const ProjectsRegistry migrated = read.ExtractValue();
    ASSERT_EQ( migrated.Projects.size(), 3u ) << "the migration lost entries";
    EXPECT_EQ( migrated.Projects[0].Path, "/a/A.deproj" );
    EXPECT_EQ( migrated.Projects[1].Path, "C:\\dev\\B\\B.deproj" ) << "escapes were not unescaped";
    EXPECT_EQ( migrated.Projects[2].Path, "/c/C.deproj" );
    for ( const ProjectRecord& record : migrated.Projects )
        EXPECT_EQ( record.LastOpened, 0 ) << "a time was invented for an entry that never had one";
}

TEST( ProjectFormat, ARegistryThatIsNeitherShapeIsRefusedInTheCurrentFormatsWords )
{
    // The legacy attempt must not become a way to swallow a genuinely broken file, and the reason
    // the user is shown must be about the format they are actually running.
    auto read = ReadProjectsRegistry( R"({"Projects":{"not":"a list"}})" );
    ASSERT_FALSE( read.IsSuccess() );
    EXPECT_NE( read.GetError().find( "projects.json" ), std::string::npos ) << read.GetError();
}

// ── binary vs history ────────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, WriterEmitsThePinnedFieldNames )
{
    const std::string json = WriteProjectFile( ProjectFile{} );
    for ( const char* field : { "\"FileVersion\"", "\"Name\"", "\"AssetsRoot\"", "\"DefaultScene\"",
                                "\"Description\"", "\"EngineVersion\"" } )
        EXPECT_NE( json.find( field ), std::string::npos ) << field << " is missing from " << json;
}

TEST( ProjectFormat, RegistryFieldNamesArePinned )
{
    const std::string json = WriteProjectsRegistry( ProjectsRegistry{ 1, { { "/a/A.deproj", 17 } } } );
    for ( const char* field : { "\"Projects\"", "\"Path\"", "\"LastOpened\"" } )
        EXPECT_NE( json.find( field ), std::string::npos ) << field << " is missing from " << json;

    // And the writer's output is what the reader takes — the loop above only proves the spelling.
    auto read = ReadProjectsRegistry( json );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    ASSERT_EQ( read.GetValue().Projects.size(), 1u );
    EXPECT_EQ( read.GetValue().Projects[0].Path, "/a/A.deproj" );
    EXPECT_EQ( read.GetValue().Projects[0].LastOpened, 17 );
}

// ── K11: a key this build does not declare is not this build's to delete ─────────────────────────
//
// The relation, once, for every file this header describes: A FILE READ AND WRITTEN BACK WITH NO
// CHANGE IS BYTE-IDENTICAL TO WHAT WAS READ. It is one assertion and it closes the whole class,
// because the only way to break it is for the writer to enumerate something other than the file —
// which is precisely what `rfl::json::write` on a fixed struct does, and what ForeignKeys repairs.
//
// Why these files and not just any: `.deproj` is TRACKED BY GIT, so a stripped key travels to the
// whole team in a commit; and the registries have TWO WRITERS IN TWO REPOSITORIES, so the two
// builds cannot be kept in step by one release. A git revert to last week's engine was enough.

TEST( ProjectFormatForeignKeys, ADescriptorReadAndWrittenBackUnchangedIsByteIdentical )
{
    // Written by "a newer build": three keys this one has never heard of, at three different JSON
    // shapes, because a scalar surviving proves nothing about an object or an array.
    const std::string fromTheFuture =
         R"({"FileVersion":1,"Name":"Dune Racer","AssetsRoot":"Assets","DefaultScene":"","Description":"",)"
         R"("EngineVersion":"","PrimaryPlatform":"Switch","Collections":["A","B"],)"
         R"("Packaging":{"Compress":true,"Level":9}})";

    auto parsed = ReadProjectFile( fromTheFuture );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    EXPECT_EQ( WriteProjectFile( parsed.GetValue() ), fromTheFuture )
         << "reading and writing back with no change altered the file";
}

TEST( ProjectFormatForeignKeys, AnEditToOneFieldLeavesEveryOtherKeyAloneIncludingTheOnesThisBuildCannotName )
{
    const std::string fromTheFuture =
         R"({"FileVersion":1,"Name":"Old","AssetsRoot":"Assets","DefaultScene":"","Description":"",)"
         R"("EngineVersion":"","PrimaryPlatform":"Switch"})";

    auto parsed = ReadProjectFile( fromTheFuture );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    ProjectFile edited = parsed.ExtractValue();
    edited.Name        = "New"; // exactly what the launcher's settings screen does

    const std::string written = WriteProjectFile( edited );
    EXPECT_NE( written.find( R"("Name":"New")" ), std::string::npos ) << written;
    EXPECT_NE( written.find( R"("PrimaryPlatform":"Switch")" ), std::string::npos )
         << "a key the writer does not declare was deleted by an edit to an unrelated field: " << written;
}

TEST( ProjectFormatForeignKeys, TheCarrierIsNeverAKeYInTheFile )
{
    // ExtraFields is spread FLAT at the struct's own level. If it ever serialized as a member, the
    // file would grow a key called "UnknownKeys" and every consumer would inherit it as a field.
    EXPECT_EQ( WriteProjectFile( ProjectFile{} ).find( "UnknownKeys" ), std::string::npos )
         << WriteProjectFile( ProjectFile{} );
    EXPECT_EQ( WriteProjectsRegistry( ProjectsRegistry{} ).find( "UnknownKeys" ), std::string::npos )
         << WriteProjectsRegistry( ProjectsRegistry{} );
}

TEST( ProjectFormatForeignKeys, TheRegistryKeepsForeignKeysAtBothItsLevels )
{
    // Two levels, because the two writers can disagree at either: the registry itself (a launcher
    // that starts remembering a window layout) and a single record (a build that starts pinning
    // projects). Both must survive the OTHER program's next write.
    const std::string fromTheFuture =
         R"({"FileVersion":1,"Projects":[{"Path":"/a/A.deproj","LastOpened":17,"Pinned":true}],)"
         R"("LastUsedTemplate":"Blank"})";

    auto parsed = ReadProjectsRegistry( fromTheFuture );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    ASSERT_EQ( parsed.GetValue().Projects.size(), 1u );
    EXPECT_EQ( WriteProjectsRegistry( parsed.GetValue() ), fromTheFuture )
         << "a foreign key was dropped from the registry or from one of its records";

    // And the ordinary mutation both hosts perform must not lose them either.
    ProjectsRegistry promoted = parsed.ExtractValue();
    PromoteRecent( promoted, "/b/B.deproj", 99 );
    const std::string written = WriteProjectsRegistry( promoted );
    EXPECT_NE( written.find( R"("Pinned":true)" ), std::string::npos ) << written;
    EXPECT_NE( written.find( R"("LastUsedTemplate":"Blank")" ), std::string::npos ) << written;
}

TEST( ProjectFormatForeignKeys, ARegistryMigratedFromTheFlatListCarriesNoLeftovers )
{
    // The legacy shape has exactly one key and the migration builds a fresh registry from it, so
    // there is nothing to preserve — asserted rather than assumed, because a carrier that picked up
    // "Projects" from the old file would write the old list back beside the new one.
    auto migrated = ReadProjectsRegistry( R"({"Projects":["/p/A.deproj"]})" );
    ASSERT_TRUE( migrated.IsSuccess() ) << migrated.GetError();
    const std::string written = WriteProjectsRegistry( migrated.GetValue() );
    EXPECT_NE( written.find( R"("Path":"/p/A.deproj")" ), std::string::npos ) << written;
    EXPECT_EQ( written.find( R"("Projects":["/p/A.deproj"])" ), std::string::npos )
         << "the flat list survived alongside the migrated one: " << written;
}

TEST( ProjectFormat, TheWriterStampsTheVersionAndTheStructDefaultMeansUnversioned )
{
    // The relation this pins is between the two meanings of the field, which a single default
    // cannot carry: 0 in the struct is "this file predates versioning", and the CURRENT version is
    // what the writer puts on disk regardless of what the producer left in the struct.
    EXPECT_EQ( ProjectFile{}.FileVersion, 0 )
         << "the struct default is claiming a version, so a file that has none would inherit it";
    EXPECT_EQ( ReadOk( WriteProjectFile( ProjectFile{} ) ).FileVersion, kProjectFileVersion )
         << "the writer did not stamp the version";

    // A producer that sets it wrong cannot win: the writer is the single site.
    ProjectFile lying;
    lying.FileVersion = 99;
    EXPECT_EQ( ReadOk( WriteProjectFile( lying ) ).FileVersion, kProjectFileVersion );

    auto registry = ReadProjectsRegistry( WriteProjectsRegistry( ProjectsRegistry{} ) );
    ASSERT_TRUE( registry.IsSuccess() ) << registry.GetError();
    EXPECT_EQ( registry.GetValue().FileVersion, kProjectFileVersion );
}

// ── the recent-list policy, which both hosts now run instead of each owning a copy ───────────────

TEST( RecentList, TheListIsNotTruncated )
{
    // The cap of ten was silent: the eleventh project simply stopped existing, with no message. It
    // lived in TWO places — the launcher's PromoteRecent and the engine's RegisterRecent — over one
    // file, so lifting only one would have had the engine erase what the launcher kept. There is
    // one place now, and this is it.
    ProjectsRegistry registry;
    for ( int i = 0; i < 25; ++i )
        PromoteRecent( registry, "/p/P" + std::to_string( i ) + ".deproj", 1000 + i );
    ASSERT_EQ( registry.Projects.size(), 25u ) << "an entry was dropped without anyone being told";
    EXPECT_EQ( registry.Projects.front().Path, "/p/P24.deproj" ) << "most recent first";
    EXPECT_EQ( registry.Projects.back().Path, "/p/P0.deproj" );
}

TEST( RecentList, ReopeningAProjectMovesItAndRestampsItWithoutDuplicatingIt )
{
    ProjectsRegistry registry;
    registry.Projects = { { "/p/A.deproj", 100 }, { "/p/B.deproj", 200 }, { "/p/C.deproj", 300 } };
    PromoteRecent( registry, "/p/C.deproj", 999 );

    ASSERT_EQ( registry.Projects.size(), 3u ) << "the entry was added instead of moved";
    EXPECT_EQ( registry.Projects[0].Path, "/p/C.deproj" );
    EXPECT_EQ( registry.Projects[0].LastOpened, 999 ) << "the tile would still show the old time";
    EXPECT_EQ( registry.Projects[1].Path, "/p/A.deproj" );
    EXPECT_EQ( registry.Projects[2].Path, "/p/B.deproj" );
    EXPECT_EQ( registry.Projects[1].LastOpened, 100 ) << "an untouched entry was restamped";
}

TEST( RecentList, PromotingAnEntryMigratedFromTheFlatListGivesItATimeAtLast )
{
    // The path a real machine takes: a registry from before LastOpened, opened once. That one entry
    // gains a time; the others keep 0 and go on showing nothing until they are opened too.
    auto read = ReadProjectsRegistry( R"({"Projects":["/a/A.deproj","/b/B.deproj"]})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    ProjectsRegistry registry = read.ExtractValue();

    PromoteRecent( registry, "/b/B.deproj", 1757203200 );
    ASSERT_EQ( registry.Projects.size(), 2u );
    EXPECT_EQ( registry.Projects[0].Path, "/b/B.deproj" );
    EXPECT_EQ( registry.Projects[0].LastOpened, 1757203200 );
    EXPECT_EQ( registry.Projects[1].LastOpened, 0 ) << "an entry nobody opened acquired a time";
}

// ── templates as data ────────────────────────────────────────────────────────────────────────────

TEST( TemplateManifest, WhatAnAuthorWritesInAFolderIsWhatTheLauncherShows )
{
    const std::string authored = R"({
        "DisplayName": "First Person",
        "Description": "A floor, a light and a player entity with a camera, driven by a Lua controller.",
        "Category": "Gameplay",
        "SortKey": 10,
        "DefaultScene": "Assets/Scenes/Main.desce"
    })";
    auto              read = ReadTemplateManifest( authored );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    const Common::Project::TemplateManifest manifest = read.ExtractValue();
    EXPECT_EQ( manifest.DisplayName, "First Person" );
    EXPECT_EQ( manifest.Category, "Gameplay" );
    EXPECT_EQ( manifest.SortKey, 10 );
    EXPECT_EQ( manifest.DefaultScene, "Assets/Scenes/Main.desce" );

    const std::string json = WriteTemplateManifest( manifest );
    for ( const char* field :
          { "\"DisplayName\"", "\"Description\"", "\"Category\"", "\"SortKey\"", "\"DefaultScene\"" } )
        EXPECT_NE( json.find( field ), std::string::npos ) << field << " is missing from " << json;
}

TEST( TemplateManifest, ADisplayNameIsTheOnlyThingATemplateHasToSay )
{
    // The point of templates-as-data is that adding one is dropping a folder in. A manifest that
    // has to spell out five fields to be legal is a manifest people get wrong.
    auto read = ReadTemplateManifest( R"({"DisplayName":"Blank"})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    EXPECT_EQ( read.GetValue().DisplayName, "Blank" );
    EXPECT_EQ( read.GetValue().SortKey, 0 );
    EXPECT_EQ( read.GetValue().DefaultScene, "" );
}

TEST( TemplateManifest, AManifestThatDoesNotParseSaysWhyInTheParsersWords )
{
    // L2 §3.4: a template that does not load is a line in the status strip, never a silent skip —
    // which means the reason has to survive the read.
    auto read = ReadTemplateManifest( R"({"DisplayName":null})" );
    ASSERT_FALSE( read.IsSuccess() );
    EXPECT_NE( read.GetError().find( "template.json" ), std::string::npos ) << read.GetError();
    EXPECT_NE( read.GetError().find( "DisplayName" ), std::string::npos )
         << "the refusal does not name the field that is wrong: " << read.GetError();
}

// ── the engine registry: the engine writes it, the launcher reads it ─────────────────────────────

TEST( EngineRegistry, WhatTheEngineRegistersIsWhatTheLauncherWouldStart )
{
    Common::Engine::EngineRegistry registry;
    Common::Engine::RegisterInstall( registry, { "/Users/dev/DesertEngine", "0.1.316+db43fdb", 316 } );

    auto read = Common::Engine::ReadEngineRegistry( Common::Engine::WriteEngineRegistry( registry ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    const Common::Engine::EngineInstall* preferred = Common::Engine::PreferredInstall( read.GetValue() );
    ASSERT_NE( preferred, nullptr );
    EXPECT_EQ( preferred->Root, "/Users/dev/DesertEngine" );
    EXPECT_EQ( preferred->VersionFull, "0.1.316+db43fdb" );
    EXPECT_EQ( preferred->CommitCount, 316 );

    const std::string json = Common::Engine::WriteEngineRegistry( registry );
    for ( const char* field : { "\"Engines\"", "\"Root\"", "\"VersionFull\"", "\"CommitCount\"" } )
        EXPECT_NE( json.find( field ), std::string::npos ) << field << " is missing from " << json;
}

TEST( EngineRegistry, RegisteringTheSameTreeAgainUpdatesItInsteadOfGrowingTheList )
{
    // The Editor registers on EVERY start. A developer's tree is one root opened a thousand times,
    // and an append would hand the launcher a thousand identical sidebar entries.
    Common::Engine::EngineRegistry registry;
    Common::Engine::RegisterInstall( registry, { "/Users/dev/DesertEngine", "0.1.316", 316 } );
    Common::Engine::RegisterInstall( registry, { "/Users/dev/DesertEngine", "0.1.492", 492 } );
    ASSERT_EQ( registry.Engines.size(), 1u ) << "the same root was registered twice";
    EXPECT_EQ( registry.Engines[0].VersionFull, "0.1.492" ) << "the entry was not refreshed";
}

TEST( EngineRegistry, TheNewestBuildIsTheOneTheLauncherPrefers )
{
    // The OLDER install is registered FIRST, on purpose. Registered newest-first the answer is
    // right whether or not anything sorts, and a test that passes for that reason proves only that
    // the list has a front element. (Written the lucky way first; a mutation that made the sort a
    // no-op did not turn it red, which is the only reason anyone found out.)
    Common::Engine::EngineRegistry registry;
    Common::Engine::RegisterInstall( registry, { "/opt/desert-old", "0.1.316", 316 } );
    Common::Engine::RegisterInstall( registry, { "/opt/desert-new", "0.1.492", 492 } );
    ASSERT_EQ( registry.Engines.size(), 2u );
    EXPECT_EQ( Common::Engine::PreferredInstall( registry )->Root, "/opt/desert-new" )
         << "the newest build is not the one the launcher would start";
    EXPECT_EQ( registry.Engines[1].Root, "/opt/desert-old" ) << "the list is not ordered, only its front";

    // And an UPDATE has to re-order too: the old tree rebuilt past the new one becomes preferred.
    Common::Engine::RegisterInstall( registry, { "/opt/desert-old", "0.1.500", 500 } );
    ASSERT_EQ( registry.Engines.size(), 2u );
    EXPECT_EQ( Common::Engine::PreferredInstall( registry )->Root, "/opt/desert-old" )
         << "a rebuilt install did not overtake the one it passed";
}

TEST( EngineRegistry, NoEngineInstalledIsAStateAndNotAFailure )
{
    // A fresh machine, or a launcher started before any Editor has ever run. The launcher has to
    // draw this; it must not arrive as a parse error.
    auto read = Common::Engine::ReadEngineRegistry( R"({"Engines":[]})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    EXPECT_EQ( Common::Engine::PreferredInstall( read.GetValue() ), nullptr );
}

TEST( ProjectFormat, CensusRowsArePinnedToWhatIsAlreadyScaffolded )
{
    // Folder names on disk are as much a contract as field names in a file: every project created
    // so far carries exactly these, and the engine re-creates them on open.
    const std::array<std::string_view, 7> expected = {
         "Meshes/", "Materials/", "Textures/", "Scenes/", "Prefabs/", "Scripts/", "Collections/",
    };
    EXPECT_EQ( StandardContentFolders, expected );
}

TEST( LaunchProtocol, LiteralsArePinnedToWhatShippedBinariesExpect )
{
    EXPECT_STREQ( Common::Launch::kProjectFlag, "--project" );
    EXPECT_STREQ( Common::Launch::kConfigDebug, "Debug" );
    EXPECT_STREQ( Common::Launch::kConfigRelease, "Release" );
}

// ── refusals are named ───────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, GarbageIsRefusedWithAReason )
{
    auto result = ReadProjectFile( "this is not json" );
    ASSERT_FALSE( result.IsSuccess() );
    EXPECT_FALSE( result.GetError().empty() );
}

TEST( ProjectFormat, AMissingFieldIsDefaultedAndAWrongTypeIsStillRefused )
{
    // RENEGOTIATED, and the previous test said where to do it. Reads now run
    // `rfl::DefaultIfMissing`, because a format that gains a field must not orphan the files
    // written before it — see ADescriptorWrittenBeforeTheNewFieldsExistedStillOpens.
    auto minimal = ReadProjectFile( R"({"Name":"X"})" );
    ASSERT_TRUE( minimal.IsSuccess() ) << minimal.GetError();
    EXPECT_EQ( minimal.GetValue().AssetsRoot, "Assets" ) << "the C++ default did not apply";

    // What did NOT become lenient: a field that is THERE and wrong. This is the live case behind
    // the launcher's unopenable tile — `"Name": null` is a descriptor the engine refuses, and the
    // tile has to be able to quote the reason.
    auto wrongType = ReadProjectFile( R"({"Name":null})" );
    ASSERT_FALSE( wrongType.IsSuccess() ) << "a null Name parsed as a project";
    EXPECT_NE( wrongType.GetError().find( "Name" ), std::string::npos ) << wrongType.GetError();
    EXPECT_FALSE( ReadProjectFile( R"({"AssetsRoot":7})" ).IsSuccess() );
}
