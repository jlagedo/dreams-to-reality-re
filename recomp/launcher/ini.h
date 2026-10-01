// A small ini reader and writer for dreams.ini.
//
// UTF-8 text, "[section]" headers, "key = value", whole-line comments starting
// with ';' or '#', and an inline ";" comment when it follows white space (so a
// path such as "a;b.cue" survives; a value that needs it is written in double
// quotes, with "" for a quote). Sections and keys keep their file order and
// their spelling, and a key the launcher does not know stays in the file.
// Comments are not kept.
#ifndef LAUNCHER_INI_H
#define LAUNCHER_INI_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct IniSection {
    std::string name;
    std::vector<std::pair<std::string, std::string>> items;
};

class Ini {
public:
    static Ini parse(std::string_view text);
    std::string dump() const;

    // Case-insensitive on section and key. nullptr when absent.
    const std::string* get(std::string_view section, std::string_view key) const;
    std::string get_or(std::string_view section, std::string_view key, std::string_view fallback) const;
    void set(std::string_view section, std::string_view key, std::string_view value);
    void erase(std::string_view section, std::string_view key);
    void clear_section(std::string_view section);  // keeps the header, drops the items
    const IniSection* section(std::string_view name) const;

private:
    IniSection* find(std::string_view name);
    std::vector<IniSection> sections_;
};

// ASCII case-insensitive equality, shared by the launcher's name lookups.
bool iequals(std::string_view a, std::string_view b);

#endif
