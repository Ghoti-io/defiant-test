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
 * The five unsafe-list tokens, and a decimal internal id in caller bytes.
 */

#include <ghoti.io/defiant-test/macros.h>
#include <ghoti.io/defiant-test/lint.h>

#include <string.h>

static const char * kItems[] = {
    "unescaped-output", /* unsafe-list item 1 */
    "unfiltered-read",  /* unsafe-list item 2 */
    "string-query",     /* unsafe-list item 3 */
    "unsigned-form",    /* unsafe-list item 4 */
    "direct-storage",   /* unsafe-list item 5 */
};

static int line_has(const char * line, size_t n, const char * needle) {
  size_t m = strlen(needle);
  size_t i;
  if (m == 0 || m > n) {
    return 0;
  }
  for (i = 0; i + m <= n; i++) {
    if (memcmp(line + i, needle, m) == 0) {
      return 1;
    }
  }
  return 0;
}

GDEFTEST_Result gdeftest_lint(const char * text, size_t text_len, int * out_line,
    int * out_item) {
  size_t at = 0;
  int line = 1;
  if (out_line != NULL) {
    *out_line = 0;
  }
  if (out_item != NULL) {
    *out_item = 0;
  }
  if ((text == NULL && text_len != 0) || out_line == NULL || out_item == NULL) {
    return GDEFTEST_ERR_INVALID;
  }
  if (text == NULL) {
    text = "";
    text_len = 0;
  }
  while (at <= text_len) {
    size_t end = at;
    int item;
    while (end < text_len && text[end] != '\n') {
      end += 1;
    }
    if (!line_has(text + at, end - at, "unsafe")) {
      for (item = 0; item < 5; item++) {
        if (line_has(text + at, end - at, kItems[item])) {
          *out_line = line;
          *out_item = item + 1;
          return GDEFTEST_ERR_INVALID;
        }
      }
    }
    if (end >= text_len) {
      break;
    }
    at = end + 1;
    line += 1;
  }
  return GDEFTEST_OK;
}

static int span_has_id(const char * text, size_t n) {
  size_t i = 0;
  if (text == NULL || n == 0) {
    return 0;
  }
  while (i < n) {
    if (text[i] == '&' && i + 1 < n && text[i + 1] == '#') {
      size_t j = i + 2;
      while (j < n && text[j] != ';') {
        j += 1;
      }
      if (j < n) {
        i = j + 1;
        continue;
      }
    }
    if (text[i] >= '0' && text[i] <= '9') {
      size_t start = i;
      int left_bare;
      int right_bare;
      while (i < n && text[i] >= '0' && text[i] <= '9') {
        i += 1;
      }
      left_bare = start == 0 ||
                  !((text[start - 1] >= '0' && text[start - 1] <= '9') ||
                      (text[start - 1] >= 'A' && text[start - 1] <= 'Z') ||
                      (text[start - 1] >= 'a' && text[start - 1] <= 'z') ||
                      text[start - 1] == '_');
      right_bare = i == n ||
                   !((text[i] >= '0' && text[i] <= '9') ||
                       (text[i] >= 'A' && text[i] <= 'Z') ||
                       (text[i] >= 'a' && text[i] <= 'z') || text[i] == '_');
      if (left_bare && right_bare) {
        return 1;
      }
      continue;
    }
    i += 1;
  }
  return 0;
}

int gdeftest_response_has_internal_id(const char * page, size_t page_len,
    const char * error, size_t error_len) {
  if ((page == NULL && page_len != 0) || (error == NULL && error_len != 0)) {
    return 1;
  }
  return span_has_id(page, page_len) || span_has_id(error, error_len);
}
