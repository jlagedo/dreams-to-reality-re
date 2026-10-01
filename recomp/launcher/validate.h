// Disc validation: what a path is, through the disc library.
#ifndef LAUNCHER_VALIDATE_H
#define LAUNCHER_VALIDATE_H

#include <string>

enum class DiscStatus {
    NotSet,
    NotFound,
    NotDisc,       // opens, but is not a Dreams to Reality disc (or not an image at all)
    WrongDisc,     // a disc, but the same one as the other row (two copies)
    WrongEdition,  // disc 1 whose GDIDREAM.EXE is not the supported release
    Found,
    FoundNoMusic,  // valid; no audio tracks (a .iso, or an extracted directory)
};

struct DiscCheck {
    DiscStatus status = DiscStatus::NotSet;
    int number = 0;       // 1 or 2 once the image is known to be that disc, else 0
    std::string message;  // one line for the status area

    bool ok() const { return status == DiscStatus::Found || status == DiscStatus::FoundNoMusic; }
};

// SHA-256 of the supported GDIDREAM.EXE (the European English release).
extern const char* const kSupportedExeSha256;

// One image, on its own. The disc library does the work; the SHA-256 of
// GDIDREAM.EXE (under 1 MB) is only computed for disc 1.
DiscCheck check_disc(const std::string& path);

// The two rows of the window.
struct DiscRows {
    std::string path[2];
    DiscCheck check[2];

    bool both_ok() const { return check[0].ok() && check[1].ok(); }
};

// Validates both rows. With swap, an image sitting in the wrong row moves to
// the right one (the order the user gave them in does not matter). A row whose
// image is the same disc as the other row's is marked WrongDisc.
void refresh(DiscRows& rows, bool swap);

// The user picked or dropped path for row (0 or 1): an image that is known to
// be the other disc goes to the other row (note says so), anything else stays
// in the row it was given for, with its status.
void assign(DiscRows& rows, int row, const std::string& path, std::string* note);

#endif
