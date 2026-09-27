/* lib9660: a simple ISO9660 reader library especially suited to embedded
 *          systems
 *
 * SPDX-License-Identifier: LicenseRef-ISC1
 * SPDX-FileCopyrightText: © 2014 Erin Shepherd
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "lib9660.h"
#include <string.h>

#ifdef DEBUG
#include <stdlib.h>
#endif

#ifdef L9660_HAVE_STDIO
#include <stdio.h>
#else
#define SEEK_END L9660_SEEK_END
#define SEEK_SET L9660_SEEK_SET
#define SEEK_CUR L9660_SEEK_CUR
#endif

#define DENT_EXISTS         (1 << 0)
#define DENT_ISDIR          (1 << 1)
#define DENT_ASSOCIATED     (1 << 2)
#define DENT_RECORD         (1 << 3)
#define DENT_PROTECTION     (1 << 4)
#define DENT_MULTIEXTENT    (1 << 5)

#define PVD(vdesc) ((l9660_vdesc_primary*)(vdesc))

#ifdef L9660_BIG_ENDIAN
#define READ16(v) ((uint16_t)(v).be[1] | ((uint16_t)(v).be[0] << 8))
#define READ32(v) ((uint32_t)(v).be[3] | ((uint32_t)(v).be[2] << 8) | ((uint32_t)(v).be[1] << 16) | ((uint32_t)(v).be[0] << 24))
#else
#define READ16(v) ((uint16_t)(v).le[0] | ((uint16_t)(v).le[1] << 8))
#define READ32(v) ((uint32_t)(v).le[0] | ((uint32_t)(v).le[1] << 8) | ((uint32_t)(v).le[2] << 16) | ((uint32_t)(v).le[3] << 24))
#endif

#ifndef L9660_SINGLEBUFFER
#define HAVEBUFFER(f) (true)
#define BUF(f) ((f)->buf)
#else
#define HAVEBUFFER(f) ((f) == last_file)
#define BUF(f) (gbuf)
static l9660_file *last_file;
static char        gbuf[2048];
#endif

static inline uint16_t fsectoff(l9660_file *f)
{
    return f->position % 2048;
}

static inline uint32_t fnextsectpos(l9660_file *f)
{
    uint64_t next = ((uint64_t)f->position + 2047) & ~(uint64_t)2047;
    return next > UINT32_MAX ? UINT32_MAX : (uint32_t)next;
}

l9660_status l9660_openfs(
    l9660_fs *fs,
    bool (*read_sector)(l9660_fs *fs, void *buf, uint32_t sector))
{
    fs->read_sector = read_sector;

#ifndef L9660_SINGLEBUFFER
    l9660_vdesc_primary *pvd = PVD(&fs->pvd);
#else
    last_file = NULL;
    l9660_vdesc_primary *pvd = PVD(gbuf);
#endif
    uint32_t idx = 0x10;
    /* A user image must not keep us in an unbounded descriptor scan. */
    for (; idx < 0x50; ++idx) {
        // Read next sector
        if (!read_sector(fs, pvd, idx))
            return L9660_EIO;

        // Validate magic
        if (memcmp(pvd->hdr.magic, "CD001", 5) != 0 || pvd->hdr.version != 1)
            return L9660_EBADFS;

        if (pvd->hdr.type == 1)
            break; // Found PVD
        else if(pvd->hdr.type == 255)
            return L9660_EBADFS;
    }

    if (idx == 0x50 || READ16(pvd->logical_block_size) != 2048 ||
        pvd->root_dir_ent.length != 34 ||
        pvd->root_dir_ent.name_len != 1 ||
        (pvd->root_dir_ent.flags & DENT_ISDIR) == 0)
        return L9660_EBADFS;

#ifdef L9660_SINGLEBUFFER
    memcpy(&fs->root_dir_ent, &pvd->root_dir_ent, pvd->root_dir_ent.length);
#endif

    return L9660_OK;
}

l9660_status l9660_fs_open_root(l9660_dir *dir, l9660_fs *fs)
{
    l9660_file *f = &dir->file;
#ifndef L9660_SINGLEBUFFER
    l9660_dirent *dirent = &PVD(&fs->pvd)->root_dir_ent;
#else
    l9660_dirent *dirent = &fs->root_dir_ent;
#endif

    f->fs = fs;
    f->first_sector = READ32(dirent->sector);
    f->length       = READ32(dirent->size);
    f->position     = 0;

    return L9660_OK;
}

static l9660_status buffer(l9660_file *f)
{
    uint32_t relative_sector = f->position / 2048;
    if (relative_sector > UINT32_MAX - f->first_sector)
        return L9660_EBADFS;
#ifdef L9660_SINGLEBUFFER
    last_file = f;
#endif
    if (!f->fs->read_sector(f->fs, BUF(f), f->first_sector + relative_sector))
        return L9660_EIO;
    else
        return L9660_OK;
}

static l9660_status prebuffer(l9660_file *f)
{
    if (!HAVEBUFFER(f) || (f->position % 2048) == 0)
        return buffer(f);
    else return L9660_OK;
}

#if defined(DEBUG)
static void print_dirent(l9660_dirent *dent)
{
    if (!getenv("L9660_DEBUG"))
        return;

    printf("| ---- dirent\n");
    printf("| length        %d\n", dent->length);
    printf("| xattr_length  %d\n", dent->xattr_length);
    printf("| sector        %u\n", READ32(dent->sector));
    printf("| size          %u\n", READ32(dent->size));
    printf("| name          \"%.*s\"\n", dent->name_len, dent->name);
    printf("| ---- end dirent\n");
}
#endif

