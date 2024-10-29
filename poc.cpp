#pragma leco tool
#include <stdlib.h>

import cavan;
import jute;
import mtime;
import pprent;
import silog;
import tora;

static const auto repo_dir = (jute::view::unsafe(getenv("HOME")) + "/.m2/repository").cstr();

void setup_schema(tora::db & db) {
  db.exec(R"(
    CREATE TABLE pom (
      id       INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      filename TEXT    NOT NULL,
      fmod     INTEGER NOT NULL,

      group_id    TEXT NOT NULL,
      artefact_id TEXT NOT NULL,
      version     TEXT NOT NULL,

      p_group_id    TEXT,
      p_artefact_id TEXT,
      p_version     TEXT,
      parent        INTEGER REFERENCES pom(id)
    ) STRICT;
  )");
}

void persist_pom(tora::stmt & stmt, cavan::pom * pom, auto ftime) {
  stmt.reset();
  stmt.bind(1, pom->filename);
  stmt.bind64(2, ftime);
  stmt.bind(3, pom->grp);
  stmt.bind(4, pom->art);
  stmt.bind(5, pom->ver);
  stmt.bind(6, pom->parent.grp);
  stmt.bind(7, pom->parent.art);
  stmt.bind(8, pom->parent.ver);
  stmt.step();
}

void process_dep(tora::stmt & stmt, jute::view path) {
  auto [l_ver, ver] = path.rsplit('/');
  auto [l_art, art] = l_ver.rsplit('/');

  auto fname = (repo_dir + path + "/" + art + "-" + ver + ".pom").cstr();
  auto ftime = mtime::of(fname.begin());
  // Some dependencies might not have a pom for reasons
  if (ftime == 0) return;

  auto pom = cavan::read_pom(fname);
  persist_pom(stmt, pom, ftime);
}

void recurse(tora::stmt & stmt, jute::view path) {
  auto full_path = (repo_dir + path).cstr();
  auto marker = repo_dir + path + "/_remote.repositories";
  if (mtime::of(marker.cstr().begin())) {
    process_dep(stmt, path);
    return;
  }
  for (auto f : pprent::list(full_path.begin())) {
    if (f[0] == '.') continue;
    auto dir = (path + "/" + jute::view::unsafe(f)).cstr();
    recurse(stmt, dir);
  }
}
void import_local_repo(tora::db & db) {
  auto stmt = db.prepare(R"(
    INSERT INTO pom (
      filename, fmod,
      group_id, artefact_id, version,
      p_group_id, p_artefact_id, p_version
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )");
  recurse(stmt, "");

  // Update parent keys with their IDs
  db.exec(R"(
    UPDATE OR IGNORE pom AS p
    SET parent = par.id
      FROM pom AS par
      WHERE par.group_id    = p.p_group_id
        AND par.artefact_id = p.p_artefact_id
        AND par.version     = p.p_version
  )");
}

int main(int argc, char ** argv) try {
  tora::db db { ":memory:" };
  setup_schema(db);
  import_local_repo(db);
} catch (...) {
  return 1;
}
