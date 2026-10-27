#pragma once
#include <QString>
#include <stdexcept>
namespace ArchiveUtils {
QString safePath(const QString &root,const QString &relative);
void copyTree(const QString &source,const QString &target);
void pack(const QString &source,const QString &archivePath);
void extract(const QString &archivePath,const QString &target,qint64 maxBytes=8LL*1024*1024*1024);
}
