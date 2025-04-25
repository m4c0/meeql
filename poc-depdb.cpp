#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import print;
import tora;

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

[[nodiscard]] static auto init_db() {
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

    CREATE TABLE dep_mgmt (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;

    CREATE TABLE dep (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_art   INTEGER NOT NULL REFERENCES art (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;
  )");
  return db;
}

class db {
  tora::db m_db = init_db();

  tora::stmt m_ins_grp_stmt = m_db.prepare(R"(
    INSERT INTO grp (name) VALUES (?) 
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");
  tora::stmt m_ins_art_stmt = m_db.prepare(R"(
    INSERT INTO art (grp, name) VALUES (?, ?) 
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");
  tora::stmt m_ins_ver_stmt = m_db.prepare(R"(
    INSERT INTO ver (art, name) VALUES (?, ?)
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");

public:
  [[nodiscard]] constexpr auto * handle() { return &m_db; }

  [[nodiscard]] unsigned ensure(jute::view grp, jute::view art, jute::view ver) {
    m_ins_grp_stmt.reset();
    m_ins_grp_stmt.bind(1, grp);
    m_ins_grp_stmt.step();

    m_ins_art_stmt.reset();
    m_ins_art_stmt.bind(1, m_ins_grp_stmt.column_int(0));
    m_ins_art_stmt.bind(2, art);
    m_ins_art_stmt.step();

    m_ins_ver_stmt.reset();
    m_ins_ver_stmt.bind(1, m_ins_art_stmt.column_int(0));
    m_ins_ver_stmt.bind(2, ver);
    m_ins_ver_stmt.step();

    return m_ins_ver_stmt.column_int(0);
  }
};

struct load_stuff {
  tora::db * db;
  tora::stmt grp_stmt;
  tora::stmt art_stmt;
  tora::stmt ver_stmt;
};
struct dep_stuff {
  tora::db * db;
  tora::stmt ver_stmt;
  tora::stmt dm_stmt;
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
  ls->ver_stmt.reset();
}

struct version_not_found {};
static unsigned find_ver(dep_stuff * ds, jute::view grp, jute::view art, jute::view ver) {
  ds->ver_stmt.reset();
  ds->ver_stmt.bind(1, grp);
  ds->ver_stmt.bind(2, art);
  ds->ver_stmt.bind(3, ver);
  if (!ds->ver_stmt.step()) throw version_not_found {};

  unsigned id = ds->ver_stmt.column_int(0);
  if (ds->ver_stmt.step()) die("duplicate version found");

  return id;
}
static void load_deps(dep_stuff * ds, jute::view pom_path) {
  static hai::varray<unsigned> buffer { 1024 };
  buffer.truncate(0);

  auto pom = cavan::read_pom(pom_path);
  cavan::merge_props(pom);

  try {
    auto from = find_ver(ds, pom->grp, pom->art, pom->ver);
    for (auto &[d, _]: pom->deps_mgmt) {
      auto grp = cavan::apply_props(pom, d.grp);
      auto ver = cavan::apply_props(pom, d.ver);
      auto to = find_ver(ds, *grp, d.art, *ver);
      buffer.push_back_doubling(to);
    }

    for (auto to : buffer) {
      ds->dm_stmt.bind(1, from);
      ds->dm_stmt.bind(2, to);
      ds->dm_stmt.step();
      ds->dm_stmt.reset();
    }
  } catch (version_not_found) {
  }
}

int main(int argc, char ** argv) try {
  db db {};

  load_stuff ls {
    .db = db.handle(),
    .grp_stmt = db.handle()->prepare(R"(
      INSERT INTO grp (name) VALUES (?) 
      ON CONFLICT DO
      UPDATE SET id = id
      RETURNING id
    )"),
    .art_stmt = db.handle()->prepare(R"(
      INSERT INTO art (grp, name) VALUES (?, ?) 
      ON CONFLICT DO
      UPDATE SET id = id
      RETURNING id
    )"),
    .ver_stmt = db.handle()->prepare(R"(
      INSERT OR IGNORE INTO ver (art, name, pom) VALUES (?, ?, ?) 
    )"),
  };
  meeql::recurse_repo_dir(curry(load_pom, &ls));
  dep_stuff ds {
    .db = db.handle(),
    .ver_stmt = db.handle()->prepare(R"(
      SELECT ver.id
      FROM ver
      JOIN art ON art.id = ver.art
      JOIN grp ON grp.id = art.grp
      WHERE grp.name = ?
        AND art.name = ?
        AND ver.name = ?
    )"),
    .dm_stmt = db.handle()->prepare(R"(
      INSERT INTO dep_mgmt (from_ver, to_ver) VALUES (?, ?)
    )"),
  };
  meeql::recurse_repo_dir(curry(load_deps, &ds));

  auto stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep_mgmt");
  stmt.step();
  putln("found ", stmt.column_int(0));

} catch (...) {
  return 13;
}
