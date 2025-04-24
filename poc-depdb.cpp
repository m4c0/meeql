#pragma leco tool
import jute;
import meeql;
import print;
import tora;

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

struct load_stuff {
  tora::db * db;
  tora::stmt grp_stmt;
  tora::stmt art_stmt;
  tora::stmt ver_stmt;
};

static void load_pom(load_stuff * ls, jute::view pom_path) {
  auto rel_path = pom_path.subview(meeql::repo_dir().size() + 1).after;
  auto [r0, fn] = rel_path.rsplit('/');
  auto [r1, ver] = r0.rsplit('/');
  auto [r2, art] = r1.rsplit('/');

  auto grp = r2.cstr();
  for (auto & c : grp) if (c == '/') c = '.';

  ls->grp_stmt.bind(1, grp);
  ls->grp_stmt.step();
  auto gid = ls->grp_stmt.column_int(0);
  ls->grp_stmt.reset();

  ls->art_stmt.bind(1, gid);
  ls->art_stmt.bind(2, art);
  ls->art_stmt.step();
  auto aid = ls->art_stmt.column_int(0);
  ls->art_stmt.reset();

  ls->ver_stmt.bind(1, aid);
  ls->ver_stmt.bind(2, ver);
  ls->ver_stmt.bind(3, pom_path);
  ls->ver_stmt.step();
  auto vid = ls->ver_stmt.column_int(0);
  ls->ver_stmt.reset();

  putln(gid, " ", aid, " ", vid, " ", grp, " ", art, " ", ver);
}

int main(int argc, char ** argv) try {
  tora::db db { ":memory:" };

  db.exec(R"(
    CREATE TABLE grp (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL UNIQUE
    ) STRICT;

    CREATE TABLE art (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      grp   INTEGER NOT NULL REFERENCES grp (id),
      UNIQUE (grp, name)
    ) STRICT;

    CREATE TABLE ver (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      art   INTEGER NOT NULL REFERENCES art (id),
      pom   TEXT NOT NULL,
      UNIQUE (art, name)
    ) STRICT;
  )");

  load_stuff ls {
    .db = &db,
    .grp_stmt = db.prepare(R"(
      INSERT INTO grp (name) VALUES (?) 
      ON CONFLICT DO
      UPDATE SET id = id
      RETURNING id
    )"),
    .art_stmt = db.prepare(R"(
      INSERT INTO art (grp, name) VALUES (?, ?) 
      ON CONFLICT DO
      UPDATE SET id = id
      RETURNING id
    )"),
    .ver_stmt = db.prepare(R"(
      INSERT INTO ver (art, name, pom) VALUES (?, ?, ?) 
      ON CONFLICT DO
      UPDATE SET id = id
      RETURNING id
    )"),
  };
  meeql::recurse_repo_dir(curry(load_pom, &ls));
} catch (...) {
  return 1;
}
