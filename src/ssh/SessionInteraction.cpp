#include "ssh/SessionInteraction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::ssh {

SessionInteraction::SessionInteraction(QObject *parent)
    : QObject(parent)
{
}

QString SessionInteraction::hostKeyCacheKey(const HostKeyInfo &info)
{
    // Keyed by the fingerprint as well as the endpoint: if the key changes
    // mid-run the user must be asked again rather than inheriting the earlier
    // "trust" answer.
    return QStringLiteral("%1:%2/%3").arg(info.hostname).arg(info.port).arg(info.sha256);
}

bool SessionInteraction::confirmHostKey(const HostKeyInfo &info)
{
    const QString key = hostKeyCacheKey(info);

    // Held across the prompt so a concurrent connection to the same host waits
    // here and then finds the cached answer instead of opening a second dialog.
    const std::lock_guard lock(m_promptMutex);

    if (const auto cached = m_hostKeyDecisions.constFind(key);
        cached != m_hostKeyDecisions.constEnd()) {
        return *cached;
    }

    bool accepted = false;

    if (QThread::currentThread() == thread()) {
        handleHostKeyRequest(info, &accepted);
    } else {
        // A functor invocation keeps the out-parameters type safe; the worker
        // thread parks here until the GUI thread has run the lambda.
        QMetaObject::invokeMethod(
            this, [this, &info, &accepted] { handleHostKeyRequest(info, &accepted); },
            Qt::BlockingQueuedConnection);
    }

    m_hostKeyDecisions.insert(key, accepted);
    return accepted;
}

std::optional<QString> SessionInteraction::askCredential(const QString &prompt, bool echo)
{
    const std::lock_guard lock(m_promptMutex);

    if (m_declinedCredentials.contains(prompt))
        return std::nullopt;

    if (const auto cached = m_credentialAnswers.constFind(prompt);
        cached != m_credentialAnswers.constEnd()) {
        return *cached;
    }

    QString answer;
    bool provided = false;

    if (QThread::currentThread() == thread()) {
        handleCredentialRequest(prompt, echo, &answer, &provided);
    } else {
        QMetaObject::invokeMethod(
            this,
            [this, &prompt, echo, &answer, &provided] {
                handleCredentialRequest(prompt, echo, &answer, &provided);
            },
            Qt::BlockingQueuedConnection);
    }

    if (!provided) {
        m_declinedCredentials.insert(prompt);
        return std::nullopt;
    }

    // Cached for the process lifetime only; nothing is written to disk here.
    m_credentialAnswers.insert(prompt, answer);
    return answer;
}

void SessionInteraction::forgetCachedAnswers()
{
    const std::lock_guard lock(m_promptMutex);
    m_credentialAnswers.clear();
    m_declinedCredentials.clear();
}

void SessionInteraction::handleHostKeyRequest(const HostKeyInfo &info, bool *accepted)
{
    Q_EMIT hostKeyDecisionRequested(info, accepted);
}

void SessionInteraction::handleCredentialRequest(const QString &prompt, bool echo, QString *answer,
                                                 bool *provided)
{
    Q_EMIT credentialRequested(prompt, echo, answer, provided);
}

} // namespace arterm::ssh
