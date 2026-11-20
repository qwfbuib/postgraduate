#include "display.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVideoWidget>

class PicturePanel final : public QWidget
{
public:
    explicit PicturePanel(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    bool load(const QString &path)
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (size.isValid() && (size.width() > 2560 || size.height() > 2560)) {
            size.scale(2560, 2560, Qt::KeepAspectRatio);
            reader.setScaledSize(size);
        }

        const auto image = reader.read();
        if (image.isNull()) {
            return false;
        }
        setImage(image);
        return true;
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#0c181c"));
        if (!m_image.isNull()) {
            const auto fitted = m_image.size().scaled(size(), Qt::KeepAspectRatio);
            const QPoint position((width() - fitted.width()) / 2, (height() - fitted.height()) / 2);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRect(position, fitted), m_image);
        }
    }

private:
    QImage m_image;
};

namespace {
using Mode = LegacyProtocol::DisplayMode;

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mediaHint"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(20);
    return label;
}

}

DisplayBoard::DisplayBoard(QWidget *parent)
    : QWidget(parent), m_player(this), m_nativeVideo(this), m_slides(this), m_cycle(this)
{
    setObjectName(QStringLiteral("displayBoard"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    m_videoPage = new QStackedWidget(m_pages);
    m_video = new QVideoWidget(m_videoPage);
    m_video->setAspectRatioMode(Qt::KeepAspectRatio);
    m_video->setMinimumSize(1, 1);
    m_video->setStyleSheet(QStringLiteral("background: #0c181c"));
    m_videoHint = hint(QStringLiteral("暂无视频"), m_videoPage);
    m_videoPage->addWidget(m_videoHint);
    m_videoPage->addWidget(m_video);
    m_nativePicture = new PicturePanel(m_videoPage);
    m_nativePicture->setObjectName(QStringLiteral("nativeVideoFrame"));
    m_videoPage->addWidget(m_nativePicture);

    m_imagePage = new QStackedWidget(m_pages);
    m_picture = new PicturePanel(m_imagePage);
    m_imageHint = hint(QStringLiteral("暂无图片"), m_imagePage);
    m_imagePage->addWidget(m_imageHint);
    m_imagePage->addWidget(m_picture);

    m_notice = new QPlainTextEdit(m_pages);
    m_notice->setObjectName(QStringLiteral("visitorNotice"));
    m_notice->setReadOnly(true);
    m_notice->setFrameShape(QFrame::NoFrame);
    m_notice->setTabChangesFocus(true);
    m_notice->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setNotice({});

    m_pages->addWidget(m_videoPage);
    m_pages->addWidget(m_imagePage);
    m_pages->addWidget(m_notice);

    m_player.setObjectName(QStringLiteral("scenePlayer"));
    m_player.setVideoOutput(m_video);
    m_player.setMuted(true);
    m_player.setNotifyInterval(250);
    m_nativeVideo.setObjectName(QStringLiteral("nativeVideo"));

    connect(&m_nativeVideo, &NativeVideo::frameReady, this, [this](const QImage &frame) {
        if (m_usingNative && m_page == Mode::Video) {
            m_nativePicture->setImage(frame);
            m_videoPage->setCurrentWidget(m_nativePicture);
        }
    });
    connect(&m_nativeVideo, &NativeVideo::ended, this, [this] {
        if (m_usingNative) {
            m_nativeEnded = true;
            if (m_page == Mode::Video) {
                playNextVideo();
            }
        }
    });
    connect(&m_nativeVideo, &NativeVideo::failed, this, [this] {
        if (m_usingNative && !m_currentVideo.isEmpty()) {
            m_usingNative = false;
            startQtVideo(m_currentVideo);
        }
    });

    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (!m_usingNative && status == QMediaPlayer::EndOfMedia && m_page == Mode::Video) {
            QTimer::singleShot(0, this, [this, ended = m_currentVideo] {
                if (m_page == Mode::Video && m_currentVideo == ended) {
                    playNextVideo();
                }
            });
        } else if (status == QMediaPlayer::InvalidMedia) {
            queueVideoFailure();
        }
    });
    connect(&m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            [this](QMediaPlayer::Error error) {
        if (error != QMediaPlayer::NoError) {
            queueVideoFailure();
        }
    });

    m_slides.setObjectName(QStringLiteral("slideTimer"));
    m_slides.setInterval(5000);
    connect(&m_slides, &QTimer::timeout, this, &DisplayBoard::showNextImage);

    m_cycle.setObjectName(QStringLiteral("pageCycle"));
    m_cycle.setInterval(15000);
    connect(&m_cycle, &QTimer::timeout, this, &DisplayBoard::advancePage);
}

void DisplayBoard::setMedia(const QStringList &videos, const QStringList &images)
{
    m_videos = videos;
    for (auto it = m_failedVideos.begin(); it != m_failedVideos.end();) {
        if (m_videos.contains(*it)) {
            ++it;
        } else {
            it = m_failedVideos.erase(it);
        }
    }

    if (!m_currentVideo.isEmpty() && !m_videos.contains(m_currentVideo)) {
        m_nativeVideo.stop();
        m_usingNative = false;
        m_nativeEnded = false;
        m_currentVideo.clear();
        m_player.stop();
        m_player.setMedia(QMediaContent());
    }

    if (m_page == Mode::Video && m_currentVideo.isEmpty()) {
        playNextVideo();
    }

    m_images = images;
    if (!m_images.contains(m_currentImage)) {
        m_currentImage.clear();
        if (m_page == Mode::Images) {
            showNextImage();
        }
    }
    updateImageTimer();
}

