#include "mainwindow.h"
#include "ui_mainwindow.h"

#include    <QHostAddress>
#include    <QHostInfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>
#include <QScreen>
#include    <QtNetwork>

QString MainWindow::getLocalIP()
{
    QString hostName=QHostInfo::localHostName();//本地主机名
    QHostInfo   hostInfo=QHostInfo::fromName(hostName);
    QString   localIP="";

    QList<QHostAddress> addList=hostInfo.addresses();//

    if (!addList.isEmpty())
    for (int i=0;i<addList.count();i++)
    {
        QHostAddress aHost=addList.at(i);
        if (QAbstractSocket::IPv4Protocol==aHost.protocol())
        {
            localIP=aHost.toString();
            break;
        }
    }
    return localIP;
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_tcpTextClient->state()==QAbstractSocket::ConnectedState)
        m_tcpTextClient->disconnectFromHost();
    if (m_tcpFileClient->state()==QAbstractSocket::ConnectedState)
        m_tcpFileClient->disconnectFromHost();
    event->accept();
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->spinPort->setValue(6666);
    //QList <QScreen *> list_screen = QGuiApplication::screens();
    //QScreen *screen = list_screen.at(0);
    //qDebug() << "list_screen.width" << screen->geometry().width();
    //qDebug() << "list_screen.height" << screen->geometry().height();

    //#if __arm__
    /* 重设大小 */
    //this->resize(list_screen.at(0)->geometry().width(),
    //list_screen.at(0)->geometry().height());
    //#endif

    m_loadSize = 4*1024; // 每次发送的文件数据大小
    m_totalBytes = 0;
    m_bytesWritten = 0;
    m_bytesToWrite = 0;

    m_bytesReceived = 0;
    m_fileNameSize = 0;

    m_tcpTextClient = new QTcpSocket(this); //创建socket变量
    m_tcpFileClient = new QTcpSocket(this);

    m_labSocketState = new QLabel("Socket状态：");//状态栏标签
    m_labSocketState->setMinimumWidth(250);
    ui->statusBar->addWidget(m_labSocketState);

    QString localIP = getLocalIP();//本机IP
    this->setWindowTitle(this->windowTitle()+"----本机IP："+localIP);
    ui->comboServer->addItem(localIP);
    ui->comboServer->addItem("192.168.0.126");
    ui->comboServer->setCurrentText("192.168.0.126");


    connect(m_tcpTextClient,SIGNAL(connected()),this,SLOT(onConnected()));
    connect(m_tcpTextClient,SIGNAL(disconnected()),this,SLOT(onDisconnected()));

    connect(m_tcpTextClient,SIGNAL(stateChanged(QAbstractSocket::SocketState)),
            this,SLOT(onSocketStateChange(QAbstractSocket::SocketState)));
    connect(m_tcpTextClient,SIGNAL(readyRead()),
            this,SLOT(onSocketReadyRead()));


    connect(m_tcpFileClient,SIGNAL(connected()),this,SLOT(onFileConnected()));
    connect(m_tcpFileClient,SIGNAL(disconnected()),this,SLOT(onFileDisconnected()));
    //当有数据发送成功时，更新进度条
    connect(m_tcpFileClient,SIGNAL(bytesWritten(qint64)),this,
           SLOT(updateClientProgress(qint64)));
    connect(m_tcpFileClient,SIGNAL(error(QAbstractSocket::SocketError)),this,
           SLOT(displayError(QAbstractSocket::SocketError)));

    connect(m_tcpFileClient,SIGNAL(readyRead()),this,
            SLOT(updateServerProgress()));

    ui->btnSendFile->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onConnected()
{ //connected()信号槽函数
    ui->plainTextEdit->appendPlainText("**已连接到服务器");
    ui->plainTextEdit->appendPlainText("**Text peer address:"+
                                   m_tcpTextClient->peerAddress().toString());
    ui->plainTextEdit->appendPlainText("**Text peer port:"+
                                   QString::number(m_tcpTextClient->peerPort()));
    ui->actConnect->setEnabled(false);
    ui->actDisconnect->setEnabled(true);
}

void MainWindow::onDisconnected()
{//disConnected()信号槽函数
    ui->plainTextEdit->appendPlainText("**已断开与服务器的连接");
    ui->actConnect->setEnabled(true);
    ui->actDisconnect->setEnabled(false);
}

