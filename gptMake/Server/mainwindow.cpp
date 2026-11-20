#include "mainwindow.h"

#include "actionbutton.h"
#include "addresslist.h"
#include "legacyprotocol.h"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr auto WindowStyle = R"(
    QMainWindow { background: #eef2f6; color: #17283b; }
    QWidget { color: #17283b; }
    QFrame#panel { background: #ffffff; border: 1px solid #d7e0e9; border-radius: 10px; }
    QLabel#windowTitle { color: #13253b; font-size: 24px; font-weight: 700; }
    QLabel#panelTitle { color: #13253b; font-size: 16px; font-weight: 700; }
    QLabel[muted="true"] { color: #51647a; }
    QLabel#listenStatus { padding: 5px 9px; border-radius: 5px; font-weight: 600; }
    QLabel#listenStatus[state="idle"] { background: #edf1f5; color: #486078; }
    QLabel#listenStatus[state="running"] { background: #e1f5ee; color: #14654d; }
    QLabel#listenStatus[state="error"] { background: #fceceb; color: #9d3029; }
    QFrame#addressStrip { background: #f4f8fc; border: 1px solid #e4ecf4; border-radius: 7px; }
    QLabel#addressTag {
        background: #ffffff; color: #294862; border: 1px solid #dce7f1;
        border-radius: 4px; padding: 2px 7px;
    }
    QLineEdit:focus, QSpinBox:focus { border: 1px solid #669ce7; }
    QLineEdit, QSpinBox {
        background: #ffffff; selection-background-color: #184f9e; selection-color: #ffffff;
        border: 1px solid #c9d5e1; border-radius: 5px; padding: 5px 7px; min-height: 22px;
    }
    QTableWidget, QPlainTextEdit {
        background: #ffffff; alternate-background-color: #f6f8fb;
        border: 1px solid #d7e0e9; border-radius: 5px;
        selection-background-color: #dbeaff; selection-color: #13253b;
    }
    QTableWidget::item { padding: 3px 6px; }
    QHeaderView::section {
        background: #eef3f9; border: 0; border-bottom: 1px solid #d7e0e9;
        padding: 6px 7px; color: #344b66; font-weight: 600;
    }
    QProgressBar {
        background: #e9eff6; border: 0; border-radius: 4px; min-height: 16px;
        text-align: center; color: #17283b;
    }
    QProgressBar::chunk { background: #9bc3fa; border-radius: 4px; }
    QSplitter::handle { background: transparent; }
)";

QLabel *heading(const QString &text, QWidget *parent, const char *name = "panelTitle")
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(name));
    return label;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    return label;
}


QFrame *panel(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QVBoxLayout *panelLayout(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(16, 15, 16, 15);
    layout->setSpacing(10);
    return layout;
}

QTableWidget *table(const QStringList &headers, QWidget *parent)
{
    auto *result = new QTableWidget(0, headers.size(), parent);
    result->setHorizontalHeaderLabels(headers);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setAlternatingRowColors(true);
    result->setShowGrid(false);
    result->verticalHeader()->hide();
    result->verticalHeader()->setDefaultSectionSize(31);
    result->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return result;
}

void setCell(QTableWidget *table, int row, int column, QTableWidgetItem *item)
{
    item->setToolTip(item->text());
    table->setItem(row, column, item);
}

QProgressBar *progress(const QString &accessibleName, QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setValue(0);
    bar->setAccessibleName(accessibleName);
    return bar;
}

void setListenState(QLabel *label, const QString &text, const char *state)
{
    label->setText(text);
    label->setProperty("state", QString::fromLatin1(state));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QStringList localAddresses()
{
    QStringList ips;
    for (const auto &ip : QNetworkInterface::allAddresses()) {
        if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback()) {
            ips.append(ip.toString());
        }
    }
    ips.removeDuplicates();
    if (ips.isEmpty()) {
        ips.append(QHostAddress(QHostAddress::LocalHost).toString());
    }
    return ips;
}

}

MainWindow::MainWindow(bool autoListen, QWidget *parent)
    : QMainWindow(parent),
      m_server(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("RecvFile")))
{
    setWindowTitle(QStringLiteral("景区信息管理"));
    resize(1260, 784);
    setMinimumSize(990, 665);
    setStyleSheet(QString::fromLatin1(WindowStyle));

    auto *workspace = new QWidget(this);
    auto *layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    layout->addWidget(buildHeader());

    auto *columns = new QSplitter(Qt::Horizontal, workspace);
    columns->setChildrenCollapsible(false);
    columns->setHandleWidth(10);
    columns->addWidget(buildTerminalPanel());
    columns->addWidget(buildQueuePanel());
    columns->addWidget(buildCommandPanel());
    columns->setStretchFactor(1, 1);
    columns->setSizes({235, 675, 290});
    layout->addWidget(columns, 1);
    layout->addWidget(buildLogPanel());
    setCentralWidget(workspace);

    connect(&m_server, &TerminalServer::listeningChanged, this, &MainWindow::updateListening);
    connect(&m_server, &TerminalServer::terminalChanged, this, &MainWindow::updateTerminal);
    connect(&m_server, &TerminalServer::logMessage, this, &MainWindow::appendLog);
    connect(&m_sender, &FileQueue::activeChanged, this, &MainWindow::updateActions);
    connect(&m_sender, &FileQueue::fileChanged, this, &MainWindow::updateFile);
    connect(&m_sender, &FileQueue::progressChanged, this, [this](int file, int batch) {
        m_fileProgress->setValue(file);
        m_batchProgress->setValue(batch);
    });
    connect(&m_sender, &FileQueue::finished, this,
            [this](const QString &terminal, bool success, const QString &detail) {
        m_batchTarget->setText(success
            ? QStringLiteral("已向 %1 发出").arg(terminal)
            : QStringLiteral("发送至 %1 的批次已中止").arg(terminal));
        appendLog(QStringLiteral("%1 · %2").arg(terminal, detail));
        m_batchRows.clear();
        m_batchTerminal.clear();
    });

    updateListening(false);
    updateActions();
    if (autoListen) {
        listen();
    }
}

