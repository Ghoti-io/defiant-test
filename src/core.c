/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Test.
 *
 * Ghoti.io Defiant Test is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Test is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Result strings and the default allocator.
 */

#include <ghoti.io/defiant-test/allocator.h>
#include <ghoti.io/defiant-test/core.h>
#include <ghoti.io/cutil/allocator.h>

const GDEFTEST_Allocator * gdeftest_allocator_default(void) {
  return gcu_allocator_default();
}

const char * gdeftest_result_string(GDEFTEST_Result result) {
  switch (result) {
  case GDEFTEST_OK:
    return "No error";
  case GDEFTEST_ERR_IO:
    return "I/O error";
  case GDEFTEST_ERR_FORMAT:
    return "Invalid format";
  case GDEFTEST_ERR_UNSUPPORTED:
    return "Unsupported format";
  case GDEFTEST_ERR_LIMIT:
    return "Limit exceeded";
  case GDEFTEST_ERR_CORRUPT:
    return "Corrupt data";
  case GDEFTEST_ERR_OOM:
    return "Out of memory";
  case GDEFTEST_ERR_INVALID:
    return "Invalid argument";
  case GDEFTEST_ERR_INTERNAL:
    return "Internal error";
  case GDEFTEST_ERR_BUSY:
    return "Writer busy";
  case GDEFTEST_RESULT_COUNT:
  default:
    return "Unknown error";
  }
}
