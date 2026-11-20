#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include "clientconnection.h"
#include "weather.h"

#include <QMainWindow>

class DisplayBoard;
class QCheckBox;
class QDialog;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QResizeEvent;
class QSettings;
class QSpinBox;

class ClientWindow final : public QMainWindow
{
    Q_OBJECT

public:
    ClientWindow(const QString &receiveDirectory, QSettings &settings, bool autoConnect = true);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *buildHeader();
    QWidget *buildWeather();
    void buildDebugPanel();
    void arrangeWeather();
    void updateWeather();
    void toggleDebug();
    void toggleFullscreen();
    void refreshWeather();
    void connectToServer();
    void rescan(bool readNotices = true);
    void fileReceived(const QString &name);
    bool readNoticeFile(const QString &path);
    void publishNotice(const QString &text);
    void updatePageTitle();
    void appendLog(const QString &text);

    QString m_receiveDirectory;
    QSettings &m_settings;
    ClientConnection m_connection;
    WeatherService m_weather;
    DisplayBoard *m_board;
    QLabel *m_pageTitle;
    QGridLayout *m_weatherLayout = nullptr;
    QLabel *m_temperature;
    QLabel *m_weatherTitle;
    QLabel *m_conditions;
    QLabel *m_report;
    bool m_weatherLaidOut = false;
    bool m_compactWeather = false;
    QDialog *m_debug;
    QLineEdit *m_host;
    QSpinBox *m_port;
    QLabel *m_connectionStatus;
    QLineEdit *m_city;
    QLineEdit *m_key;
    QLabel *m_weatherStatus;
    QPlainTextEdit *m_log;
    qint64 m_noticeTime = 0;
};

#endif
