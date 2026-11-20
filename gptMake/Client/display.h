#ifndef DISPLAY_H
#define DISPLAY_H

#include "legacyprotocol.h"
#include "nativevideo.h"

#include <QMediaPlayer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QLabel;
class PicturePanel;
class QPlainTextEdit;
class QStackedWidget;
class QVideoWidget;

class DisplayBoard final : public QWidget
{
    Q_OBJECT

public:
    explicit DisplayBoard(QWidget *parent = nullptr);

    void setMedia(const QStringList &videos, const QStringList &images);
    void setNotice(const QString &notice);
    void select(LegacyProtocol::DisplayMode mode);

    LegacyProtocol::DisplayMode page() const;
    bool cycling() const;
    QString activeImage() const;

signals:
    void pageChanged();
    void logMessage(const QString &message);

private:
    void showPage(LegacyProtocol::DisplayMode page);
    void advancePage();
    void playNextVideo();
    void startQtVideo(const QString &path);
    void queueVideoFailure();
    void showNextImage();
    void updateImageTimer();

    QStackedWidget *m_pages;
    QStackedWidget *m_videoPage;
    QVideoWidget *m_video;
    QLabel *m_videoHint;
    PicturePanel *m_nativePicture;
    QStackedWidget *m_imagePage;
    PicturePanel *m_picture;
    QLabel *m_imageHint;
    QPlainTextEdit *m_notice;
    QMediaPlayer m_player;
    NativeVideo m_nativeVideo;
    bool m_usingNative = false;
    bool m_nativeEnded = false;
    QTimer m_slides;
    QTimer m_cycle;
    QStringList m_videos;
    QStringList m_images;
    QSet<QString> m_failedVideos;
    QString m_currentVideo;
    QString m_currentImage;
    LegacyProtocol::DisplayMode m_page = LegacyProtocol::DisplayMode::Video;
};

#endif
