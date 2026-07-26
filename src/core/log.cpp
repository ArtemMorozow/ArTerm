#include "core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

namespace arterm
{
	namespace
	{

		std::atomic<bool> verbose{ false };

		constexpr std::string_view level_name( LogLevel level ){
			switch( level ){
				case LogLevel::DEBUG:
					return "debug";
				case LogLevel::INFO:
					return "info ";
				case LogLevel::WARNING:
					return "warn ";
				case LogLevel::ERROR:
					return "error";
			}
			return "?    ";
		}

	} // namespace

	void set_verbose_logging( bool enabled ){
		verbose.store( enabled, std::memory_order_relaxed );
	}

	void log_line( LogLevel level, std::string_view category, std::string_view message ){
		if( level == LogLevel::DEBUG && !verbose.load( std::memory_order_relaxed ) )
			return;

		auto const now      = std::chrono::system_clock::now();
		auto const midnight = std::chrono::floor<std::chrono::days>( now );
		auto const time     = std::chrono::hh_mm_ss( now - midnight );

		// One fprintf per line keeps concurrent writers from interleaving.
		std::string const line = std::format(
			"{:02}:{:02}:{:02}.{:03} {} [{}] {}\n", time.hours().count(), time.minutes().count(),
			time.seconds().count(), std::chrono::duration_cast<std::chrono::milliseconds>( time.subseconds() ).count(),
			level_name( level ), category, message );
		std::fputs( line.c_str(), stderr );
	}

} // namespace arterm
