#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QPointer>
#include <QQueue>
#include <QUrl>
#include <QVariantMap>
#include <functional>
struct Transfer { QUrl url; QString destination; QByteArray digest; QString algorithm="sha1"; QUrl fallback; qint64 expectedSize=-1; };
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    explicit DownloadEngine(QObject *parent=nullptr);
    bool busy() const { return m_busy; }
    void start(QList<Transfer> jobs,std::function<void(QString)> finished);
    void pause(); void resume(); void cancel();
    static bool verified(const QString &,const QByteArray &,const QString &);
    QVariantMap state() const;
signals:
    void changed(const QVariantMap &state);
private:
    QNetworkAccessManager m_manager;
    QList<Transfer> m_jobs;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    int m_index=0;
    qint64 m_offset=0,m_received=0,m_total=0;
    bool m_busy=false,m_paused=false,m_cancelled=false,m_headersChecked=false;
    bool m_pauseAbort=false;
    quint64 m_generation=0;
    int m_retryAttempts=0;
    bool m_usingFallback=false,m_preferFallback=false;
    QString m_writeError,m_error;
    bool commitPartial(const QString &);
    std::function<void(QString)> m_finished;
    void next(); void finish(const QString &);void publish();void retryLater(int delayMs);
};