QWidget *MainWindow::buildHeader()
{
    auto *header = panel(this);
    auto *layout = new QVBoxLayout(header);
    layout->setContentsMargins(17, 12, 17, 12);
    layout->setSpacing(9);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(9);
    toolbar->addWidget(heading(QStringLiteral("景区信息管理"), header, "windowTitle"));
    toolbar->addStretch();

    auto *portLabel = mutedLabel(QStringLiteral("监听端口"), header);
    toolbar->addWidget(portLabel);
    m_port = new QSpinBox(header);
    m_port->setObjectName(QStringLiteral("listenPort"));
    m_port->setAccessibleName(portLabel->text());
    m_port->setRange(1, 65535);
    m_port->setValue(6666);
    toolbar->addWidget(m_port);

    m_listening = new QLabel(header);
    m_listening->setObjectName(QStringLiteral("listenStatus"));
    toolbar->addWidget(m_listening);

    m_start = new ActionButton(QStringLiteral("开始监听"), ActionButton::Glyph::Start,
                               header, ActionButton::Tone::Primary);
    m_start->setObjectName(QStringLiteral("startListening"));
    m_stop = new ActionButton(QStringLiteral("停止监听"), ActionButton::Glyph::Stop,
                              header, ActionButton::Tone::Stop);
    m_stop->setObjectName(QStringLiteral("stopListening"));
    toolbar->addWidget(m_start);
    toolbar->addWidget(m_stop);
    layout->addLayout(toolbar);

    auto *addressStrip = new QFrame(header);
    addressStrip->setObjectName(QStringLiteral("addressStrip"));
    auto *addressRow = new QHBoxLayout(addressStrip);
    addressRow->setContentsMargins(9, 5, 9, 5);
    addressRow->setSpacing(10);
    addressRow->addWidget(mutedLabel(QStringLiteral("本机地址"), addressStrip), 0, Qt::AlignTop);
    m_addresses = new AddressList(addressStrip);
    m_addresses->setAddresses(localAddresses());
    addressRow->addWidget(m_addresses, 1);
    layout->addWidget(addressStrip);

    connect(m_start, &QPushButton::clicked, this, &MainWindow::listen);
    connect(m_stop, &QPushButton::clicked, &m_server, &TerminalServer::stop);
    return header;
}

