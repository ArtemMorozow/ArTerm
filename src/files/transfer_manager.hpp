#pragma once

#include "ssh/ssh_types.hpp"

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QList>
#include <QString>

namespace arterm::session
{
	class RemoteFileService;
}

namespace arterm::files
{

	/// One queued or running copy between the local and the remote side.
	struct TransferJob
	{
		quint64                id{ 0 };
		quint64                request_id{ 0 }; ///< Id used by RemoteFileService while running.
		ssh::TransferDirection direction{ ssh::TransferDirection::DOWNLOAD };
		ssh::TransferState     state{ ssh::TransferState::QUEUED };

		QString source_path;
		QString destination_path;
		QString display_name;

		ssh::TransferProgress progress;
		QString               error_message;

		QElapsedTimer clock;
	};

	/// The transfer queue behind the file browser's bottom panel.
	///
	/// Jobs run strictly one at a time: they all share a single SFTP connection, so
	/// running two in parallel would only interleave them on the same socket while
	/// making progress reporting meaningless.
	class TransferManager : public QAbstractTableModel
	{
		Q_OBJECT

	public:
		enum class Column { NAME = 0, DIRECTION, PROGRESS, SPEED, STATUS, COUNT };

		explicit TransferManager( session::RemoteFileService* service, QObject* parent = nullptr );

		// QAbstractTableModel.
		[[nodiscard]] int      rowCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] int      columnCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] QVariant data( QModelIndex const& index, int role ) const override;
		[[nodiscard]] QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;

		/// Roles the progress delegate reads.
		enum Roles{
			FRACTION_ROLE = Qt::UserRole + 100,
			STATE_ROLE,
			JOB_ID_ROLE,
		};

		/// Queue a download of `remote_path` into the local directory `local_directory`.
		void enqueue_download( QString const& remote_path, QString const& local_directory );

		/// Queue an upload of `local_path` into the remote directory `remote_directory`.
		void enqueue_upload( QString const& local_path, QString const& remote_directory );

		void cancel( quint64 job_id );
		void cancel_all();
		/// Drop finished, failed and cancelled rows.
		void clear_completed();

		[[nodiscard]] bool has_active_transfers() const;
		[[nodiscard]] int  active_count() const;

	Q_SIGNALS:
		/// A job finished successfully; the panes refresh the affected side.
		void transfer_completed( arterm::ssh::TransferDirection direction, QString const& destination_path );
		void transfer_failed( QString const& display_name, QString const& message );
		/// Aggregate progress for the status bar: 0.0-1.0, or -1 when idle.
		void queue_progress_changed( double fraction, int remaining );

	private:
		void              start_next();
		void              finish_current( ssh::TransferState state, QString const& message );
		[[nodiscard]] int index_of_request( quint64 request_id ) const;
		void              emit_row_changed( int row );
		void              publish_queue_progress();

		session::RemoteFileService* _service;
		QList<TransferJob>          _jobs;
		quint64                     _next_job_id{ 1 };
		int                         _running_row{ -1 };
	};

} // namespace arterm::files
