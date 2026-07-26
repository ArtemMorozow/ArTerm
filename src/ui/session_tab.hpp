#pragma once

#include "ssh/shell_session.hpp"
#include "ssh/ssh_types.hpp"

#include <QWidget>

#include <memory>

class QLabel;
class QSplitter;

namespace arterm::term
{
	class TerminalWidget;
}

namespace arterm::files
{
	class FileBrowser;
}

namespace arterm::session
{
	class RemoteFileService;
	class ShellService;
} // namespace arterm::session

namespace arterm::ssh
{
	class SessionInteraction;
}

namespace arterm::ui
{

	/// One connected host: the terminal on top, the file browser below.
	///
	/// The tab owns both worker-backed services. Two SSH connections are opened per
	/// host, one for the shell and one for SFTP, because a libssh2 session cannot be
	/// driven from two threads and interleaving a large upload with interactive
	/// typing on one connection makes the terminal stutter.
	class SessionTab : public QWidget
	{
		Q_OBJECT

	public:
		SessionTab( ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
					QWidget* parent = nullptr );
		~SessionTab() override;

		/// Opens both connections.
		void connect_to_host();

		/// Closes them and stops the worker threads.
		void disconnect_from_host();

		[[nodiscard]] ssh::HostProfile const& profile() const noexcept { return _profile; }
		[[nodiscard]] QString const&          session_id() const noexcept { return _session_id; }

		/// Tab title: the remote title when the shell set one, the host otherwise.
		[[nodiscard]] QString display_title() const;

		[[nodiscard]] bool is_connected() const;
		[[nodiscard]] bool has_active_transfers() const;

		[[nodiscard]] term::TerminalWidget* terminal() const noexcept { return _terminal; }
		[[nodiscard]] files::FileBrowser*   file_browser() const noexcept { return _browser; }

		void               set_file_browser_visible( bool visible );
		[[nodiscard]] bool is_file_browser_visible() const;
		void               focus_terminal();

	Q_SIGNALS:
		void title_changed( QString const& title );
		void status_message( QString const& message );
		void connection_state_changed();
		/// The shell ended; the window may want to close the tab.
		void session_closed( int exit_status );

	private:
		void build_ui();
		void wire_shell();
		void wire_files();
		void show_banner( QString const& message, QString const& state );
		void hide_banner();

		ssh::HostProfile                         _profile;
		QString                                  _session_id;
		std::shared_ptr<ssh::SessionInteraction> _interaction;

		session::ShellService*      _shell{ nullptr };
		session::RemoteFileService* _files{ nullptr };

		term::TerminalWidget* _terminal{ nullptr };
		files::FileBrowser*   _browser{ nullptr };
		QSplitter*            _splitter{ nullptr };
		QLabel*               _banner{ nullptr };

		QString _remote_title;
		bool    _shell_ready{ false };
	};

} // namespace arterm::ui
