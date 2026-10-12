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
 * Story 15. One test per matrix row, on the memory backend. The round trip
 * also opens SQLite. Plants are GHOTI_DEFTEST_PLANT and live only here.
 */

#include <ghoti.io/archive/reader.h>
#include <ghoti.io/archive/sink.h>
#include <ghoti.io/archive/stream.h>
#include <ghoti.io/archive/writer.h>
#include <ghoti.io/defiant-core/defiant-core.h>
#include <ghoti.io/defiant-storage-memory/memory.h>
#include <ghoti.io/defiant-storage-sqlite/sqlite.h>
#include <ghoti.io/defiant-storage/projection.h>
#include <ghoti.io/defiant-storage/snapshot.h>
#include <ghoti.io/defiant-storage/store.h>
#include <ghoti.io/defiant-test/defiant-test.h>

#include <gtest/gtest.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace {

const char kPlain[] = "platypus-secret";
const char kTitle[] = "Hello";
const char kCanary[] = "CANARY-HOSTILE";
const uint64_t kLife = 3600;

const char * Plant() {
  const char * plant = getenv("GHOTI_DEFTEST_PLANT");
  return plant == NULL ? "" : plant;
}

bool HasBytes(const uint8_t * hay, size_t n, const void * needle, size_t m) {
  size_t i;
  if (needle == NULL || m == 0 || m > n) {
    return false;
  }
  for (i = 0; i + m <= n; i++) {
    if (memcmp(hay + i, needle, m) == 0) {
      return true;
    }
  }
  return false;
}

struct ZipPart {
  const char * name;
  const uint8_t * body;
  size_t body_len;
};

int MakeZip(const ZipPart * parts, size_t n, int deflate, uint8_t ** out,
    size_t * out_len) {
  GARC_Sink * sink = NULL;
  GARC_Writer * writer = NULL;
  GARC_Writer_Options opt;
  size_t i;
  const void * data = NULL;
  size_t size = 0;
  uint8_t * copy;
  if (garc_sink_create_memory(&sink) != GARC_OK) {
    return 0;
  }
  garc_writer_options_default(&opt);
  if (deflate) {
    opt.zip_method = GARC_ZIP_METHOD_DEFLATE;
  }
  if (garc_writer_create(sink, GARC_FORMAT_ZIP, &opt, &writer) != GARC_OK) {
    garc_sink_destroy(sink);
    return 0;
  }
  for (i = 0; i < n; i++) {
    GARC_Member member;
    memset(&member, 0, sizeof member);
    member.name = parts[i].name;
    member.name_length = strlen(parts[i].name);
    member.type = GARC_MEMBER_FILE;
    member.size = parts[i].body_len;
    if (garc_writer_add(writer, &member) != GARC_OK ||
        (parts[i].body_len > 0 &&
            garc_writer_write(writer, parts[i].body, parts[i].body_len) !=
                GARC_OK)) {
      garc_writer_destroy(writer);
      garc_sink_destroy(sink);
      return 0;
    }
  }
  if (garc_writer_finish(writer) != GARC_OK) {
    garc_writer_destroy(writer);
    garc_sink_destroy(sink);
    return 0;
  }
  garc_writer_destroy(writer);
  if (garc_sink_data(sink, &data, &size) != GARC_OK) {
    garc_sink_destroy(sink);
    return 0;
  }
  copy = (uint8_t *)malloc(size == 0 ? 1 : size);
  if (copy == NULL) {
    garc_sink_destroy(sink);
    return 0;
  }
  if (size > 0) {
    memcpy(copy, data, size);
  }
  garc_sink_destroy(sink);
  *out = copy;
  *out_len = size;
  return 1;
}

bool ZipMembers(const uint8_t * bytes, size_t len,
    std::map<std::string, std::string> * members) {
  GARC_Stream * stream = NULL;
  GARC_Archive * archive = NULL;
  if (bytes == NULL ||
      garc_stream_create_memory(bytes, len, &stream) != GARC_OK) {
    return false;
  }
  if (garc_open(stream, NULL, &archive) != GARC_OK) {
    garc_stream_destroy(stream);
    return false;
  }
  for (;;) {
    const GARC_Member * member = NULL;
    GARC_Result gr = garc_next(archive, &member);
    std::string body;
    size_t got_total = 0;
    uint8_t * buf = NULL;
    int short_read = 0;
    if (gr == GARC_END) {
      break;
    }
    if (gr != GARC_OK || member->name == NULL) {
      garc_close(archive);
      garc_stream_destroy(stream);
      return false;
    }
    if (member->size > 0 && member->type == GARC_MEMBER_FILE) {
      if (member->size > SIZE_MAX) {
        garc_close(archive);
        garc_stream_destroy(stream);
        return false;
      }
      buf = (uint8_t *)malloc((size_t)member->size);
      if (buf == NULL) {
        garc_close(archive);
        garc_stream_destroy(stream);
        return false;
      }
      while (got_total < (size_t)member->size) {
        size_t got = 0;
        gr = garc_read_member(archive, buf + got_total,
            (size_t)member->size - got_total, &got);
        if (gr != GARC_OK || got == 0) {
          short_read = 1;
          break;
        }
        got_total += got;
      }
      if (short_read || got_total != (size_t)member->size) {
        free(buf);
        garc_close(archive);
        garc_stream_destroy(stream);
        return false;
      }
      body.assign((const char *)buf, got_total);
      free(buf);
    }
    (*members)[std::string(member->name, member->name_length)] = body;
  }
  garc_close(archive);
  garc_stream_destroy(stream);
  return true;
}

