#pragma leco tool
#include <stdio.h>
#include <stdlib.h>

import cavan;
import jute;
import mtime;
import pprent;
import silog;
import sitime;
import tora;

static const auto home_dir = jute::view::unsafe(getenv("HOME"));
static const auto repo_dir = (home_dir + "/.m2/repository").cstr();

static tora::stmt g_pom_stmt;
static tora::stmt g_dep_stmt;
static tora::stmt g_prop_stmt;
static tora::stmt g_mod_stmt;
static tora::stmt g_excl_stmt;

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

    CREATE TABLE dep (
      id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      dep_mgmt   INTEGER NOT NULL,

      owner_pom  INTEGER NOT NULL REFERENCES pom(id),

      group_id       TEXT NOT NULL,
      artefact_id    TEXT NOT NULL,
      version        TEXT,
      type           TEXT DEFAULT 'jar',
      scope          TEXT DEFAULT 'compile',
      classification TEXT,
      optional       INTEGER NOT NULL
    ) STRICT;

    CREATE TABLE prop (
      id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      owner_pom INTEGER NOT NULL REFERENCES pom(id),

      key   TEXT NOT NULL,
      value TEXT NULL
    ) STRICT;

    CREATE TABLE mod (
      id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      owner_pom INTEGER NOT NULL REFERENCES pom(id),

      name TEXT NOT NULL
    ) STRICT;

    CREATE TABLE excl (
      id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      owner_dep INTEGER NOT NULL REFERENCES dep(id),

      group_id    TEXT NOT NULL,
      artefact_id TEXT NOT NULL
    ) STRICT;
  )");
}

void prepare_statements(tora::db & db) {
  g_pom_stmt = db.prepare(R"(
    INSERT INTO pom (
      filename, fmod,
      group_id, artefact_id, version,
      p_group_id, p_artefact_id, p_version
    )
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    RETURNING id
  )");
  g_dep_stmt = db.prepare(R"(
    INSERT INTO dep (
      owner_pom, dep_mgmt,
      group_id, artefact_id, version,
      type, scope, classification, optional
    )
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    RETURNING id
  )");
  g_prop_stmt = db.prepare(R"(
    INSERT INTO prop (owner_pom, key, value)
    VALUES (?, ?, ?)
  )");
  g_mod_stmt = db.prepare(R"(
    INSERT INTO mod (owner_pom, name)
    VALUES (?, ?)
  )");
  g_excl_stmt = db.prepare(R"(
    INSERT INTO excl (owner_dep, group_id, artefact_id)
    VALUES (?, ?, ?)
  )");
}

void persist_deps(const cavan::deps & list, bool dm) {
  for (auto &[d, _] : list) {
    g_dep_stmt.reset();
    g_dep_stmt.bind(1, g_pom_stmt.column_int(0));
    g_dep_stmt.bind(2, dm ? 1 : 0);
    g_dep_stmt.bind(3, *d.grp);
    g_dep_stmt.bind(4, d.art);
    g_dep_stmt.bind(5, *d.ver);
    g_dep_stmt.bind(6, d.typ);
    g_dep_stmt.bind(7, d.scp);
    g_dep_stmt.bind(8, d.cls);
    g_dep_stmt.bind(9, d.opt ? 1 : 0);
    g_dep_stmt.step();

    if (d.exc) for (auto & e: *d.exc) {
      g_excl_stmt.reset();
      g_excl_stmt.bind(1, g_dep_stmt.column_int(0));
      g_excl_stmt.bind(2, e.grp);
      g_excl_stmt.bind(3, e.art);
      g_excl_stmt.step();
    }
  }
}

void persist_pom(cavan::pom * pom, auto ftime) {
  g_pom_stmt.reset();
  g_pom_stmt.bind(1, pom->filename);
  g_pom_stmt.bind64(2, ftime);
  g_pom_stmt.bind(3, pom->grp);
  g_pom_stmt.bind(4, pom->art);
  g_pom_stmt.bind(5, pom->ver);
  g_pom_stmt.bind(6, pom->parent.grp);
  g_pom_stmt.bind(7, pom->parent.art);
  g_pom_stmt.bind(8, pom->parent.ver);
  g_pom_stmt.step();

  persist_deps(pom->deps, false);
  persist_deps(pom->deps_mgmt, true);
  
  for (auto &[k, v] : pom->props) {
    g_prop_stmt.reset();
    g_prop_stmt.bind(1, g_pom_stmt.column_int(0));
    g_prop_stmt.bind(2, k);
    g_prop_stmt.bind(3, v);
    g_prop_stmt.step();
  }
  for (auto m : pom->modules) {
    g_mod_stmt.reset();
    g_mod_stmt.bind(1, g_pom_stmt.column_int(0));
    g_mod_stmt.bind(2, m);
    g_mod_stmt.step();
  }
}

void process_dep(jute::view path) {
  auto [l_ver, ver] = path.rsplit('/');
  auto [l_art, art] = l_ver.rsplit('/');

  auto fname = (repo_dir + path + "/" + art + "-" + ver + ".pom").cstr();
  auto ftime = mtime::of(fname.begin());
  // Some dependencies might not have a pom for reasons
  if (ftime == 0) return;

  auto pom = cavan::read_pom(fname);
  persist_pom(pom, ftime);
}

void recurse(jute::view path) {
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
void import_local_repo(tora::db & db) {
  silog::log(silog::info, "parsing and importing local repo");
  recurse("");
}

void update_parent_keys(tora::db & db) {
  silog::log(silog::info, "updating parent keys");
  db.exec(R"(
    UPDATE OR IGNORE pom AS p
    SET parent = par.id
      FROM pom AS par
      WHERE par.group_id    = p.p_group_id
        AND par.artefact_id = p.p_artefact_id
        AND par.version     = p.p_version
  )");
}

void dump_stats(tora::db & db) {
  auto stmt = db.prepare("SELECT COUNT(*) FROM pom");
  stmt.step();
  silog::trace("poms", stmt.column_int(0));

  stmt = db.prepare("SELECT COUNT(*) FROM dep");
  stmt.step();
  silog::trace("deps", stmt.column_int(0));

  stmt = db.prepare("SELECT COUNT(*) FROM prop");
  stmt.step();
  silog::trace("props", stmt.column_int(0));

  stmt = db.prepare("SELECT COUNT(*) FROM mod");
  stmt.step();
  silog::trace("mods", stmt.column_int(0));

  stmt = db.prepare("SELECT COUNT(*) FROM excl");
  stmt.step();
  silog::trace("excls", stmt.column_int(0));
}

int main(int argc, char ** argv) try {
  sitime::stopwatch t {};

  auto file = (home_dir + "/.m2/meeql").cstr();
  remove(file.begin());

  tora::db db { file.begin() };
  setup_schema(db);
  prepare_statements(db);
  import_local_repo(db);
  update_parent_keys(db);
  dump_stats(db);

  silog::log(silog::info, "All of that done in %dms", t.millis());
} catch (...) {
  return 1;
}
