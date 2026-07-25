#include "files/FileMimeData.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>

namespace arterm::files {

void setRemotePayload(QMimeData *mime, const RemoteDragPayload &payload)
{
    if (mime == nullptr || !payload.isValid())
        return;

    QJsonArray paths;
    for (const QString &path : payload.paths)
        paths.append(path);

    QJsonObject root;
    root.insert(QStringLiteral("session"), payload.sessionId);
    root.insert(QStringLiteral("paths"), paths);

    mime->setData(QLatin1String(kRemoteFilesMimeType), QJsonDocument(root).toJson(QJsonDocument::Compact));

    // A readable text fallback so dropping onto the terminal or a text editor
    // yields the paths rather than nothing.
    mime->setText(payload.paths.join(QLatin1Char(' ')));
}

RemoteDragPayload remotePayload(const QMimeData *mime)
{
    RemoteDragPayload payload;
    if (mime == nullptr || !mime->hasFormat(QLatin1String(kRemoteFilesMimeType)))
        return payload;

    const QJsonDocument document =
        QJsonDocument::fromJson(mime->data(QLatin1String(kRemoteFilesMimeType)));
    if (!document.isObject())
        return payload;

    const QJsonObject root = document.object();
    payload.sessionId = root.value(QStringLiteral("session")).toString();

    const QJsonArray paths = root.value(QStringLiteral("paths")).toArray();
    payload.paths.reserve(paths.size());
    for (const QJsonValue &value : paths) {
        const QString path = value.toString();
        if (!path.isEmpty())
            payload.paths << path;
    }

    return payload;
}

QStringList localPaths(const QMimeData *mime)
{
    QStringList paths;
    if (mime == nullptr || !mime->hasUrls())
        return paths;

    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }

    return paths;
}

} // namespace arterm::files