std::set<std::string> ManifestKeys(const std::string & manifest) {
  const std::string needle = "\"key\":\"";
  std::set<std::string> keys;
  size_t at = 0;
  while ((at = manifest.find(needle, at)) != std::string::npos) {
    size_t end;
    at += needle.size();
    end = manifest.find('"', at);
    if (end == std::string::npos) {
      break;
    }
    keys.insert(manifest.substr(at, end - at));
    at = end + 1;
  }
  return keys;
}

bool StoresEqual(GDEFSTOR_Store * left, GDEFSTOR_Store * right,
    const uint8_t * tenant, size_t tenant_len) {
  uint8_t * a = NULL;
  uint8_t * b = NULL;
  size_t a_len = 0;
  size_t b_len = 0;
  std::map<std::string, std::string> ma;
  std::map<std::string, std::string> mb;
  std::string man_a;
  std::string man_b;
  bool same;
  if (gdefstor_snapshot_export(left, (const char *)tenant, tenant_len, NULL, &a,
          &a_len) != GDEFSTOR_OK ||
      gdefstor_snapshot_export(right, (const char *)tenant, tenant_len, NULL, &b,
          &b_len) != GDEFSTOR_OK) {
    gdefstor_bytes_free(NULL, a);
    gdefstor_bytes_free(NULL, b);
    return false;
  }
  if (!ZipMembers(a, a_len, &ma) || !ZipMembers(b, b_len, &mb)) {
    gdefstor_bytes_free(NULL, a);
    gdefstor_bytes_free(NULL, b);
    return false;
  }
  man_a = ma["manifest"];
  man_b = mb["manifest"];
  ma.erase("manifest");
  mb.erase("manifest");
  same = ma == mb && ManifestKeys(man_a) == ManifestKeys(man_b);
  gdefstor_bytes_free(NULL, a);
  gdefstor_bytes_free(NULL, b);
  return same;
}

void OneValue(GDEFCORE_Value * value, const char * field, const char * text,
    const char ** slot, size_t * len_slot) {
  memset(value, 0, sizeof *value);
  *slot = text;
  *len_slot = strlen(text);
  value->size = sizeof *value;
  value->field = field;
  value->field_len = strlen(field);
  value->texts = slot;
  value->text_lens = len_slot;
  value->count = 1;
}

class Site : public ::testing::Test {
 protected:
  GDEFSTOR_Store * registry_;
  GDEFSTOR_Store * site_;
  GDEFSTOR_Store * side_;
  GDEFCORE_Inst * inst_;
  GDEFCORE_Catalog * catalog_;
  GDEFCORE_Subjects * subjects_;
  GDEFCORE_Request * request_;
  uint8_t root_[GDEFCORE_ACCOUNT_ROOT_LEN];
  uint8_t storage_[16];
  char tenant_[GDEFCORE_PUBLIC_ID_LEN + 1];
  std::string session_;

