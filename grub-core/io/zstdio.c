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

#include <include/grub/zstd.h>

#define MAX_BUF_SIZE		(16384)
#define ZSTD_BTRFS_MAX_WINDOWLOG 17
#define ZSTD_BTRFS_MAX_INPUT (1 << ZSTD_BTRFS_MAX_WINDOWLOG)
#define ZSTD_BTRFS_DEFAULT_LEVEL 3

struct grub_zstdio
{
  grub_file_t file;
  grub_uint8_t inbuf[MAX_BUF_SIZE];
  grub_uint8_t outbuf[MAX_BUF_SIZE];
  ZSTD_DStream *stream;
  void *workmem;
  size_t worksize;
  void *buf;
  ZSTD_inBuffer in_buf;
  ZSTD_outBuffer out_buf;
  grub_off_t saved_offset;
};

typedef struct grub_zstdio *grub_zstdio_t;
static struct grub_fs grub_zstdio_fs;

static grub_file_t
grub_zstdio_open (grub_file_t io,
		const char *name __attribute__ ((unused)))
{
  grub_file_t file;
  grub_zstdio_t zstdio;

  file = (grub_file_t) grub_zalloc (sizeof (*file));
  if (!file)
    return 0;

  zstdio = grub_zalloc (sizeof (*zstdio));
  if (!zstdio)
    {
      grub_free (file);
      return 0;
    }

  zstdio->file = io;
  file->device = io->device;
  file->data = zstdio;
  file->fs = &grub_zstdio_fs;
  file->size = GRUB_FILE_SIZE_UNKNOWN;
  file->not_easily_seekable = 1;

  if (grub_file_tell (zstdio->file) != 0)
    grub_file_seek (zstdio->file, 0);

  zstdio->worksize = ZSTD_DStreamWorkspaceBound(ZSTD_BTRFS_MAX_INPUT);
  zstdio->workmem = grub_zalloc (zstdio->worksize);
  if (!zstdio->workmem)
    {
      grub_free (zstdio);
      grub_free (file);
      return 0;
    }

  zstdio->stream = ZSTD_initDStream((1 << ZSTD_BTRFS_MAX_WINDOWLOG),
		  zstdio->workmem, zstdio->worksize);
  if (!zstdio->stream)
    {
      grub_free (zstdio->workmem);
      grub_free (zstdio);
      grub_free (file);
      return 0;
    }

  zstdio->in_buf.src = zstdio->inbuf;
  zstdio->in_buf.pos = 0;
  zstdio->in_buf.size = MAX_BUF_SIZE;
  zstdio->out_buf.dst = zstdio->outbuf;
  zstdio->out_buf.pos = 0;
  zstdio->out_buf.size = MAX_BUF_SIZE;

  /* TODO: check validity of the stream */

  return file;
}

static grub_ssize_t
grub_zstdio_read (grub_file_t file, char *buf, grub_size_t len)
{
  grub_ssize_t ret = 0;
  grub_ssize_t readret;
  grub_size_t zstdret;
  grub_zstdio_t zstdio = file->data;
  grub_off_t current_offset;

  /* If seek backward need to reset decoder and start from beginning of file.
     TODO Possible improvement by jumping blocks.  */
  if (file->offset < zstdio->saved_offset)
    {
      /* TODO: replacement? */
      /* xz_dec_reset (xzio->dec); */
      zstdio->saved_offset = 0;
      zstdio->out_buf.pos = 0;
      zstdio->in_buf.pos = 0;
      zstdio->in_buf.size = 0;
      grub_file_seek (zstdio->file, 0);
    }

  current_offset = zstdio->saved_offset;

  while (len > 0)
    {
      grub_size_t outsize;

      outsize = file->offset + ret + len - current_offset;
      if (outsize > MAX_BUF_SIZE)
	outsize = MAX_BUF_SIZE;

      zstdio->out_buf.size = outsize;

      /* Feed input.  */
      if (zstdio->in_buf.pos == zstdio->in_buf.size)
	{
	  readret = grub_file_read (zstdio->file, zstdio->inbuf, MAX_BUF_SIZE);
	  if (readret < 0)
	    return -1;
	  zstdio->in_buf.size = readret;
	  zstdio->in_buf.pos = 0;
	}

      zstdret = ZSTD_decompressStream(zstdio->stream, &zstdio->out_buf,
		      &zstdio->in_buf);

      if (ZSTD_isError(zstdret))
        {
	  grub_error (GRUB_ERR_BAD_COMPRESSED_DATA,
		      N_("zstd file corrupted or unsupported block options"));
	  return -1;
	}

      {
	grub_off_t new_offset = current_offset + zstdio->out_buf.pos;

	if (file->offset <= new_offset)
	  /* Store first chunk of data in buffer.  */
	  {
	    grub_size_t delta = new_offset - (file->offset + ret);
	    grub_memmove (buf,
			    zstdio->out_buf.dst + (zstdio->out_buf.pos - delta),
			    delta);
	    len -= delta;
	    buf += delta;
	    ret += delta;
	  }
	current_offset = new_offset;
      }
      zstdio->out_buf.pos = 0;
    }

  if (ret >= 0)
    zstdio->saved_offset = file->offset + ret;

  return ret;
}

/* Release everything, including the underlying file object.  */
static grub_err_t
grub_zstdio_close (grub_file_t file)
{
  grub_zstdio_t zstdio = file->data;

  grub_file_close (zstdio->file);
  grub_free (zstdio->workmem);
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
