#ifndef WEATHERTRANSPORT_H
#define WEATHERTRANSPORT_H

#include <QByteArray>
#include <QString>
#include <QUrl>

struct WeatherResponse {
    QByteArray body;
    QString error;
};

WeatherResponse fetchWeatherHttps(const QUrl &url);

#endif