void MainWindow::onFileConnected()
{ //connected()信号槽函数
    ui->plainTextEdit_file->appendPlainText("**已连接到服务器");
    ui->plainTextEdit_file->appendPlainText("**File peer address:"+
                                   m_tcpFileClient->peerAddress().toString());
    ui->plainTextEdit_file->appendPlainText("**File peer port:"+
                                   QString::number(m_tcpFileClient->peerPort()));
}

void MainWindow::onFileDisconnected()
{//disConnected()信号槽函数
    ui->plainTextEdit_file->appendPlainText("**已断开与服务器的连接");
}

void MainWindow::onSocketReadyRead()
{//readyRead()信号槽函数
    while(m_tcpTextClient->canReadLine())
        ui->plainTextEdit->appendPlainText("[in] "+m_tcpTextClient->readLine());
}

void MainWindow::onSocketStateChange(QAbstractSocket::SocketState socketState)
{//stateChange()信号槽函数
    switch(socketState)
    {
    case QAbstractSocket::UnconnectedState:
        m_labSocketState->setText("scoket状态：UnconnectedState");
        break;
    case QAbstractSocket::HostLookupState:
        m_labSocketState->setText("scoket状态：HostLookupState");
        break;
    case QAbstractSocket::ConnectingState:
        m_labSocketState->setText("scoket状态：ConnectingState");
        break;

    case QAbstractSocket::ConnectedState:
        m_labSocketState->setText("scoket状态：ConnectedState");
        break;

    case QAbstractSocket::BoundState:
        m_labSocketState->setText("scoket状态：BoundState");
        break;

    case QAbstractSocket::ClosingState:
        m_labSocketState->setText("scoket状态：ClosingState");
        break;

    case QAbstractSocket::ListeningState:
        m_labSocketState->setText("scoket状态：ListeningState");
    }
}

void MainWindow::on_actConnect_triggered()
{//连接到服务器
    QString addr = ui->comboServer->currentText();
    quint16 port = ui->spinPort->value();
    m_tcpTextClient->connectToHost(addr, port);

    m_Sleep(100);

    m_bytesWritten = 0; // 初始化已发送字节为0
    m_tcpFileClient->connectToHost(addr, port);
}

void MainWindow::on_actDisconnect_triggered()
{//断开与服务器的连接
    if (m_tcpTextClient->state()==QAbstractSocket::ConnectedState)
        m_tcpTextClient->disconnectFromHost();
    if (m_tcpFileClient->state()==QAbstractSocket::ConnectedState)
        m_tcpFileClient->disconnectFromHost();
}

void MainWindow::on_actClear_triggered()
{
    ui->plainTextEdit->clear();
    ui->plainTextEdit_file->clear();
}

void MainWindow::on_btnSend_clicked()
{//发送数据
    QString  msg=ui->editMsg->text();

    if (msg.isEmpty())
    {
        QMessageBox::information(this, "提示", "发送的消息不能为空!");
        return;
    }

    ui->plainTextEdit->appendPlainText("[out] "+msg);
    ui->editMsg->clear();
    ui->editMsg->setFocus();

    QByteArray  str=msg.toUtf8();
    str.append('\n');
    m_tcpTextClient->write(str);
}

void MainWindow::on_btnSelectFile_clicked()
{
    m_fileName = QFileDialog::getOpenFileName(this, "选择文件");
    if (!m_fileName.isEmpty())
    {
        ui->plainTextEdit_file->appendPlainText(QString("打开文件 %1 成功！").arg(m_fileName));
        ui->btnSendFile->setEnabled(true);
    }

}

void MainWindow::startTransfer()  //实现文件大小等信息的发送
{
    m_localFile = new QFile(m_fileName);
    if(!m_localFile->open(QFile::ReadOnly))
    {
       qDebug() << "open file error!";
       return;
    }

    // 文件总大小
    m_totalBytes = m_localFile->size();

    QDataStream sendOut(&m_outBlock, QIODevice::WriteOnly);
    sendOut.setVersion(QDataStream::Qt_5_9);
    QString currentFileName = m_fileName.right(m_fileName.size() - m_fileName.lastIndexOf('/')-1);

    // 依次写入文件总大小信息空间，文件名大小信息空间，文件名
    sendOut << qint64(0) << qint64(0) << currentFileName;

    // 总大小是文件名大小等信息和实际文件大小的总和
    m_totalBytes += m_outBlock.size();

    sendOut.device()->seek(0);
    // 返回outBolock的开始，用实际的大小信息代替两个qint64(0)空间
    sendOut<<m_totalBytes<<qint64((m_outBlock.size() - sizeof(qint64)*2));
    // 发送完头数据后剩余数据的大小
    m_bytesToWrite = m_totalBytes - m_tcpFileClient->write(m_outBlock);

    m_outBlock.resize(0);
}