  void SetUp() override {
    registry_ = NULL;
    site_ = NULL;
    side_ = NULL;
    inst_ = NULL;
    catalog_ = NULL;
    subjects_ = NULL;
    request_ = NULL;
    memset(root_, 0x42, sizeof root_);
    memset(storage_, 0, sizeof storage_);
    memset(tenant_, 0, sizeof tenant_);
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &registry_));
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &site_));
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &side_));
  }

  void TearDown() override {
    gdefcore_request_destroy(request_);
    gdefcore_subject_destroy(subjects_);
    gdefcore_catalog_destroy(catalog_);
    gdefcore_inst_destroy(inst_);
    gdefstor_store_destroy(side_);
    gdefstor_store_destroy(site_);
    gdefstor_store_destroy(registry_);
  }

  void Open() {
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_open_side(registry_, site_, side_, root_, sizeof root_, "",
            0, NULL, &inst_));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_default(inst_, tenant_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_storage_key(inst_, tenant_, strlen(tenant_), storage_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_inst_catalog_open(inst_, tenant_, strlen(tenant_), &catalog_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_subject_open(side_, storage_, sizeof storage_, root_,
            sizeof root_,
            gdefcore_inst_tenant_keyring(inst_, tenant_, strlen(tenant_)), NULL,
            &subjects_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_request_create(storage_, sizeof storage_, 0, 0, NULL,
            &request_));
  }

  void Install() {
    uint8_t * bytes = NULL;
    size_t n = 0;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_kit(catalog_, &bytes, &n));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_install(catalog_, bytes, n));
    gdefcore_text_free(catalog_, (char *)bytes);
  }

  void Login() {
    char admin[GDEFCORE_PUBLIC_ID_LEN + 1];
    char id[GDEFCORE_PUBLIC_ID_LEN + 1];
    char * token = NULL;
    size_t n = 0;
    GDEFCORE_Keyring * keyring =
        gdefcore_inst_tenant_keyring(inst_, tenant_, strlen(tenant_));
    ASSERT_NE(keyring, nullptr);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_access_admin_principal(catalog_, admin));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_bind(catalog_, keyring, root_, sizeof root_, admin,
            strlen(admin), "ada", 3, "secret", 6, id));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_login(catalog_, keyring, root_, sizeof root_, "ada", 3,
            "secret", 6, kLife, &token, &n));
    session_.assign(token, n);
    gdefcore_text_free(catalog_, token);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_account_check(catalog_, request_, keyring, root_, sizeof root_,
            session_.data(), session_.size()));
  }

  void Publish(const char * alias, char id[GDEFCORE_PUBLIC_ID_LEN + 1]) {
    GDEFCORE_Value values[2];
    const char * slots[2];
    size_t lens[2];
    int conflict = 1;
    char * token = NULL;
    size_t token_len = 0;
    const char * body = "Body";
    GDEFCORE_Keyring * keyring =
        gdefcore_inst_tenant_keyring(inst_, tenant_, strlen(tenant_));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_entity_create(catalog_, "site.post", 9, id, NULL));
    OneValue(&values[0], "title", kTitle, &slots[0], &lens[0]);
    OneValue(&values[1], "body", body, &slots[1], &lens[1]);
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_form_sign(catalog_, request_, keyring, root_, sizeof root_,
            session_.data(), session_.size(), "edit", 4, id, strlen(id), "en",
            2, values, 2, alias, strlen(alias), &token, &token_len));
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_form_submit(catalog_, request_, keyring, root_, sizeof root_,
            session_.data(), session_.size(), token, token_len, &conflict));
    gdefcore_text_free(catalog_, token);
    EXPECT_EQ(0, conflict);
    ASSERT_EQ(GDEFCORE_OK, gdefcore_form_publish(catalog_, id, strlen(id)));
  }

  void DefinePerson() {
    GDEFCORE_Field field;
    memset(&field, 0, sizeof field);
    field.size = sizeof field;
    field.name = "name";
    field.name_len = 4;
    field.required = 1;
    field.personal = 1;
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_type_define(catalog_, "site.person", 11, GDEFCORE_TYPE_CONTENT,
            &field, 1));
  }

  void SavePersonal(const char * subject, const char * entity) {
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_subject_save(subjects_, catalog_, subject, strlen(subject),
            entity, strlen(entity), "name", 4, "en", 2, kPlain, strlen(kPlain)));
  }

  void Snapshot(uint8_t ** out, size_t * out_len) {
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_subject_snapshot(subjects_, site_, out, out_len));
  }

  bool ReadIs(GDEFCORE_Catalog * catalog, const char * subject,
      const char * entity, const char * expect) {
    char * text = NULL;
    size_t n = 0;
    GDEFCORE_Result rc = gdefcore_subject_read(subjects_, catalog, subject,
        strlen(subject), entity, strlen(entity), "name", 4, "en", 2, &text, &n);
    bool ok = rc == GDEFCORE_OK && text != NULL && n == strlen(expect) &&
              memcmp(text, expect, n) == 0;
    gdefcore_subject_free(subjects_, text);
    return ok;
  }

  void PlantLedger(const uint8_t * zip, size_t zip_len) {
    uint8_t * ledger = NULL;
    size_t ledger_len = 0;
    GDEFSTOR_Write * write = NULL;
    GDEFSTOR_Column column;
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_snapshot_ledger(site_, zip, zip_len, NULL, &ledger,
            &ledger_len));
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_write_begin(side_, (const char *)storage_, sizeof storage_,
            NULL, &write));
    memset(&column, 0, sizeof column);
    column.name = "text";
    column.name_len = 4;
    column.value = ledger_len == 0 ? (const uint8_t *)"" : ledger;
    column.value_len = ledger_len;
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_projection_put(write, GDEFCORE_SUBJECT_LEDGER_TYPE,
            strlen(GDEFCORE_SUBJECT_LEDGER_TYPE), GDEFCORE_SUBJECT_LEDGER_ID,
            strlen(GDEFCORE_SUBJECT_LEDGER_ID), &column, 1));
    ASSERT_EQ(GDEFSTOR_OK, gdefstor_write_commit(write));
    gdefstor_bytes_free(NULL, ledger);
  }

  void Refuse(const char * name, const void * body, size_t body_len, int deflate,
      GDEFCORE_Result expect) {
    ZipPart part;
    uint8_t * zip = NULL;
    size_t zip_len = 0;
    GDEFSTOR_Store * empty = NULL;
    uint8_t * again = NULL;
    size_t again_len = 0;
    GDEFSTOR_Result exported;
    part.name = name;
    part.body = (const uint8_t *)body;
    part.body_len = body_len;
    ASSERT_TRUE(MakeZip(&part, 1, deflate, &zip, &zip_len));
    ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &empty));
    EXPECT_EQ(expect,
        gdefcore_subject_import(subjects_, empty, zip, zip_len));
    exported = gdefstor_snapshot_export(empty, (const char *)storage_,
        sizeof storage_, NULL, &again, &again_len);
    ASSERT_EQ(GDEFSTOR_OK, exported);
    EXPECT_FALSE(HasBytes(again, again_len, kCanary, strlen(kCanary)));
    EXPECT_LT(again_len, (size_t)65536);
    gdefstor_bytes_free(NULL, again);
    gdefstor_store_destroy(empty);
    free(zip);
  }
};

