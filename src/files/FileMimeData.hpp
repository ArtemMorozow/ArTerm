#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace arterm::files {

/// Standard payload for local files, understood by Finder and every other
/// application.
inline constexpr char kUriListMimeType[] = "text/uri-list";

/// ArTerm's own payload for remote files.
///
/// A remote file has no local URL, so a drag out of the remote pane carries the
/// session id plus the remote paths instead. Only ArTerm can interpret it; a
/// drop onto Finder is therefore rejected rather than silently producing an
/// empty file.
inline constexpr char kRemoteFilesMimeType[] = "application/x-arterm-remote-files";

/// The payload described by `kRemoteFilesMimeType`.
struct RemoteDragPayload {
    QString sessionId;
    QStringList paths;

    [[nodiscard]] bool isValid() const { return !sessionId.isEmpty() && !paths.isEmpty(); }
};

/// Serialise a remote drag into `mime` (JSON, so the format stays debuggable).
void setRemotePayload(QMimeData *mime, const RemoteDragPayload &payload);

/// Read a remote drag back. Returns an invalid payload when `mime` carries
/// something else.
[[nodiscard]] RemoteDragPayload remotePayload(const QMimeData *mime);

/// Local file paths from a `text/uri-list` payload, skipping non-file URLs.
[[nodiscard]] QStringList localPaths(const QMimeData *mime);

} // namespace arterm::files
