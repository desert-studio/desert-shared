// Relative on purpose — the file is compiled by each host, and this way it needs no include
// path beyond its own repository.
#include "../Include/DesertShared/ProjectFormat.hpp"

#include <rflcpp/rfl/DefaultIfMissing.hpp>
#include <rflcpp/rfl/json.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

namespace Common::Project
{
    namespace
    {
        // EVERY read runs this processor, and that is the migration mechanism for added fields: a
        // `.deproj` written before `Description` existed has no `Description` key, and without this
        // the parser would refuse the whole file rather than default one member. A field that is
        // PRESENT and of the wrong type is still an error — `"Name": null` is refused exactly as
        // before, which is the case a launcher tile has to be able to report verbatim.
        template <typename T>
        Common::ResultStr<T> ReadWithDefaults( const std::string& json, const char* what )
        {
            auto parsed = rfl::json::read<T, rfl::DefaultIfMissing>( json );
            if ( !parsed.has_value() )
                return Common::MakeFormattedError<T>( "Corrupt {}: {}", what, parsed.error().what() );
            return Common::MakeSuccess( std::move( parsed.value() ) );
        }

        // The registry as it was before LastOpened: `{"Projects": ["a.deproj", "b.deproj"]}`. It
        // exists ONLY to be read — nothing writes this shape any more, and the moment anything
        // saves the registry the file on disk is the current shape.
        struct LegacyProjectsRegistry
        {
            std::vector<std::string> Projects;
        };
    } // namespace

    Common::ResultStr<ProjectFile> ReadProjectFile( const std::string& json )
    {
        return ReadWithDefaults<ProjectFile>( json, ".deproj" );
    }

    std::string WriteProjectFile( const ProjectFile& file )
    {
        // The version is stamped HERE and nowhere else. Producers do not set it — if they did,
        // "which version is this file" would have as many answers as there are call sites, and a
        // caller that forgot would write a descriptor claiming to be older than it is.
        ProjectFile stamped  = file;
        stamped.FileVersion  = kProjectFileVersion;
        return rfl::json::write( stamped );
    }

    Common::ResultStr<ProjectsRegistry> ReadProjectsRegistry( const std::string& json )
    {
        auto current = ReadWithDefaults<ProjectsRegistry>( json, "projects.json" );
        if ( current.IsSuccess() )
            return current;

        // Not the current shape. Before deciding the file is corrupt, try the one that was current
        // last month: a developer with a registry from before this change must not be told their
        // project list is broken, and must not lose it.
        auto legacy = rfl::json::read<LegacyProjectsRegistry, rfl::DefaultIfMissing>( json );
        if ( !legacy.has_value() )
            return current; // neither shape — report the CURRENT format's error, not the old one's

        ProjectsRegistry migrated;
        migrated.Projects.reserve( legacy.value().Projects.size() );
        for ( std::string& path : legacy.value().Projects )
            // Position is preserved, and LastOpened stays 0: the flat list never held a time, and
            // inventing one (say, the file's mtime) would put the same wrong date on every entry.
            migrated.Projects.push_back( ProjectRecord{ std::move( path ), 0 } );
        return Common::MakeSuccess( std::move( migrated ) );
    }

    std::string WriteProjectsRegistry( const ProjectsRegistry& registry )
    {
        ProjectsRegistry stamped = registry;
        stamped.FileVersion      = kProjectFileVersion;
        return rfl::json::write( stamped );
    }

    Common::ResultStr<TemplateManifest> ReadTemplateManifest( const std::string& json )
    {
        return ReadWithDefaults<TemplateManifest>( json, "template.json" );
    }

    std::string WriteTemplateManifest( const TemplateManifest& manifest )
    {
        return rfl::json::write( manifest );
    }

    void PromoteRecent( ProjectsRegistry& registry, const std::string& deprojPath, long long nowUnixSeconds )
    {
        auto& projects = registry.Projects;
        projects.erase( std::remove_if( projects.begin(), projects.end(),
                                        [&]( const ProjectRecord& r ) { return r.Path == deprojPath; } ),
                        projects.end() );
        projects.insert( projects.begin(), ProjectRecord{ deprojPath, nowUnixSeconds } );
    }

    long long UnixNow()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
             .count();
    }
} // namespace Common::Project
