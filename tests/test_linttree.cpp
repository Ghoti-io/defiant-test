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
 * The unsafe-list lint (AD-18 items 1-5) over real source trees.
 *
 * Harness.Lint and Harness.LintNames show that the lint finds a token in a
 * string. They say nothing about whether any source file of any Defiant
 * library carries one without its marker. LintTree.RealSource walks the
 * trees named by GDEFTEST_LINT_ROOTS (separated by the platform's path-list
 * separator; unset means the working directory alone) and requires every
 * line that names a token to carry the marker. LintTree.Control runs the same
 * walker over a tree it builds, with one planted construct of each item, and
 * requires each to be found by line and item number: a walker that found
 * nothing would otherwise read as a clean tree. The planted-defect run
 * (tools/check-lint.sh) edits copies of real files and requires
 * LintTree.RealSource to fail by name.
 *
 * This file builds the tokens from halves so that it is itself lint-clean.
 */

#include <ghoti.io/defiant-test/defiant-test.h>

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char * const kHalves[5][2] = {
    {"unescaped-", "output"},
    {"unfiltered-", "read"},
    {"string-", "query"},
    {"unsigned-", "form"},
    {"direct-", "storage"},
};

std::string Token(int item) {
  return std::string(kHalves[item - 1][0]) + kHalves[item - 1][1];
}

/* Directory names that hold generated or foreign bytes, not source. */
bool Skipped(const std::string & name) {
  return name == "build" || name == ".git" || name == "docs" ||
         name.rfind(".local", 0) == 0;
}

struct Walk {
  size_t files = 0;
  size_t binary = 0;
  std::map<std::string, size_t> per_root;
  std::vector<std::string> hits;
};

/* Lint every text file under @p root. A file with a NUL in it is counted and
 * not linted; every other file is, whatever its extension, because the
 * markers are meant to be read in templates and notes as well as in C. */
void WalkRoot(const fs::path & root, Walk * out) {
  std::error_code ec;
  fs::recursive_directory_iterator it(root,
      fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const fs::directory_entry & entry = *it;
    if (entry.is_directory(ec)) {
      if (Skipped(entry.path().filename().string())) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!entry.is_regular_file(ec) || entry.is_symlink(ec)) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if (text.find('\0') != std::string::npos) {
      out->binary += 1;
      continue;
    }
    out->files += 1;
    out->per_root[root.string()] += 1;
    /* gdeftest_lint reports the first hit; resume after it so that every
     * unmarked line in a file is reported, not only the first. */
    size_t base = 0;
    int base_line = 0;
    for (;;) {
      int line = 0;
      int item = 0;
      if (gdeftest_lint(text.data() + base, text.size() - base, &line,
              &item) == GDEFTEST_OK) {
        break;
      }
      out->hits.push_back(entry.path().string() + ":" +
          std::to_string(base_line + line) + ": item " + std::to_string(item) +
          " (" + Token(item) + ") without the marker");
      /* Move past the offending line. */
      size_t at = base;
      for (int l = 1; l <= line && at < text.size(); l++) {
        size_t nl = text.find('\n', at);
        at = nl == std::string::npos ? text.size() : nl + 1;
      }
      base_line += line;
      base = at;
    }
  }
}

std::vector<fs::path> Roots() {
  std::vector<fs::path> roots;
  const char * env = getenv("GDEFTEST_LINT_ROOTS");
  if (env == NULL || env[0] == '\0') {
    roots.push_back(fs::current_path());
    return roots;
  }
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::stringstream list(env);
  std::string piece;
  while (std::getline(list, piece, sep)) {
    if (!piece.empty()) {
      roots.push_back(piece);
    }
  }
  return roots;
}

}  // namespace

TEST(LintTree, Control) {
  char tmpl[512];
  const char * tmp = getenv("TMPDIR");
  snprintf(tmpl, sizeof tmpl, "%s/gdeftest-linttree-XXXXXX",
      tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
  char * made = mkdtemp(tmpl);
  ASSERT_NE(made, nullptr);
  fs::path root(made);
  fs::create_directories(root / "src");
  fs::create_directories(root / "build");
  /* One planted construct of each item, on line 3 of its own file. */
  for (int item = 1; item <= 5; item++) {
    std::ofstream out(root / "src" / ("planted" + std::to_string(item) + ".c"));
    out << "int a;\nint b;\nconst char * q = \"" << Token(item) << "\";\n";
  }
  {
    /* The same constructs, marked: clean. */
    std::ofstream out(root / "src" / "marked.c");
    for (int item = 1; item <= 5; item++) {
      out << "/* unsafe: " << Token(item) << " */\n";
    }
  }
  {
    /* An unmarked construct in a generated directory is not source. */
    std::ofstream out(root / "build" / "generated.c");
    out << Token(1) << "\n";
  }
  {
    /* Bytes with a NUL are not linted, and are counted. */
    std::ofstream out(root / "src" / "blob.bin", std::ios::binary);
    out << Token(2) << '\0' << "\n";
  }
  Walk walk;
  WalkRoot(root, &walk);
  EXPECT_EQ((size_t)6, walk.files);
  EXPECT_EQ((size_t)1, walk.binary);
  ASSERT_EQ((size_t)5, walk.hits.size());
  std::set<std::string> seen(walk.hits.begin(), walk.hits.end());
  for (int item = 1; item <= 5; item++) {
    std::string want = (root / "src" / ("planted" + std::to_string(item) + ".c"))
                           .string() + ":3: item " + std::to_string(item);
    bool found = false;
    for (const std::string & hit : seen) {
      if (hit.rfind(want, 0) == 0) {
        found = true;
      }
    }
    EXPECT_TRUE(found) << "the walker did not report " << want;
  }
  fs::remove_all(root);
}

TEST(LintTree, RealSource) {
  std::vector<fs::path> roots = Roots();
  Walk walk;
  ASSERT_FALSE(roots.empty());
  for (const fs::path & root : roots) {
    ASSERT_TRUE(fs::is_directory(root)) << root << " is not a directory";
    WalkRoot(root, &walk);
    /* A root that yields nothing is a path typed wrong, not a clean tree. */
    EXPECT_GT(walk.per_root[root.string()], (size_t)5)
        << root << " yielded too few files to be a library";
  }
  for (const std::string & hit : walk.hits) {
    ADD_FAILURE() << hit;
  }
  EXPECT_GT(walk.files, (size_t)20);
  printf("LintTree.RealSource: %zu files in %zu roots, %zu binary skipped, "
         "%zu unmarked\n",
      walk.files, roots.size(), walk.binary, walk.hits.size());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
