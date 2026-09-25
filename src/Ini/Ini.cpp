#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Ini.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace
{
    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    bool EqN(const char* a, const char* b, size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            int ca = static_cast<unsigned char>(a[i]);
            int cb = static_cast<unsigned char>(b[i]);
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return false;
        }
        return true;
    }


    bool EqI(const char* a, const char* b)
    {
        while (*a && *b) {
            int ca = static_cast<unsigned char>(*a);
            int cb = static_cast<unsigned char>(*b);
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == 0 && *b == 0;
    }

    size_t CopyTrimmed(const char* start, const char* end, char* out, size_t outChars)
    {
        if (outChars == 0) return 0;
        while (start < end && IsSpace(*start)) ++start;
        while (end > start && IsSpace(*(end - 1))) --end;

        size_t len = static_cast<size_t>(end - start);
        if (len >= outChars) len = outChars - 1;
        memcpy(out, start, len);
        out[len] = 0;
        return len;
    }

    const char* FindSectionHeader(const char* content, const char* section)
    {
        const size_t secLen = strlen(section);
        if (secLen == 0 || secLen + 2 >= 128)
            return nullptr;

        char header[128];
        header[0] = '[';
        memcpy(header + 1, section, secLen);
        header[secLen + 1] = ']';
        header[secLen + 2] = 0;
        const size_t hdrLen = secLen + 2;

        const char* line = content;
        while (line && *line) {
            const char* p = line;
            while (*p == ' ' || *p == '\t')
                ++p;

            if (*p == '[' && EqN(p, header, hdrLen)) {
                const char after = p[hdrLen];
                if (after == 0 || after == '\r' || after == '\n' ||
                    after == ' ' || after == '\t')
                    return p;
            }

            const char* nl = strchr(line, '\n');
            if (!nl) break;
            line = nl + 1;
        }
        return nullptr;
    }

    const char* FindSectionEnd(const char* headerStart)
    {
        const char* searchPos = strchr(headerStart, '\n');
        if (!searchPos)
            return headerStart + strlen(headerStart);

        const char* sectionEnd = headerStart + strlen(headerStart);
        while (searchPos) {
            const char* nextLine = searchPos + 1;
            while (*nextLine == ' ' || *nextLine == '\t' || *nextLine == '\r')
                ++nextLine;
            if (*nextLine == '[') {
                sectionEnd = searchPos;
                break;
            }
            searchPos = strchr(nextLine, '\n');
        }
        return sectionEnd;
    }
}

namespace Ini
{
    std::optional<std::string> Load(const wchar_t* path, size_t maxBytes)
    {
        if (!path)
            return std::nullopt;

        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return std::nullopt;

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size)) {
            CloseHandle(h);
            return std::nullopt;
        }

        if (size.QuadPart == 0) {
            CloseHandle(h);
            return std::string{};
        }

        if (size.QuadPart < 0 || size.QuadPart > 0xFFFFFFFFLL ||
            (maxBytes != 0 && static_cast<size_t>(size.QuadPart) > maxBytes)) {
            CloseHandle(h);
            return std::nullopt;
        }

        std::string buf;
        buf.resize(static_cast<size_t>(size.QuadPart));

        DWORD got = 0;
        const BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr);
        CloseHandle(h);

        if (!ok || got != buf.size())
            return std::nullopt;

        if (buf.size() >= 3 &&
            static_cast<unsigned char>(buf[0]) == 0xEF &&
            static_cast<unsigned char>(buf[1]) == 0xBB &&
            static_cast<unsigned char>(buf[2]) == 0xBF) {
            buf.erase(0, 3);
        }

        return buf;
    }

    bool GetValue(const char* content, const char* section, const char* key,
                  char* out, size_t outChars)
    {
        if (!content || !section || !key || !out || outChars == 0)
            return false;
        out[0] = 0;

        const char* headerStart = FindSectionHeader(content, section);
        if (!headerStart)
            return false;

        const char* sectionEnd = FindSectionEnd(headerStart);


        const char* pos = strchr(headerStart, '\n');
        if (!pos)
            return false;
        ++pos;

        const size_t keyLen = strlen(key);

        while (pos < sectionEnd) {
            const char* lineEnd = pos;
            while (lineEnd < sectionEnd && *lineEnd != '\r' && *lineEnd != '\n')
                ++lineEnd;

            const char* p = pos;
            while (p < lineEnd && (*p == ' ' || *p == '\t'))
                ++p;

            if (*p != ';' && *p != '#') {          
                const char* eq = nullptr;
                for (const char* q = p; q < lineEnd; ++q) {
                    if (*q == '=') { eq = q; break; }
                }

                if (eq) {
                    const char* kEnd = eq;
                    while (kEnd > p && (*(kEnd - 1) == ' ' || *(kEnd - 1) == '\t'))
                        --kEnd;

                    if (static_cast<size_t>(kEnd - p) == keyLen && EqN(p, key, keyLen)) {
                        const char* vBegin = eq + 1;
                        const char* vEnd = lineEnd;
                        for (const char* c = vBegin; c < vEnd; ++c) {
                            if (*c == ';' || *c == '#') { vEnd = c; break; }
                        }
                        while (vBegin < vEnd && (*vBegin == ' ' || *vBegin == '\t'))
                            ++vBegin;
                        while (vEnd > vBegin && (*(vEnd - 1) == ' ' || *(vEnd - 1) == '\t'))
                            --vEnd;

                        size_t n = static_cast<size_t>(vEnd - vBegin);
                        if (n >= outChars) n = outChars - 1;
                        memcpy(out, vBegin, n);
                        out[n] = 0;
                        return true;
                    }
                }
            }

            pos = lineEnd + 1;
        }
        return false;
    }

    int ParseInt(const char* value, int fallback)
    {
        if (!value || !*value)
            return fallback;

        const char* p = value;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            return fallback;

        char* end = nullptr;
        errno = 0;
        const long long v = strtoll(p, &end, 10);
        if (end == p)                             
            return fallback;


        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end != 0)
            return fallback;

        if (errno == ERANGE || v > 2147483647LL || v < -2147483648LL)
            return fallback;

        return static_cast<int>(v);
    }

    bool ParseBool(const char* value, bool fallback)
    {
        if (!value || !*value)
            return fallback;
        if (EqI(value, "1") || EqI(value, "true") || EqI(value, "yes") || EqI(value, "on"))
            return true;
        if (EqI(value, "0") || EqI(value, "false") || EqI(value, "no") || EqI(value, "off"))
            return false;
        return fallback;
    }

    bool ForEachEntry(const char* content, const char* section,
                      EntryCallback cb, void* ctx)
    {
        if (!content || !section || !cb)
            return false;

        const char* hdr = FindSectionHeader(content, section);
        if (!hdr)
            return false;

        const char* sectionEnd = FindSectionEnd(hdr);
        const char* pos = strchr(hdr, '\n');
        if (!pos)
            return false;
        ++pos;

        while (pos < sectionEnd) {
            const char* nl = pos;
            while (nl < sectionEnd && *nl != '\n')
                ++nl;

            char trimmed[256];
            CopyTrimmed(pos, nl, trimmed, sizeof(trimmed));

            if (trimmed[0] && trimmed[0] != ';' && trimmed[0] != '#') {
                if (!cb(trimmed, ctx))
                    return true;                   
            }

            pos = nl + 1;
        }
        return true;
    }
}
