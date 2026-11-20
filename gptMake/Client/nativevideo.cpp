#include "nativevideo.h"

#ifdef Q_OS_WIN

#include <QElapsedTimer>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QRect>
#include <QThread>
#include <QWaitCondition>
#include <QtCore/qt_windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstring>
#include <memory>

namespace {

struct ReleaseCom {
    void operator()(IUnknown *object) const { object->Release(); }
};

template<class Interface>
using ComPtr = std::unique_ptr<Interface, ReleaseCom>;

class MediaFoundation
{
public:
    MediaFoundation()
        : m_comReady(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))),
          m_ready(m_comReady && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)))
    {}

    ~MediaFoundation()
    {
        if (m_ready) {
            MFShutdown();
        }
        if (m_comReady) {
            CoUninitialize();
        }
    }

    bool isReady() const { return m_ready; }

private:
    bool m_comReady;
    bool m_ready;
};

constexpr DWORD VideoStream = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
constexpr DWORD AllStreams = MF_SOURCE_READER_ALL_STREAMS;

bool configureReader(IMFSourceReader *reader)
{
    IMFMediaType *rawType = nullptr;
    if (FAILED(MFCreateMediaType(&rawType))) {
        return false;
    }

    const ComPtr<IMFMediaType> type(rawType);
    return SUCCEEDED(reader->SetStreamSelection(AllStreams, FALSE))
        && SUCCEEDED(reader->SetStreamSelection(VideoStream, TRUE))
        && SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video))
        && SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32))
        && SUCCEEDED(reader->SetCurrentMediaType(VideoStream, nullptr, type.get()));
}

struct FrameGeometry {
    QSize storage;
    QRect visible;
    int stride = 0;
};

QSize frameSize(IMFMediaType *type)
{
    UINT64 packed = 0;
    if (FAILED(type->GetUINT64(MF_MT_FRAME_SIZE, &packed))) {
        return {};
    }
    return QSize(int(quint32(packed >> 32)), int(quint32(packed)));
}

QRect displayAperture(IMFMediaType *type, const QSize &storage)
{
    for (const auto *attribute : {&MF_MT_MINIMUM_DISPLAY_APERTURE, &MF_MT_GEOMETRIC_APERTURE}) {
        MFVideoArea area{};
        UINT32 bytes = 0;
        if (SUCCEEDED(type->GetBlob(*attribute, reinterpret_cast<UINT8 *>(&area), sizeof(area), &bytes))
            && bytes == sizeof(area)) {
            const QRect rect(area.OffsetX.value, area.OffsetY.value, area.Area.cx, area.Area.cy);
            if (rect.isValid() && QRect(QPoint(0, 0), storage).contains(rect)) {
                return rect;
            }
        }
    }
    return {};
}

bool readFrameGeometry(IMFSourceReader *reader, FrameGeometry &geometry)
{
    IMFMediaType *rawType = nullptr;
    if (FAILED(reader->GetCurrentMediaType(VideoStream, &rawType))) {
        return false;
    }

    const ComPtr<IMFMediaType> type(rawType);
    geometry.storage = frameSize(type.get());
    const int width = geometry.storage.width();
    const int height = geometry.storage.height();
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        return false;
    }

    // A decoder can expose macroblock-aligned storage even when the source is 1080p.
    const QRect bounds(QPoint(0, 0), geometry.storage);
    geometry.visible = displayAperture(type.get(), geometry.storage);
    if (!geometry.visible.isValid() || geometry.visible == bounds) {
        IMFMediaType *rawNative = nullptr;
        if (SUCCEEDED(reader->GetNativeMediaType(VideoStream, 0, &rawNative))) {
            const ComPtr<IMFMediaType> native(rawNative);
            const auto aperture = displayAperture(native.get(), geometry.storage);
            const QRect nativeBounds(QPoint(0, 0), frameSize(native.get()));
            if (aperture.isValid()) {
                geometry.visible = aperture;
            } else if (nativeBounds.isValid() && bounds.contains(nativeBounds)) {
                geometry.visible = nativeBounds;
            }
        }
    }
    if (!geometry.visible.isValid()) {
        geometry.visible = bounds;
    }

    UINT32 stride = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
        geometry.stride = qint32(stride);
    } else {
        QLibrary library(QStringLiteral("mfplat"));
        using GetStride = decltype(&MFGetStrideForBitmapInfoHeader);
        const auto getStride = reinterpret_cast<GetStride>(library.resolve("MFGetStrideForBitmapInfoHeader"));
        LONG computed = 0;
        if (!getStride || FAILED(getStride(MFVideoFormat_RGB32.Data1, width, &computed))) {
            return false;
        }
        geometry.stride = computed;
    }
    return qAbs(qint64(geometry.stride)) >= qint64(width) * 4;
}


