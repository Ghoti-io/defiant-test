/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Test.
 *
 * Ghoti.io Defiant Test is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Test is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Read a zip and write each member name and body to stdout. Linked only
 * against archive, so a snapshot can be read without the entity API.
 */

#include <ghoti.io/archive/reader.h>
#include <ghoti.io/archive/stream.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char * path, uint8_t ** out, size_t * out_len) {
  FILE * fp;
  long n;
  uint8_t * buf;
  fp = fopen(path, "rb");
  if (fp == NULL) {
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 0;
  }
  n = ftell(fp);
  if (n < 0) {
    fclose(fp);
    return 0;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 0;
  }
  buf = (uint8_t *)malloc(n == 0 ? 1 : (size_t)n);
  if (buf == NULL) {
    fclose(fp);
    return 0;
  }
  if (n > 0 && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
    free(buf);
    fclose(fp);
    return 0;
  }
  fclose(fp);
  *out = buf;
  *out_len = (size_t)n;
  return 1;
}

int main(int argc, char ** argv) {
  uint8_t * bytes = NULL;
  size_t len = 0;
  GARC_Stream * stream = NULL;
  GARC_Archive * archive = NULL;
  GARC_Result gr;
  if (argc != 2) {
    fprintf(stderr, "usage: readzip <file>\n");
    return 2;
  }
  if (!read_file(argv[1], &bytes, &len)) {
    fprintf(stderr, "readzip: cannot read %s\n", argv[1]);
    return 1;
  }
  if (garc_stream_create_memory(bytes, len, &stream) != GARC_OK) {
    fprintf(stderr, "readzip: not a zip\n");
    free(bytes);
    return 1;
  }
  if (garc_open(stream, NULL, &archive) != GARC_OK) {
    fprintf(stderr, "readzip: not a zip\n");
    garc_stream_destroy(stream);
    free(bytes);
    return 1;
  }
  for (;;) {
    const GARC_Member * member = NULL;
    uint8_t * body = NULL;
    size_t got_total = 0;
    gr = garc_next(archive, &member);
    if (gr == GARC_END) {
      break;
    }
    if (gr != GARC_OK || member->name == NULL) {
      fprintf(stderr, "readzip: bad member\n");
      garc_close(archive);
      garc_stream_destroy(stream);
      free(bytes);
      return 1;
    }
    if ((member->name_length > 0 &&
            fwrite(member->name, 1, member->name_length, stdout) !=
                member->name_length) ||
        fputc('\n', stdout) == EOF) {
      garc_close(archive);
      garc_stream_destroy(stream);
      free(bytes);
      return 1;
    }
    if (member->size > SIZE_MAX) {
      fprintf(stderr, "readzip: member too large\n");
      garc_close(archive);
      garc_stream_destroy(stream);
      free(bytes);
      return 1;
    }
    if (member->size == 0 || member->type != GARC_MEMBER_FILE) {
      continue;
    }
    body = (uint8_t *)malloc((size_t)member->size);
    if (body == NULL) {
      garc_close(archive);
      garc_stream_destroy(stream);
      free(bytes);
      return 1;
    }
    while (got_total < (size_t)member->size) {
      size_t got = 0;
      gr = garc_read_member(archive, body + got_total,
          (size_t)member->size - got_total, &got);
      if (gr != GARC_OK || got == 0) {
        free(body);
        garc_close(archive);
        garc_stream_destroy(stream);
        free(bytes);
        return 1;
      }
      got_total += got;
    }
    if ((got_total > 0 && fwrite(body, 1, got_total, stdout) != got_total) ||
        fputc('\n', stdout) == EOF) {
      free(body);
      garc_close(archive);
      garc_stream_destroy(stream);
      free(bytes);
      return 1;
    }
    free(body);
  }
  garc_close(archive);
  garc_stream_destroy(stream);
  free(bytes);
  return 0;
}