QWidget *MainWindow::buildTerminalPanel()
{
    auto *frame = panel(this);

    auto *layout = panelLayout(frame);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(heading(QStringLiteral("终端"), frame));
    titleRow->addStretch();
    m_terminalCount = mutedLabel(QStringLiteral("0 台连接"), frame);
    titleRow->addWidget(m_terminalCount);
    layout->addLayout(titleRow);

    m_terminals = table({QStringLiteral("终端 IP"), QStringLiteral("连接状态")}, frame);
    m_terminals->setObjectName(QStringLiteral("terminalList"));
    m_terminals->setAccessibleName(QStringLiteral("终端列表"));
    auto *header = m_terminals->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Fixed);
    header->resizeSection(1, qMax(88, header->fontMetrics().horizontalAdvance(QStringLiteral("连接状态")) + 24));
    const int ipWidth = m_terminals->fontMetrics().horizontalAdvance(QStringLiteral("255.255.255.255"));
    frame->setMinimumWidth(ipWidth + header->sectionSize(1) + 55);
    layout->addWidget(m_terminals);
    connect(m_terminals, &QTableWidget::itemSelectionChanged, this, [this] {
        refreshFileStatuses();
        updateActions();
    });
    return frame;
}

QWidget *MainWindow::buildQueuePanel()
{
    auto *frame = panel(this);
    frame->setMinimumWidth(425);
    auto *layout = panelLayout(frame);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(heading(QStringLiteral("素材发送队列"), frame));
    titleRow->addStretch();
    m_batchTarget = mutedLabel(QStringLiteral("等待发送"), frame);
    m_batchTarget->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(m_batchTarget);
    layout->addLayout(titleRow);

    auto *toolsRow = new QHBoxLayout;
    m_addFiles = new ActionButton(QStringLiteral("添加文件"), ActionButton::Glyph::Add, frame);
    m_addFiles->setObjectName(QStringLiteral("addFiles"));
    m_addFolder = new ActionButton(QStringLiteral("导入文件夹"), ActionButton::Glyph::Folder, frame);
    m_addFolder->setObjectName(QStringLiteral("addFolder"));
    m_removeFiles = new ActionButton(QStringLiteral("移除"), ActionButton::Glyph::Remove, frame);
    m_removeFiles->setObjectName(QStringLiteral("removeFiles"));
    m_sendFiles = new ActionButton(QStringLiteral("发送素材"), ActionButton::Glyph::Send,
                                   frame, ActionButton::Tone::Primary);
    m_sendFiles->setObjectName(QStringLiteral("sendFiles"));
    toolsRow->addWidget(m_addFiles);
    toolsRow->addWidget(m_addFolder);
    toolsRow->addWidget(m_removeFiles);
    toolsRow->addStretch();
    toolsRow->addWidget(m_sendFiles);
    layout->addLayout(toolsRow);

    m_files = table({QStringLiteral("文件名"), QStringLiteral("大小"), QStringLiteral("状态")}, frame);
    m_files->setObjectName(QStringLiteral("fileQueue"));
    m_files->setAccessibleName(QStringLiteral("素材文件队列"));
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto *header = m_files->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Fixed);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    header->resizeSection(1, 92);
    header->resizeSection(2, 85);
    layout->addWidget(m_files, 1);

    auto *progressGrid = new QGridLayout;
    progressGrid->setHorizontalSpacing(10);
    progressGrid->setVerticalSpacing(6);
    m_fileProgress = progress(QStringLiteral("当前文件进度"), frame);
    m_batchProgress = progress(QStringLiteral("整批发送进度"), frame);
    progressGrid->addWidget(mutedLabel(QStringLiteral("当前文件"), frame), 0, 0);
    progressGrid->addWidget(m_fileProgress, 0, 1);
    progressGrid->addWidget(mutedLabel(QStringLiteral("整批进度"), frame), 1, 0);
    progressGrid->addWidget(m_batchProgress, 1, 1);
    progressGrid->setColumnStretch(1, 1);
    layout->addLayout(progressGrid);

    connect(m_addFiles, &QPushButton::clicked, this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, QStringLiteral("添加发送文件"));
        if (!paths.isEmpty()) {
            addFiles(paths);
        }
    });
    connect(m_addFolder, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(m_removeFiles, &QPushButton::clicked, this, &MainWindow::removeFiles);
    connect(m_sendFiles, &QPushButton::clicked, this, &MainWindow::startBatch);
    connect(m_files, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateActions);
    return frame;
}