QImage copyFrame(IMFSample *sample, const FrameGeometry &geometry)
{
    IMFMediaBuffer *rawBuffer = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&rawBuffer))) {
        return {};
    }
    const ComPtr<IMFMediaBuffer> buffer(rawBuffer);

    QImage image(geometry.visible.size(), QImage::Format_RGB32);
    if (image.isNull()) {
        return {};
    }

    const auto copyRows = [&image, &geometry](const BYTE *first, int stride) {
        first += qint64(geometry.visible.top()) * stride + geometry.visible.left() * 4;
        for (int y = 0; y < image.height(); ++y) {
            std::memcpy(image.scanLine(y), first + qint64(y) * stride, size_t(image.width()) * 4);
        }
    };

    IMF2DBuffer *rawSurface = nullptr;
    if (SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer, reinterpret_cast<void **>(&rawSurface)))) {
        const ComPtr<IMF2DBuffer> surface(rawSurface);
        BYTE *first = nullptr;
        LONG stride = 0;
        if (SUCCEEDED(surface->Lock2D(&first, &stride))) {
            const bool valid = first && qAbs(qint64(stride)) >= qint64(geometry.storage.width()) * 4;
            if (valid) {
                copyRows(first, stride);
            }
            surface->Unlock2D();
            return valid ? image : QImage{};
        }
    }

    BYTE *start = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&start, nullptr, &length))) {
        return {};
    }
    const qint64 lastRow = qAbs(qint64(geometry.stride)) * (geometry.storage.height() - 1);
    const bool valid = start && qint64(length) >= lastRow + qint64(geometry.storage.width()) * 4;
    if (valid) {
        copyRows(start + (geometry.stride < 0 ? lastRow : 0), geometry.stride);
    }
    buffer->Unlock();
    return valid ? image : QImage{};
}

}

class VideoDecoder final : public QThread
{
    Q_OBJECT

public:
    VideoDecoder(const QString &path, QObject *parent) : QThread(parent), m_path(path) {}

    void setPaused(bool paused)
    {
        QMutexLocker lock(&m_mutex);
        m_paused = paused;
        m_changed.wakeAll();
    }

    void requestStop()
    {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
        m_changed.wakeAll();
    }

signals:
    void decoded(const QImage &frame);
    void ended();
    void failed();

protected:
    void run() override
    {
        MediaFoundation platform;
        QLibrary library(QStringLiteral("mfreadwrite"));
        using CreateReader = decltype(&MFCreateSourceReaderFromURL);
        const auto openReader = reinterpret_cast<CreateReader>(library.resolve("MFCreateSourceReaderFromURL"));
        IMFAttributes *rawOptions = nullptr;
        if (!platform.isReady() || !openReader || FAILED(MFCreateAttributes(&rawOptions, 1))) {
            emit failed();
            return;
        }

        const ComPtr<IMFAttributes> options(rawOptions);
        const auto path = m_path.toStdWString();
        IMFSourceReader *rawReader = nullptr;
        if (FAILED(options->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))
            || FAILED(openReader(path.c_str(), options.get(), &rawReader))) {
            emit failed();
            return;
        }
        const ComPtr<IMFSourceReader> reader(rawReader);
        FrameGeometry geometry;
        if (!configureReader(reader.get()) || !readFrameGeometry(reader.get(), geometry)) {
            emit failed();
            return;
        }

