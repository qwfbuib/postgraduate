#include "weather.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace {

QString valueText(const QJsonObject &object, const QString &key)
{
    const auto value = object.value(key);
    return value.isDouble() ? QString::number(value.toDouble()) : value.toString().trimmed();
}

}

WeatherService::WeatherService(Fetch fetch, QObject *parent)
    : QObject(parent), m_fetch(std::move(fetch))
{
    connect(&m_request, &QFutureWatcher<WeatherResponse>::finished, this, &WeatherService::received);
    m_period.setInterval(30 * 60 * 1000);
    connect(&m_period, &QTimer::timeout, this, &WeatherService::refresh);
}

void WeatherService::configure(const QString &cityCode, const QString &key)
{
    if (m_cityCode != cityCode || m_key != key) {
        if (m_cityCode != cityCode) {
            m_live = {};
        }
        m_cityCode = cityCode;
        m_key = key;
        ++m_revision;
    }
    refresh();
}

void WeatherService::setDemo(bool enabled)
{
    m_demo = enabled;
    if (m_key.isEmpty()) {
        refresh();
    }
}

void WeatherService::refresh()
{
    if (m_key.isEmpty()) {
        m_period.stop();
        m_error.clear();
        m_status = m_demo ? Status::Demo : m_live.city.isEmpty() ? Status::NoKey : Status::Stale;
        emit changed();
        return;
    }
    if (!isCityCode(m_cityCode)) {
        m_period.stop();
        m_error = QStringLiteral("请输入六位城市编码");
        m_status = Status::Unavailable;
        emit changed();
        return;
    }
    if (m_inFlight) {
        return;
    }

    QUrl url(QStringLiteral("https://restapi.amap.com/v3/weather/weatherInfo"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("city"), m_cityCode);
    query.addQueryItem(QStringLiteral("key"), m_key);
    query.addQueryItem(QStringLiteral("extensions"), QStringLiteral("base"));
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("JSON"));
    url.setQuery(query);

    m_requestRevision = m_revision;
    m_inFlight = true;
    m_status = Status::Loading;
    m_error.clear();
    m_period.start();
    emit changed();
    m_request.setFuture(QtConcurrent::run(m_fetch, url));
}

void WeatherService::received()
{
    m_inFlight = false;
    if (m_requestRevision != m_revision) {
        refresh();
        return;
    }

    const auto response = m_request.result();
    m_error = response.error;
    if (m_error.isEmpty()) {
        const auto next = parse(response.body, m_error);
        if (m_error.isEmpty()) {
            m_live = next;
            m_status = Status::Live;
        }
    }
    if (!m_error.isEmpty()) {
        m_status = m_live.city.isEmpty() ? Status::Unavailable : Status::Stale;
    }
    emit changed();
}

WeatherService::Status WeatherService::status() const
{
    return m_status;
}

WeatherReading WeatherService::reading() const
{
    if (m_status == Status::Demo) {
        return {QStringLiteral("成都市"), QStringLiteral("多云"), QStringLiteral("22"),
                QStringLiteral("64"), QStringLiteral("东北"), QStringLiteral("≤3"), {}};
    }
    return m_live;
}

QString WeatherService::error() const
{
    return m_error;
}

bool WeatherService::isCityCode(const QString &code)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{6}$"));
    return pattern.match(code).hasMatch();
}

WeatherReading WeatherService::parse(const QByteArray &response, QString &error)
{
    error.clear();
    QJsonParseError jsonError;
    const auto data = QJsonDocument::fromJson(response, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !data.isObject()) {
        error = QStringLiteral("无法解析天气响应");
        return {};
    }

    const auto object = data.object();
    if (valueText(object, QStringLiteral("status")) != QStringLiteral("1")) {
        error = QStringLiteral("高德天气查询失败");
        const auto code = valueText(object, QStringLiteral("infocode"));
        static const QRegularExpression codePattern(QStringLiteral("^[0-9]{5,8}$"));
        if (codePattern.match(code).hasMatch()) {
            error += QStringLiteral("（%1）").arg(code);
        }
        return {};
    }

    const auto entries = object.value(QStringLiteral("lives")).toArray();
    const auto first = entries.isEmpty() ? QJsonObject{} : entries.first().toObject();
    WeatherReading reading = {
        valueText(first, QStringLiteral("city")),
        valueText(first, QStringLiteral("weather")),
        valueText(first, QStringLiteral("temperature")),
        valueText(first, QStringLiteral("humidity")),
        valueText(first, QStringLiteral("winddirection")),
        valueText(first, QStringLiteral("windpower")),
        valueText(first, QStringLiteral("reporttime")),
    };

    bool validTemperature = false;
    reading.temperature.toDouble(&validTemperature);
    if (reading.city.isEmpty() || reading.condition.isEmpty() || !validTemperature
        || reading.published.isEmpty()) {
        error = QStringLiteral("天气实况数据不完整");
        return {};
    }
    return reading;
}
