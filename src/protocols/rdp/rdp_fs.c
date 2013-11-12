
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is libguac-client-rdp.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <errno.h>

#include <guacamole/pool.h>

#include "rdp_fs.h"
#include "rdp_status.h"
#include "debug.h"
#include "unicode.h"

guac_rdp_fs* guac_rdp_fs_alloc(const char* drive_path) {

    guac_rdp_fs* fs = malloc(sizeof(guac_rdp_fs));

    fs->drive_path = strdup(drive_path);
    fs->file_id_pool = guac_pool_alloc(0);
    fs->open_files = 0;

    return fs;

}

void guac_rdp_fs_free(guac_rdp_fs* fs) {
    guac_pool_free(fs->file_id_pool);
    free(fs->drive_path);
    free(fs);
}

/**
 * Translates an absolute Windows virtual_path to an absolute virtual_path
 * which is within the "drive virtual_path" specified in the connection
 * settings.
 */
static void __guac_rdp_fs_translate_path(guac_rdp_fs* fs,
        const char* virtual_path, char* real_path) {

    /* Get drive path */
    char* drive_path = fs->drive_path;

    int i;

    /* Start with path from settings */
    for (i=0; i<GUAC_RDP_FS_MAX_PATH-1; i++) {

        /* Break on end-of-string */
        char c = *(drive_path++);
        if (c == 0)
            break;

        /* Copy character */
        *(real_path++) = c;

    }

    /* Translate path */
    for (; i<GUAC_RDP_FS_MAX_PATH-1; i++) {

        /* Stop at end of string */
        char c = *(virtual_path++);
        if (c == 0)
            break;

        /* Translate backslashes to forward slashes */
        if (c == '\\')
            c = '/';

        /* Store in real path buffer */
        *(real_path++)= c;

    }

    /* Null terminator */
    *real_path = 0;

}

int guac_rdp_fs_get_errorcode(int err) {

    /* Translate errno codes to GUAC_RDP_FS codes */
    if (err == ENFILE)  return GUAC_RDP_FS_ENFILE;
    if (err == ENOENT)  return GUAC_RDP_FS_ENOENT;
    if (err == ENOTDIR) return GUAC_RDP_FS_ENOTDIR;
    if (err == ENOSPC)  return GUAC_RDP_FS_ENOSPC;
    if (err == EISDIR)  return GUAC_RDP_FS_EISDIR;
    if (err == EACCES)  return GUAC_RDP_FS_EACCES;
    if (err == EEXIST)  return GUAC_RDP_FS_EEXIST;
    if (err == EINVAL)  return GUAC_RDP_FS_EINVAL;
    if (err == ENOSYS)  return GUAC_RDP_FS_ENOSYS;
    if (err == ENOTSUP) return GUAC_RDP_FS_ENOTSUP;

    /* Default to invalid parameter */
    return GUAC_RDP_FS_EINVAL;

}

int guac_rdp_fs_get_status(int err) {

    /* Translate GUAC_RDP_FS error code to RDPDR status code */
    if (err == GUAC_RDP_FS_ENFILE)  return STATUS_NO_MORE_FILES;
    if (err == GUAC_RDP_FS_ENOENT)  return STATUS_NO_SUCH_FILE;
    if (err == GUAC_RDP_FS_ENOTDIR) return STATUS_NOT_A_DIRECTORY;
    if (err == GUAC_RDP_FS_ENOSPC)  return STATUS_DISK_FULL;
    if (err == GUAC_RDP_FS_EISDIR)  return STATUS_FILE_IS_A_DIRECTORY;
    if (err == GUAC_RDP_FS_EACCES)  return STATUS_ACCESS_DENIED;
    if (err == GUAC_RDP_FS_EEXIST)  return STATUS_OBJECT_NAME_COLLISION;
    if (err == GUAC_RDP_FS_EINVAL)  return STATUS_INVALID_PARAMETER;
    if (err == GUAC_RDP_FS_ENOSYS)  return STATUS_NOT_IMPLEMENTED;
    if (err == GUAC_RDP_FS_ENOTSUP) return STATUS_NOT_SUPPORTED;

    /* Default to invalid parameter */
    return STATUS_INVALID_PARAMETER;

}