QWidget *MainWindow::buildCommandPanel()
{
    auto *frame = panel(this);
    frame->setMinimumWidth(254);
    auto *layout = panelLayout(frame);
    layout->addWidget(heading(QStringLiteral("终端控制"), frame));
    m_target = mutedLabel(QStringLiteral("请选择终端"), frame);
    m_target->setWordWrap(true);
    layout->addWidget(m_target);

    auto *actions = new QGridLayout;
    actions->setSpacing(7);
    using LegacyProtocol::DisplayMode;
    using Glyph = ActionButton::Glyph;
    const struct {
        DisplayMode mode;
        Glyph glyph;
        const char *name;
        QString caption;
    } shortcuts[] = {
        {DisplayMode::Video, Glyph::Video, "displayVideo", QStringLiteral("播放视频")},
        {DisplayMode::Images, Glyph::Image, "displayImages", QStringLiteral("显示图片")},
        {DisplayMode::Notice, Glyph::Notice, "displayNotice", QStringLiteral("显示通知")},
        {DisplayMode::Cycle, Glyph::Cycle, "displayCycle", QStringLiteral("循环显示")},
    };
    int index = 0;
    for (const auto &action : shortcuts) {
        auto *shortcut = new ActionButton(action.caption, action.glyph, frame, ActionButton::Tone::Shortcut);
        shortcut->setObjectName(QString::fromLatin1(action.name));
        connect(shortcut, &QPushButton::clicked, this, [this, mode = action.mode] {
            sendCommand(LegacyProtocol::command(mode));
        });
        actions->addWidget(shortcut, index / 2, index % 2);
        m_shortcuts.append(shortcut);
        ++index;
    }
    layout->addLayout(actions);
    layout->addSpacing(9);
    layout->addWidget(mutedLabel(QStringLiteral("自定义消息"), frame));

    m_message = new QLineEdit(frame);
    m_message->setObjectName(QStringLiteral("customMessage"));
    m_message->setAccessibleName(QStringLiteral("自定义消息"));
    m_message->setPlaceholderText(QStringLiteral("输入一行指令或消息"));
    layout->addWidget(m_message);

    m_sendMessage = new ActionButton(QStringLiteral("发送消息"), ActionButton::Glyph::Send, frame);
    m_sendMessage->setObjectName(QStringLiteral("sendMessage"));
    layout->addWidget(m_sendMessage);
    layout->addStretch();
    const auto send = [this] {
        if (m_server.sendText(selectedTerminal(), m_message->text())) {
            m_message->clear();
        } else {
            appendLog(QStringLiteral("消息发送失败，请确认终端连接和消息内容"));
        }
    };
    connect(m_message, &QLineEdit::textChanged, this, &MainWindow::updateActions);
    connect(m_message, &QLineEdit::returnPressed, this, [this, send] {
        if (m_sendMessage->isEnabled()) {
            send();
        }
    });
    connect(m_sendMessage, &QPushButton::clicked, this, send);
    return frame;
}

