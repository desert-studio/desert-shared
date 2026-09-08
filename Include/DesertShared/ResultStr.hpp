#pragma once

// The result type the shared-format API is written in (L2 design §7 item 3). Moved VERBATIM from
// the engine's Common/Core/ResultStr.hpp — the engine now includes it from here, so there is one
// definition and the namespace stays `Common::` (renaming it would have churned every file in the
// engine for zero information). No logger lives in this repo: functions return a ResultStr and the
// HOST decides who shouts.
//
// Host-supplied dependency: the fmt headers (the engine takes them from its vendored spdlog; a
// standalone host can vendor either spdlog or bare fmt). Only MakeFormattedError and the
// formatting Error constructor need them — but they are templates in this header, so the header
// requires fmt to be on the include path of every consumer.

#include <optional>
#include <variant>
#include <string>
#include <type_traits>
#include <cstdio>

#include <spdlog/fmt/fmt.h>

#include <utility>

namespace Common
{
    // WHO SHOUTS WHEN A FAILED RESULT IS UNWRAPPED ANYWAY.
    //
    // `GetValue()`/`ExtractValue()` on a FAILED result used to hand back a default-constructed T in
    // total silence, so a failure read as a success holding nothing — the exact shape the delivery
    // contract §1.4 forbids ("an empty successful answer is a silent wrong answer, not a refusal"),
    // living inside the very type that §1.4 is enforced with. For a handle type that default is
    // `VK_NULL_HANDLE`; for a shared_ptr it is null; and in every case the caller's next line
    // proceeds as if it had a value.
    //
    // The rvalue overloads below make the UNGUARDABLE form (`Foo().GetValue()` — a temporary, so
    // there is no variable anyone could have checked) a compile error. This hook covers the form
    // the compiler cannot see: a NAMED result whose guard is missing, or wrong.
    //
    // No logger lives in this repository on purpose, so the report is a host-installed function
    // rather than a call into one. A host that installs nothing still gets the message on stderr —
    // silence is never the default. Install once at startup; it is not synchronised because it is
    // written before any worker exists and only read afterwards.
    // Header-only on purpose: ~100 engine files and every tool in the workspace include this header,
    // and their generated makefiles list source files EXPLICITLY. An out-of-line definition here
    // would link only for the targets that happen to compile the submodule's Source/, so the ones
    // that do not would fail to link over a diagnostic.
    using ResultUnwrapReporter = void ( * )( const char* message );

    inline ResultUnwrapReporter& ResultUnwrapReporterSlot()
    {
        static ResultUnwrapReporter s_Reporter = nullptr;
        return s_Reporter;
    }

    inline void SetResultUnwrapReporter( ResultUnwrapReporter reporter )
    {
        ResultUnwrapReporterSlot() = reporter;
    }

    inline void ReportFailedUnwrap( const std::string& error )
    {
        const std::string message = "A FAILED result was unwrapped and its value used. The failure said: " + error;
        if ( ResultUnwrapReporterSlot() )
        {
            ResultUnwrapReporterSlot()( message.c_str() );
            return;
        }
        std::fputs( message.c_str(), stderr );
        std::fputc( '\n', stderr );
    }

    template <typename T>
    class ResultStr;

    template <typename T = bool>
    ResultStr<T> MakeError( const std::string& message );

