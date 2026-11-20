#include "clientwindow.h"

#include "display.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTextCodec>
#include <QVBoxLayout>

namespace {

using Mode = LegacyProtocol::DisplayMode;

constexpr auto Theme = R"(
    QMainWindow, QDialog { background: #0f2025; }
    QWidget { color: #e6efeb; }
    QLabel#brand { font-size: 27px; font-weight: 700; color: #f4f7ee; }
    QLabel#pageTitle { color: #acccba; font-size: 16px; font-weight: 600; }
    QWidget#displayBoard { background: #0c181c; border: 1px solid #35524e; }
    QLabel#mediaHint { color: #a6c1b6; font-size: 20px; background: #14292d; }
    QPlainTextEdit#visitorNotice {
        background: #193236; color: #eef3e6; padding: 27px 29px;
        border-left: 4px solid #94bfa8; font-size: 23px; selection-background-color: #446a60;
    }
    QFrame#weatherCard { background: #173238; border: 1px solid #3b5954; border-radius: 10px; }
    QLabel#weatherTemperature { color: #e3efcf; font-size: 35px; font-weight: 700; }
    QLabel#weatherTitle { color: #f0f5e8; font-size: 16px; font-weight: 600; }
    QLabel#weatherConditions { color: #c2d7cd; }
    QLabel#weatherReport { color: #b7d4c7; font-size: 13px; }
    QLabel#debugHeading { font-size: 15px; font-weight: 700; color: #d4e8d8; }
    QLineEdit, QSpinBox, QPlainTextEdit#debugLog {
        background: #11272c; color: #e6efeb; border: 1px solid #4b6861; border-radius: 5px;
        padding: 5px 7px; selection-background-color: #426b5e;
    }
    QLineEdit:focus, QSpinBox:focus { border-color: #9cc9b0; }
    QPushButton {
        background: #28464b; color: #e8f2e9; border: 1px solid #4d7067;
        border-radius: 5px; padding: 5px 9px; min-height: 20px;
    }
    QPushButton:hover { background: #34595c; }
    QPushButton:pressed { background: #213f41; }
    QPushButton:focus { border-color: #b1d8ba; }
    QCheckBox { spacing: 6px; }
    QScrollBar:vertical { background: #193236; width: 13px; }
    QScrollBar::handle:vertical { background: #52776d; border-radius: 4px; min-height: 22px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)";

class ScenicMark final : public QWidget
{
public:
    explicit ScenicMark(QWidget *parent) : QWidget(parent) { setFixedSize(43, 43); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#264941"));
        painter.drawRoundedRect(QRectF(1, 1, 41, 41), 9, 9);
        painter.setPen(QPen(QColor("#c5ddbb"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(QPolygonF(QVector<QPointF>{{8, 31}, {18, 16}, {23, 24}, {28, 19}, {35, 31}}));
        painter.drawLine(QPointF(8, 34), QPointF(35, 34));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#edcc91"));
        painter.drawEllipse(QPointF(28, 12), 2.6, 2.6);
    }
};

QLabel *label(const QString &text, const char *objectName, QWidget *parent)
{
    auto *widget = new QLabel(text, parent);
    widget->setTextFormat(Qt::PlainText);
    widget->setObjectName(QString::fromLatin1(objectName));
    return widget;
}

QPushButton *button(const QString &text, const char *objectName, QWidget *parent)
{
    auto *widget = new QPushButton(text, parent);
    widget->setObjectName(QString::fromLatin1(objectName));
    return widget;
}

QString displayValue(const QString &value, const QString &unit)
{
    return value.isEmpty() ? QStringLiteral("—") : value + unit;
}

}

ClientWindow::ClientWindow(const QString &receiveDirectory, QSettings &settings, bool autoConnect)
    : m_receiveDirectory(receiveDirectory), m_settings(settings),
      m_connection(receiveDirectory), m_weather()
{
    setWindowTitle(QStringLiteral("景区信息展示"));
    resize(1280, 720);
    setMinimumSize(370, 480);
    setStyleSheet(QString::fromLatin1(Theme));

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setSpacing(13);
    layout->setContentsMargins(21, 16, 21, 17);
    layout->addWidget(buildHeader());
    m_board = new DisplayBoard(content);
    layout->addWidget(m_board, 1);
    layout->addWidget(buildWeather());
    setCentralWidget(content);
    buildDebugPanel();

    connect(m_board, &DisplayBoard::pageChanged, this, &ClientWindow::updatePageTitle);
    connect(m_board, &DisplayBoard::logMessage, this, &ClientWindow::appendLog);
    connect(&m_connection, &ClientConnection::statusChanged, m_connectionStatus, &QLabel::setText);
    connect(&m_connection, &ClientConnection::logMessage, this, &ClientWindow::appendLog);
    connect(&m_connection, &ClientConnection::displayRequested, m_board, &DisplayBoard::select);
    connect(&m_connection, &ClientConnection::noticeReceived, this, &ClientWindow::publishNotice);
    connect(&m_connection, &ClientConnection::fileReceived, this, &ClientWindow::fileReceived);
    connect(&m_weather, &WeatherService::changed, this, &ClientWindow::updateWeather);

    auto *debugShortcut = new QShortcut(QKeySequence(Qt::Key_F12), this);
    debugShortcut->setContext(Qt::ApplicationShortcut);
    connect(debugShortcut, &QShortcut::activated, this, &ClientWindow::toggleDebug);
    auto *fullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullscreenShortcut->setContext(Qt::ApplicationShortcut);
    connect(fullscreenShortcut, &QShortcut::activated, this, &ClientWindow::toggleFullscreen);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        if (isFullScreen()) {
            showNormal();
        }
    });

    m_noticeTime = m_settings.value(QStringLiteral("notice/time"), 0).toLongLong();
    m_board->setNotice(m_settings.value(QStringLiteral("notice/text")).toString());
    updatePageTitle();
    rescan();
    refreshWeather();
    if (autoConnect) {
        connectToServer();
    }
}

QWidget *ClientWindow::buildHeader()
{
    auto *header = new QWidget(this);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new ScenicMark(header));
    layout->addWidget(label(QStringLiteral("景区信息"), "brand", header));
    layout->addStretch();
    m_pageTitle = label({}, "pageTitle", header);
    layout->addWidget(m_pageTitle);
    return header;
}

QWidget *ClientWindow::buildWeather()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("weatherCard"));
    m_weatherLayout = new QGridLayout(card);
    m_weatherLayout->setContentsMargins(18, 10, 18, 10);
    m_weatherLayout->setHorizontalSpacing(17);
    m_weatherLayout->setVerticalSpacing(4);

    m_temperature = label(QStringLiteral("—"), "weatherTemperature", card);
    m_weatherTitle = label(QStringLiteral("天气实况"), "weatherTitle", card);
    m_weatherTitle->setWordWrap(true);
    m_conditions = label({}, "weatherConditions", card);
    m_conditions->setWordWrap(true);
    m_report = label({}, "weatherReport", card);
    m_report->setWordWrap(true);
    arrangeWeather();
    return card;
}

void ClientWindow::arrangeWeather()
{
    const bool compact = width() < 720 || width() < height();
    if (m_weatherLaidOut && compact == m_compactWeather) {
        return;
    }
    m_weatherLaidOut = true;
    m_compactWeather = compact;

    for (auto *widget : {m_temperature, m_weatherTitle, m_conditions, m_report}) {
        m_weatherLayout->removeWidget(widget);
    }

    m_weatherLayout->addWidget(m_temperature, 0, 0, 2, 1);
    m_weatherLayout->addWidget(m_weatherTitle, 0, 1);
    m_weatherLayout->addWidget(m_conditions, 1, 1);
    if (compact) {
        m_weatherLayout->addWidget(m_report, 2, 0, 1, 2);
    } else {
        m_weatherLayout->addWidget(m_report, 0, 2, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_weatherLayout->setColumnStretch(1, 1);
    m_weatherLayout->parentWidget()->setProperty("compact", compact);
}

void ClientWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_weatherLayout) {
        arrangeWeather();
    }
}

void ClientWindow::buildDebugPanel()
{
    m_debug = new QDialog(this, Qt::Tool);
    m_debug->setObjectName(QStringLiteral("debugPanel"));
    m_debug->setWindowTitle(QStringLiteral("展示端调试面板"));
    m_debug->resize(475, 605);
    auto *layout = new QVBoxLayout(m_debug);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(8);
    layout->addWidget(label(QStringLiteral("管理端连接"), "debugHeading", m_debug));

    auto *connection = new QGridLayout;
    m_host = new QLineEdit(m_settings.value(QStringLiteral("server/host"), QStringLiteral("127.0.0.1")).toString(), m_debug);
    m_host->setAccessibleName(QStringLiteral("服务器地址"));
    m_host->setObjectName(QStringLiteral("serverHost"));
    m_port = new QSpinBox(m_debug);
    m_port->setObjectName(QStringLiteral("serverPort"));
    m_port->setRange(1, 65535);
    m_port->setAccessibleName(QStringLiteral("服务器端口"));
    m_port->setValue(m_settings.value(QStringLiteral("server/port"), 6666).toInt());
    auto *start = button(QStringLiteral("连接"), "connectServer", m_debug);
    auto *stop = button(QStringLiteral("断开"), "disconnectServer", m_debug);
    connection->addWidget(label(QStringLiteral("地址"), "", m_debug), 0, 0);
    connection->addWidget(m_host, 0, 1, 1, 2);
    connection->addWidget(label(QStringLiteral("端口"), "", m_debug), 0, 3);
    connection->addWidget(m_port, 0, 4);
    m_connectionStatus = label(QStringLiteral("未连接"), "connectionStatus", m_debug);
    connection->addWidget(m_connectionStatus, 1, 0, 1, 3);
    connection->addWidget(start, 1, 3);
    connection->addWidget(stop, 1, 4);
    connection->setColumnStretch(1, 1);
    layout->addLayout(connection);
    connect(start, &QPushButton::clicked, this, &ClientWindow::connectToServer);
    connect(stop, &QPushButton::clicked, &m_connection, &ClientConnection::disconnectFromServer);

    layout->addWidget(label(QStringLiteral("展示与素材"), "debugHeading", m_debug));
    auto *screens = new QHBoxLayout;
    const struct { Mode mode; const char *name; QString caption; } pages[] = {
        {Mode::Video, "previewVideo", QStringLiteral("视频")},
        {Mode::Images, "previewImages", QStringLiteral("图片")},
        {Mode::Notice, "previewNotice", QStringLiteral("通知")},
        {Mode::Cycle, "previewCycle", QStringLiteral("循环")},
    };
    for (const auto &page : pages) {
        auto *action = button(page.caption, page.name, m_debug);
        connect(action, &QPushButton::clicked, m_board, [this, mode = page.mode] {
            m_board->select(mode);
        });
        screens->addWidget(action);
    }
    layout->addLayout(screens);
    auto *assets = new QHBoxLayout;
    auto *directory = button(QStringLiteral("打开素材目录"), "openMedia", m_debug);
    auto *rescanButton = button(QStringLiteral("重新扫描"), "rescanMedia", m_debug);
    assets->addWidget(directory);
    assets->addWidget(rescanButton);
    layout->addLayout(assets);
    connect(directory, &QPushButton::clicked, this, [this] {
        if (!QDir().mkpath(m_receiveDirectory)
            || !QDesktopServices::openUrl(QUrl::fromLocalFile(m_receiveDirectory))) {
            appendLog(QStringLiteral("无法打开素材目录"));
        }
    });
    connect(rescanButton, &QPushButton::clicked, this, [this] {
        rescan();
        appendLog(QStringLiteral("已重新扫描素材目录"));
    });

    layout->addWidget(label(QStringLiteral("高德天气"), "debugHeading", m_debug));
    auto *weatherForm = new QGridLayout;
    m_city = new QLineEdit(m_settings.value(QStringLiteral("weather/city"), QStringLiteral("510100")).toString(), m_debug);
    m_city->setObjectName(QStringLiteral("weatherCity"));
    m_city->setAccessibleName(QStringLiteral("六位城市编码"));
    m_city->setMaxLength(6);
    m_key = new QLineEdit(qEnvironmentVariable("AMAP_WEATHER_KEY"), m_debug);
    m_key->setObjectName(QStringLiteral("weatherKey"));
    m_key->setAccessibleName(QStringLiteral("高德 Web 服务 Key"));
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("Web 服务 Key"));
    auto *refresh = button(QStringLiteral("应用并刷新"), "refreshWeather", m_debug);
    auto *demo = new QCheckBox(QStringLiteral("演示天气"), m_debug);
    demo->setObjectName(QStringLiteral("demoWeather"));
    demo->setEnabled(m_key->text().trimmed().isEmpty());
    weatherForm->addWidget(label(QStringLiteral("城市编码"), "", m_debug), 0, 0);
    weatherForm->addWidget(m_city, 0, 1);
    weatherForm->addWidget(label(QStringLiteral("Key"), "", m_debug), 1, 0);
    weatherForm->addWidget(m_key, 1, 1, 1, 2);
    weatherForm->addWidget(demo, 2, 1);
    weatherForm->addWidget(refresh, 2, 2);
    m_weatherStatus = label({}, "weatherStatus", m_debug);
    m_weatherStatus->setWordWrap(true);
    weatherForm->addWidget(m_weatherStatus, 3, 0, 1, 3);
    layout->addLayout(weatherForm);
    connect(refresh, &QPushButton::clicked, this, &ClientWindow::refreshWeather);
    connect(demo, &QCheckBox::toggled, &m_weather, &WeatherService::setDemo);
    connect(m_key, &QLineEdit::textChanged, this, [demo](const QString &key) {
        demo->setEnabled(key.trimmed().isEmpty());
    });

    layout->addWidget(label(QStringLiteral("连接与素材日志"), "debugHeading", m_debug));
    m_log = new QPlainTextEdit(m_debug);
    m_log->setObjectName(QStringLiteral("debugLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    layout->addWidget(m_log, 1);
}

void ClientWindow::toggleDebug()
{
    if (m_debug->isVisible()) {
        m_debug->hide();
    } else {
        m_debug->show();
        m_debug->raise();
        m_debug->activateWindow();
    }
}

void ClientWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void ClientWindow::connectToServer()
{
    const QString host = m_host->text().trimmed();
    if (host.isEmpty()) {
        appendLog(QStringLiteral("请输入服务器地址"));
        m_host->setFocus();
        return;
    }
    m_settings.setValue(QStringLiteral("server/host"), host);
    m_settings.setValue(QStringLiteral("server/port"), m_port->value());
    m_settings.sync();
    m_connection.connectToServer(host, quint16(m_port->value()));
}

void ClientWindow::refreshWeather()
{
    const QString city = m_city->text().trimmed();
    if (!WeatherService::isCityCode(city)) {
        m_weatherStatus->setText(QStringLiteral("请输入六位城市编码"));
        return;
    }

    m_settings.setValue(QStringLiteral("weather/city"), city);
    m_settings.sync();
    m_weather.configure(city, m_key->text().trimmed());
}

void ClientWindow::updateWeather()
{
    using Status = WeatherService::Status;
    const auto status = m_weather.status();
    const auto weather = m_weather.reading();

    m_temperature->setText(displayValue(weather.temperature, QStringLiteral("°C")));
    m_weatherTitle->setText(weather.city.isEmpty() ? QStringLiteral("天气实况")
                          : QStringLiteral("%1 · %2").arg(weather.city, weather.condition));
    const auto wind = (weather.windDirection + QLatin1Char(' ')
                       + (weather.windPower.isEmpty() ? QString{} : weather.windPower + QStringLiteral("级"))).trimmed();
    m_conditions->setText(weather.city.isEmpty() ? QString{}
        : QStringLiteral("湿度 %1    风向 %2").arg(displayValue(weather.humidity, QStringLiteral("%")),
                           wind.isEmpty() ? QStringLiteral("—") : wind));

    QString report;
    QString statusText;
    switch (status) {
    case Status::NoKey:
        report = QStringLiteral("暂未配置天气");
        statusText = QStringLiteral("未设置高德 Web 服务 Key");
        break;
    case Status::Loading:
        report = QStringLiteral("天气更新中");
        statusText = report;
        break;
    case Status::Live:
        statusText = QStringLiteral("已获取实况天气");
        break;
    case Status::Stale:
        report = QStringLiteral("天气未更新");
        statusText = m_weather.error().isEmpty() ? report : m_weather.error();
        break;
    case Status::Unavailable:
        report = QStringLiteral("天气暂不可用");
        statusText = m_weather.error();
        break;
    case Status::Demo:
        report = QStringLiteral("演示数据");
        statusText = QStringLiteral("正在展示演示天气");
        break;
    }

    if (!weather.published.isEmpty()) {
        const QString published = QStringLiteral("发布于 %1").arg(weather.published);
        report = report.isEmpty() ? published : report + QStringLiteral("；") + published;
    }
    m_report->setText(report);
    m_conditions->setVisible(!weather.city.isEmpty());
    m_weatherStatus->setText(statusText);
}

void ClientWindow::rescan(bool readNotices)
{
    const QDir directory(m_receiveDirectory);
    const auto files = directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    const QStringList videos = {QStringLiteral("mp4"), QStringLiteral("wmv"), QStringLiteral("avi"),
                                QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("mkv")};
    const QStringList images = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                                QStringLiteral("bmp"), QStringLiteral("webp")};
    QStringList videoPaths;
    QStringList imagePaths;
    QFileInfo newestNotice;
    for (const auto &file : files) {
        const QString extension = file.suffix().toLower();
        if (videos.contains(extension)) {
            videoPaths.append(file.absoluteFilePath());
        } else if (images.contains(extension)) {
            imagePaths.append(file.absoluteFilePath());
        } else if (readNotices && extension == QStringLiteral("txt")
                   && file.lastModified() > newestNotice.lastModified()) {
            newestNotice = file;
        }
    }

    m_board->setMedia(videoPaths, imagePaths);
    if (newestNotice.exists() && newestNotice.lastModified().toMSecsSinceEpoch() > m_noticeTime) {
        readNoticeFile(newestNotice.absoluteFilePath());
    }
}

void ClientWindow::fileReceived(const QString &name)
{
    if (QFileInfo(name).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0) {
        readNoticeFile(QDir(m_receiveDirectory).filePath(name));
    } else {
        rescan(false);
    }
}

bool ClientWindow::readNoticeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > LegacyProtocol::MaxTextBytes) {
        appendLog(QStringLiteral("无法读取通知文件：%1").arg(QFileInfo(path).fileName()));
        return false;
    }

    const auto contents = file.readAll();
    QTextCodec::ConverterState conversion;
    const QString text = QTextCodec::codecForName("UTF-8")->toUnicode(
        contents.constData(), contents.size(), &conversion);
    if (file.error() != QFileDevice::NoError || conversion.invalidChars || conversion.remainingChars
        || text.trimmed().isEmpty()) {
        appendLog(QStringLiteral("通知文件需包含 UTF-8 文本：%1").arg(QFileInfo(path).fileName()));
        return false;
    }

    publishNotice(text);
    return true;
}

void ClientWindow::publishNotice(const QString &text)
{
    m_board->setNotice(text);
    m_noticeTime = QDateTime::currentMSecsSinceEpoch();
    m_settings.setValue(QStringLiteral("notice/text"), text);
    m_settings.setValue(QStringLiteral("notice/time"), m_noticeTime);
    m_settings.sync();
    appendLog(QStringLiteral("已更新展示通知"));
}

void ClientWindow::updatePageTitle()
{
    switch (m_board->page()) {
    case Mode::Video: m_pageTitle->setText(QStringLiteral("景区影像")); break;
    case Mode::Images: m_pageTitle->setText(QStringLiteral("风景图集")); break;
    case Mode::Notice: m_pageTitle->setText(QStringLiteral("景区通知")); break;
    case Mode::Cycle: break;
    }
}

void ClientWindow::appendLog(const QString &text)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), text));
}
