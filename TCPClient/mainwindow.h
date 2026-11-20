#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QLabel>
#include <QFile>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    QTcpSocket  *m_tcpTextClient;  //文本消息socket
    QLabel  *m_labSocketState;  //状态栏显示标签

    QFile *m_localFile;  //要发送的文件
    qint64 m_totalBytes;  //数据总大小
    qint64 m_bytesWritten;  //已经发送数据大小
    qint64 m_bytesToWrite;   //剩余数据大小
    qint64 m_loadSize;   //每次发送数据的大小
    QString m_fileName;  //保存文件路径
    QByteArray m_outBlock;  //数据缓冲区，即存放每次要发送的数据
    QTcpSocket  *m_tcpFileClient;  //文件消息socket

    qint64 m_bytesReceived;  // 已收到数据的大小
    qint64 m_fileNameSize;  // 文件名的大小信息
    QByteArray m_inBlock;

    QString getLocalIP();//获取本机IP地址
    void m_Sleep(int msec);
protected:
    void    closeEvent(QCloseEvent *event);


private slots:
    void    onConnected();
    void    onDisconnected();
    void    onSocketStateChange(QAbstractSocket::SocketState socketState);
    void    onSocketReadyRead();//读取socket传入的数据

    void    onFileConnected();
    void    onFileDisconnected();

    void startTransfer();  //发送文件大小等信息
    void updateClientProgress(qint64); //发送数据，更新进度条
    void displayError(QAbstractSocket::SocketError); //显示错误
    void updateServerProgress();

    void on_actConnect_triggered();

    void on_actDisconnect_triggered();

    void on_actClear_triggered();

    void on_btnSend_clicked();

    void on_btnSelectFile_clicked();

    void on_btnSendFile_clicked();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
