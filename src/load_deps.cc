#include "pkg/load_deps.h"

#include <fstream>
#include <ostream>

#include "boost/filesystem.hpp"

#include "fmt/format.h"

#include "pkg/dependency_loader.h"
#include "pkg/detect_branch.h"
#include "pkg/exec.h"
#include "pkg/git.h"

namespace fs = boost::filesystem;

namespace pkg {

void load_deps(fs::path const& repo, fs::path const& deps_root) {
  dependency_loader l{deps_root};
  l.retrieve(repo, [&](dep* d) {
    if (fs::is_directory(d->path_)) {
      fmt::print("already cloned: {}\n", d->name());

      auto const commit = get_commit(d->path_);
      if (d->commit_ != commit) {
        fmt::print("[{}] warning:\n  required={}\n  current={}\n", d->name(),
                   d->commit_, commit);
      }
    } else {
      fmt::print("cloning: {}\n", d->name());
      git_clone(d);
    }

    auto const branch_head_commit =
        get_commit(d->path_, "remotes/origin/" + d->branch_);
    if (!d->branch_.empty() && branch_head_commit == d->commit_) {
      exec(d->path_, "git checkout {}", d->branch_);
    }
  });

  std::ofstream of{"deps/CMakeLists.txt"};
  of << "project(" + deps_root.string() << ")\n"
     << "cmake_minimum_required(VERSION 3.10)\n\n";
  for (auto const& v : l.sorted()) {
    if (v->url_ == ROOT) {
      continue;
    }
    of << "add_subdirectory(" << v->name() << " EXCLUDE_FROM_ALL)\n";
  }
}

}  // namespace pkg