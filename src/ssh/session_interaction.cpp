#include "ssh/session_interaction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::ssh
{

	SessionInteraction::SessionInteraction( QObject* parent )
		: QObject( parent )
	{}

	QString SessionInteraction::host_key_cache_key( HostKeyInfo const& info ){
		// Keyed by the fingerprint as well as the endpoint: if the key changes
		// mid-run the user must be asked again rather than inheriting the earlier
		// "trust" answer.
		return QStringLiteral( "%1:%2/%3" ).arg( info.hostname ).arg( info.port ).arg( info.sha256 );
	}

	bool SessionInteraction::confirm_host_key( HostKeyInfo const& info ){
		QString const key = host_key_cache_key( info );

		// Held across the prompt so a concurrent connection to the same host waits
		// here and then finds the cached answer instead of opening a second dialog.
		std::lock_guard const lock( _prompt_mutex );

		if( auto const cached = _host_key_decisions.constFind( key ); cached != _host_key_decisions.constEnd() ){
			return *cached;
		}

		bool accepted = false;

		if( QThread::currentThread() == thread() ){
			handle_host_key_request( info, &accepted );
		}
		else{
			// A functor invocation keeps the out-parameters type safe; the worker
			// thread parks here until the GUI thread has run the lambda.
			QMetaObject::invokeMethod(
				this, [this, &info, &accepted] { handle_host_key_request( info, &accepted ); },
				Qt::BlockingQueuedConnection );
		}

		_host_key_decisions.insert( key, accepted );
		return accepted;
	}

	std::optional<QString> SessionInteraction::ask_credential( QString const& prompt, bool echo ){
		std::lock_guard const lock( _prompt_mutex );

		if( _declined_credentials.contains( prompt ) )
			return std::nullopt;

		if( auto const cached = _credential_answers.constFind( prompt ); cached != _credential_answers.constEnd() ){
			return *cached;
		}

		QString answer;
		bool    provided = false;

		if( QThread::currentThread() == thread() ){
			handle_credential_request( prompt, echo, &answer, &provided );
		}
		else{
			QMetaObject::invokeMethod(
				this,
				[this, &prompt, echo, &answer, &provided]{
					handle_credential_request( prompt, echo, &answer, &provided );
				},
				Qt::BlockingQueuedConnection );
		}

		if( !provided ){
			_declined_credentials.insert( prompt );
			return std::nullopt;
		}

		// Cached for the process lifetime only; nothing is written to disk here.
		_credential_answers.insert( prompt, answer );
		return answer;
	}

	void SessionInteraction::forget_cached_answers(){
		std::lock_guard const lock( _prompt_mutex );
		_credential_answers.clear();
		_declined_credentials.clear();
	}

	void SessionInteraction::handle_host_key_request( HostKeyInfo const& info, bool* accepted ){
		Q_EMIT host_key_decision_requested( info, accepted );
	}

	void SessionInteraction::handle_credential_request( QString const& prompt, bool echo, QString* answer,
														bool* provided ){
		Q_EMIT credential_requested( prompt, echo, answer, provided );
	}

} // namespace arterm::ssh
