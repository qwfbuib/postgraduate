#include "clientwindow.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QSettings>
#include <QStyleFactory>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ScenicDisplay"));
    app.setApplicationName(QStringLiteral("ScenicClient"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QSettings settings;
    const auto directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("RecvFile"));
    ClientWindow window(directory, settings);
    window.show();
    return app.exec();
}
