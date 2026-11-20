#include "weathertransport.h"

#ifdef Q_OS_WIN

#include <QtCore/qt_windows.h>
#include <winhttp.h>
#include <memory>

namespace {

struct CloseInternetHandle {
    void operator()(void *handle) const { WinHttpCloseHandle(handle); }
};

using InternetHandle = std::unique_ptr<void, CloseInternetHandle>;

WeatherResponse networkError()
{
    const DWORD error = GetLastError();
    return {{}, QStringLiteral("天气请求失败（Windows %1）").arg(error)};
}

}

WeatherResponse fetchWeatherHttps(const QUrl &url)
{
    if (url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("restapi.amap.com")) {
        return {{}, QStringLiteral("天气服务地址无效")};
    }

    const auto hostname = url.host().toStdWString();
    const auto resource = (url.path(QUrl::FullyEncoded) + QLatin1Char('?')
                           + url.query(QUrl::FullyEncoded)).toStdWString();

    InternetHandle session(WinHttpOpen(L"ScenicDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 5000)) {
        return networkError();
    }
    InternetHandle connection(WinHttpConnect(session.get(), hostname.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        return networkError();
    }

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request) {
        return networkError();
    }
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))
        || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request.get(), nullptr)) {
        return networkError();
    }

    DWORD code = 0;
    DWORD codeSize = sizeof(code);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX)) {
        return networkError();
    }
    if (code != 200) {
        return {{}, QStringLiteral("天气服务返回 HTTP %1").arg(code)};
    }

    QByteArray body;
    char chunk[4096];
    DWORD received = 0;
    do {
        if (!WinHttpReadData(request.get(), chunk, sizeof(chunk), &received)) {
            return networkError();
        }
        body.append(chunk, int(received));
        if (body.size() > 256 * 1024) {
            return {{}, QStringLiteral("天气响应超出大小限制")};
        }
    } while (received != 0);

    return {body, {}};
}

#else

WeatherResponse fetchWeatherHttps(const QUrl &)
{
    return {{}, QStringLiteral("此平台尚未接入天气 HTTPS 模块")};
}

#endif
