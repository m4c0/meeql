#pragma leco tool

import jute;
import meeql;
import print;
import tora;

static void help() {
  errln(R"(
Available commands (in alphabetical order):
  * help - This command, list all commands
  * load - Loads the current project dependencies into the database.
)");
}

static void init(tora::db & db) {
}

int main(int argc, char ** argv) {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  auto file = meeql::m2_dir() + "/meeql-brute.sqlite\0";
  tora::db db { file.begin() };

  auto cmd = shift();
       if (cmd == "")     help();
  else if (cmd == "init") init(db);
}
