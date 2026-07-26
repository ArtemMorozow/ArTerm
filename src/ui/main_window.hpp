#pragma once

#include "ssh/ssh_types.hpp"

#include <QMainWindow>

#include <memory>

class QLabel;
class QProgressBar;
class QTabWidget;

namespace arterm::model
{
	class HostStore;
}

namespace arterm::ssh
{
	class SessionInteraction;
}

namespace arterm::ui
{

	class HostSidebar;
	class SessionTab;

	/// The application window: host sidebar on the left, session tabs on the right.
	class MainWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		explicit MainWindow( QWidget* parent = nullptr );
		~MainWindow() override;

		/// Opens a session for a stored host.
		void open_session( QString const& host_id );

	protected:
		void closeEvent( QCloseEvent* event ) override;

	private Q_SLOTS:
		void new_host();
		void edit_host( QString const& host_id );
		void close_tab( int index );
		void close_current_tab();
		void reconnect_current();
		void toggle_file_browser();
		void import_ssh_config();

	private:
		void build_ui();
		void build_menus();
		void build_tool_bar();
		void wire_interaction();
		void restore_window_state();
		void save_window_state() const;

		[[nodiscard]] SessionTab* current_session() const;
		[[nodiscard]] SessionTab* session_at( int index ) const;
		void                      update_tab_title( SessionTab* session );
		void                      show_status( QString const& message, int timeout_ms = 6000 );

		model::HostStore*                        _store{ nullptr };
		std::shared_ptr<ssh::SessionInteraction> _interaction;

		HostSidebar* _sidebar{ nullptr };
		QTabWidget*  _tabs{ nullptr };
		QLabel*      _status_label{ nullptr };
		QLabel*      _empty_state{ nullptr };
	};

} // namespace arterm::ui
