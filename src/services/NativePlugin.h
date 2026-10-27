#pragma once
#include <QtPlugin>
#include <QVariantMap>
#include <QString>

// Native ABI v1: same architecture, Qt major/minor and compatible C++ toolchain.
// Plugins execute with the launcher's full user privileges, without a sandbox.
class NativePlugin {
public:
    virtual ~NativePlugin() = default;
    virtual bool start(const QVariantMap &context, QString *error) = 0;
    virtual QVariantMap execute(const QString &command, const QVariantMap &arguments) = 0;
    // Must synchronously disconnect callbacks and stop/join all plugin work.
    virtual void stop() noexcept = 0;
};
#define XMCL_NATIVE_PLUGIN_IID "org.xmcl.NativePlugin/1.0"
Q_DECLARE_INTERFACE(NativePlugin, XMCL_NATIVE_PLUGIN_IID)
