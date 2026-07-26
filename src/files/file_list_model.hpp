#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>

namespace arterm::files
{

	/// Columns shared by the local and remote panes so a single view class, a
	/// single delegate and a single set of sort rules serve both sides.
	enum class FileColumn { NAME = 0, SIZE, MODIFIED, PERMISSIONS, COUNT };

	/// Item data roles used by the pane and its delegate.
	enum FileRole{
		PATH_ROLE = Qt::UserRole + 1, ///< Absolute path on its own side.
		IS_DIRECTORY_ROLE,
		IS_SYMLINK_ROLE,
		IS_EXECUTABLE_ROLE,
		SIZE_ROLE,     ///< Raw byte count for sorting.
		MODIFIED_ROLE, ///< QDateTime for sorting.
		NAME_ROLE,     ///< Raw file name, unformatted.
	};

	/// Interface every browsable directory listing implements.
	///
	/// The local and remote implementations differ in how they fetch data - one is
	/// synchronous, the other goes through a worker thread - but the pane only ever
	/// sees this API.
	class FileListModel : public QAbstractTableModel
	{
		Q_OBJECT

	public:
		using QAbstractTableModel::QAbstractTableModel;

		/// The directory currently listed.
		[[nodiscard]] virtual QString current_path() const = 0;

		/// Navigate. Emits `path_changed` once the listing has arrived.
		virtual void set_current_path( QString const& path ) = 0;

		/// Re-read the current directory.
		virtual void refresh() = 0;

		/// True for the SFTP-backed side.
		[[nodiscard]] virtual bool is_remote() const = 0;

		/// Parent of `current_path`, or an empty string at the root.
		[[nodiscard]] virtual QString parent_path() const = 0;

		/// Join `current_path` with a child name using the right separator.
		[[nodiscard]] virtual QString child_path( QString const& name ) const = 0;

		/// Whether hidden entries are listed.
		[[nodiscard]] bool shows_hidden() const noexcept { return _show_hidden; }
		virtual void       set_show_hidden( bool show ) = 0;

		// -- Mutations. Implementations that cannot perform them report an error --

		virtual void create_directory( QString const& name )                = 0;
		virtual void rename( QString const& path, QString const& new_name ) = 0;
		virtual void remove( QStringList const& paths )                     = 0;

		[[nodiscard]] bool is_loading() const noexcept { return _loading; }

	Q_SIGNALS:
		void path_changed( QString const& path );
		void loading_changed( bool loading );
		void error_occurred( QString const& message );
		/// A mutation finished and callers may want to refresh the other pane.
		void content_changed();

	protected:
		void set_loading( bool loading ){
			if( _loading == loading )
				return;
			_loading = loading;
			Q_EMIT loading_changed( loading );
		}

		bool _show_hidden{ false };

	private:
		bool _loading{ false };
	};

	/// "1.4 GB", "812 KB", "43 bytes" - the sizes shown in the Size column.
	[[nodiscard]] QString format_file_size( quint64 bytes );

	/// "Today 14:03", "Yesterday 09:11", "12 Mar 2024" - the Modified column.
	[[nodiscard]] QString format_timestamp( QDateTime const& timestamp );

} // namespace arterm::files
