#pragma leco tool

import cavan;
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

int main(int argc, char ** argv) try {
  tora::db db { ":memory:" };

  init(db);
} catch (...) {
  return 1;
}