// 更新进度条，实现文件的传送
void MainWindow::updateClientProgress(qint64 numBytes)
{

    // 已经发送数据的大小
    m_bytesWritten += (int)numBytes;

    if(m_bytesToWrite > 0) // 剩余数据大小
    {
        // 从文件中取出数据到发送缓冲区，每次发送loadSize大小的数据，这里设置为4KB，如果剩余的数据不足4KB，就发送剩余数据的大小
        m_outBlock = m_localFile->read(qMin(m_bytesToWrite, m_loadSize));

        // 从发送缓冲区发送数据，计算发送完一次数据后还剩余数据的大小
        m_bytesToWrite -= (int)m_tcpFileClient->write(m_outBlock);

        // 清空发送缓冲区
        m_outBlock.resize(0);

    } else
    {
        m_localFile->close(); // 没有数据待发送，则关闭文件
    }

    //更新进度条
    ui->progressBar->setMaximum(m_totalBytes);
    ui->progressBar->setValue(m_bytesWritten);

    if(m_bytesWritten == m_totalBytes) //发送完毕
    {
        ui->plainTextEdit_file->appendPlainText(QString("传送文件 %1 成功").arg(m_fileName));
        m_localFile->close();
    }
}

void MainWindow::on_btnSendFile_clicked()
{
    m_totalBytes = 0;
    m_bytesWritten = 0;
    m_bytesToWrite = 0;
    startTransfer();
}

void MainWindow::updateServerProgress()  //更新进度条，接收数据
{
    QDataStream in(m_tcpFileClient);
    in.setVersion(QDataStream::Qt_5_9);
    if (m_bytesReceived <= sizeof(qint64)*2)
    { //如果接收到的数据小于16个字节，那么是刚开始接收数据，我们保存接收到的的头文件信息
        if((m_tcpFileClient->bytesAvailable() >= sizeof(qint64)*2) && (m_fileNameSize == 0))
        { //接收数据总大小信息和文件名大小信息
            in >> m_totalBytes >> m_fileNameSize;
            m_bytesReceived += sizeof(qint64) * 2;
        }
        if((m_tcpFileClient->bytesAvailable() >= m_fileNameSize) && (m_fileNameSize != 0))
        {  //接收文件名，并建立文件
            in >> m_fileName;
            ui->plainTextEdit_file->appendPlainText(tr("接收文件 %1 ...").arg(m_fileName));
            m_bytesReceived += m_fileNameSize;
            m_localFile= new QFile(QApplication::applicationDirPath() + "/" + m_fileName);
            if(!m_localFile->open(QFile::WriteOnly))
            {
                qDebug() << "open file error!";
                return;
            }
        }
        else
            return;
    }
    if(m_bytesReceived < m_totalBytes)
    {  // 如果接收的数据小于总数据，那么写入文件
        m_bytesReceived += m_tcpFileClient->bytesAvailable();
        m_inBlock= m_tcpFileClient->readAll();
        m_localFile->write(m_inBlock);
        m_inBlock.resize(0);
    }
    //更新进度条
    ui->progressBar->setMaximum(m_totalBytes);
    ui->progressBar->setValue(m_bytesReceived);

    if(m_bytesReceived == m_totalBytes)
    { //接收数据完成时
        m_totalBytes = 0;
        m_bytesReceived = 0;
        m_fileNameSize = 0;
        m_localFile->close();
        ui->plainTextEdit_file->appendPlainText(QString("接收文件 %1 成功！").arg(m_fileName));
    }
}

void MainWindow::displayError(QAbstractSocket::SocketError) //显示错误
{
    ui->plainTextEdit_file->appendPlainText(m_tcpFileClient->errorString());
    m_tcpFileClient->close();
    ui->progressBar->reset();
}

void MainWindow::m_Sleep(int msec)
{
    QEventLoop loop;//定义一个新的事件循环
    QTimer::singleShot(msec, &loop, SLOT(quit()));//创建单次定时器，槽函数为事件循环的退出函数
    loop.exec();//事件循环开始执行，程序会卡在这里，直到定时时间到，本循环被退出
}
