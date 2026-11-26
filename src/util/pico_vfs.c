/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <ioutils/pico_vfs.h>
#include <string.h>
#include <assert.h>
#include <microkit.h>
#include <libtrustedlo.h>

#define PROGNAME "  => [@picovfs] "

static uint64_t openfile(const char fname[], uint64_t *size)
{
    ptrdiff_t buffer;
    int err = fs_buffer_allocate(&buffer);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "failed to allocate buffer to open file");
        // halt...
        while (1) {
            ;
        }
    }

    uint64_t path_len = strlen(fname) + 1;
    memcpy(fs_buffer_ptr(buffer), fname, path_len);

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion,
                              (fs_cmd_t){.type = FS_CMD_FILE_OPEN,
                                         .params.file_open = {
                                             .path.offset = buffer,
                                             .path.size = path_len,
                                             .flags = 0 & (0 | FS_OPEN_FLAGS_CREATE),
                                         }});
    fs_buffer_free(buffer);
    if (err || completion.status != FS_STATUS_SUCCESS) {
        return -1;
    }
    uint64_t fd = completion.data.file_open.fd;

    err = fs_command_blocking(&completion,
                              (fs_cmd_t){
                                  .type = FS_CMD_FILE_SIZE,
                                  .params.file_size.fd = fd,
                              });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        fs_command_blocking(&completion,
                            (fs_cmd_t){
                                .type = FS_CMD_FILE_CLOSE,
                                .params.file_close.fd = fd,
                            });
        TSLDR_DBG_PRINT(PROGNAME "failed to size file");
        return -1;
    }
    if (size != NULL) {
        *size = completion.data.file_size.size;
    }
    return fd;
}

static uint64_t readfile(void *dest, uint64_t size, uint64_t fd, uint64_t pos)
{
    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "failed to allocate buffer to read file");
        // halt...
        while (1) {
            ;
        }
    }

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion,
                              (fs_cmd_t){.type = FS_CMD_FILE_READ,
                                         .params.file_read = {
                                             .fd = fd,
                                             .offset = pos,
                                             .buf.offset = read_buffer,
                                             .buf.size = size,
                                         }});
    if (err || completion.status != FS_STATUS_SUCCESS) {
        fs_buffer_free(read_buffer);
        TSLDR_DBG_PRINT(PROGNAME "failed to read file");
        return -1;
    }

    memcpy(dest, fs_buffer_ptr(read_buffer), completion.data.file_read.len_read);
    fs_buffer_free(read_buffer);

    return pos + completion.data.file_read.len_read;
}

static void closefile(uint64_t fd)
{
    fs_cmpl_t completion;
    fs_command_blocking(&completion,
                        (fs_cmd_t){
                            .type = FS_CMD_FILE_CLOSE,
                            .params.file_close.fd = fd,
                        });
}

bool pico_vfs_file_exists(const char *path)
{
    uint64_t fd = openfile(path, NULL);

    if (fd == (uint64_t)-1) {
        return false;
    }
    closefile(fd);
    return true;
}

uint64_t pico_vfs_readfile2buf(void *buf, const char *path, int *err)
{
    *err = seL4_NoError;

    uint64_t file_fd = openfile(path, NULL);
    if (file_fd != (uint64_t)-1) {
        TSLDR_DBG_PRINT(PROGNAME "(file open): fd is %d opened\n", file_fd);
    } else {
        *err = -1;
        TSLDR_DBG_PRINT(PROGNAME "(file open): failed to open %s\n", path);
        return 0;
    }

    uint64_t pos = 0;
    uint64_t pre;
    while (true) {
        pre = pos;
        pos = readfile(buf, FS_BUFFER_SIZE, file_fd, pos);
        if (pos == (uint64_t)-1) {
            *err = -1;
            TSLDR_DBG_PRINT(PROGNAME "(file read): failed to read from fd: %d\n", file_fd);
            return 0;
        }
        if (pos == pre) {
            TSLDR_DBG_PRINT(PROGNAME "(file read): all read from %d\n", file_fd);
            break;
        }
        buf += pos - pre;
    }
    TSLDR_DBG_PRINT(PROGNAME "(file read): read %d data from %d \n", pos, file_fd);

    closefile(file_fd);

    return pos;
}

int pico_vfs_read_to_cache(void *cache, uint64_t capacity, const char *path, uint64_t *size)
{
    if (path == NULL || size == NULL) {
        TSLDR_DBG_PRINT(PROGNAME "cache read: invalid arguments\n");
        return -1;
    }
    *size = 0;

    uint64_t file_size;
    uint64_t fd = openfile(path, &file_size);
    if (fd == (uint64_t)-1) {
        TSLDR_DBG_PRINT(PROGNAME "cache read: failed to open %s\n", path);
        return -1;
    }
    if (file_size > capacity || (file_size != 0 && cache == NULL)) {
        TSLDR_DBG_PRINT(PROGNAME "cache read: %s needs %lu bytes, capacity is %lu\n",
                        path,
                        file_size,
                        capacity);
        closefile(fd);
        return file_size > capacity ? -2 : -1;
    }

    uint64_t pos = 0;
    while (pos < file_size) {
        uint64_t remaining = file_size - pos;
        uint64_t chunk = remaining < FS_BUFFER_SIZE ? remaining : FS_BUFFER_SIZE;
        uint64_t next = readfile((uint8_t *)cache + pos, chunk, fd, pos);
        if (next == (uint64_t)-1 || next <= pos || next > file_size) {
            TSLDR_DBG_PRINT(PROGNAME "cache read: failed to read %s at offset %lu\n", path, pos);
            closefile(fd);
            return -1;
        }
        pos = next;
    }

    closefile(fd);
    *size = pos;
    TSLDR_DBG_PRINT(PROGNAME "cache read: loaded %s (%lu bytes)\n", path, pos);
    return 0;
}
