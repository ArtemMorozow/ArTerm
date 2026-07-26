#pragma once

#include "files/file_mime_data.hpp"

#include <QWidget>

class QLineEdit;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;
class QLabel;

namespace arterm::files
{

	class FileListModel;

	/// A table view that handles the drag and drop wiring the panes need.
	///
	/// Qt's model-level drop handling is bypassed: a drop here does not insert a
	/// row, it starts a transfer, and the target directory depends on whether the
	/// pointer was over a folder or over empty space.
	class FileTableView : public QWidget
	{
		Q_OBJECT

	public:
		explicit FileTableView( QWidget* parent = nullptr );

		void                                 set_model( FileListModel* model );
		[[nodiscard]] QTableView*            view() const noexcept { return _view; }
		[[nodiscard]] QSortFilterProxyModel* proxy() const noexcept { return _proxy; }

		/// Absolute paths of the selected rows.
		[[nodiscard]] QStringList selected_paths() const;
		[[nodiscard]] bool        has_selection() const;

		void set_name_filter( QString const& pattern );

	Q_SIGNALS:
		/// Local files were dropped; `target_directory` is where they belong.
		void local_files_dropped( QStringList const& paths, QString const& target_directory );
		/// Remote files were dropped, carrying the originating session id.
		void remote_files_dropped( RemoteDragPayload const& payload, QString const& target_directory );
		void activated( QString const& path, bool is_directory );
		void selection_changed();
		void context_menu_requested( QPoint const& global_position );

	protected:
		bool eventFilter( QObject* watched, QEvent* event ) override;

	private:
		/// Directory a drop at `position` should land in: the folder under the
		/// pointer, or the directory being listed when the pointer is over a file
		/// or over empty space.
		[[nodiscard]] QString drop_target_directory( QPoint const& position ) const;
		void                  handle_drag_move( class QDragMoveEvent* event );
		void                  handle_drop( class QDropEvent* event );

		QTableView*            _view{ nullptr };
		QSortFilterProxyModel* _proxy{ nullptr };
		FileListModel*         _model{ nullptr };

		/// Row currently highlighted as a drop target, or -1.
		int _drop_row{ -1 };
	};

	/// One side of the file browser: toolbar, path bar, listing and status line.
	class FilePane : public QWidget
	{
		Q_OBJECT

	public:
		explicit FilePane( QWidget* parent = nullptr );

		void                         set_model( FileListModel* model );
		[[nodiscard]] FileListModel* model() const noexcept { return _model; }
		[[nodiscard]] FileTableView* table() const noexcept { return _table; }

		/// Shown in the pane header, e.g. "This Mac" or "user@host".
		void set_title( QString const& title );

		/// Replaces the listing with a message, used while connecting or after a
		/// failure.
		void set_placeholder( QString const& message );
		void clear_placeholder();

		[[nodiscard]] QString     current_directory() const;
		[[nodiscard]] QStringList selected_paths() const;

	Q_SIGNALS:
		/// The user asked to send the selection to the other pane.
		void transfer_requested( QStringList const& paths );
		void local_files_dropped( QStringList const& paths, QString const& target_directory );
		void remote_files_dropped( RemoteDragPayload const& payload, QString const& target_directory );
		void selection_changed();

	public Q_SLOTS:
		void navigate_up();
		void navigate_back();
		void navigate_forward();
		void navigate_home();
		void refresh();
		void create_folder();
		void rename_selection();
		void delete_selection();
		void copy_path_to_clipboard();
		void toggle_hidden_files();

	private:
		void build_ui();
		void apply_path( QString const& path );
		void push_history( QString const& path );
		void update_status();
		void show_context_menu( QPoint const& global_position );

		FileListModel* _model{ nullptr };

		QLabel*        _title_label{ nullptr };
		QLineEdit*     _path_edit{ nullptr };
		QLineEdit*     _filter_edit{ nullptr };
		QToolButton*   _back_button{ nullptr };
		QToolButton*   _forward_button{ nullptr };
		QToolButton*   _up_button{ nullptr };
		QToolButton*   _hidden_button{ nullptr };
		FileTableView* _table{ nullptr };
		QLabel*        _status_label{ nullptr };
		QLabel*        _placeholder_label{ nullptr };

		QStringList _history;
		int         _history_index{ -1 };
		/// Set while navigating through history so the visit is not re-recorded.
		bool _navigating_history{ false };
	};

} // namespace arterm::files