struct Job {
  GDEFCORE_Sched * sched;
  GDEFCORE_Catalog * catalog;
  GDEFCORE_Catalog * leak;
  GDEFCORE_Routes * routes;
  const char * path;
  GDEFCORE_Result resolve_rc;
  GDEFCORE_Result render_rc;
  size_t page_len;
  char * front;
  size_t front_len;
  char ** ids;
  size_t list_count;
  char mark;
  char * trace;
  size_t * trace_n;
};

extern "C" GDEFCORE_Turn JobTurn(GDEFCORE_Request * request, void * user) {
  Job * job = (Job *)user;
  char * resolved = NULL;
  size_t resolved_len = 0;
  char * page = NULL;
  size_t page_len = 0;
  char ** ids = NULL;
  size_t count = 0;
  GDEFCORE_Access_List query;
  GDEFCORE_Catalog * used;
  if (*job->trace_n + 2 < 16) {
    job->trace[(*job->trace_n)++] = job->mark;
    job->trace[*job->trace_n] = '\0';
  }
  job->resolve_rc = gdefcore_form_resolve(job->routes, job->catalog, request,
      job->path, strlen(job->path), &resolved, &resolved_len);
  gdefcore_text_free(job->catalog, resolved);
  if (gdefcore_sched_yield(job->sched) != GDEFCORE_OK) {
    job->render_rc = GDEFCORE_ERR_INTERNAL;
    return GDEFCORE_TURN_DONE;
  }
  if (*job->trace_n + 2 < 16) {
    job->trace[(*job->trace_n)++] = (char)(job->mark - 'A' + 'a');
    job->trace[*job->trace_n] = '\0';
  }
  used = job->leak != NULL ? job->leak : job->catalog;
  job->render_rc = gdefcore_render_page(used, job->routes, request, job->path,
      strlen(job->path), GDEFCORE_RENDER_PAGE_FUEL, &page, &page_len);
  job->page_len = (job->render_rc == GDEFCORE_OK && page != NULL) ? page_len : 0;
  gdefcore_text_free(used, page);
  page = NULL;
  page_len = 0;
  if (gdefcore_render_page(used, job->routes, request, "/", 1,
          GDEFCORE_RENDER_PAGE_FUEL, &page, &page_len) == GDEFCORE_OK &&
      page != NULL && page_len > 0) {
    job->front = (char *)malloc(page_len + 1);
    if (job->front != NULL) {
      memcpy(job->front, page, page_len);
      job->front[page_len] = '\0';
      job->front_len = page_len;
    }
  }
  gdefcore_text_free(used, page);
  memset(&query, 0, sizeof query);
  query.size = sizeof query;
  query.type_name = "site.post";
  query.type_name_len = 9;
  query.lang = "en";
  query.lang_len = 2;
  if (gdefcore_access_list(used, request, &query, &ids, &count) == GDEFCORE_OK) {
    size_t i;
    job->list_count = count;
    if (count > 0) {
      job->ids = (char **)calloc(count, sizeof(char *));
      if (job->ids != NULL) {
        for (i = 0; i < count; i++) {
          if (ids[i] != NULL) {
            job->ids[i] = strdup(ids[i]);
          }
        }
      }
    }
  }
  gdefcore_access_free(used, ids, count);
  return GDEFCORE_TURN_DONE;
}

}  // namespace

TEST_F(Site, RoundTrip) {
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  char subject[GDEFCORE_PUBLIC_ID_LEN + 1];
  char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint8_t raw[GDEFCORE_SUBJECT_KEY_LEN];
  uint8_t wrap[GDEFCORE_KEYRING_WRAP_LEN];
  uint8_t * zip = NULL;
  size_t zip_len = 0;
  size_t use = 0;
  char dir[] = "/tmp/gdeftest-sqlite-XXXXXX";
  GDEFSTOR_Store * sql = NULL;
  Open();
  Install();
  Login();
  Publish("hello", post);
  DefinePerson();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_subject_create(subjects_, subject));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_entity_create(catalog_, "site.person", 11, entity, NULL));
  SavePersonal(subject, entity);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_subject_key(subjects_, subject, strlen(subject), raw));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_keyring_export(
          gdefcore_inst_tenant_keyring(inst_, tenant_, strlen(tenant_)), wrap,
          sizeof wrap));
  Snapshot(&zip, &zip_len);
  EXPECT_TRUE(HasBytes(zip, zip_len, kTitle, strlen(kTitle)));
  EXPECT_TRUE(HasBytes(zip, zip_len, "s1:", 3));
  EXPECT_FALSE(HasBytes(zip, zip_len, kPlain, strlen(kPlain)));
  EXPECT_FALSE(HasBytes(zip, zip_len, raw, sizeof raw));
  EXPECT_FALSE(HasBytes(zip, zip_len, wrap, sizeof wrap));
  ASSERT_NE(mkdtemp(dir), nullptr);
  ASSERT_EQ(GDEFSSQLITE_OK, gdefssqlite_store_create(dir, NULL, NULL, &sql));
  use = zip_len;
  if (strcmp(Plant(), "lose") == 0 && zip_len > 0) {
    use = zip_len - 1;
  }
  ASSERT_EQ(GDEFCORE_OK, gdefcore_subject_import(subjects_, sql, zip, use));
  EXPECT_TRUE(StoresEqual(site_, sql, storage_, sizeof storage_));
  gdefstor_store_destroy(sql);
  gdefstor_bytes_free(NULL, zip);
  std::filesystem::remove_all(dir);
  memset(raw, 0, sizeof raw);
  memset(wrap, 0, sizeof wrap);
}

