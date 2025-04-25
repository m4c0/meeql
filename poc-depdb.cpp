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
      pom   TEXT,
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

struct version_not_found {};
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
    INSERT INTO ver (art, name, pom) VALUES (?, ?, ?)
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");

  tora::stmt m_ins_dm_stmt = m_db.prepare(R"(
    INSERT INTO dep_mgmt (from_ver, to_ver) VALUES (?, ?)
  )");

  tora::stmt m_find_ver_stmt = m_db.prepare(R"(
    SELECT ver.id
    FROM ver
    JOIN art ON art.id = ver.art
    JOIN grp ON grp.id = art.grp
    WHERE grp.name = ?
      AND art.name = ?
      AND ver.name = ?
  )");

public:
  [[nodiscard]] constexpr auto * handle() { return &m_db; }

  unsigned insert(jute::view grp, jute::view art, jute::view ver, jute::view pom) {
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
    if (pom != "") m_ins_ver_stmt.bind(3, pom);
    m_ins_ver_stmt.step();

    return m_ins_ver_stmt.column_int(0);
  }

  void add_dep_mgmt(unsigned from, unsigned to) {
    m_ins_dm_stmt.reset();
    m_ins_dm_stmt.bind(1, from);
    m_ins_dm_stmt.bind(2, to);
    m_ins_dm_stmt.step();
  }
};

static void load_deps(db * db, jute::view pom_path) {
  auto pom = cavan::read_pom(pom_path);
  cavan::merge_props(pom);

  auto from = db->insert(pom->grp, pom->art, pom->ver, pom_path);
  for (auto &[d, _]: pom->deps_mgmt) {
    auto grp = cavan::apply_props(pom, d.grp);
    auto ver = cavan::apply_props(pom, d.ver);
    db->add_dep_mgmt(from, db->insert(*grp, d.art, *ver, ""));
  }
}

int main(int argc, char ** argv) try {
  db db {};

  meeql::recurse_repo_dir(curry(load_deps, &db));

  auto stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep_mgmt");
  stmt.step();
  putln("found ", stmt.column_int(0));

} catch (...) {
  return 13;
}
