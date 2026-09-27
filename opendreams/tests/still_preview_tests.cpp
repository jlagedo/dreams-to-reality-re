#define _CRT_SECURE_NO_WARNINGS
#include "inspect/catalog.h"
#include "inspect/still_preview.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
uint64_t fnv(const std::vector<uint8_t>& bytes) {
    uint64_t hash=14695981039346656037ull;
    for (uint8_t b : bytes) hash=(hash^b)*1099511628211ull;
    return hash;
}
const od::inspect::Row* find(const od::inspect::Source& source,
                             const std::string& kind, const std::string& path,
                             const std::string& key, const std::string& parent={}) {
    for (const auto& row : source.rows) {
        if (row.kind!=kind || row.path!=path || (!key.empty() && row.key!=key)) continue;
        if (!parent.empty() && (row.parent==SIZE_MAX ||
                                source.rows[row.parent].name!=parent)) continue;
        return &row;
    }
    return nullptr;
}
}

int main() {
    const char* cue=std::getenv("DREAMS_CUE2");
    if (!cue || !*cue) { std::cout << "DREAMS_CUE2 is not configured\n"; return 77; }
    od::inspect::Catalog catalog;
    std::string error;
    if (!catalog.replace(0,std::filesystem::u8path(cue),error)) {
        std::cerr << error << '\n'; return 1;
    }
    for (int n=0; n<2000 && !catalog.source(0)->complete; ++n) catalog.tick(4);
    const auto* source=catalog.source(0);
    if (!source || !source->complete) { std::cerr << "catalog did not finish\n"; return 1; }
    struct Case { const char* kind; const char* path; const char* key;
                  const char* parent; uint64_t expected; };
    const Case cases[] = {
        {"Font glyph","DATA/FONT/HI640.SPR","slot:65","",0x3c1bd8a666c4e8bfull},
        {"Sprite slot","DATA/OBJET/SOUR.ALP","slot:0","",0x3ecff3f96ec232e0ull},
        {"Sprite slot","DATA/ICONE/ICONES.BF","slot:0","MAGIE.ALP",0xd1823452cc596afcull},
        {"VGA sprite","DATA/OBJET/ALPHABET.SPR","slot:2","",0x958a854650dbdaf3ull},
        {"Texture bank","DATA/3DC/ESSAI.3DM","","",0x750567040ed6cb34ull},
        {"Material texture","DATA/3DC/CAI.DAN","material:0","",0xc2aa41d6eae6de56ull},
        {"Scene texture","DATA/3DC/E29USINE.DSN","texture:0","",0x97740b71d78f2db3ull},
    };
    for (const auto& item : cases) {
        const auto* row=find(*source,item.kind,item.path,item.key,item.parent);
        if (!row) { std::cerr << "missing row " << item.kind << ' ' << item.path << '\n'; return 1; }
        od::inspect::StillImage image;
        if (!od::inspect::load_still_image(*source,*row,image,error)) {
            std::cerr << item.kind << ": " << error << '\n'; return 1;
        }
        std::vector<uint8_t> rgba;
        if (!image.render(image.default_row,image.transparent_zero,rgba,error) ||
            rgba.size()!=static_cast<size_t>(image.width)*image.height*4) {
            std::cerr << item.kind << ": " << error << '\n'; return 1;
        }
        if (fnv(rgba)!=item.expected) {
            std::cerr << item.kind << ' ' << row->name << " differs from Python image oracle\n";
            return 1;
        }
    }
    const auto* malformed=find(*source,"Font glyph","DATA/FONT/HI320.SPR","slot:37");
    if (!malformed || malformed->status!=od::inspect::Status::invalid) {
        std::cerr << "known malformed HI320 glyph is not reported invalid\n";
        return 1;
    }
    using Ends = std::pair<const od::inspect::Row*,const od::inspect::Row*>;
    std::map<std::string,Ends> scene_pages, dan_banks;
    for (const auto& row : source->rows) {
        auto* groups = row.kind=="Scene texture" ? &scene_pages :
                       row.kind=="Material texture" ? &dan_banks : nullptr;
        if (!groups) continue;
        auto& ends=(*groups)[row.path];
        if (!ends.first) ends.first=&row;
        ends.second=&row;
    }
    size_t checked=0;
    for (const auto* groups : {&scene_pages,&dan_banks}) {
        if (groups->empty()) { std::cerr << "texture group has no indexed pages\n"; return 1; }
        for (const auto& [path,ends] : *groups) {
            for (const auto* row : {ends.first,ends.second}) {
                if (row==ends.second && ends.first==ends.second) continue;
                od::inspect::StillImage image;
                std::vector<uint8_t> rgba;
                if (!od::inspect::load_still_image(*source,*row,image,error) ||
                    !image.render(image.default_row,image.transparent_zero,rgba,error) ||
                    rgba.size()!=256u*256u*4u) {
                    std::cerr << path << " texture " << row->name << ": " << error << '\n';
                    return 1;
                }
                ++checked;
            }
        }
    }
    std::cout << "seven images match Python RGBA output; " << checked
              << " first/last scene and DAN textures rendered; malformed glyph bounded\n";
    return 0;
}