static l9660_status openat_raw(l9660_file *child, l9660_dir *parent, const char *name, bool isdir)
{
    l9660_status rv;
    l9660_dirent *dent = NULL;
    if (!name || !*name) return L9660_ENOENT;
    if ((rv = l9660_seekdir(parent, 0))) return rv;

    do {
        const char *seg = name;
        const char *slash = strchr(name, '/');
        size_t seglen = slash ? (size_t)(slash - seg) : strlen(seg);
        if (!seglen) return L9660_ENOENT;
        name = slash ? slash + 1 : seg + seglen;

        /* ISO9660 stores '.' as '\0' */
        if (seglen == 1 && *seg == '.')
            seg = "\0";

        /* ISO9660 stores ".." as '\1' */
        if (seglen == 2 && seg[0] == '.' && seg[1] == '.') {
            seg    = "\1";
            seglen = 1;
        }

        for(;;) {
            if ((rv = l9660_readdir(parent, &dent)))
                return rv;

            /* EOD */
            if (!dent)
                return L9660_ENOENT;

#ifdef DEBUG
            print_dirent(dent);
#endif

            /* wrong length */
            if (seglen > dent->name_len)
                continue;

            /* check name */
            if (memcmp(seg, dent->name, seglen) != 0)
                continue;

            /* check for a revision tag */
            if (dent->name_len > seglen && dent->name[seglen] != ';')
                continue;

            /* all tests pass */
            break;
        }

        child->fs           = parent->file.fs;
        child->first_sector = READ32(dent->sector) + dent->xattr_length;
        child->length       = READ32(dent->size);
        child->position     = 0;

        if (*name && (dent->flags & DENT_ISDIR) == 0)
            return L9660_ENOTDIR;

        parent = (l9660_dir*) child;
    } while(*name);

    if (isdir) {
        if ((dent->flags & DENT_ISDIR) == 0)
            return L9660_ENOTDIR;
    } else {
        if ((dent->flags & DENT_ISDIR) != 0)
            return L9660_ENOTFILE;
    }

    return L9660_OK;
}

l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent, const char *path)
{
    return openat_raw(&dir->file, parent, path, true);
}

static inline unsigned aligneven(unsigned v) {
    return v + (v & 1);
}

l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **pdirent)
{
    l9660_status rv;
    l9660_file *f = &dir->file;
    if (!pdirent || f->position > f->length) return L9660_EINVAL;

rebuffer:
    if(f->position >= f->length) {
        *pdirent = NULL;
        return L9660_OK;
    }

    if ((rv = prebuffer(f)))
        return rv;

    char *off = BUF(f) + fsectoff(f);
    if (*off == 0) {
        // Padded end of sector
        uint32_t next = fnextsectpos(f);
        if (next <= f->position) return L9660_EBADFS;
        f->position = next > f->length ? f->length : next;
        goto rebuffer;
    }

    if (2048u - fsectoff(f) < offsetof(l9660_dirent, name) + 1 ||
        f->length - f->position < offsetof(l9660_dirent, name) + 1)
        return L9660_EBADFS;
    l9660_dirent *dirent = (l9660_dirent*) off;
    unsigned advance = aligneven(dirent->length);
    if (dirent->length < offsetof(l9660_dirent, name) + 1 ||
        dirent->name_len == 0 ||
        (size_t)dirent->name_len > (size_t)dirent->length - offsetof(l9660_dirent, name) ||
        advance > 2048u - fsectoff(f) ||
        advance > f->length - f->position)
        return L9660_EBADFS;
    f->position += advance;

    *pdirent = dirent;
    return L9660_OK;
}

l9660_status l9660_openat(l9660_file *child, l9660_dir *parent, const char * name)
{
    return openat_raw(child, parent, name, false);
}

/*! Seek the file to \p offset from \p whence */
l9660_status l9660_seek(l9660_file *f, int whence, int32_t offset)
{
    l9660_status rv;
    int64_t next;

    switch (whence) {
        case SEEK_SET:
            next = offset;
            break;

        case SEEK_CUR:
            next = (int64_t)f->position + offset;
            break;

        case SEEK_END:
            next = (int64_t)f->length + offset;
            break;
        default:
            return L9660_EINVAL;
    }

    if (next < 0 || (uint64_t)next > f->length) return L9660_EINVAL;
    f->position = (uint32_t)next;

    /* A newly opened file has no buffered sector, even when seeking within 0. */
    if (f->position < f->length && fsectoff(f) != 0) {
        if ((rv = buffer(f)))
            return rv;
    }

    return L9660_OK;
}

uint32_t l9660_tell(l9660_file *f)
{
    return f->position;
}

l9660_status l9660_read(l9660_file *f, void* buf, size_t size, size_t *read)
{
    l9660_status rv;
    if (!read || (!buf && size) || f->position > f->length)
        return L9660_EINVAL;
    *read = 0;
    if (f->position == f->length || size == 0) return L9660_OK;

    if ((rv = prebuffer(f)))
        return rv;

    size_t rem = 2048 - fsectoff(f);
    if (rem > f->length - f->position)
        rem = f->length - f->position;
    if (rem < size)
        size = rem;

    memcpy(buf, BUF(f) + fsectoff(f), size);

    *read = size;
    f->position += (uint32_t)size;

    return L9660_OK;
}
