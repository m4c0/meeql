#pragma leco tool

import jute;
import mtime;
import pprent;
import silog;
import sysstd;
import tora;

static const auto home_dir = jute::view::unsafe(sysstd::env("HOME"));
static const auto repo_dir = (home_dir + "/.m2/repository").cstr();

static void setup_schema(tora::db & db) {
  db.exec(R"(
    CREATE TABLE jar (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
    ) STRICT;

    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      jar   INTEGER NOT NULL REFERENCES jar(id)
    ) STRICT;
  )");
}

static void process_dep(jute::view path) {
}

static void recurse(jute::view path) {
  auto full_path = (repo_dir + path).cstr();
  auto marker = repo_dir + path + "/_remote.repositories";
  if (mtime::of(marker.cstr().begin())) {
    process_dep(path);
    return;
  }
  for (auto f : pprent::list(full_path.begin())) {
    if (f[0] == '.') continue;
    auto dir = (path + "/" + jute::view::unsafe(f)).cstr();
    recurse(dir);
  }
}
static void import_local_repo(tora::db & db) {
  silog::log(silog::info, "parsing and importing local repo");
  recurse("");
}

int main() {
  tora::db db { ":memory:" };

  setup_schema(db);
  import_local_repo(db);
}
