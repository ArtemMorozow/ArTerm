#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>

namespace arterm::files {

/// Columns shared by the local and remote panes so a single view class, a
/// single delegate and a single set of sort rules serve both sides.
enum class FileColumn { Name = 0, Size, Modified, Permissions, Count };

/// Item data roles used by the pane and its delegate.
enum FileRole {
    PathRole = Qt::UserRole + 1, ///< Absolute path on its own side.
    IsDirectoryRole,
    IsSymlinkRole,
    IsExecutableRole,
    SizeRole,      ///< Raw byte count for sorting.
    ModifiedRole,  ///< QDateTime for sorting.
    NameRole,      ///< Raw file name, unformatted.
};

/// Interface every browsable directory listing implements.
///
/// The local and remote implementations differ in how they fetch data - one is
/// synchronous, the other goes through a worker thread - but the pane only ever
/// sees this API.
class FileListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    using QAbstractTableModel::QAbstractTableModel;

    /// The directory currently listed.
    [[nodiscard]] virtual QString currentPath() const = 0;

    /// Navigate. Emits `pathChanged` once the listing has arrived.
    virtual void setCurrentPath(const QString &path) = 0;

    /// Re-read the current directory.
    virtual void refresh() = 0;

    /// True for the SFTP-backed side.
    [[nodiscard]] virtual bool isRemote() const = 0;

    /// Parent of `currentPath`, or an empty string at the root.
    [[nodiscard]] virtual QString parentPath() const = 0;

    /// Join `currentPath` with a child name using the right separator.
    [[nodiscard]] virtual QString childPath(const QString &name) const = 0;

    /// Whether hidden entries are listed.
    [[nodiscard]] bool showsHidden() const noexcept { return m_showHidden; }
    virtual void setShowHidden(bool show) = 0;

    // -- Mutations. Implementations that cannot perform them report an error --

    virtual void createDirectory(const QString &name) = 0;
    virtual void rename(const QString &path, const QString &newName) = 0;
    virtual void remove(const QStringList &paths) = 0;

    [[nodiscard]] bool isLoading() const noexcept { return m_loading; }

Q_SIGNALS:
    void pathChanged(const QString &path);
    void loadingChanged(bool loading);
    void errorOccurred(const QString &message);
    /// A mutation finished and callers may want to refresh the other pane.
    void contentChanged();

protected:
    void setLoading(bool loading)
    {
        if (m_loading == loading)
            return;
        m_loading = loading;
        Q_EMIT loadingChanged(loading);
    }

    bool m_showHidden{false};

private:
    bool m_loading{false};
};

/// "1.4 GB", "812 KB", "43 bytes" - the sizes shown in the Size column.
[[nodiscard]] QString formatFileSize(quint64 bytes);

/// "Today 14:03", "Yesterday 09:11", "12 Mar 2024" - the Modified column.
[[nodiscard]] QString formatTimestamp(const QDateTime &timestamp);

} // namespace arterm::files