int guac_rdp_fs_open(guac_rdp_fs* fs, const char* path,
        int access, int file_attributes, int create_disposition,
        int create_options) {

    char real_path[GUAC_RDP_FS_MAX_PATH];
    char normalized_path[GUAC_RDP_FS_MAX_PATH];

    struct stat file_stat;
    int fd;
    int file_id;
    guac_rdp_fs_file* file;

    int flags = 0;

    GUAC_RDP_DEBUG(2, "path=\"%s\", access=0x%x, file_attributes=0x%x, "
                      "create_disposition=0x%x, create_options=0x%x",
                      path, access, file_attributes, create_disposition,
                      create_options);

    /* If no files available, return too many open */
    if (fs->open_files >= GUAC_RDP_FS_MAX_FILES) {
        GUAC_RDP_DEBUG(1, "%s", "Failure - too many open files.");
        return GUAC_RDP_FS_ENFILE;
    }

    /* If path empty, transform to root path */
    if (path[0] == '\0')
        path = "\\";

    /* If path is relative, the file does not exist */
    else if (path[0] != '\\') {
        GUAC_RDP_DEBUG(1, "Failure - path \"%s\" is relative.", path);
        return GUAC_RDP_FS_ENOENT;
    }

    /* Translate access into flags */
    if (access & ACCESS_GENERIC_ALL)
        flags = O_RDWR;
    else if ((access & ( ACCESS_GENERIC_WRITE
                       | ACCESS_FILE_WRITE_DATA
                       | ACCESS_FILE_APPEND_DATA))
          && (access & (ACCESS_GENERIC_READ  | ACCESS_FILE_READ_DATA)))
        flags = O_RDWR;
    else if (access & ( ACCESS_GENERIC_WRITE
                      | ACCESS_FILE_WRITE_DATA
                      | ACCESS_FILE_APPEND_DATA))
        flags = O_WRONLY;
    else
        flags = O_RDONLY;

    /* Normalize path, return no-such-file if invalid  */
    if (guac_rdp_fs_normalize_path(path, normalized_path)) {
        GUAC_RDP_DEBUG(1, "Normalization of path \"%s\" failed.", path);
        return GUAC_RDP_FS_ENOENT;
    }

    GUAC_RDP_DEBUG(2, "Normalized path \"%s\" to \"%s\".",
            path, normalized_path);

    /* Create \Download if it doesn't exist */
    if (strcmp(normalized_path, "\\") == 0) {

        int download_id = guac_rdp_fs_open(fs, "\\Download",
                ACCESS_GENERIC_READ, 0,
                DISP_FILE_OPEN_IF, FILE_DIRECTORY_FILE);

        if (download_id < 0)
            return download_id;

        guac_rdp_fs_close(fs, download_id);
    }

    /* Translate normalized path to real path */
    __guac_rdp_fs_translate_path(fs, normalized_path, real_path);

    GUAC_RDP_DEBUG(2, "Translated path \"%s\" to \"%s\".",
            normalized_path, real_path);

    switch (create_disposition) {

        /* Create if not exist, fail otherwise */
        case DISP_FILE_CREATE:
            flags |= O_CREAT | O_EXCL;
            break;

        /* Open file if exists and do not overwrite, fail otherwise */
        case DISP_FILE_OPEN:
            /* No flag necessary - default functionality of open */
            break;

        /* Open if exists, create otherwise */
        case DISP_FILE_OPEN_IF:
            flags |= O_CREAT;
            break;

        /* Overwrite if exists, fail otherwise */
        case DISP_FILE_OVERWRITE:
            flags |= O_TRUNC;
            break;

        /* Overwrite if exists, create otherwise */
        case DISP_FILE_OVERWRITE_IF:
            flags |= O_CREAT | O_TRUNC;
            break;

        /* Supersede (replace) if exists, otherwise create */
        case DISP_FILE_SUPERSEDE:
            unlink(real_path);
            flags |= O_CREAT | O_TRUNC;
            break;

        /* Unrecognised disposition */
        default:
            return GUAC_RDP_FS_ENOSYS;

    }

    /* Create directory first, if necessary */
    if ((create_options & FILE_DIRECTORY_FILE) && (flags & O_CREAT)) {

        if (mkdir(real_path, S_IRWXU)) {
            if (errno != EEXIST || (flags & O_EXCL)) {
                GUAC_RDP_DEBUG(1, "mkdir() failed: %s", strerror(errno));
                return guac_rdp_fs_get_errorcode(errno);
            }
        }

        /* Unset O_CREAT and O_EXCL as directory must exist before open() */
        flags &= ~(O_CREAT | O_EXCL);

    }

    GUAC_RDP_DEBUG(2, "native open: real_path=\"%s\", flags=0x%x",
            real_path, flags);

    /* Open file */
    fd = open(real_path, flags, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        GUAC_RDP_DEBUG(1, "open() failed: %s", strerror(errno));
        return guac_rdp_fs_get_errorcode(errno);
    }

    /* Get file ID, init file */
    file_id = guac_pool_next_int(fs->file_id_pool);
    file = &(fs->files[file_id]);
    file->id = file_id;
    file->fd  = fd;
    file->dir = NULL;
    file->dir_pattern[0] = '\0';
    file->absolute_path = strdup(normalized_path);
    file->real_path = strdup(real_path);
    file->bytes_written = 0;

    GUAC_RDP_DEBUG(2, "Opened \"%s\" as file_id=%i", normalized_path, file_id);

    /* Attempt to pull file information */
    if (fstat(fd, &file_stat) == 0) {

        /* Load size and times */
        file->size  = file_stat.st_size;
        file->ctime = WINDOWS_TIME(file_stat.st_ctime);
        file->mtime = WINDOWS_TIME(file_stat.st_mtime);
        file->atime = WINDOWS_TIME(file_stat.st_atime);

        /* Set type */
        if (S_ISDIR(file_stat.st_mode))
            file->attributes = FILE_ATTRIBUTE_DIRECTORY;
        else
            file->attributes = FILE_ATTRIBUTE_NORMAL;

    }

    /* If information cannot be retrieved, fake it */
    else {

        /* Init information to 0, lacking any alternative */
        file->size  = 0;
        file->ctime = 0;
        file->mtime = 0;
        file->atime = 0;
        file->attributes = FILE_ATTRIBUTE_NORMAL;

    }

    fs->open_files++;

    return file_id;

}

