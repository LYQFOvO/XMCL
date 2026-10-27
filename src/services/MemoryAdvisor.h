#pragma once
#include <QString>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#elif defined(Q_OS_LINUX)
#  include <unistd.h>
#endif

// 按本机物理内存给出智能内存建议：
// - 为系统与启动器预留约 2 GB；
// - 游戏堆上限取可用内存的约 40%（向上取整），限制在 2–16 GB；
// - 实例与全局默认都使用同一算法，用户仍可手动覆盖。
namespace MemoryAdvisor {
inline qint64 totalPhysicalBytes() {
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? qint64(status.ullTotalPhys) : 0;
#elif defined(Q_OS_MACOS)
    qint64 memory = 0;
    size_t size = sizeof(memory);
    return sysctlbyname("hw.memsize", &memory, &size, nullptr, 0) == 0 ? memory : 0;
#elif defined(Q_OS_LINUX)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    return (pages > 0 && pageSize > 0) ? qint64(pages) * qint64(pageSize) : 0;
#else
    return 0;
#endif
}
inline int totalGb() {
    const auto bytes = totalPhysicalBytes();
    return bytes > 0 ? qMax(1, int(bytes / (1024ll * 1024 * 1024))) : 0;
}
// 允许的最大堆：至少 2 GB，且为系统保留 2 GB；未知内存时回退 16 GB。
inline int maxGb() {
    const int total = totalGb();
    return total > 0 ? qBound(2, total - 2, 16) : 16;
}
// 纯函数便于测试：totalGbValue 为本机物理内存 GB。
inline int suggestForTotalGb(int totalGbValue) {
    if (totalGbValue <= 0) return 4;
    if (totalGbValue <= 4) return 2;
    const int usable = qMax(2, totalGbValue - 2);
    const int value = (usable * 2 + 4) / 5; // ceil(usable * 0.4)
    return qBound(2, value, qMin(16, qMax(2, totalGbValue - 1)));
}
inline int suggestedGb() { return suggestForTotalGb(totalGb()); }
inline QString detectedText() {
    const int total = totalGb();
    if (total <= 0) return QStringLiteral("无法检测物理内存，请手动选择内存");
    return QStringLiteral("本机物理内存 %1 GB · 智能建议 %2 GB").arg(total).arg(suggestedGb());
}
}
