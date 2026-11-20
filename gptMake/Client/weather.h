#ifndef WEATHER_H
#define WEATHER_H

#include "weathertransport.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <functional>

struct WeatherReading {
    QString city;
    QString condition;
    QString temperature;
    QString humidity;
    QString windDirection;
    QString windPower;
    QString published;
};

class WeatherService final : public QObject
{
    Q_OBJECT

public:
    enum class Status { NoKey, Loading, Live, Stale, Unavailable, Demo };

    using Fetch = std::function<WeatherResponse(const QUrl &)>;

    explicit WeatherService(Fetch fetch = fetchWeatherHttps, QObject *parent = nullptr);

    void configure(const QString &cityCode, const QString &key);
    void setDemo(bool enabled);
    void refresh();

    Status status() const;
    WeatherReading reading() const;
    QString error() const;

    static bool isCityCode(const QString &code);
    static WeatherReading parse(const QByteArray &response, QString &error);

signals:
    void changed();

private:
    void received();

    Fetch m_fetch;
    QString m_cityCode;
    QString m_key;
    QString m_error;
    WeatherReading m_live;
    Status m_status = Status::NoKey;
    bool m_demo = false;
    bool m_inFlight = false;
    unsigned m_revision = 0;
    unsigned m_requestRevision = 0;
    QTimer m_period;
    QFutureWatcher<WeatherResponse> m_request;
};

#endif