TEST_F(Site, Readable) {
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  char subject[GDEFCORE_PUBLIC_ID_LEN + 1];
  char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint8_t raw[GDEFCORE_SUBJECT_KEY_LEN];
  uint8_t * zip = NULL;
  size_t zip_len = 0;
  const char * bin = getenv("GDEFTEST_READZIP");
  char path[] = "/tmp/gdeftest-zip-XXXXXX";
  int fd;
  int pipes[2];
  pid_t pid;
  std::string dumped;
  std::string needed;
  FILE * elf;
  Open();
  Install();
  Login();
  Publish("hello", post);
  DefinePerson();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_subject_create(subjects_, subject));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_entity_create(catalog_, "site.person", 11, entity, NULL));
  SavePersonal(subject, entity);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_subject_key(subjects_, subject, strlen(subject), raw));
  Snapshot(&zip, &zip_len);
  ASSERT_NE(bin, nullptr);
  fd = mkstemp(path);
  ASSERT_GE(fd, 0);
  ASSERT_EQ((ssize_t)zip_len, write(fd, zip, zip_len));
  close(fd);
  ASSERT_EQ(0, pipe(pipes));
  pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    dup2(pipes[1], STDOUT_FILENO);
    close(pipes[0]);
    close(pipes[1]);
    execl(bin, bin, path, (char *)NULL);
    _exit(127);
  }
  close(pipes[1]);
  for (;;) {
    char buf[4096];
    ssize_t n = read(pipes[0], buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (n == 0) {
      break;
    }
    dumped.append(buf, (size_t)n);
  }
  close(pipes[0]);
  ASSERT_EQ(pid, waitpid(pid, NULL, 0));
  unlink(path);
  EXPECT_NE(std::string::npos, dumped.find(kTitle));
  EXPECT_NE(std::string::npos, dumped.find("s1:"));
  EXPECT_EQ(std::string::npos, dumped.find(kPlain));
  EXPECT_EQ(std::string::npos,
      dumped.find(std::string((const char *)raw, sizeof raw)));
  elf = popen((std::string("readelf -d ") + bin + " 2>/dev/null").c_str(), "r");
  ASSERT_NE(elf, nullptr);
  for (;;) {
    char buf[512];
    if (fgets(buf, sizeof buf, elf) == NULL) {
      break;
    }
    needed.append(buf);
  }
  {
    int elf_status = pclose(elf);
    ASSERT_TRUE(WIFEXITED(elf_status));
    ASSERT_EQ(0, WEXITSTATUS(elf_status));
  }
  EXPECT_EQ(std::string::npos, needed.find("defiant-"));
  gdefstor_bytes_free(NULL, zip);
  memset(raw, 0, sizeof raw);
}

TEST_F(Site, Restore) {
  char subject[GDEFCORE_PUBLIC_ID_LEN + 1];
  char entity[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint8_t * zip = NULL;
  size_t zip_len = 0;
  GDEFSTOR_Store * empty = NULL;
  GDEFCORE_Catalog * restored = NULL;
  int erased = 0;
  Open();
  DefinePerson();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_subject_create(subjects_, subject));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_entity_create(catalog_, "site.person", 11, entity, NULL));
  SavePersonal(subject, entity);
  Snapshot(&zip, &zip_len);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_subject_erase(subjects_, subject, strlen(subject), subject,
          strlen(subject)));
  ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &empty));
  if (strcmp(Plant(), "ledger") == 0) {
    ASSERT_EQ(GDEFSTOR_OK,
        gdefstor_snapshot_import(empty, (const char *)storage_, sizeof storage_,
            zip, zip_len, NULL));
    PlantLedger(zip, zip_len);
  } else {
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_subject_import(subjects_, empty, zip, zip_len));
  }
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_catalog_open(empty, storage_, sizeof storage_, NULL, NULL,
          &restored));
  erased = ReadIs(restored, subject, entity, GDEFCORE_SUBJECT_ERASED) ? 1 : 0;
  gdefcore_catalog_destroy(restored);
  gdefstor_store_destroy(empty);
  gdefstor_bytes_free(NULL, zip);
  EXPECT_EQ(1, erased);
}

