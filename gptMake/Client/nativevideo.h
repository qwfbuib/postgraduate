#ifndef NATIVEVIDEO_H
#define NATIVEVIDEO_H

#include <QImage>
#include <QObject>

class VideoDecoder;

class NativeVideo final : public QObject
{
    Q_OBJECT

public:
    explicit NativeVideo(QObject *parent = nullptr);
    ~NativeVideo() override;

    static bool accepts(const QString &path);
    void start(const QString &path);
    void setPaused(bool paused);
    void stop();

signals:
    void frameReady(const QImage &frame);
    void ended();
    void failed();

private:
    VideoDecoder *m_decoder = nullptr;
};

#endif