QWidget *MainWindow::buildLogPanel()
{
    auto *frame = panel(this);
    auto *layout = panelLayout(frame);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(heading(QStringLiteral("收发日志"), frame));
    titleRow->addStretch();
    auto *clear = new ActionButton(QStringLiteral("清空"), ActionButton::Glyph::Clear, frame);
    clear->setAccessibleName(QStringLiteral("清空日志"));
    titleRow->addWidget(clear);
    layout->addLayout(titleRow);

    m_log = new QPlainTextEdit(frame);
    m_log->setObjectName(QStringLiteral("eventLog"));
    m_log->setAccessibleName(QStringLiteral("收发日志"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setFixedHeight(121);
    layout->addWidget(m_log);
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    return frame;
}

void MainWindow::listen()
{
    m_addresses->setAddresses(localAddresses());
    if (!m_server.start(quint16(m_port->value()))) {
        setListenState(m_listening, QStringLiteral("监听失败"), "error");
        m_listening->setToolTip(m_server.errorString());
    }
}

void MainWindow::updateListening(bool listening)
{
    setListenState(m_listening,
                   listening ? QStringLiteral("监听中") : QStringLiteral("未监听"),
                   listening ? "running" : "idle");
    m_listening->setToolTip({});
    m_port->setEnabled(!listening);
    m_start->setEnabled(!listening);
    m_stop->setEnabled(listening);
}

void MainWindow::updateTerminal(const QString &terminal, bool controlConnected, bool fileConnected)
{
    const auto matches = m_terminals->findItems(terminal, Qt::MatchExactly);
    if (!controlConnected) {
        if (!matches.isEmpty()) {
            m_terminals->removeRow(matches.first()->row());
        }
    } else {
        int row = matches.isEmpty() ? -1 : matches.first()->row();
        if (row < 0) {
            row = m_terminals->rowCount();
            m_terminals->insertRow(row);
            setCell(m_terminals, row, 0, new QTableWidgetItem(terminal));
        }

        auto *state = new QTableWidgetItem(fileConnected
            ? QStringLiteral("就绪") : QStringLiteral("仅指令"));
        state->setForeground(QColor(fileConnected ? "#136249" : "#51647a"));
        setCell(m_terminals, row, 1, state);
    }

    if (m_terminals->rowCount() > 0 && selectedTerminal().isEmpty()) {
        m_terminals->selectRow(0);
    }
    m_terminalCount->setText(QStringLiteral("%1 台连接").arg(m_terminals->rowCount()));
    updateActions();
}

QString MainWindow::selectedTerminal() const
{
    const auto rows = m_terminals->selectionModel()->selectedRows();
    const auto *item = rows.isEmpty() ? nullptr : m_terminals->item(rows.first().row(), 0);
    return item ? item->text() : QString{};
}

void MainWindow::updateActions()
{
    const QString terminal = selectedTerminal();
    const bool control = m_server.hasControl(terminal);
    m_target->setText(control
        ? QStringLiteral("当前终端  %1").arg(terminal) : QStringLiteral("请选择终端"));
    for (auto *shortcut : m_shortcuts) {
        shortcut->setEnabled(control);
    }
    m_message->setEnabled(control);
    m_sendMessage->setEnabled(control && !m_message->text().trimmed().isEmpty());

    const bool idle = !m_sender.isActive();
    m_addFiles->setEnabled(idle);
    m_addFolder->setEnabled(idle);
    m_removeFiles->setEnabled(idle && m_files->selectionModel()->hasSelection());
    m_sendFiles->setEnabled(idle && m_server.fileSocket(terminal) && !m_materials.isEmpty());
}

void MainWindow::addFiles(const QStringList &paths)
{
    int added = 0;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString absolutePath = info.absoluteFilePath();
        const auto duplicate = std::any_of(m_materials.cbegin(), m_materials.cend(),
                                           [&absolutePath](const Material &material) {
            return material.path.compare(absolutePath, Qt::CaseInsensitive) == 0;
        });
        if (!info.isFile() || !info.isReadable() || duplicate) {
            continue;
        }

        m_materials.append(Material{absolutePath, {}});
        const int row = m_files->rowCount();
        m_files->insertRow(row);
        auto *name = new QTableWidgetItem(info.fileName());
        name->setToolTip(QDir::toNativeSeparators(absolutePath));
        m_files->setItem(row, 0, name);
        setCell(m_files, row, 1, new QTableWidgetItem(QLocale().formattedDataSize(info.size())));
        setCell(m_files, row, 2, new QTableWidgetItem(QStringLiteral("未发送")));
        ++added;
    }

    appendLog(added > 0 ? QStringLiteral("已添加 %1 个素材").arg(added)
                        : QStringLiteral("没有可添加的新文件"));
    updateActions();
}

void MainWindow::addFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("导入素材文件夹"));
    if (path.isEmpty()) {
        return;
    }

    const auto entries = QDir(path).entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    QStringList paths;
    for (const auto &entry : entries) {
        paths.append(entry.absoluteFilePath());
    }
    addFiles(paths);
}

