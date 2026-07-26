#pragma once

#include "ssh/ssh_types.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <mutex>
#include <optional>

namespace arterm::ssh
{

	/// Bridges the worker threads back to the GUI thread when a connection needs a
	/// human decision (host key trust, a missing password).
	///
	/// The object lives on the GUI thread. Workers call `confirm_host_key` /
	/// `ask_credential` directly; those forward to the GUI thread with a blocking
	/// queued invocation, so the calling worker parks until the user answers.
	class SessionInteraction : public QObject
	{
		Q_OBJECT

	public:
		explicit SessionInteraction( QObject* parent = nullptr );

		/// Thread safe. Blocks the calling thread until the user decides.
		[[nodiscard]] bool confirm_host_key( HostKeyInfo const& info );

		/// Thread safe. Blocks until the user submits or cancels the prompt.
		///
		/// An answer is remembered for the process lifetime so a reconnect does not
		/// ask again; nothing is persisted to disk.
		[[nodiscard]] std::optional<QString> ask_credential( QString const& prompt, bool echo );

		/// Drops the remembered passwords, e.g. after one was rejected.
		void forget_cached_answers();

	Q_SIGNALS:
		/// Emitted on the GUI thread. A slot must call `resolve_host_key` before it
		/// returns, otherwise the request is treated as a rejection.
		void host_key_decision_requested( arterm::ssh::HostKeyInfo const& info, bool* accepted );

		/// Emitted on the GUI thread. `answer` stays empty when the user cancels;
		/// `provided` distinguishes "cancelled" from "submitted an empty string".
		void credential_requested( QString const& prompt, bool echo, QString* answer, bool* provided );

	private:
		void handle_host_key_request( HostKeyInfo const& info, bool* accepted );
		void handle_credential_request( QString const& prompt, bool echo, QString* answer, bool* provided );

		[[nodiscard]] static QString host_key_cache_key( HostKeyInfo const& info );

		/// Serialises prompts and remembers the answers.
		///
		/// A session opens two connections at once, so without this both would race
		/// to put up their own dialog for the same host key and the user would be
		/// asked twice for the same decision. The mutex makes the second connection
		/// wait for the first answer, and the caches let it reuse that answer
		/// instead of prompting again.
		std::mutex              _prompt_mutex;
		QHash<QString, bool>    _host_key_decisions;
		QHash<QString, QString> _credential_answers;
		/// Prompts the user cancelled, so a retry does not re-ask immediately.
		QSet<QString> _declined_credentials;
	};

} // namespace arterm::ssh
