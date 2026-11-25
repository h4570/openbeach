#include "game/mods/http_fetch.h"

#include <atomic>
#include <bit>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace obl::net {

namespace {

std::atomic<uint64_t> g_nextId{1};
std::mutex g_mutex;

struct Job {
    bool done = false;
    bool cancelled = false;
    FetchResult result;
};

std::map<uint64_t, std::shared_ptr<Job>> g_jobs;

std::string PercentDecode(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            unsigned v = 0;
            const auto res = std::from_chars(input.data() + i + 1, input.data() + i + 3, v, 16);
            if (res.ec == std::errc{} && res.ptr == input.data() + i + 3) {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

FetchResult FetchLocalFile(std::string_view url, size_t maxBytes)
{
    FetchResult res;
    if (!url.starts_with("file://")) {
        res.error = "not a file:// URL";
        return res;
    }
    std::string pathStr = PercentDecode(url.substr(7));
#ifdef _WIN32
    // file:///C:/... -> C:/...
    if (pathStr.size() >= 3 && pathStr[0] == '/' && pathStr[2] == ':') pathStr.erase(0, 1);
#endif
    const std::filesystem::path filePath(std::u8string_view(std::bit_cast<const char8_t*>(pathStr.data()), pathStr.size()));
    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in) {
        res.error = "cannot open local file: " + pathStr;
        return res;
    }
    const auto endPos = in.tellg();
    if (endPos < 0 || static_cast<uint64_t>(endPos) > maxBytes) {
        res.error = "local file exceeds size limit";
        return res;
    }
    in.seekg(0, std::ios::beg);
    res.bytes.resize(static_cast<size_t>(endPos));
    if (!res.bytes.empty() && !in.read(std::bit_cast<char*>(res.bytes.data()), static_cast<std::streamsize>(res.bytes.size()))) {
        res.bytes.clear();
        res.error = "failed to read local file";
        return res;
    }
    res.ok = true;
    return res;
}

#ifdef _WIN32

std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
    return wide;
}

FetchResult FetchHttpsBlocking(const std::string& url, size_t maxBytes, const std::shared_ptr<Job>& job)
{
    FetchResult res;
    if (!url.starts_with("https://")) {
        res.error = "only https:// URLs are supported";
        return res;
    }
    const std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS comps{};
    comps.dwStructSize = sizeof(comps);
    comps.dwHostNameLength = static_cast<DWORD>(-1);
    comps.dwUrlPathLength = static_cast<DWORD>(-1);
    comps.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &comps) || comps.nScheme != INTERNET_SCHEME_HTTPS ||
            comps.dwHostNameLength == 0) {
        res.error = "invalid HTTPS URL";
        return res;
    }
    const std::wstring host(comps.lpszHostName, comps.dwHostNameLength);
    std::wstring path(comps.lpszUrlPath, comps.dwUrlPathLength);
    if (comps.dwExtraInfoLength > 0) path.append(comps.lpszExtraInfo, comps.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    HINTERNET session = WinHttpOpen(L"OpenBeach", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);
    HINTERNET connect = WinHttpConnect(session, host.c_str(), comps.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    if (!request) {
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        res.error = "cannot open HTTPS request";
        return res;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (ok &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                    &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        if (status != 200) {
            res.error = "HTTP status " + std::to_string(status);
            ok = false;
        }
    } else if (ok) {
        res.error = "cannot read HTTP status";
        ok = false;
    } else {
        res.error = "HTTPS download failed";
    }

    while (ok) {
        {
            const std::lock_guard lock(g_mutex);
            if (job->cancelled) {
                ok = false;
                res.error = "cancelled";
                break;
            }
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            ok = false;
            res.error = "HTTPS read failed";
            break;
        }
        if (avail == 0) break;
        if (avail > maxBytes - res.bytes.size()) {
            ok = false;
            res.error = "download exceeds size limit";
            break;
        }
        const size_t offset = res.bytes.size();
        res.bytes.resize(offset + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, res.bytes.data() + offset, avail, &read)) {
            ok = false;
            res.error = "HTTPS read failed";
            break;
        }
        res.bytes.resize(offset + read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    res.ok = ok;
    if (!ok) res.bytes.clear();
    return res;
}

#elif defined(__ANDROID__)

FetchResult FetchHttpsBlocking(const std::string& url, size_t maxBytes, const std::shared_ptr<Job>&)
{
    FetchResult res;
    if (!url.starts_with("https://")) {
        res.error = "only https:// URLs are supported";
        return res;
    }
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) {
        res.error = "JNI environment unavailable";
        return res;
    }
    jclass cls = env->FindClass("org/openbeach/game/OpenBeachActivity");
    if (!cls) {
        env->ExceptionClear();
        res.error = "OpenBeachActivity class not found";
        return res;
    }
    jmethodID method = env->GetStaticMethodID(cls, "fetchHttps", "(Ljava/lang/String;I)[B");
    if (!method) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        res.error = "fetchHttps method not found";
        return res;
    }
    jstring jurl = env->NewStringUTF(url.c_str());
    auto jbytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(cls, method, jurl, static_cast<jint>(maxBytes)));
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(cls);
    if (env->ExceptionCheck() || !jbytes) {
        env->ExceptionClear();
        res.error = "HTTPS download failed";
        return res;
    }
    const jsize len = env->GetArrayLength(jbytes);
    if (len < 0 || static_cast<size_t>(len) > maxBytes) {
        env->DeleteLocalRef(jbytes);
        res.error = "download exceeds size limit";
        return res;
    }
    res.bytes.resize(static_cast<size_t>(len));
    if (len > 0) env->GetByteArrayRegion(jbytes, 0, len, reinterpret_cast<jbyte*>(res.bytes.data()));
    env->DeleteLocalRef(jbytes);
    res.ok = true;
    return res;
}

