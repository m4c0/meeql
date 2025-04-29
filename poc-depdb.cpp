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

// TODO: consider splitting dep_a and dep_v into different tables
[[nodiscard]] static auto init_db() {
  tora::db db { ":memory:" };
  db.exec(R"(
    DROP TABLE IF EXISTS grp;
    CREATE TABLE grp (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL UNIQUE
    ) STRICT;

    DROP TABLE IF EXISTS art;
    CREATE TABLE art (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      grp   INTEGER NOT NULL REFERENCES grp (id),
      UNIQUE (grp, name)
    ) STRICT;

    DROP TABLE IF EXISTS ver;
    CREATE TABLE ver (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      art   INTEGER NOT NULL REFERENCES art (id),
      pom   TEXT,
      UNIQUE (art, name)
    ) STRICT;

    DROP VIEW IF EXISTS gab;
    CREATE VIEW gav AS
    SELECT ver.id   AS id
         , grp.name AS grp
         , art.name AS art
         , ver.name AS ver
    FROM ver
    JOIN art ON ver.art = art.id
    JOIN grp ON art.grp = grp.id;

    DROP TABLE IF EXISTS dep_mgmt;
    CREATE TABLE dep_mgmt (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;

    DROP TABLE IF EXISTS dep;
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

  tora::stmt m_ins_depv_stmt = m_db.prepare(R"(
    INSERT INTO dep (from_ver, to_art, to_ver) VALUES (?, ?, ?)
  )");
  tora::stmt m_ins_depa_stmt = m_db.prepare(R"(
    INSERT INTO dep (from_ver, to_art) VALUES (?, ?)
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

  [[nodiscard]] unsigned find_ver(jute::view grp, jute::view art, jute::view ver) {
    m_find_ver_stmt.reset();
    m_find_ver_stmt.bind(1, grp);
    m_find_ver_stmt.bind(2, art);
    m_find_ver_stmt.bind(3, ver);
    if (!m_find_ver_stmt.step()) throw version_not_found {};
    auto id = m_find_ver_stmt.column_int(0); 
    if (m_find_ver_stmt.step()) die("duplicate version found");
    return id;
  }

public:
  [[nodiscard]] constexpr auto * handle() { return &m_db; }

  unsigned insert(jute::view grp, jute::view art) {
    m_ins_grp_stmt.reset();
    m_ins_grp_stmt.bind(1, grp);
    m_ins_grp_stmt.step();

    m_ins_art_stmt.reset();
    m_ins_art_stmt.bind(1, m_ins_grp_stmt.column_int(0));
    m_ins_art_stmt.bind(2, art);
    m_ins_art_stmt.step();

    return m_ins_art_stmt.column_int(0);
  }
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
    m_ins_ver_stmt.bind(3, pom);
    m_ins_ver_stmt.step();

    return m_ins_ver_stmt.column_int(0);
  }

  void add_dep_mgmt(unsigned from, unsigned to) {
    m_ins_dm_stmt.reset();
    m_ins_dm_stmt.bind(1, from);
    m_ins_dm_stmt.bind(2, to);
    m_ins_dm_stmt.step();
  }
  void add_dep_a(unsigned from, unsigned to_a) {
    m_ins_depv_stmt.reset();
    m_ins_depv_stmt.bind(1, from);
    m_ins_depv_stmt.bind(2, to_a);
    m_ins_depv_stmt.step();
  }
  void add_dep_v(unsigned from, unsigned to_v) {
    m_ins_depv_stmt.reset();
    m_ins_depv_stmt.bind(1, from);
    m_ins_depv_stmt.bind(2, to_v);
    m_ins_depv_stmt.step();
  }
};

static void load_deps(db * db, jute::view pom_path) try {
  auto pom = cavan::read_pom(pom_path);
  cavan::read_parent_chain(pom);
  cavan::merge_props(pom);

  auto from = db->insert(pom->grp, pom->art, pom->ver, pom_path);
  for (auto &[d, _]: pom->deps_mgmt) {
    auto grp = cavan::apply_props(pom, d.grp);
    auto ver = cavan::apply_props(pom, d.ver);
    db->add_dep_mgmt(from, db->insert(*grp, d.art, *ver, ""));
  }
  for (auto &[d, _]: pom->deps) {
    auto grp = cavan::apply_props(pom, d.grp);
    auto ver = cavan::apply_props(pom, d.ver);
    if (*ver == "") db->add_dep_a(from, db->insert(*grp, d.art));
    else db->add_dep_v(from, db->insert(*grp, d.art, *ver, ""));
  }
} catch (...) {
  // TODO: have more catchable errors in cavan
}

int main(int argc, char ** argv) try {
  db db {};

  meeql::recurse_repo_dir(curry(load_deps, &db));

  auto stmt = db.handle()->prepare("SELECT COUNT(*) FROM ver");
  stmt.step();
  putln("found ", stmt.column_int(0), " vers");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep_mgmt");
  stmt.step();
  putln("found ", stmt.column_int(0), " dm links");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep");
  stmt.step();
  putln("found ", stmt.column_int(0), " dep links");

} catch (...) {
  return 13;
}
