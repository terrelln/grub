/* zstdio.c - decompression support for zstd */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2010  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copied from xzio.c
 */

#include <grub/err.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/fs.h>
#include <grub/dl.h>

GRUB_MOD_LICENSE ("GPLv3+");

#define XZBUFSIZ 0x2000

struct grub_zstdio
{
  grub_file_t file;
  grub_uint8_t inbuf[XZBUFSIZ];
  grub_uint8_t outbuf[XZBUFSIZ];
  grub_off_t saved_offset;
};

typedef struct grub_zstdio *grub_zstdio_t;
static struct grub_fs grub_zstdio_fs;

static grub_file_t
grub_zstdio_open (grub_file_t io,
		const char *name __attribute__ ((unused)))
{
  return 0;
}

static grub_ssize_t
grub_zstdio_read (grub_file_t file, char *buf, grub_size_t len)
{
  return -1;
}

/* Release everything, including the underlying file object.  */
static grub_err_t
grub_zstdio_close (grub_file_t file)
{
  grub_zstdio_t zstdio = file->data;

  grub_file_close (zstdio->file);
  grub_free (zstdio);

  /* Device must not be closed twice.  */
  file->device = 0;
  file->name = 0;
  return grub_errno;
}

static struct grub_fs grub_zstdio_fs = {
  .name = "zstdio",
  .dir = 0,
  .open = 0,
  .read = grub_zstdio_read,
  .close = grub_zstdio_close,
  .label = 0,
  .next = 0
};

GRUB_MOD_INIT (zstdio)
{
  grub_file_filter_register (GRUB_FILE_FILTER_ZSTDIO, grub_zstdio_open);
}

GRUB_MOD_FINI (zstdio)
{
  grub_file_filter_unregister (GRUB_FILE_FILTER_ZSTDIO);
}