        QElapsedTimer clock;
        clock.start();
        qint64 pauseTime = 0;
        LONGLONG firstTimestamp = 0;
        bool started = false;
        while (waitUntil(0, clock, pauseTime)) {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            IMFSample *rawSample = nullptr;
            const HRESULT result = reader->ReadSample(VideoStream, 0, nullptr, &flags, &timestamp, &rawSample);
            const ComPtr<IMFSample> sample(rawSample);
            if (FAILED(result) || (flags & MF_SOURCE_READERF_ERROR)) {
                emit failed();
                return;
            }
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                && !readFrameGeometry(reader.get(), geometry)) {
                emit failed();
                return;
            }
            if (sample) {
                if (!started) {
                    firstTimestamp = timestamp;
                    clock.restart();
                    pauseTime = 0;
                    started = true;
                }
                if (!waitUntil(qMax(qint64(0), qint64(timestamp - firstTimestamp) / 10000), clock, pauseTime)) {
                    return;
                }
                const auto frame = copyFrame(sample.get(), geometry);
                if (frame.isNull()) {
                    emit failed();
                    return;
                }
                emit decoded(frame);
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (started) {
                    emit ended();
                } else {
                    emit failed();
                }
                return;
            }
        }
    }

private:
    bool waitUntil(qint64 position, const QElapsedTimer &clock, qint64 &pauseTime)
    {
        QMutexLocker lock(&m_mutex);
        while (!m_stop) {
            if (m_paused) {
                const auto pauseStarted = clock.elapsed();
                do {
                    m_changed.wait(&m_mutex);
                } while (m_paused && !m_stop);
                pauseTime += clock.elapsed() - pauseStarted;
            } else {
                const qint64 remaining = position + pauseTime - clock.elapsed();
                if (remaining <= 0) {
                    return true;
                }
                m_changed.wait(&m_mutex, unsigned(qMin(remaining, qint64(1000))));
            }
        }
        return false;
    }

    QString m_path;
    QMutex m_mutex;
    QWaitCondition m_changed;
    bool m_paused = false;
    bool m_stop = false;
};

NativeVideo::NativeVideo(QObject *parent) : QObject(parent) {}

NativeVideo::~NativeVideo()
{
    for (auto *decoder : findChildren<VideoDecoder *>()) {
        decoder->requestStop();
        decoder->wait();
    }
}

bool NativeVideo::accepts(const QString &path)
{
    const auto extension = QFileInfo(path).suffix().toLower();
    return extension == QStringLiteral("mp4") || extension == QStringLiteral("m4v")
        || extension == QStringLiteral("mov");
}

void NativeVideo::start(const QString &path)
{
    stop();
    auto *decoder = new VideoDecoder(path, this);
    m_decoder = decoder;
    connect(decoder, &VideoDecoder::decoded, this, [this, decoder](const QImage &frame) {
        if (m_decoder == decoder) {
            emit frameReady(frame);
        }
    });
    connect(decoder, &VideoDecoder::ended, this, [this, decoder] {
        if (m_decoder == decoder) {
            emit ended();
        }
    });
    connect(decoder, &VideoDecoder::failed, this, [this, decoder] {
        if (m_decoder == decoder) {
            emit failed();
        }
    });
    connect(decoder, &QThread::finished, this, [this, decoder] {
        if (m_decoder == decoder) {
            m_decoder = nullptr;
        }
        decoder->deleteLater();
    });
    decoder->start();
}

void NativeVideo::setPaused(bool paused)
{
    if (m_decoder) {
        m_decoder->setPaused(paused);
    }
}

void NativeVideo::stop()
{
    if (m_decoder) {
        auto *decoder = m_decoder;
        m_decoder = nullptr;
        decoder->requestStop();
    }
}

#include "nativevideo.moc"

#else

NativeVideo::NativeVideo(QObject *parent) : QObject(parent) {}
NativeVideo::~NativeVideo() = default;
bool NativeVideo::accepts(const QString &) { return false; }
void NativeVideo::start(const QString &) {}
void NativeVideo::setPaused(bool) {}
void NativeVideo::stop() {}

#endif
