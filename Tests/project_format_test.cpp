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
    written.Name         = "Neo \"Dune\" \\ 2";
    written.DefaultScene = written.AssetsRoot + "/Scenes/Main.desce";

    const ProjectFile read = ReadOk( WriteProjectFile( written ) );
    EXPECT_EQ( read.Name, written.Name );
    EXPECT_EQ( read.AssetsRoot, written.AssetsRoot );
    EXPECT_EQ( read.DefaultScene, written.DefaultScene );
}

TEST( ProjectFormat, RegistryRoundTripsAWindowsPath )
{
    // The launcher's old reader was a naive quoted-string scanner: it returned the JSON escapes
    // RAW, so every backslash the Editor had escaped came back doubled and the path pointed
    // nowhere.
    ProjectsRegistry written;
    written.Projects = { "C:\\Users\\dev\\My Game\\MyGame.deproj", "/Users/dev/Other/Other.deproj" };

    auto read = ReadProjectsRegistry( WriteProjectsRegistry( written ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    EXPECT_EQ( read.GetValue().Projects, written.Projects );
}

// ── binary vs history ────────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, DeprojFieldNamesArePinnedToWhatIsAlreadyOnDisk )
{
    // A verbatim .deproj as every binary before this one wrote it. Renaming a ProjectFile member
    // must fail HERE, not on a user's project folder.
    const ProjectFile read =
         ReadOk( R"({"Name":"MyGame","AssetsRoot":"Assets","DefaultScene":"Assets/Scenes/MyGame.desce"})" );
    EXPECT_EQ( read.Name, "MyGame" );
    EXPECT_EQ( read.AssetsRoot, "Assets" );
    EXPECT_EQ( read.DefaultScene, "Assets/Scenes/MyGame.desce" );
}

TEST( ProjectFormat, WriterEmitsThePinnedFieldNames )
{
    const std::string json = WriteProjectFile( ProjectFile{} );
    EXPECT_NE( json.find( "\"Name\"" ), std::string::npos ) << json;
    EXPECT_NE( json.find( "\"AssetsRoot\"" ), std::string::npos ) << json;
    EXPECT_NE( json.find( "\"DefaultScene\"" ), std::string::npos ) << json;
}

TEST( ProjectFormat, RegistryFieldNameIsPinned )
{
    auto read = ReadProjectsRegistry( R"({"Projects":["/a/A.deproj","/b/B.deproj"]})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    ASSERT_EQ( read.GetValue().Projects.size(), 2u );
    EXPECT_EQ( read.GetValue().Projects[0], "/a/A.deproj" );

    const std::string json = WriteProjectsRegistry( ProjectsRegistry{} );
    EXPECT_NE( json.find( "\"Projects\"" ), std::string::npos ) << json;
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

TEST( ProjectFormat, AMissingFieldIsRefusedNotDefaulted )
{
    // rfl requires every non-optional member, so all three fields are mandatory ON DISK even
    // though two have C++ defaults. That is fine while every producer goes through
    // WriteProjectFile (it always emits all three) — but it means the C++ defaults never apply
    // during a read. If a hand-authored minimal .deproj should ever open, this is the test to
    // renegotiate.
    EXPECT_FALSE( ReadProjectFile( R"({"Name":"X"})" ).IsSuccess() );
    EXPECT_FALSE( ReadProjectsRegistry( R"({})" ).IsSuccess() );
}