TEST_F(Site, OwnGrants) {
  char role_unused[GDEFCORE_PUBLIC_ID_LEN + 1];
  GDEFCORE_Permission permission;
  char ** labels = NULL;
  size_t count = 0;
  uint8_t * zip = NULL;
  size_t zip_len = 0;
  GDEFSTOR_Store * empty = NULL;
  GDEFCORE_Catalog * restored = NULL;
  int found = 0;
  size_t i;
  Open();
  memset(&permission, 0, sizeof permission);
  permission.size = sizeof permission;
  permission.label = "Read posts";
  permission.label_len = 10;
  permission.module_name = "site";
  permission.module_name_len = 4;
  permission.action = "read";
  permission.action_len = 4;
  permission.condition = GDEFCORE_ACCESS_ANY;
  permission.type_name = "site.post";
  permission.type_name_len = 9;
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_declare(catalog_, &permission));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_role_create(catalog_, "editors", 7, role_unused));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant(catalog_, "editors", 7, "Read posts", 10, "core.role",
          9));
  Snapshot(&zip, &zip_len);
  ASSERT_EQ(GDEFSMEMORY_OK, gdefsmemory_store_create(NULL, NULL, &empty));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_subject_import(subjects_, empty, zip, zip_len));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_catalog_open(empty, storage_, sizeof storage_, NULL, NULL,
          &restored));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_role_grants(restored, "editors", 7, &labels, &count));
  for (i = 0; i < count; i++) {
    if (strcmp(labels[i], "Read posts") == 0) {
      found = 1;
    }
  }
  gdefcore_access_free(restored, labels, count);
  gdefcore_catalog_destroy(restored);
  gdefstor_store_destroy(empty);
  gdefstor_bytes_free(NULL, zip);
  EXPECT_EQ(1, found);
}

TEST_F(Site, Traversal) {
  Open();
  Refuse("../x", kCanary, strlen(kCanary), 0, GDEFCORE_ERR_CORRUPT);
}

TEST_F(Site, Bomb) {
  uint8_t * zeros = NULL;
  Open();
  zeros = (uint8_t *)calloc(65536, 1);
  ASSERT_NE(zeros, nullptr);
  Refuse("b:aa", zeros, 65536, 1, GDEFCORE_ERR_LIMIT);
  free(zeros);
}

TEST_F(Site, NamedTenant) {
  const char body[] =
      "{\"tenant\":\"bee\",\"body\":\"CANARY-HOSTILE\"}";
  Open();
  Refuse("r:1.1", body, strlen(body), 0, GDEFCORE_ERR_INVALID);
}

TEST_F(Site, InternalId) {
  const char body[] =
      "{\"format\":\"defiant-revision/1\",\"seq\":1,\"row\":1,"
      "\"entity\":\"7\",\"type\":\"core.post\",\"lang\":{},"
      "\"body\":\"CANARY-HOSTILE\"}";
  Open();
  Refuse("r:1.1", body, strlen(body), 0, GDEFCORE_ERR_INVALID);
}

TEST_F(Site, Installation) {
  char id[GDEFCORE_PUBLIC_ID_LEN + 1];
  char body[512];
  int n;
  Open();
  ASSERT_EQ(GDEFCORE_OK, gdefcore_id_generate(id));
  n = snprintf(body, sizeof body,
      "{\"format\":\"defiant-revision/1\",\"seq\":1,\"row\":1,"
      "\"entity\":\"%s\",\"type\":\"core.tenant\",\"lang\":{},"
      "\"body\":\"CANARY-HOSTILE\"}",
      id);
  ASSERT_GT(n, 0);
  ASSERT_LT((size_t)n, sizeof body);
  Refuse("r:1.1", body, (size_t)n, 0, GDEFCORE_ERR_INVALID);
}

TEST_F(Site, Mistype) {
  const char body[] =
      "{\"format\":\"defiant-revision/1\",\"seq\":1,\"row\":1,"
      "\"entity\":\"not-a-public-id\",\"type\":\"core.post\",\"lang\":{},"
      "\"body\":\"CANARY-HOSTILE\"}";
  Open();
  Refuse("r:1.1", body, strlen(body), 0, GDEFCORE_ERR_INVALID);
}

TEST_F(Site, Grant) {
  const char body[] =
      "{\"role\":\"anonymous\",\"label\":\"Read posts\","
      "\"body\":\"CANARY-HOSTILE\"}";
  const char anon[] =
      "{\"role\":\"anonymous\",\"label\":\"Read posts\","
      "\"confirmation\":\"not-the-confirmation\","
      "\"body\":\"CANARY-HOSTILE\"}";
  const char auth[] =
      "{\"role\":\"authenticated\",\"label\":\"Read posts\","
      "\"confirmation\":\"not-the-confirmation\","
      "\"body\":\"CANARY-HOSTILE\"}";
  const char other[] =
      "{\"role\":\"editors\",\"label\":\"Read posts\","
      "\"confirmation\":\"not-the-confirmation\","
      "\"body\":\"CANARY-HOSTILE\"}";
  Open();
  Refuse("r:1.1", body, strlen(body), 0, GDEFCORE_ERR_INVALID);
  Refuse("r:1.1", anon, strlen(anon), 0, GDEFCORE_ERR_INVALID);
  Refuse("r:1.1", auth, strlen(auth), 0, GDEFCORE_ERR_INVALID);
  Refuse("r:1.1", other, strlen(other), 0, GDEFCORE_ERR_INVALID);
}

