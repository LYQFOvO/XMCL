#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QImage>
#include <QIcon>
#include <QTimer>
#include <QTemporaryDir>
#include "LauncherController.h"
#include "services/ServiceHub.h"
#include <QStandardPaths>

Q_IMPORT_QML_PLUGIN(XMCLPlugin)

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("XMCL");
    QCoreApplication::setApplicationName("XMCL");
    QCoreApplication::setApplicationVersion("0.2.0");
    QGuiApplication::setWindowIcon(QIcon(":/assets/icons/grass.svg"));
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.setApplicationDescription("XMCL — Minecraft native Minecraft launcher");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({
        {"screenshot", "Save a rendered window PNG and exit.", "path"},
        {"page", "Initial page: 0 home, 1 library, 2 downloads, 3 settings.", "number", "0"},
        {"size", "Initial window dimensions, for example 1060x720.", "WxH", "1320x860"},
        {"preview-skin", "Open a local skin PNG in the wardrobe preview.", "path"},
        {"clean-settings", "Use temporary preferences for preview or visual verification."}
        ,{"module", "Open a native feature panel (instances, game, accounts, resources, community, packs, bridge).", "name"}
    });
    parser.process(app);
    bool pageOk = false;
    const int page = parser.value("page").toInt(&pageOk);
    const auto dimensions = parser.value("size").split('x');
    bool widthOk = false, heightOk = false;
    const int width = dimensions.value(0).toInt(&widthOk);
    const int height = dimensions.value(1).toInt(&heightOk);
    if (!pageOk || page < 0 || page > 3 || dimensions.size() != 2 || !widthOk || !heightOk || width < 1060 || height < 720) {
        qCritical("Use --page 0..3 and --size WIDTHxHEIGHT (minimum 1060x720).");
        return 2;
    }
    QTemporaryDir temporarySettings;
    LauncherController controller(parser.isSet("clean-settings") ? temporarySettings.filePath("settings.ini") : QString());
    ServiceHub features(parser.isSet("clean-settings") ? temporarySettings.filePath("data") : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    features.setDefaultMemory(controller.memoryGb());
    QObject::connect(&controller,&LauncherController::memoryGbChanged,&features,[&]{features.setDefaultMemory(controller.memoryGb());});
    controller.setNativeInstances(features.instances());
    QObject::connect(&features, &ServiceHub::instancesChanged, &controller, [&]{ controller.setNativeInstances(features.instances()); });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("launcher", &controller);
    engine.rootContext()->setContextProperty("features", &features);
    engine.loadFromModule("XMCL", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 1;
    window->resize(width, height);
    window->setProperty("currentPage", page);
    if (parser.isSet("module")) {
        const QString module = parser.value("module");
        if (features.actions(module).isEmpty()) { qCritical("Unknown feature module."); return 2; }
        QMetaObject::invokeMethod(window, "openFeatures", Q_ARG(QVariant, module), Q_ARG(QVariant, QVariantMap{}));
    }
    if(parser.isSet("preview-skin")){
        QMetaObject::invokeMethod(window,"openFeatures",Q_ARG(QVariant,"accounts"),Q_ARG(QVariant,QVariantMap{}));
        features.execute("accounts","skin.import",{{"path",parser.value("preview-skin")},{"variant","classic"}});
    }
    if (parser.isSet("screenshot")) {
        const auto path = parser.value("screenshot");
        QTimer::singleShot(1800, window, [window, path, &app] {
            const auto image = window->grabWindow();
            if (image.isNull() || !image.save(path)) {
                qCritical("Unable to save rendered window screenshot.");
                app.exit(1);
            } else {
                qInfo().noquote() << "Saved screenshot:" << path;
                app.exit(0);
            }
        });
    }
    return app.exec();
}