int guac_rdp_fs_read(guac_rdp_fs* fs, int file_id, int offset,
        void* buffer, int length) {

    int bytes_read;

    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(1, "Read from bad file_id: %i", file_id);
        return GUAC_RDP_FS_EINVAL;
    }

    /* Attempt read */
    lseek(file->fd, offset, SEEK_SET);
    bytes_read = read(file->fd, buffer, length);

    /* Translate errno on error */
    if (bytes_read < 0)
        return guac_rdp_fs_get_errorcode(errno);

    return bytes_read;

}

int guac_rdp_fs_write(guac_rdp_fs* fs, int file_id, int offset,
        void* buffer, int length) {

    int bytes_written;

    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(1, "Write to bad file_id: %i", file_id);
        return GUAC_RDP_FS_EINVAL;
    }

    /* Attempt write */
    lseek(file->fd, offset, SEEK_SET);
    bytes_written = write(file->fd, buffer, length);

    /* Translate errno on error */
    if (bytes_written < 0)
        return guac_rdp_fs_get_errorcode(errno);

    file->bytes_written += bytes_written;
    return bytes_written;

}

int guac_rdp_fs_rename(guac_rdp_fs* fs, int file_id,
        const char* new_path) {

    char real_path[GUAC_RDP_FS_MAX_PATH];
    char normalized_path[GUAC_RDP_FS_MAX_PATH];

    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(1, "Rename of bad file_id: %i", file_id);
        return GUAC_RDP_FS_EINVAL;
    }

    /* Normalize path, return no-such-file if invalid  */
    if (guac_rdp_fs_normalize_path(new_path, normalized_path)) {
        GUAC_RDP_DEBUG(1, "Normalization of path \"%s\" failed.", new_path);
        return GUAC_RDP_FS_ENOENT;
    }

    /* Translate normalized path to real path */
    __guac_rdp_fs_translate_path(fs, normalized_path, real_path);

    GUAC_RDP_DEBUG(2, "Renaming \"%s\" -> \"%s\"", file->real_path, real_path);

    /* Perform rename */
    if (rename(file->real_path, real_path)) {
        GUAC_RDP_DEBUG(1, "rename() failed: \"%s\" -> \"%s\"",
                file->real_path, real_path);
        return guac_rdp_fs_get_errorcode(errno);
    }

    return 0;

}

int guac_rdp_fs_delete(guac_rdp_fs* fs, int file_id) {

    /* Get file */
    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(1, "Delete of bad file_id: %i", file_id);
        return GUAC_RDP_FS_EINVAL;
    }

    /* Attempt deletion */
    if (unlink(file->real_path)) {
        GUAC_RDP_DEBUG(1, "unlink() failed: \"%s\"", file->real_path);
        return guac_rdp_fs_get_errorcode(errno);
    }

    return 0;

}

int guac_rdp_fs_truncate(guac_rdp_fs* fs, int file_id, int length) {

    /* Get file */
    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(1, "Delete of bad file_id: %i", file_id);
        return GUAC_RDP_FS_EINVAL;
    }

    /* Attempt truncate */
    if (ftruncate(file->fd, length)) {
        GUAC_RDP_DEBUG(1, "ftruncate() to %i bytes failed: \"%s\"",
                length, file->real_path);
        return guac_rdp_fs_get_errorcode(errno);
    }

    return 0;

}