TEST_F(Site, CrossTenant) {
  char id_b[GDEFCORE_PUBLIC_ID_LEN + 1];
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  uint8_t key_b[16];
  GDEFCORE_Catalog * catalog_b = NULL;
  GDEFCORE_Request * request_a = NULL;
  GDEFCORE_Request * request_b = NULL;
  GDEFCORE_Routes * routes = NULL;
  GDEFCORE_Sched * sched = NULL;
  GDEFCORE_Result unknown = GDEFCORE_OK;
  GDEFCORE_Result unknown_render = GDEFCORE_OK;
  char * unknown_out = NULL;
  size_t unknown_len = 0;
  char * unknown_page = NULL;
  size_t unknown_page_len = 0;
  size_t i;
  int listed = 0;
  char trace[16];
  size_t trace_n = 0;
  Job job_a;
  Job job_b;
  Open();
  Install();
  Login();
  Publish("hello", post);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_tenant_create(inst_, "bee", 3, "", 0, id_b));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_inst_storage_key(inst_, id_b, strlen(id_b), key_b));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_inst_catalog_open(inst_, id_b, strlen(id_b), &catalog_b));
  {
    uint8_t * kit = NULL;
    size_t kit_len = 0;
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_kit(catalog_b, &kit, &kit_len));
    ASSERT_EQ(GDEFCORE_OK, gdefcore_render_install(catalog_b, kit, kit_len));
    gdefcore_text_free(catalog_b, (char *)kit);
  }
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_request_create(storage_, sizeof storage_, 0, 0, NULL, &request_a));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_request_create(key_b, sizeof key_b, 0, 0, NULL, &request_b));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_routes_create(NULL, &routes));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_sched_create(NULL, 0, 0, &sched));
  memset(trace, 0, sizeof trace);
  memset(&job_a, 0, sizeof job_a);
  memset(&job_b, 0, sizeof job_b);
  job_a.sched = sched;
  job_a.catalog = catalog_;
  job_a.routes = routes;
  job_a.path = "hello";
  job_a.mark = 'A';
  job_a.trace = trace;
  job_a.trace_n = &trace_n;
  job_b.sched = sched;
  job_b.catalog = catalog_b;
  job_b.routes = routes;
  job_b.path = "hello";
  job_b.mark = 'B';
  job_b.trace = trace;
  job_b.trace_n = &trace_n;
  if (strcmp(Plant(), "drop-tenant") == 0) {
    job_b.leak = catalog_;
  }
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_sched_spawn(sched, request_a, JobTurn, &job_a));
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_sched_spawn(sched, request_b, JobTurn, &job_b));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_sched_run(sched));
  unknown = gdefcore_form_resolve(routes, catalog_b, request_b, "not-a-path",
      11, &unknown_out, &unknown_len);
  gdefcore_text_free(catalog_b, unknown_out);
  unknown_render = gdefcore_render_page(catalog_b, routes, request_b, "not-a-path",
      11, GDEFCORE_RENDER_PAGE_FUEL, &unknown_page, &unknown_page_len);
  gdefcore_text_free(catalog_b, unknown_page);
  EXPECT_STREQ("ABab", trace);
  EXPECT_EQ(GDEFCORE_OK, job_a.render_rc);
  EXPECT_GT(job_a.page_len, 0u);
  EXPECT_NE(job_a.front, nullptr);
  EXPECT_NE(std::string::npos,
      std::string(job_a.front == NULL ? "" : job_a.front, job_a.front_len)
          .find(kTitle));
  EXPECT_GT(job_a.list_count, 0u);
  EXPECT_EQ(unknown, job_b.resolve_rc);
  EXPECT_EQ(unknown_render, job_b.render_rc);
  EXPECT_EQ(0u, unknown_page_len);
  EXPECT_EQ(0u, job_b.page_len);
  EXPECT_EQ(std::string::npos,
      std::string(job_b.front == NULL ? "" : job_b.front, job_b.front_len)
          .find(kTitle));
  EXPECT_EQ(std::string::npos,
      std::string(job_b.front == NULL ? "" : job_b.front, job_b.front_len)
          .find(post));
  for (i = 0; i < job_b.list_count; i++) {
    if (job_b.ids != NULL && job_b.ids[i] != NULL &&
        strcmp(job_b.ids[i], post) == 0) {
      listed = 1;
    }
  }
  EXPECT_EQ(0, listed);
  free(job_a.front);
  free(job_b.front);
  if (job_a.ids != NULL) {
    for (i = 0; i < job_a.list_count; i++) {
      free(job_a.ids[i]);
    }
    free(job_a.ids);
  }
  if (job_b.ids != NULL) {
    for (i = 0; i < job_b.list_count; i++) {
      free(job_b.ids[i]);
    }
    free(job_b.ids);
  }
  gdefcore_sched_destroy(sched);
  gdefcore_routes_destroy(routes);
  gdefcore_request_destroy(request_a);
  gdefcore_request_destroy(request_b);
  gdefcore_catalog_destroy(catalog_b);
}

