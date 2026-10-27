#include "ArchiveUtils.h"
#include "ServiceUtils.h"
#include <QDirIterator>
#include <QFileInfo>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
using ServiceUtils::require;
namespace ArchiveUtils {
QString safePath(const QString &root,const QString &relative) {
    QString rel=relative; rel.replace('\\','/');
    require(!rel.isEmpty()&&!QDir::isAbsolutePath(rel)&&!rel.contains(':')&&!rel.contains(QChar(0)),QStringLiteral("无效的相对路径"));
    const auto parts=rel.split('/');
    for(const auto &p:parts) require(p!="..",QStringLiteral("路径不能越出目标目录"));
    QString base=QDir(root).absolutePath();
    require(!QFileInfo(base).isSymLink(),QStringLiteral("目标目录不能是符号链接"));
    QString check=base;
    for(const auto &part:parts) { if(part.isEmpty()||part==".") continue; check=QDir(check).filePath(part); require(!QFileInfo(check).isSymLink(),QStringLiteral("拒绝访问符号链接：")+check); }
    const auto result=QDir::cleanPath(QDir(base).filePath(rel));
    require(result==base||result.startsWith(base+'/'),QStringLiteral("路径越界"));
    return result;
}
void copyTree(const QString &source,const QString &target) {
    const QString src=QFileInfo(source).canonicalFilePath();
    const QString dst=QDir(target).absolutePath();
    require(QFileInfo(source).isDir()&&!QFileInfo(source).isSymLink()&&!src.isEmpty(),QStringLiteral("源目录无效"));
    require(dst!=src&&!dst.startsWith(src+'/'),QStringLiteral("不能复制到自身或子目录"));
    require(QDir().mkpath(dst),QStringLiteral("无法创建目标目录"));
    QDirIterator it(src,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        it.next(); const auto info=it.fileInfo(); require(!info.isSymLink(),QStringLiteral("复制不允许符号链接"));
        const QString path=safePath(dst,QDir(src).relativeFilePath(info.filePath()));
        if(info.isDir()) require(QDir().mkpath(path),QStringLiteral("创建目录失败"));
        else { require(info.isFile(),QStringLiteral("不支持的文件类型")); require(!QFile::exists(path),QStringLiteral("目标文件已存在：")+path); require(QDir().mkpath(QFileInfo(path).absolutePath())&&QFile::copy(info.filePath(),path),QStringLiteral("复制失败：")+info.fileName()); }
    }
}
void pack(const QString &source,const QString &archivePath) {
    require(!QFileInfo(source).isSymLink()&&QFileInfo(source).isDir(),QStringLiteral("备份目录无效"));
    require(!QFile::exists(archivePath),QStringLiteral("备份文件已存在"));
    std::unique_ptr<archive,decltype(&archive_write_free)> out(archive_write_new(),archive_write_free);
    archive_write_set_format_zip(out.get());
    require(archive_write_open_filename(out.get(),QFile::encodeName(archivePath).constData())==ARCHIVE_OK,QStringLiteral("无法创建备份"));
    QDirIterator it(source,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        it.next(); const auto info=it.fileInfo(); require(!info.isSymLink(),QStringLiteral("备份不允许符号链接"));
        std::unique_ptr<archive_entry,decltype(&archive_entry_free)> entry(archive_entry_new(),archive_entry_free);
        const auto name=QDir(source).relativeFilePath(info.filePath()).toUtf8(); archive_entry_set_pathname(entry.get(),name.constData());
        archive_entry_set_filetype(entry.get(),info.isDir()?AE_IFDIR:AE_IFREG); archive_entry_set_perm(entry.get(),info.isDir()?0755:0644);
        archive_entry_set_size(entry.get(),info.isDir()?0:info.size());
        require(archive_write_header(out.get(),entry.get())==ARCHIVE_OK,QStringLiteral("备份头写入失败"));
        if(info.isFile()) { QFile f(info.filePath());require(f.open(QIODevice::ReadOnly),QStringLiteral("备份文件读取失败")); while(!f.atEnd()) {const auto data=f.read(1024*1024); require(archive_write_data(out.get(),data.constData(),data.size())==data.size(),QStringLiteral("备份写入失败"));} }
    }
    require(archive_write_close(out.get())==ARCHIVE_OK,QStringLiteral("备份提交失败"));
}
void extract(const QString &archivePath,const QString &target,qint64 maxBytes) {
    require(QFileInfo(archivePath).isFile()&&!QFileInfo(archivePath).isSymLink(),QStringLiteral("压缩包路径无效"));
    require(QDir().mkpath(target),QStringLiteral("无法创建解压目录"));
    std::unique_ptr<archive,decltype(&archive_read_free)> in(archive_read_new(),archive_read_free);
    archive_read_support_filter_all(in.get()); archive_read_support_format_all(in.get());
    require(archive_read_open_filename(in.get(),QFile::encodeName(archivePath).constData(),65536)==ARCHIVE_OK,QStringLiteral("无法读取压缩包"));
    archive_entry *entry=nullptr; qint64 total=0; int count=0,status;
    while((status=archive_read_next_header(in.get(),&entry))==ARCHIVE_OK) {
        require(++count<=100000,QStringLiteral("压缩包文件数量超限"));
        const int type=archive_entry_filetype(entry);
        require(!archive_entry_symlink(entry)&&!archive_entry_hardlink(entry)&&(type==AE_IFDIR||type==AE_IFREG),QStringLiteral("压缩包包含链接或特殊文件"));
        const auto path=safePath(target,QString::fromUtf8(archive_entry_pathname(entry)));
        if(type==AE_IFDIR) { require(QDir().mkpath(path),QStringLiteral("创建解压目录失败")); continue; }
        require(archive_entry_size(entry)>=0&&archive_entry_size(entry)<=maxBytes-total,QStringLiteral("压缩包解压体积超限"));
        require(!QFile::exists(path)&&QDir().mkpath(QFileInfo(path).absolutePath()),QStringLiteral("压缩包包含重复文件或无法创建目录"));
        QFile f(path); require(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),QStringLiteral("解压写入失败"));
        char buffer[65536]; la_ssize_t size;
        while((size=archive_read_data(in.get(),buffer,sizeof(buffer)))>0) { total+=size;require(total<=maxBytes,QStringLiteral("解压体积超限"));require(f.write(buffer,size)==size,QStringLiteral("解压磁盘写入失败")); }
        require(size==0,QStringLiteral("压缩包数据损坏"));
    }
    require(status==ARCHIVE_EOF,QStringLiteral("压缩包目录损坏"));
}
}
