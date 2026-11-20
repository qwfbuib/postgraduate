#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "filequeue.h"
#include "terminalserver.h"

#include <QDateTime>
#include <QHash>
#include <QMainWindow>

class AddressList;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool autoListen = true, QWidget *parent = nullptr);

private:
    QWidget *buildHeader();
    QWidget *buildTerminalPanel();
    QWidget *buildQueuePanel();
    QWidget *buildCommandPanel();
    QWidget *buildLogPanel();

    void listen();
    void updateListening(bool listening);
    void updateTerminal(const QString &terminal, bool controlConnected, bool fileConnected);
    QString selectedTerminal() const;
    void updateActions();
    void addFiles(const QStringList &paths);
    void addFolder();
    void removeFiles();
    void startBatch();
    void sendCommand(const QString &text);
    void updateFile(int index, FileQueue::Status state, const QString &detail);
    void appendLog(const QString &message);

    TerminalServer m_server;
    FileQueue m_sender;
    struct FileStatus {
        FileQueue::Status state = FileQueue::Status::Sending;
        QString detail;
        qint64 sentSize = -1;
        QDateTime sentModified;
    };

    struct Material {
        QString path;
        QHash<QString, FileStatus> terminalStatus;
    };

    const FileStatus *fileStatus(const Material &material, const QString &terminal) const;
    void refreshFileStatuses();
    void renderFileStatus(int row, const QString &terminal);

    QList<Material> m_materials;
    QList<int> m_batchRows;
    QString m_batchTerminal;
    QSpinBox *m_port;
    AddressList *m_addresses;
    QLabel *m_listening;
    QPushButton *m_start;
    QPushButton *m_stop;
    QTableWidget *m_terminals;
    QLabel *m_terminalCount;
    QLabel *m_target;
    QList<QPushButton *> m_shortcuts;
    QLineEdit *m_message;
    QPushButton *m_sendMessage;
    QTableWidget *m_files;
    QPushButton *m_addFiles;
    QPushButton *m_addFolder;
    QPushButton *m_removeFiles;
    QPushButton *m_sendFiles;
    QLabel *m_batchTarget;
    QProgressBar *m_fileProgress;
    QProgressBar *m_batchProgress;
    QPlainTextEdit *m_log;
};

#endif
