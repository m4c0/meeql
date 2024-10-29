#pragma leco tool
#include <stdlib.h>

import cavan;
import jute;
import mtime;
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

void persist_pom(tora::stmt & stmt, cavan::pom * pom, auto ftime) {
  stmt.reset();
  stmt.bind(1, pom->filename);
  stmt.bind(2, ftime);
  stmt.bind(3, pom->grp);
  stmt.bind(4, pom->art);
  stmt.bind(5, pom->ver);
  stmt.step();
}

void process_dep(tora::stmt & stmt, jute::view repo_dir, jute::view path) {
  auto [l_ver, ver] = path.rsplit('/');
  auto [l_art, art] = l_ver.rsplit('/');

  auto fname = (repo_dir + path + "/" + art + "-" + ver + ".pom").cstr();
  auto ftime = mtime::of(fname.begin());
  // Some dependencies might not have a pom for reasons
  if (ftime == 0) return;

  auto pom = cavan::read_pom(fname);
  persist_pom(stmt, pom, ftime);
}

void recurse(tora::stmt & stmt, jute::view repo_dir, jute::view path) {
  auto full_path = (repo_dir + path).cstr();
  auto marker = repo_dir + path + "/_remote.repositories";
  if (mtime::of(marker.cstr().begin())) {
    process_dep(stmt, repo_dir, path);
    return;
  }
  for (auto f : pprent::list(full_path.begin())) {
    if (f[0] == '.') continue;
    auto dir = (path + "/" + jute::view::unsafe(f)).cstr();
    recurse(stmt, repo_dir, dir);
  }
}

int main(int argc, char ** argv) try {
  const auto repo_dir = (jute::view::unsafe(getenv("HOME")) + "/.m2/repository").cstr();
  tora::db db { ":memory:" };
  init(db);

  auto stmt = db.prepare(R"(
    INSERT INTO pom (
      filename, fmod, group_id, artefact_id, version, parent
    ) VALUES (?, ?, ?, ?, ?, ?)
  )");
  recurse(stmt, repo_dir, "");
} catch (...) {
  return 1;
}