void DisplayBoard::setNotice(const QString &notice)
{
    m_notice->setPlainText(notice.isEmpty() ? QStringLiteral("暂无通知") : notice);
}

void DisplayBoard::select(Mode mode)
{
    if (mode == Mode::Cycle) {
        m_cycle.start();
        showPage(Mode::Video);
    } else {
        m_cycle.stop();
        showPage(mode);
    }
}

Mode DisplayBoard::page() const
{
    return m_page;
}

bool DisplayBoard::cycling() const
{
    return m_cycle.isActive();
}

QString DisplayBoard::activeImage() const
{
    return m_currentImage;
}

void DisplayBoard::showPage(Mode page)
{
    m_page = page;
    if (page == Mode::Video) {
        m_pages->setCurrentWidget(m_videoPage);
        if (m_currentVideo.isEmpty() || m_failedVideos.contains(m_currentVideo)
            || m_nativeEnded || (!m_usingNative && m_player.mediaStatus() == QMediaPlayer::EndOfMedia)) {
            playNextVideo();
        } else if (m_usingNative) {
            m_nativeVideo.setPaused(false);
        } else {
            m_player.play();
        }
    } else {
        m_nativeVideo.setPaused(true);
        if (m_player.state() == QMediaPlayer::PlayingState || m_player.mediaStatus() == QMediaPlayer::LoadingMedia) {
            m_player.pause();
        }
        if (page == Mode::Images) {
            m_pages->setCurrentWidget(m_imagePage);
            if (m_currentImage.isEmpty()) {
                showNextImage();
            }
        } else {
            m_pages->setCurrentWidget(m_notice);
        }
    }
    updateImageTimer();
    emit pageChanged();
}

void DisplayBoard::advancePage()
{
    if (m_page == Mode::Video) {
        showPage(Mode::Images);
    } else if (m_page == Mode::Images) {
        showPage(Mode::Notice);
    } else {
        showPage(Mode::Video);
    }
}

void DisplayBoard::playNextVideo()
{
    const int start = m_videos.indexOf(m_currentVideo) + 1;
    for (int offset = 0; offset < m_videos.size(); ++offset) {
        const QString path = m_videos.at((start + offset) % m_videos.size());
        if (m_failedVideos.contains(path)) {
            continue;
        }

        m_currentVideo = path;
        m_nativeVideo.stop();
        m_nativeEnded = false;
        m_usingNative = NativeVideo::accepts(path);
        if (m_usingNative) {
            m_player.stop();
            m_player.setMedia(QMediaContent());
            m_nativePicture->setImage({});
            m_videoHint->setText(QStringLiteral("正在加载视频…"));
            m_videoPage->setCurrentWidget(m_videoHint);
            m_nativeVideo.start(path);
        } else {
            startQtVideo(path);
        }
        return;
    }

    m_nativeVideo.stop();
    m_usingNative = false;
    m_nativeEnded = false;
    m_currentVideo.clear();
    m_player.stop();
    m_player.setMedia(QMediaContent());
    m_videoHint->setText(m_videos.isEmpty() ? QStringLiteral("暂无视频")
                                          : QStringLiteral("当前视频无法播放"));
    m_videoPage->setCurrentWidget(m_videoHint);
}

void DisplayBoard::startQtVideo(const QString &path)
{
    const QUrl source = QUrl::fromLocalFile(path);
    m_videoPage->setCurrentWidget(m_video);
    if (m_player.currentMedia().request().url() == source) {
        m_player.setPosition(0);
    } else {
        m_player.setMedia(source);
    }
    if (m_page == Mode::Video) {
        m_player.play();
    }
}

void DisplayBoard::queueVideoFailure()
{
    if (m_usingNative || m_currentVideo.isEmpty() || m_failedVideos.contains(m_currentVideo)) {
        return;
    }

    const auto failed = m_currentVideo;
    m_failedVideos.insert(failed);
    emit logMessage(QStringLiteral("无法播放视频：%1").arg(QFileInfo(failed).fileName()));
    QTimer::singleShot(0, this, [this, failed] {
        if (m_page == Mode::Video && m_currentVideo == failed) {
            playNextVideo();
        }
    });
}

void DisplayBoard::showNextImage()
{
    const int start = m_images.indexOf(m_currentImage) + 1;
    for (int offset = 0; offset < m_images.size(); ++offset) {
        const QString path = m_images.at((start + offset) % m_images.size());
        if (path == m_currentImage || m_picture->load(path)) {
            m_currentImage = path;
            m_imagePage->setCurrentWidget(m_picture);
            return;
        }
        emit logMessage(QStringLiteral("无法读取图片：%1").arg(QFileInfo(path).fileName()));
    }

    m_currentImage.clear();
    m_imageHint->setText(m_images.isEmpty() ? QStringLiteral("暂无图片")
                                          : QStringLiteral("当前图片无法读取"));
    m_imagePage->setCurrentWidget(m_imageHint);
}

void DisplayBoard::updateImageTimer()
{
    if (m_page == Mode::Images && m_images.size() > 1) {
        if (!m_slides.isActive()) {
            m_slides.start();
        }
    } else {
        m_slides.stop();
    }
}
