#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace arterm::files
{

	/// Standard payload for local files, understood by Finder and every other
	/// application.
	inline constexpr char URI_LIST_MIME_TYPE[] = "text/uri-list";

	/// ArTerm's own payload for remote files.
	///
	/// A remote file has no local URL, so a drag out of the remote pane carries the
	/// session id plus the remote paths instead. Only ArTerm can interpret it; a
	/// drop onto Finder is therefore rejected rather than silently producing an
	/// empty file.
	inline constexpr char REMOTE_FILES_MIME_TYPE[] = "application/x-arterm-remote-files";

	/// The payload described by `REMOTE_FILES_MIME_TYPE`.
	struct RemoteDragPayload
	{
		QString     session_id;
		QStringList paths;

		[[nodiscard]] bool is_valid() const { return !session_id.isEmpty() && !paths.isEmpty(); }
	};

	/// Serialise a remote drag into `mime` (JSON, so the format stays debuggable).
	void set_remote_payload( QMimeData* mime, RemoteDragPayload const& payload );

	/// Read a remote drag back. Returns an invalid payload when `mime` carries
	/// something else.
	[[nodiscard]] RemoteDragPayload remote_payload( QMimeData const* mime );

	/// Local file paths from a `text/uri-list` payload, skipping non-file URLs.
	[[nodiscard]] QStringList local_paths( QMimeData const* mime );

} // namespace arterm::files
