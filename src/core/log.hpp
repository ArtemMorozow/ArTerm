#pragma once

#include <format>
#include <string_view>

namespace arterm
{

	/// Log levels mirror what the Qt logging categories offered; DEBUG is compiled
	/// in but filtered out at runtime unless verbose logging is enabled.
	enum class LogLevel{
		DEBUG,
		INFO,
		WARNING,
		ERROR,
	};

	/// Enable DEBUG output (the --verbose flag).
	void set_verbose_logging( bool enabled );

	/// Writes one line to stderr: `HH:MM:SS.mmm level [category] message`.
	/// Thread safe in the only way that matters: lines are written atomically.
	void log_line( LogLevel level, std::string_view category, std::string_view message );

	template <typename... Args_>
	void log_debug( std::string_view category, std::format_string<Args_...> format, Args_&&... args ){
		log_line( LogLevel::DEBUG, category, std::format( format, std::forward<Args_>( args )... ) );
	}

	template <typename... Args_>
	void log_info( std::string_view category, std::format_string<Args_...> format, Args_&&... args ){
		log_line( LogLevel::INFO, category, std::format( format, std::forward<Args_>( args )... ) );
	}

	template <typename... Args_>
	void log_warning( std::string_view category, std::format_string<Args_...> format, Args_&&... args ){
		log_line( LogLevel::WARNING, category, std::format( format, std::forward<Args_>( args )... ) );
	}

	template <typename... Args_>
	void log_error( std::string_view category, std::format_string<Args_...> format, Args_&&... args ){
		log_line( LogLevel::ERROR, category, std::format( format, std::forward<Args_>( args )... ) );
	}

} // namespace arterm