#elif !defined(__EMSCRIPTEN__)

FetchResult FetchHttpsBlocking(const std::string& url, size_t maxBytes, const std::shared_ptr<Job>& job)
{
    FetchResult res;
    if (!url.starts_with("https://")) {
        res.error = "only https:// URLs are supported";
        return res;
    }
    int pipeFd[2];
    if (pipe(pipeFd) != 0) {
        res.error = "cannot create pipe";
        return res;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFd[0]);
        close(pipeFd[1]);
        res.error = "cannot start curl";
        return res;
    }
    if (pid == 0) {
        close(pipeFd[0]);
        dup2(pipeFd[1], STDOUT_FILENO);
        close(pipeFd[1]);
        const std::string maxStr = std::to_string(maxBytes);
        char* const argv[] = {const_cast<char*>("curl"), const_cast<char*>("--fail"), const_cast<char*>("--silent"),
                const_cast<char*>("--location"), const_cast<char*>("--proto"), const_cast<char*>("=https"),
                const_cast<char*>("--proto-redir"), const_cast<char*>("=https"), const_cast<char*>("--max-redirs"), const_cast<char*>("5"),
                const_cast<char*>("--connect-timeout"), const_cast<char*>("15"), const_cast<char*>("--max-time"), const_cast<char*>("60"),
                const_cast<char*>("--max-filesize"), const_cast<char*>(maxStr.c_str()), const_cast<char*>("--"),
                const_cast<char*>(url.c_str()), nullptr};
        execvp("curl", argv);
        _exit(127);
    }
    close(pipeFd[1]);
    uint8_t buf[16384];
    bool tooBig = false;
    bool cancelled = false;
    for (;;) {
        {
            const std::lock_guard lock(g_mutex);
            if (job->cancelled) {
                cancelled = true;
                break;
            }
        }
        const ssize_t n = read(pipeFd[0], buf, sizeof(buf));
        if (n < 0) {
            res.error = "read failed";
            break;
        }
        if (n == 0) break;
        if (static_cast<size_t>(n) > maxBytes - res.bytes.size()) {
            tooBig = true;
            break;
        }
        res.bytes.insert(res.bytes.end(), buf, buf + n);
    }
    close(pipeFd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (cancelled) {
        res.bytes.clear();
        res.error = "cancelled";
        return res;
    }
    if (tooBig) {
        res.bytes.clear();
        res.error = "download exceeds size limit";
        return res;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        res.bytes.clear();
        res.error = WIFEXITED(status) && WEXITSTATUS(status) == 127 ? "curl is not installed" : "HTTPS download failed";
        return res;
    }
    res.ok = true;
    return res;
}

