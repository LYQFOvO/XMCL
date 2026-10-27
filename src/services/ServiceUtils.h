#pragma once
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QUrl>
#include <stdexcept>
namespace ServiceUtils {
inline void require(bool ok,const QString &message) { if(!ok) throw std::runtime_error(message.toStdString()); }
inline QVariantMap read(const QString &path,bool optional=false) {
    QFile file(path); if(optional && !file.exists()) return {};
    require(file.open(QIODevice::ReadOnly),QStringLiteral("无法读取：")+path);
    require(file.size()<=32*1024*1024,QStringLiteral("配置文件过大：")+path);
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),QStringLiteral("配置格式错误：")+path);
    return doc.object().toVariantMap();
}
inline void write(const QString &path,const QVariantMap &value) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()),QStringLiteral("无法创建配置目录"));
    QSaveFile file(path); require(file.open(QIODevice::WriteOnly),QStringLiteral("无法保存：")+path);
    const auto data=QJsonDocument::fromVariant(value).toJson();
    require(file.write(data)==data.size() && file.commit(),QStringLiteral("保存失败：")+path);
}
inline QString localPath(const QVariant &value) { const QString s=value.toString(); return s.startsWith("file:")?QUrl(s).toLocalFile():s; }
inline QVariantMap field(QString key,QString label,QString type="text",QVariant value={},bool required=false,QVariantList options={}) {
    return {{"key",key},{"label",label},{"type",type},{"default",value},{"required",required},{"options",options}};
}
inline QVariantMap action(QString id,QString label,QVariantList fields={},QString description={},bool confirm=false) {
    return {{"id",id},{"label",label},{"fields",fields},{"description",description},{"confirm",confirm}};
}
inline bool https(const QUrl &url,bool allowLoopback=false) {
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
        (url.scheme()=="https" || (allowLoopback && url.scheme()=="http" && (url.host()=="127.0.0.1"||url.host()=="::1"||url.host()=="localhost")));
}
}
