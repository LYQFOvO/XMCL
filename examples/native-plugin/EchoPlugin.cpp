#include <QObject>
#include "NativePlugin.h"

class EchoPlugin final : public QObject, public NativePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID XMCL_NATIVE_PLUGIN_IID FILE "echo.json")
    Q_INTERFACES(NativePlugin)
public:
    bool start(const QVariantMap &, QString *) override { m_running = true; return true; }
    QVariantMap execute(const QString &command, const QVariantMap &arguments) override {
        if (!m_running || command != "echo") return {{"message", "Expected echo command on an enabled plugin"}};
        return {{"message", arguments.value("text")}, {"running", m_running}};
    }
    void stop() noexcept override { m_running = false; }
private:
    bool m_running = false;
};
#include "EchoPlugin.moc"
