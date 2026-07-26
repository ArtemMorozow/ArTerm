#include "ssh/session_interaction.hpp"

#include "core/dispatch.hpp"

#include <format>

namespace arterm::ssh
{

	std::string SessionInteraction::host_key_cache_key( HostKeyInfo const& info ){
		// Keyed by the fingerprint as well as the endpoint: if the key changes
		// mid-run the user must be asked again rather than inheriting the earlier
		// "trust" answer.
		return std::format( "{}:{}/{}", info.hostname, info.port, info.sha256 );
	}

	bool SessionInteraction::confirm_host_key( HostKeyInfo const& info ){
		std::string const key = host_key_cache_key( info );

		// Held across the prompt so a concurrent connection to the same host waits
		// here and then finds the cached answer instead of opening a second dialog.
		std::lock_guard const lock( _prompt_mutex );

		if( auto const cached = _host_key_decisions.find( key ); cached != _host_key_decisions.end() )
			return cached->second;

		bool accepted = false;

		// The worker parks here until the main queue has run the handler.
		on_main_sync( [this, &info, &accepted]{
			if( _host_key_handler )
				accepted = _host_key_handler( info );
		} );

		_host_key_decisions.emplace( key, accepted );
		return accepted;
	}

	std::optional<std::string> SessionInteraction::ask_credential( std::string const& prompt, bool echo ){
		std::lock_guard const lock( _prompt_mutex );

		if( _declined_credentials.contains( prompt ) )
			return std::nullopt;

		if( auto const cached = _credential_answers.find( prompt ); cached != _credential_answers.end() )
			return cached->second;

		std::optional<std::string> answer;

		on_main_sync( [this, &prompt, echo, &answer]{
			if( _credential_handler )
				answer = _credential_handler( prompt, echo );
		} );

		if( !answer ){
			_declined_credentials.insert( prompt );
			return std::nullopt;
		}

		// Cached for the process lifetime only; nothing is written to disk here.
		_credential_answers.emplace( prompt, *answer );
		return answer;
	}

	void SessionInteraction::forget_cached_answers(){
		std::lock_guard const lock( _prompt_mutex );
		_credential_answers.clear();
		_declined_credentials.clear();
	}

} // namespace arterm::ssh
