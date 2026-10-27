#include "DownloadEngine.h"
#include "ServiceUtils.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QTimer>
#include <QSaveFile>
#include <QRegularExpression>
using namespace ServiceUtils;
DownloadEngine::DownloadEngine(QObject *p):QObject(p),m_manager(this){}
bool DownloadEngine::verified(const QString &path,const QByteArray &expected,const QString &algorithm){
    if(expected.isEmpty())return false;QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
    QCryptographicHash::Algorithm type=algorithm=="sha256"?QCryptographicHash::Sha256:algorithm=="sha512"?QCryptographicHash::Sha512:QCryptographicHash::Sha1;
    QCryptographicHash hash(type);return hash.addData(&file)&&hash.result().toHex()==expected.toLower();
}
QVariantMap DownloadEngine::state()const{return {{"error",m_error},{"active",m_busy},{"paused",m_paused},{"completedFiles",m_index},{"totalFiles",m_jobs.size()},{"bytes",m_received},{"totalBytes",m_total},{"name",m_index<m_jobs.size()?QFileInfo(m_jobs[m_index].destination).fileName():QString()}};}
void DownloadEngine::publish(){emit changed(state());}
void DownloadEngine::start(QList<Transfer> jobs,std::function<void(QString)> finished){if(m_busy){finished("已有下载任务正在进行");return;}m_jobs=std::move(jobs);m_index=0;m_busy=true;m_paused=false;m_cancelled=false;m_pauseAbort=false;m_error.clear();m_received=0;m_total=0;++m_generation;m_retryAttempts=0;m_usingFallback=false;m_preferFallback=false;m_finished=std::move(finished);publish();QTimer::singleShot(0,this,&DownloadEngine::next);}
void DownloadEngine::retryLater(int delayMs){const auto generation=m_generation;QTimer::singleShot(delayMs,this,[this,generation]{if(generation!=m_generation||!m_busy)return;next();});}
void DownloadEngine::finish(const QString &error){if(!m_busy)return;m_error=error;m_busy=false;m_file.close();m_reply=nullptr;publish();auto cb=std::move(m_finished);if(cb)cb(error);}
void DownloadEngine::pause(){if(!m_busy||m_paused)return;m_paused=true;if(m_reply){m_pauseAbort=true;m_reply->abort();}publish();}
void DownloadEngine::resume(){if(!m_busy||!m_paused)return;m_paused=false;publish();if(!m_reply)next();}
void DownloadEngine::cancel(){if(!m_busy)return;++m_generation;m_cancelled=true;if(m_reply)m_reply->abort();else finish("下载已取消");}
void DownloadEngine::next(){
    if(!m_busy||m_paused)return;if(m_cancelled){finish("下载已取消");return;}
    while(m_index<m_jobs.size()&&verified(m_jobs[m_index].destination,m_jobs[m_index].digest,m_jobs[m_index].algorithm))++m_index;
    if(m_index==m_jobs.size()){finish({});return;}
    const auto job=m_jobs[m_index];
    // 镜像地址失败时可用官方地址兜底；同一文件最多切换一次。
    const QUrl requestUrl=((m_usingFallback||m_preferFallback)&&job.fallback.isValid())?job.fallback:job.url;
    if(!https(requestUrl,true)||QFileInfo(job.destination).isSymLink()||QFileInfo(job.destination+".part").isSymLink()){finish("下载地址或目标路径不安全");return;}
    if(!QDir().mkpath(QFileInfo(job.destination).absolutePath())){finish("无法创建下载目录");return;}
    if(verified(job.destination+".part",job.digest,job.algorithm)){
        if(!commitPartial(job.destination)){finish("无法提交下载文件，原文件已保留");return;}
        ++m_index;publish();QTimer::singleShot(0,this,&DownloadEngine::next);return;
    }
    m_file.setFileName(job.destination+".part");if(!m_file.open(QIODevice::ReadWrite)){finish("无法写入下载文件");return;}m_offset=m_file.size();m_file.seek(m_offset);m_received=m_offset;m_total=0;m_writeError.clear();m_headersChecked=false;
    QNetworkRequest request(requestUrl);request.setRawHeader("User-Agent","XMCL/0.2 (native Qt)");request.setTransferTimeout(60000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    if(m_offset)request.setRawHeader("Range",("bytes="+QString::number(m_offset)+"-").toLatin1());
    auto *reply=m_manager.get(request);m_reply=reply;
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{
        if(!m_headersChecked){const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();if(code!=200&&code!=206){reply->readAll();return;}m_headersChecked=true;
            if(code==200&&m_offset){m_file.resize(0);m_file.seek(0);m_offset=0;}
            if(code==206&&!reply->rawHeader("Content-Range").startsWith(("bytes "+QString::number(m_offset)+"-").toLatin1())){m_writeError="下载断点与服务器响应不一致";reply->abort();return;}}
        const auto bytes=reply->readAll();if(m_file.write(bytes)!=bytes.size()){m_writeError="下载写入磁盘失败";reply->abort();}
    });
    connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 bytes,qint64 total){m_received=m_offset+bytes;m_total=total<0?0:m_offset+total;publish();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,job]{
        m_file.close();m_reply=nullptr;reply->deleteLater();if(m_cancelled){finish("下载已取消");return;}if(m_pauseAbort){m_pauseAbort=false;if(!m_paused)QTimer::singleShot(0,this,&DownloadEngine::next);return;}if(m_paused)return;
        if(!m_writeError.isEmpty()){finish(m_writeError);return;}
        const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto errorText=reply->errorString();
        if(reply->error()!=QNetworkReply::NoError||code<200||code>=300){
            // 镜像节点偶发 5xx/限流或连接中断时，保留 .part 并有限重试；4xx/摘要错误不重试。
            const bool transient=(reply->error()!=QNetworkReply::NoError&&reply->error()!=QNetworkReply::OperationCanceledError)
                ||code==408||code==429||code==500||code==502||code==503||code==504;
            if(transient&&m_retryAttempts<2){++m_retryAttempts;publish();retryLater(600*m_retryAttempts);return;}
            // 镜像节点不可用或镜像缺少该文件时，退回官方地址再试一次。
            if(!m_usingFallback&&job.fallback.isValid()&&(transient||code==403||code==404)){m_usingFallback=true;m_preferFallback=true;m_retryAttempts=0;m_file.close();publish();retryLater(200);return;}
            finish(QString("下载失败（HTTP %1）：%2").arg(code).arg(errorText));return;
        }
        if(!job.digest.isEmpty()&&!verified(job.destination+".part",job.digest,job.algorithm)){QFile::remove(job.destination+".part");if(!m_usingFallback&&job.fallback.isValid()){m_usingFallback=true;m_preferFallback=true;m_retryAttempts=0;publish();retryLater(200);return;}finish("文件摘要校验失败，已丢弃损坏文件");return;}
        if(!commitPartial(job.destination)){finish("无法提交下载文件，原文件已保留");return;}
        m_retryAttempts=0;m_usingFallback=false;++m_index;publish();QTimer::singleShot(0,this,&DownloadEngine::next);
    });
}

bool DownloadEngine::commitPartial(const QString &path){
    QFile input(path+".part"); QSaveFile output(path);
    if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))return false;
    while(!input.atEnd()){
        const auto bytes=input.read(1024*1024);
        if((bytes.isEmpty()&&input.error()!=QFile::NoError)||output.write(bytes)!=bytes.size())return false;
    }
    if(!output.commit())return false;
    input.close();QFile::remove(path+".part");return true;
}
