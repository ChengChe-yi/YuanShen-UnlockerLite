#pragma once
#include <cstddef>
#include <optional>
#include <string>



namespace Ini
{
   
    std::optional<std::string> Load(const wchar_t* path, size_t maxBytes = 0);


    bool GetValue(const char* content, const char* section, const char* key,
                  char* out, size_t outChars);


    int ParseInt(const char* value, int fallback);


    bool ParseBool(const char* value, bool fallback);


    typedef bool (*EntryCallback)(const char* value, void* ctx);


    bool ForEachEntry(const char* content, const char* section,
                      EntryCallback cb, void* ctx);
}
