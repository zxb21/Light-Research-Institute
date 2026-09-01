#include "MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("真空深低温控制系统"));
    app.setOrganizationName(QStringLiteral("CryoLab"));
    // Fusion 风格 + 全局暗黑 QSS（QSS 在 MainWindow 构造时注入）
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow w;
    w.show();
    return app.exec();
}
