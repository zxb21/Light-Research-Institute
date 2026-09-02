#include "MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    // QApplication 管理事件循环、全局样式和窗口生命周期；一个桌面程序只创建一次。
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("真空深低温控制系统"));
    app.setOrganizationName(QStringLiteral("CryoLab"));
    // Fusion 风格 + 全局暗黑 QSS（QSS 在 MainWindow 构造时注入）
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // 创建窗口后进入事件循环；后续按钮点击和定时器都由 app.exec() 分发。
    MainWindow w;
    w.show();
    return app.exec();
}