#endif

} // namespace

uint64_t StartFetch(std::string_view url, size_t maxBytes, bool allowLocal)
{
    const uint64_t id = g_nextId.fetch_add(1);
    auto job = std::make_shared<Job>();
    {
        const std::lock_guard lock(g_mutex);
        g_jobs[id] = job;
    }

    if (allowLocal && url.starts_with("file://")) {
        job->result = FetchLocalFile(url, maxBytes);
        job->done = true;
        return id;
    }

#ifdef __EMSCRIPTEN__
    const std::string urlCopy(url);
    EM_ASM(
            {
                if (Module["startCatalogFetch"]) {
                    Module["startCatalogFetch"]($0, UTF8ToString($1), $2);
                }
            },
            static_cast<int>(id), urlCopy.c_str(), static_cast<int>(maxBytes));
#else
    std::string urlCopy(url);
    std::thread([job, url = std::move(urlCopy), maxBytes]() {
        FetchResult result = FetchHttpsBlocking(url, maxBytes, job);
        const std::lock_guard lock(g_mutex);
        job->result = std::move(result);
        job->done = true;
    }).detach();
#endif
    return id;
}

bool PollFetch(uint64_t id, FetchResult& result)
{
#ifdef __EMSCRIPTEN__
    {
        const std::lock_guard lock(g_mutex);
        const auto it = g_jobs.find(id);
        if (it == g_jobs.end()) return false;
        if (it->second->done) {
            result = std::move(it->second->result);
            g_jobs.erase(it);
            return true;
        }
    }
    const int status = EM_ASM_INT({ return Module["pollCatalogFetch"] ? Module["pollCatalogFetch"]($0) : -1; }, static_cast<int>(id));
    if (status == 0) return false;
    {
        const std::lock_guard lock(g_mutex);
        g_jobs.erase(id);
    }
    if (status < 0) {
        result.ok = false;
        result.bytes.clear();
        char errBuf[256] = {};
        EM_ASM(
                {
                    const msg = Module["catalogFetchError"] ? Module["catalogFetchError"]($0) : "download failed";
                    stringToUTF8(msg, $1, $2);
                },
                static_cast<int>(id), errBuf, static_cast<int>(sizeof(errBuf)));
        result.error = errBuf[0] ? errBuf : "HTTPS download failed";
        return true;
    }
    result.ok = true;
    result.error.clear();
    result.bytes.resize(static_cast<size_t>(status));
    EM_ASM({ Module["copyCatalogFetch"]($0, HEAPU8, $1); }, static_cast<int>(id), result.bytes.data());
    return true;
#else
    const std::lock_guard lock(g_mutex);
    const auto it = g_jobs.find(id);
    if (it == g_jobs.end() || !it->second->done) return false;
    result = std::move(it->second->result);
    g_jobs.erase(it);
    return true;
#endif
}

void CancelFetch(uint64_t id)
{
    if (!id) return;
    {
        const std::lock_guard lock(g_mutex);
        const auto it = g_jobs.find(id);
        if (it != g_jobs.end()) {
            it->second->cancelled = true;
            g_jobs.erase(it);
        }
    }
#ifdef __EMSCRIPTEN__
    EM_ASM(
            {
                if (Module["cancelCatalogFetch"]) Module["cancelCatalogFetch"]($0);
            },
            static_cast<int>(id));
#endif
}

} // namespace obl::net
