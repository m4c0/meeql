#pragma leco tool

import meeql;

int main() {
  auto db = meeql::db();

  // Creates a table of the parent chain of the request artefact
  db.exec(R"(
    DROP TABLE IF EXISTS f_pom_tree;

    CREATE TABLE f_pom_tree AS
    WITH RECURSIVE
      pom_chain(id, root, depth) AS (
        SELECT id, id, 0
        FROM pom
        UNION ALL
        SELECT pom.parent, pom_chain.root, pom_chain.depth + 1
        FROM pom
        JOIN pom_chain ON pom_chain.id = pom.id
        WHERE pom.parent IS NOT NULL
      )
    SELECT * FROM pom_chain
  )");
}