    template <typename T = bool, typename... Args>
    ResultStr<T> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args );

    template <typename T = bool>
    auto MakeSuccess( T&& value );

    using BoolResultStr = ResultStr<bool>;

    template <typename T>
    class ResultStr
    {
    public:
        class Error
        {
        public:
            explicit Error( const std::string& errorMessage ) : m_ErrorMessage( errorMessage )
            {
            }

            // The format string is checked by the COMPILER (see Common/Core/Logger.hpp for the full why).
            // This constructor builds the message of a FAILURE, so a mismatched brace count here threw
            // `fmt::format_error` at the one moment the code was already handling something going wrong -
            // and nothing catches it. A genuinely runtime format must say so with `fmt::runtime(...)`.
            //
            // The `const std::string&` overload above still wins for a single non-literal argument, so a
            // message that merely CONTAINS braces and formats nothing is unaffected, exactly as before.
            template <typename... Args>
            explicit Error( fmt::format_string<Args...> format, Args&&... args )
                 : m_ErrorMessage( fmt::format( format, std::forward<Args>( args )... ) )
            {
            }

            const std::string& GetMessage() const
            {
                return m_ErrorMessage;
            }

        private:
            std::string m_ErrorMessage;
        };

    public:
        /**
         * @brief Default constructor creates an error state.
         */
        ResultStr() : m_Outcome( Error( "Uninitialized Result" ) ), m_IsSuccess( false )
        {
        }

        bool IsSuccess() const
        {
            return m_IsSuccess;
        }

        // THE RVALUE OVERLOAD IS DELETED, AND THAT IS THE ONLY PART OF THIS THE COMPILER CAN ENFORCE.
        //
        // `Foo().GetValue()` unwraps a TEMPORARY: there is no variable in the caller's hands, so no
        // reviewer, no census and no amount of care can ever add the missing check to it — the form is
        // unguardable by construction. Nine such sites existed in the engine when this was written
        // (four in MaterialExecutor, two in VulkanQueue, two in VulkanSwapChain, one in
        // SceneEnvironment), and each of them wrote a possibly-null handle or shared_ptr into a member
        // and carried on. Deleting the rvalue overload makes every one of them a compile error, which
        // is why the migration was done by the compiler rather than by eye.
        //
        // The lvalue form still compiles unchecked — C++ cannot express "this was tested" in the type
        // system without rewriting all ~145 call sites into a callback or pointer shape, which was
        // measured and refused (see the task report). What it no longer does is stay SILENT: an
        // unwrap of a failure reports through ReportFailedUnwrap above, naming the error it is
        // discarding. The remaining lvalue sites are held by the ResultUnwrapCensus gate.
        const T& GetValue() const&
        {
            if ( !m_IsSuccess )
            {
                ReportFailedUnwrap( GetError() );
                // Const on purpose, and const is load-bearing. This used to be a mutable static
                // returned by a non-const overload, i.e. ONE process-wide object shared by every
                // failed unwrap of this T, writable by any caller and read by all the others.
                static const T empty{};
                return empty;
            }
            return std::get<T>( m_Outcome );
        }

        const T& GetValue() const&& = delete;

        T ExtractValue() &
        {
            if ( !m_IsSuccess )
            {
                ReportFailedUnwrap( GetError() );
                return T{};
            }
            return std::move( std::get<T>( m_Outcome ) );
        }

        T ExtractValue() && = delete;

        std::string GetError() const
        {
            if ( m_IsSuccess )
            {
                return s_NoError;
            }
            return std::get<Error>( m_Outcome ).GetMessage();
        }

        explicit operator bool() const
        {
            return m_IsSuccess;
        }

    private:
        /**
         * @brief Constructor for Success state.
         */
        template <typename U, typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, Error>>>
        explicit ResultStr( U&& value ) : m_Outcome( std::forward<U>( value ) ), m_IsSuccess( true )
        {
        }

        /**
         * @brief Constructor for Error state.
         */
        explicit ResultStr( Error&& error ) : m_Outcome( std::move( error ) ), m_IsSuccess( false )
        {
        }

        static inline std::string s_NoError = "No error";
        std::variant<T, Error>    m_Outcome;
        bool                      m_IsSuccess = false;

    private:
        template <typename U>
        friend ResultStr<U> MakeError( const std::string& message );

        template <typename U>
        friend auto MakeSuccess( U&& value );

        template <typename U, typename... Args>
        friend ResultStr<U> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args );
    };

    template <typename T>
    ResultStr<T> MakeError( const std::string& message )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( message ) );
    }

    // THE FORMAT STRING HAS TO STAY A FORMAT STRING ALL THE WAY DOWN. This took `std::string&&`, which
    // erased its compile-time nature at exactly this boundary: every call site below it was writing a
    // literal, and every one of them lost its checking here. That is why the whole conversion showed up as
    // errors on this one line - the template was instantiating the checked constructor with a runtime
    // string, from every caller in the engine at once.
    template <typename T, typename... Args>
    ResultStr<T> MakeFormattedError( fmt::format_string<Args...> format, Args&&... args )
    {
        return ResultStr<T>( typename ResultStr<T>::Error( format, std::forward<Args>( args )... ) );
    }

    template <typename T>
    auto MakeSuccess( T&& value )
    {
        using ValueType = std::decay_t<T>;
        return ResultStr<ValueType>( std::forward<T>( value ) );
    }

} // namespace Common
