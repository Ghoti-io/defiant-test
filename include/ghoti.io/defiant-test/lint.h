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
 * @file lint.h
 * @brief Unsafe-list items 1–5, and a scanner for an internal id.
 * @stability free
 *
 * A line is clean when it carries the greppable marker `unsafe`. Items 6
 * and 7 are not in this lint. The scanner reads bytes the caller already
 * has: a rendered page and a visitor-facing error. There is no HTTP
 * response here.
 */

#ifndef GHOTI_IO_GDEFTEST_LINT_H
#define GHOTI_IO_GDEFTEST_LINT_H

#include <ghoti.io/defiant-test/macros.h>
#include <ghoti.io/defiant-test/core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Grep @p text for unsafe-list items 1–5.
 *
 * The five constructs are these tokens, one per unsafe-list item:
 * - `unescaped-output` (unsafe-list item 1),
 * - `unfiltered-read` (unsafe-list item 2),
 * - `string-query` (unsafe-list item 3),
 * - `unsigned-form` (unsafe-list item 4) and
 * - `direct-storage` (unsafe-list item 5).
 *
 * A line containing `unsafe` is clean. On a hit, @p out_line is the
 * 1-based line and @p out_item is 1–5. Both stay 0 when the text is clean.
 *
 * @return ::GDEFTEST_OK when every hit is marked, ::GDEFTEST_ERR_INVALID
 *   when a line is not, ::GDEFTEST_ERR_INVALID when @p text is NULL and
 *   @p text_len is not 0.
 */
GDEFTEST_API GDEFTEST_Result gdeftest_lint(const char * text, size_t text_len,
    int * out_line, int * out_item);

/**
 * @brief Whether @p page or @p error contains an internal-id shape.
 *
 * An internal id is a decimal integer standing on its own. A numeric
 * character reference (`&#...;`) is not one. NULL with length 0 is empty.
 *
 * @return 1 when a shape is present, 0 when it is not.
 */
GDEFTEST_API int gdeftest_response_has_internal_id(const char * page,
    size_t page_len, const char * error, size_t error_len);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GDEFTEST_LINT_H */
