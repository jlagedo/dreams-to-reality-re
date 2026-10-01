#include "ini.h"

#include <cctype>

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
    return s;
}

// A value as written after '=': quoted, or plain up to an inline comment.
std::string parse_value(std::string_view v) {
    v = trim(v);
    std::string out;
    if (!v.empty() && v.front() == '"') {
        for (size_t i = 1; i < v.size(); i++) {
            if (v[i] == '"') {
                if (i + 1 < v.size() && v[i + 1] == '"') { out += '"'; i++; continue; }
                break;  // closing quote; anything after it is a comment
            }
            out += v[i];
        }
        return out;
    }
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] == ';' && (i == 0 || std::isspace((unsigned char)v[i - 1]))) { v = v.substr(0, i); break; }
    return std::string(trim(v));
}

bool needs_quotes(const std::string& v) {
    if (v.empty()) return false;
    if (std::isspace((unsigned char)v.front()) || std::isspace((unsigned char)v.back())) return true;
    if (v.front() == '"' || v.front() == ';') return true;
    for (size_t i = 1; i < v.size(); i++)
        if (v[i] == ';' && std::isspace((unsigned char)v[i - 1])) return true;
    return false;
}

std::string format_value(const std::string& raw) {
    std::string v;
    for (char c : raw) v += (c == '\n' || c == '\r') ? ' ' : c;  // one line per key
    if (!needs_quotes(v)) return v;
    std::string q = "\"";
    for (char c : v) {
        if (c == '"') q += '"';
        q += c;
    }
    return q + '"';
}

}  // namespace

Ini Ini::parse(std::string_view text) {
    Ini ini;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    IniSection* cur = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            size_t close = line.find(']');
            if (close == std::string_view::npos) continue;
            std::string_view name = trim(line.substr(1, close - 1));
            cur = ini.find(name);
            if (!cur) {
                ini.sections_.push_back({std::string(name), {}});
                cur = &ini.sections_.back();
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string_view::npos || !cur) continue;  // a key before any section is not ours
        std::string_view key = trim(line.substr(0, eq));
        if (key.empty()) continue;
        std::string value = parse_value(line.substr(eq + 1));
        bool replaced = false;
        for (auto& kv : cur->items)
            if (iequals(kv.first, key)) { kv.second = value; replaced = true; break; }  // the last one wins
        if (!replaced) cur->items.emplace_back(std::string(key), std::move(value));
    }
    return ini;
}

std::string Ini::dump() const {
    std::string out;
    bool first = true;
    for (const auto& s : sections_) {
        if (!first) out += "\n";
        first = false;
        out += "[" + s.name + "]\n";
        for (const auto& kv : s.items) {
            std::string v = format_value(kv.second);
            out += kv.first + " =" + (v.empty() ? "" : " " + v) + "\n";
        }
    }
    return out;
}

IniSection* Ini::find(std::string_view name) {
    for (auto& s : sections_)
        if (iequals(s.name, name)) return &s;
    return nullptr;
}

const IniSection* Ini::section(std::string_view name) const {
    for (const auto& s : sections_)
        if (iequals(s.name, name)) return &s;
    return nullptr;
}

const std::string* Ini::get(std::string_view section, std::string_view key) const {
    const IniSection* s = this->section(section);
    if (!s) return nullptr;
    for (const auto& kv : s->items)
        if (iequals(kv.first, key)) return &kv.second;
    return nullptr;
}

std::string Ini::get_or(std::string_view section, std::string_view key, std::string_view fallback) const {
    const std::string* v = get(section, key);
    return v ? *v : std::string(fallback);
}

void Ini::set(std::string_view section, std::string_view key, std::string_view value) {
    IniSection* s = find(section);
    if (!s) {
        sections_.push_back({std::string(section), {}});
        s = &sections_.back();
    }
    for (auto& kv : s->items)
        if (iequals(kv.first, key)) { kv.second = std::string(value); return; }
    s->items.emplace_back(std::string(key), std::string(value));
}

void Ini::erase(std::string_view section, std::string_view key) {
    IniSection* s = find(section);
    if (!s) return;
    for (size_t i = 0; i < s->items.size(); i++)
        if (iequals(s->items[i].first, key)) { s->items.erase(s->items.begin() + (long)i); return; }
}

void Ini::clear_section(std::string_view section) {
    if (IniSection* s = find(section)) s->items.clear();
}
