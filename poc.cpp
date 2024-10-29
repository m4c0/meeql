#pragma leco tool
#include <stdlib.h>

import cavan;
import jute;
import pprent;
import silog;
import tora;

void init(tora::db & db) {
  db.exec(R"(
    CREATE TABLE pom (
      id       INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      filename TEXT NOT NULL,
      fmod     DATETIME NOT NULL,

      group_id    TEXT NOT NULL,
      artefact_id TEXT NOT NULL,
      version     TEXT NOT NULL,

      parent  INTEGER REFERENCES pom(id)
    );
  )");
}

void process_dep(tora::db & db, jute::view repo_dir, jute::view path) {
  silog::trace(path);
}

void recurse(tora::db & db, jute::view repo_dir, jute::view path) {
  auto full_path = (repo_dir + path).cstr();
  for (auto f : pprent::list(full_path.begin())) {
    auto ff = jute::view::unsafe(f);
    if (f[0] == '.') continue;
    if (ff == "_remote.repositories") process_dep(db, repo_dir, path.subview(1).after);
    else {
      auto dir = (path + "/" + jute::view::unsafe(f)).cstr();
      recurse(db, repo_dir, dir);
    }
  }
}

int main(int argc, char ** argv) try {
  const auto repo_dir = (jute::view::unsafe(getenv("HOME")) + "/.m2/repository/").cstr();
  tora::db db { ":memory:" };
  init(db);
  recurse(db, repo_dir, "");
} catch (...) {
  return 1;
}
