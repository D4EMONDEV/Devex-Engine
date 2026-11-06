#include "FetchText.hpp"

#include <devex/core/BuildInfo.hpp>

#include <curl/curl.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <system_error>

namespace devex::editor {

namespace {

// More than any answer asked for, and a limit to what a wrong server can send.
constexpr std::size_t mostBytes = std::size_t{8} * 1024 * 1024;

struct Download
{
    std::string text;
    std::stop_token stop;
};

std::size_t receive(char* data, std::size_t size, std::size_t count, void* user)
{
    Download& download = *static_cast<Download*>(user);
    const std::size_t bytes = size * count;
    if (download.text.size() + bytes > mostBytes)
    {
        return 0;
    }
    download.text.append(data, bytes);
    return bytes;
}

int progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return static_cast<Download*>(user)->stop.stop_requested() ? 1 : 0;
}

} // namespace

core::Result<std::string> fetchText(const std::string& address, std::stop_token stop)
{
    static std::once_flag started;
    std::call_once(started, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
    {
        return core::makeError(core::ErrorCode::Io, "libcurl did not start");
    }
    Download download{.stop = std::move(stop)};
    const std::string agent = std::format("Devex-Engine/{}", core::version());
    const std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(curl_slist_append(nullptr, "Accept: application/vnd.github+json"), curl_slist_free_all);

    curl_easy_setopt(curl.get(), CURLOPT_URL, address.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    // On a thread of its own, signals are not for it.
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &download);
#ifdef _WIN32
    // Windows checks the certificate. A server of revoked certificates that cannot be reached is no reason to fail.
    curl_easy_setopt(curl.get(), CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#else
    // OpenSSL comes linked in, and the place it was built to look in is not on this machine: the system's certificates are in
    // one of these, as each family of Linux puts them.
    for (const char* const certificates : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"})
    {
        std::error_code error;
        if (std::filesystem::exists(certificates, error))
        {
            curl_easy_setopt(curl.get(), CURLOPT_CAINFO, certificates);
            break;
        }
    }
#endif

    if (const CURLcode result = curl_easy_perform(curl.get()); result != CURLE_OK)
    {
        return core::makeError(core::ErrorCode::Io, "{}", curl_easy_strerror(result));
    }
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 200)
    {
        return core::makeError(core::ErrorCode::Io, "the server answered {}", status);
    }
    return std::move(download.text);
}

} // namespace devex::editor
