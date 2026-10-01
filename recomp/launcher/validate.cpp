#include "validate.h"

#include <SDL3/SDL_filesystem.h>
#include <string.h>

#include <utility>

#include "disc.h"

const char* const kSupportedExeSha256 = "b2f053bd26627eb618f034481fbeb49c2287bec834351787385a69d74db05001";

namespace {

DiscCheck make(DiscStatus status, int number, std::string message) {
    DiscCheck c;
    c.status = status;
    c.number = number;
    c.message = std::move(message);
    return c;
}

}  // namespace

DiscCheck check_disc(const std::string& path) {
    if (path.empty()) return make(DiscStatus::NotSet, 0, "not set");
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return make(DiscStatus::NotFound, 0, "file not found");

    char err[256] = "";
    Disc* disc = disc_open(path.c_str(), err, sizeof err);
    if (!disc) return make(DiscStatus::NotDisc, 0, std::string("not a Dreams to Reality disc image: ") + err);

    DiscCheck result;
    int number = disc_number(disc);
    if (number != 1 && number != 2) {
        result = make(DiscStatus::NotDisc, 0,
                      number == DISC_NUMBER_AMBIGUOUS
                          ? "not a Dreams to Reality disc: it holds both disc markers"
                          : "not a Dreams to Reality disc: no DATA\\1CD.ID or DATA\\2CD.ID");
    } else {
        bool edition_ok = true;
        if (number == 1) {
            // The exe's data sections are what the recompiled game runs on, so
            // another edition cannot work even though it is a disc 1.
            char hex[65] = "";
            DiscEntry exe;
            edition_ok = disc_find(disc, "GDIDREAM.EXE", &exe) && disc_sha256_file(disc, &exe, hex) == 0 &&
                         strcmp(hex, kSupportedExeSha256) == 0;
        }
        if (!edition_ok) {
            result = make(DiscStatus::WrongEdition, number,
                          "wrong edition: this port supports the European English release");
        } else if (disc_track_count(disc) > 1) {
            result = make(DiscStatus::Found, number, "found");
        } else {
            result = make(DiscStatus::FoundNoMusic, number, "found, no music (.iso or no audio tracks)");
        }
    }
    disc_close(disc);
    return result;
}

void refresh(DiscRows& rows, bool swap) {
    for (int i = 0; i < 2; i++) rows.check[i] = check_disc(rows.path[i]);
    if (swap) {
        int a = rows.check[0].number, b = rows.check[1].number;
        if ((a == 2 && b != 2) || (b == 1 && a != 1)) {
            std::swap(rows.path[0], rows.path[1]);
            std::swap(rows.check[0], rows.check[1]);
        }
    }
    for (int i = 0; i < 2; i++) {
        DiscCheck& c = rows.check[i];
        if (c.number == 0 || c.number == i + 1) continue;
        // Only reachable when both rows hold the same disc.
        c = make(DiscStatus::WrongDisc, c.number,
                 "wrong disc: this is disc " + std::to_string(c.number) + ", like the other row (two copies of the same disc)");
    }
}

void assign(DiscRows& rows, int row, const std::string& path, std::string* note) {
    DiscCheck c = check_disc(path);
    int target = c.number == 1 || c.number == 2 ? c.number - 1 : row;
    if (target != row && note)
        *note = "That image is disc " + std::to_string(target + 1) + ": moved to the disc " +
                std::to_string(target + 1) + " row.";
    rows.path[target] = path;
    refresh(rows, false);
}