void MainWindow::removeFiles()
{
    QList<int> rows;
    for (const QModelIndex &index : m_files->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.rbegin(), rows.rend());
    for (int row : rows) {
        m_materials.removeAt(row);
        m_files->removeRow(row);
    }
    updateActions();
}

void MainWindow::startBatch()
{
    const QString terminal = selectedTerminal();
    auto *socket = m_server.fileSocket(terminal);
    if (!socket || m_sender.isActive()) {
        return;
    }

    QStringList paths;
    m_batchRows.clear();
    for (int row = 0; row < m_materials.size(); ++row) {
        const auto &material = m_materials.at(row);
        const auto *status = fileStatus(material, terminal);
        if (!status || status->state != FileQueue::Status::Sent) {
            m_batchRows.append(row);
            paths.append(material.path);
        }
    }

    if (paths.isEmpty()) {
        appendLog(QStringLiteral("%1 · 没有待发送的素材").arg(terminal));
        return;
    }

    m_batchTerminal = terminal;
    refreshFileStatuses();
    m_fileProgress->setValue(0);
    m_batchProgress->setValue(0);
    m_batchTarget->setText(QStringLiteral("发送至 %1").arg(terminal));
    appendLog(QStringLiteral("%1 · 开始发送 %2 个素材").arg(terminal).arg(paths.size()));
    m_sender.start(terminal, socket, paths);
}

void MainWindow::sendCommand(const QString &text)
{
    if (!m_server.sendText(selectedTerminal(), text)) {
        appendLog(QStringLiteral("指令发送失败，请确认终端连接"));
    }
}

const MainWindow::FileStatus *MainWindow::fileStatus(const Material &material, const QString &terminal) const
{
    const auto it = material.terminalStatus.constFind(terminal);
    if (it == material.terminalStatus.cend()) {
        return nullptr;
    }

    if (it->state == FileQueue::Status::Sent) {
        const QFileInfo file(material.path);
        if (file.isFile() && (file.size() != it->sentSize || file.lastModified() != it->sentModified)) {
            return nullptr;
        }
    }
    return &*it;
}

void MainWindow::refreshFileStatuses()
{
    const QString terminal = selectedTerminal();
    for (int row = 0; row < m_materials.size(); ++row) {
        renderFileStatus(row, terminal);
    }
}

void MainWindow::renderFileStatus(int row, const QString &terminal)
{
    auto *item = m_files->item(row, 2);
    const auto *status = fileStatus(m_materials.at(row), terminal);
    if (!status) {
        item->setText(QStringLiteral("未发送"));
        item->setForeground(QBrush());
        item->setToolTip(item->text());
        return;
    }

    switch (status->state) {
    case FileQueue::Status::Sending:
        item->setText(QStringLiteral("发送中"));
        item->setForeground(QColor("#184f9e"));
        break;
    case FileQueue::Status::Sent:
        item->setText(QStringLiteral("已发出"));
        item->setForeground(QColor("#136249"));
        break;
    case FileQueue::Status::Failed:
        item->setText(QStringLiteral("失败"));
        item->setForeground(QColor("#9d3029"));
        break;
    }
    item->setToolTip(status->detail);
}

void MainWindow::updateFile(int index, FileQueue::Status state, const QString &detail)
{
    const int row = m_batchRows.at(index);
    auto &material = m_materials[row];
    auto &status = material.terminalStatus[m_batchTerminal];
    status.state = state;
    status.detail = detail;

    if (state == FileQueue::Status::Sending) {
        const QFileInfo file(material.path);
        status.sentSize = file.size();
        status.sentModified = file.lastModified();
        auto *size = m_files->item(row, 1);
        size->setText(QLocale().formattedDataSize(file.size()));
        size->setToolTip(size->text());
    } else if (state == FileQueue::Status::Sent) {
        appendLog(QStringLiteral("%1 · 已发出文件：%2").arg(m_batchTerminal, detail));
    }

    if (selectedTerminal() == m_batchTerminal) {
        renderFileStatus(row, m_batchTerminal);
    }
}


void MainWindow::appendLog(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[%1]  %2")
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}
