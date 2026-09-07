// Relative on purpose — see the note at the top of ProjectFormat.cpp.
#include "../Include/DesertShared/EngineRegistry.hpp"

#include <rflcpp/rfl/DefaultIfMissing.hpp>
#include <rflcpp/rfl/json.hpp>

#include <algorithm>
#include <utility>

namespace Common::Engine
{
    Common::ResultStr<EngineRegistry> ReadEngineRegistry( const std::string& json )
    {
        auto parsed = rfl::json::read<EngineRegistry, rfl::DefaultIfMissing>( json );
        if ( !parsed.has_value() )
            return Common::MakeFormattedError<EngineRegistry>( "Corrupt engines.json: {}", parsed.error().what() );
        return Common::MakeSuccess( std::move( parsed.value() ) );
    }

    std::string WriteEngineRegistry( const EngineRegistry& registry )
    {
        // Stamped by the writer, for the reason spelled out on ProjectFile::FileVersion: the struct
        // default has to mean "this file predates versioning", so it cannot also mean "current".
        EngineRegistry stamped = registry;
        stamped.FileVersion    = kEngineRegistryVersion;
        return rfl::json::write( stamped );
    }

    void RegisterInstall( EngineRegistry& registry, const EngineInstall& install )
    {
        const auto existing = std::find_if( registry.Engines.begin(), registry.Engines.end(),
                                            [&]( const EngineInstall& e ) { return e.Root == install.Root; } );
        if ( existing != registry.Engines.end() )
            *existing = install; // same tree, new build — update, never append
        else
            registry.Engines.push_back( install );

        // Newest build first, so PreferredInstall is the front element and the launcher's sidebar
        // shows the same engine the launcher would start. `stable_sort` so two installs built from
        // the same commit keep the order they were registered in rather than swapping every start.
        std::stable_sort( registry.Engines.begin(), registry.Engines.end(),
                          []( const EngineInstall& a, const EngineInstall& b )
                          {
                              // A known build always outranks an unknown one, and two unknowns keep
                              // the order they were registered in. Comparing optionals directly would
                              // make "unknown" the smallest number rather than no number at all.
                              if ( a.CommitCount.has_value() != b.CommitCount.has_value() )
                                  return a.CommitCount.has_value();
                              if ( !a.CommitCount.has_value() )
                                  return false;
                              return *a.CommitCount > *b.CommitCount;
                          } );
    }

    const EngineInstall* PreferredInstall( const EngineRegistry& registry )
    {
        return registry.Engines.empty() ? nullptr : &registry.Engines.front();
    }
} // namespace Common::Engine