void guac_rdp_fs_close(guac_rdp_fs* fs, int file_id) {

    guac_rdp_fs_file* file = guac_rdp_fs_get_file(fs, file_id);
    if (file == NULL) {
        GUAC_RDP_DEBUG(2, "Ignoring close for bad file_id: %i",
                file_id);
        return;
    }

    file = &(fs->files[file_id]);

    GUAC_RDP_DEBUG(2, "Closed \"%s\" (file_id=%i)",
            file->absolute_path, file_id);

    /* Close directory, if open */
    if (file->dir != NULL)
        closedir(file->dir);

    /* Close file */
    close(file->fd);

    /* Free name */
    free(file->absolute_path);
    free(file->real_path);

    /* Free ID back to pool */
    guac_pool_free_int(fs->file_id_pool, file_id);
    fs->open_files--;

}

const char* guac_rdp_fs_read_dir(guac_rdp_fs* fs, int file_id) {

    guac_rdp_fs_file* file;

    struct dirent* result;

    /* Only read if file ID is valid */
    if (file_id < 0 || file_id >= GUAC_RDP_FS_MAX_FILES)
        return NULL;

    file = &(fs->files[file_id]);

    /* Open directory if not yet open, stop if error */
    if (file->dir == NULL) {
        file->dir = fdopendir(file->fd);
        if (file->dir == NULL)
            return NULL;
    }

    /* Read next entry, stop if error */
    if (readdir_r(file->dir, &(file->__dirent), &result))
        return NULL;

    /* If no more entries, return NULL */
    if (result == NULL)
        return NULL;

    /* Return filename */
    return file->__dirent.d_name;

}

int guac_rdp_fs_normalize_path(const char* path, char* abs_path) {

    int i;
    int path_depth = 0;
    char path_component_data[GUAC_RDP_FS_MAX_PATH];
    const char* path_components[64];

    const char** current_path_component      = &(path_components[0]);
    const char*  current_path_component_data = &(path_component_data[0]);

    /* If original path is not absolute, normalization fails */
    if (path[0] != '\\' && path[0] != '/')
        return 1;

    /* Skip past leading slash */
    path++;

    /* Copy path into component data for parsing */
    strncpy(path_component_data, path, GUAC_RDP_FS_MAX_PATH-1);

    /* Find path components within path */
    for (i=0; i<GUAC_RDP_FS_MAX_PATH; i++) {

        /* If current character is a path separator, parse as component */
        char c = path_component_data[i];
        if (c == '/' || c == '\\' || c == 0) {

            /* Terminate current component */
            path_component_data[i] = 0;

            /* If component refers to parent, just move up in depth */
            if (strcmp(current_path_component_data, "..") == 0) {
                if (path_depth > 0)
                    path_depth--;
            }

            /* Otherwise, if component not current directory, add to list */
            else if (strcmp(current_path_component_data,   ".") != 0
                     && strcmp(current_path_component_data, "") != 0)
                path_components[path_depth++] = current_path_component_data;

            /* If end of string, stop */
            if (c == 0)
                break;

            /* Update start of next component */
            current_path_component_data = &(path_component_data[i+1]);

        } /* end if separator */

    } /* end for each character */

    /* If no components, the path is simply root */
    if (path_depth == 0) {
        strcpy(abs_path, "\\");
        return 0;
    }

    /* Ensure last component is null-terminated */
    path_component_data[i] = 0;

    /* Convert components back into path */
    for (; path_depth > 0; path_depth--) {

        const char* filename = *(current_path_component++);

        /* Add separator */
        *(abs_path++) = '\\';

        /* Copy string */
        while (*filename != 0)
            *(abs_path++) = *(filename++);

    }

    /* Terminate absolute path */
    *(abs_path++) = 0;
    return 0;

}

int guac_rdp_fs_convert_path(const char* parent, const char* rel_path, char* abs_path) {

    int i;
    char combined_path[GUAC_RDP_FS_MAX_PATH];
    char* current = combined_path;

    /* Copy parent path */
    for (i=0; i<GUAC_RDP_FS_MAX_PATH; i++) {

        char c = *(parent++);
        if (c == 0)
            break;

        *(current++) = c;

    }

    /* Add trailing slash */
    *(current++) = '\\';

    /* Copy remaining path */
    strncpy(current, rel_path, GUAC_RDP_FS_MAX_PATH-i-2);

    /* Normalize into provided buffer */
    return guac_rdp_fs_normalize_path(combined_path, abs_path);

}

guac_rdp_fs_file* guac_rdp_fs_get_file(guac_rdp_fs* fs, int file_id) {

    /* Validate ID */
    if (file_id < 0 || file_id >= GUAC_RDP_FS_MAX_FILES)
        return NULL;

    /* Return file at given ID */
    return &(fs->files[file_id]);

}

int guac_rdp_fs_matches(const char* filename, const char* pattern) {
    return fnmatch(pattern, filename, FNM_NOESCAPE) != 0;
}