TEST_F(Site, Bypass) {
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  char who[GDEFCORE_PUBLIC_ID_LEN + 1];
  char missing[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * page = NULL;
  size_t page_len = 0;
  char * other = NULL;
  size_t other_len = 0;
  char ** texts = NULL;
  size_t * lens = NULL;
  size_t count = 0;
  GDEFCORE_Result rendered;
  GDEFCORE_Result unknown;
  GDEFCORE_Routes * routes = NULL;
  Open();
  Install();
  Login();
  Publish("hello", post);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_access_grant(catalog_, "admin", 5, "Read posts", 10, "core.role",
          9));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_access_principal_create(catalog_, who));
  if (strcmp(Plant(), "skip") != 0) {
    ASSERT_EQ(GDEFCORE_OK,
        gdefcore_request_set_principal(request_, who, strlen(who)));
  }
  ASSERT_EQ(GDEFCORE_OK, gdefcore_id_generate(missing));
  ASSERT_EQ(GDEFCORE_OK, gdefcore_routes_create(NULL, &routes));
  rendered = gdefcore_render_page(catalog_, routes, request_, "hello", 5,
      GDEFCORE_RENDER_PAGE_FUEL, &page, &page_len);
  unknown = gdefcore_render_page(catalog_, routes, request_, "not-a-path", 11,
      GDEFCORE_RENDER_PAGE_FUEL, &other, &other_len);
  EXPECT_EQ(unknown, rendered);
  EXPECT_EQ(GDEFCORE_ERR_INVALID, rendered);
  EXPECT_EQ(nullptr, page);
  EXPECT_EQ(0u, page_len);
  gdefcore_text_free(catalog_, page);
  gdefcore_text_free(catalog_, other);
  EXPECT_EQ(gdefcore_access_read(catalog_, request_, post, strlen(post), "en", 2,
                "title", 5, &texts, &lens, &count),
      gdefcore_access_read(catalog_, request_, missing, strlen(missing), "en", 2,
          "title", 5, &texts, &lens, &count));
  EXPECT_EQ(GDEFCORE_ERR_INVALID,
      gdefcore_access_read(catalog_, request_, post, strlen(post), "en", 2,
          "title", 5, &texts, &lens, &count));
  gdefcore_routes_destroy(routes);
}

TEST_F(Site, Scanner) {
  char post[GDEFCORE_PUBLIC_ID_LEN + 1];
  char * page = NULL;
  size_t page_len = 0;
  const char * err;
  std::string bytes;
  GDEFCORE_Routes * routes = NULL;
  Open();
  Install();
  Login();
  Publish("hello", post);
  ASSERT_EQ(GDEFCORE_OK, gdefcore_routes_create(NULL, &routes));
  gdefcore_request_set_principal(request_, NULL, 0);
  ASSERT_EQ(GDEFCORE_OK,
      gdefcore_render_page(catalog_, routes, request_, "hello", 5,
          GDEFCORE_RENDER_PAGE_FUEL, &page, &page_len));
  ASSERT_NE(page, nullptr);
  bytes.assign(page, page_len);
  gdefcore_text_free(catalog_, page);
  gdefcore_routes_destroy(routes);
  err = gdefcore_result_string(GDEFCORE_ERR_INVALID);
  ASSERT_NE(err, nullptr);
  if (strcmp(Plant(), "plant-id") == 0) {
    bytes += " 42";
  }
  EXPECT_EQ(0,
      gdeftest_response_has_internal_id(bytes.data(), bytes.size(), err,
          strlen(err)));
  EXPECT_EQ(0,
      gdeftest_response_has_internal_id("&#42;", 5, err, strlen(err)));
  EXPECT_EQ(1,
      gdeftest_response_has_internal_id("id 42.", 6, NULL, 0));
}

TEST(Harness, Lint) {
  const char * clean =
      "unescaped-output unsafe\n"
      "unfiltered-read unsafe\n"
      "string-query unsafe\n"
      "unsigned-form unsafe\n"
      "direct-storage unsafe\n";
  std::string text = clean;
  int line = 0;
  int item = 0;
  if (strcmp(Plant(), "unmark") == 0) {
    size_t at;
    while ((at = text.find(" unsafe")) != std::string::npos) {
      text.erase(at, 7);
    }
  }
  EXPECT_EQ(GDEFTEST_OK, gdeftest_lint(text.data(), text.size(), &line, &item));
  EXPECT_EQ(0, line);
  EXPECT_EQ(0, item);
}

TEST(Harness, LintNames) {
  const char * tokens[] = {
      "unescaped-output",
      "unfiltered-read",
      "string-query",
      "unsigned-form",
      "direct-storage",
  };
  int i;
  for (i = 0; i < 5; i++) {
    int line = 0;
    int item = 0;
    std::string text = std::string("keep\n") + tokens[i] + "\n";
    EXPECT_EQ(GDEFTEST_ERR_INVALID,
        gdeftest_lint(text.data(), text.size(), &line, &item));
    EXPECT_EQ(2, line);
    EXPECT_EQ(i + 1, item);
  }
  {
    const char * other = "items 6 and 7 stay out\n";
    int line = 0;
    int item = 0;
    EXPECT_EQ(GDEFTEST_OK, gdeftest_lint(other, strlen(other), &line, &item));
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
