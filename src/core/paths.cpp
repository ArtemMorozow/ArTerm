#include "core/paths.hpp"

#include <cstdlib>
#include <filesystem>

#include <pwd.h>
#include <unistd.h>

namespace arterm
{

	std::string home_directory(){
		if( char const* home = std::getenv( "HOME" ); home != nullptr && *home != '\0' )
			return home;

		if( passwd const* entry = getpwuid( getuid() ); entry != nullptr && entry->pw_dir != nullptr )
			return entry->pw_dir;

		return "/tmp";
	}

	std::string expand_home( std::string_view path ){
		if( path.starts_with( "~/" ) )
			return home_directory() + std::string( path.substr( 1 ) );
		if( path == "~" )
			return home_directory();
		return std::string( path );
	}

	std::string application_data_directory(){
		std::string const directory = home_directory() + "/Library/Application Support/ArTerm";

		std::error_code ignored;
		std::filesystem::create_directories( directory, ignored );

		return directory;
	}

} // namespace arterm
