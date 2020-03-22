#include "pkg/load_deps.h"

#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "boost/asio/io_service.hpp"
#include "boost/asio/strand.hpp"
#include "boost/filesystem.hpp"

#include "fmt/format.h"

#include "pkg/dependency_loader.h"
#include "pkg/detect_branch.h"
#include "pkg/exec.h"
#include "pkg/git.h"

namespace fs = boost::filesystem;

namespace pkg {

void load_deps(fs::path const& repo, fs::path const& deps_root,
               bool const clone_https) {
  if (!boost::filesystem::is_directory(deps_root)) {
    boost::filesystem::create_directories(deps_root);
  }

  boost::asio::io_service ios;
  boost::asio::io_service::strand main{ios};

  std::mutex print_mutex;
  auto const print = [&](auto fn) {
    std::lock_guard g{print_mutex};
    fn();
  };

  dependency_loader l{deps_root};
  main.post([&] {
    l.retrieve_async(repo, [&](dep* d, dependency_loader::iteration_fn_t cb) {
      if (fs::is_directory(d->path_)) {
        fmt::print("already cloned: {}", d->name());
        if (auto const commit = get_commit(d->path_); d->commit_ != commit) {
          fmt::print(" (updating current={} to required={})\n",
                     git_shorten(d, commit), git_shorten(d, d->commit_));
        } else {
          fmt::print("\n");
        }
        std::cout << std::flush;

        executor e;
        try {
          git_attach(e, d);
        } catch (std::exception const& ex) {
          fmt::print("Checkout failed for {}: {}\n", d->name(), ex.what());
          if (!e.results_.empty()) {
            std::cout << "*** TRACE:\n";
            for (auto const& r : e.results_) {
              std::cout << r << "\n";
            }
          }
        }
        return cb(d);
      } else {
        fmt::print("cloning: {}\n", d->name());
        std::cout << std::flush;
        ios.post([d_copy = *d, d, &main, cb_mv = std::move(cb), &print, &clone_https] {
          executor e;
          try {
            git_clone(e, &d_copy, clone_https);
          } catch (std::exception const& ex) {
            print([&] {
              fmt::print("Repo checkout failed for {}: {}\n", d_copy.name(),
                         ex.what());
              if (!e.results_.empty()) {
                std::cout << "*** TRACE:\n";
                for (auto const& r : e.results_) {
                  std::cout << r << "\n";
                }
              }
            });
          }
          return main.post([cb_mv_1 = std::move(cb_mv), d] { cb_mv_1(d); });
        });
      }
    });
  });

  std::vector<std::thread> worker(5);
  std::for_each(begin(worker), end(worker),
                [&](auto& w) { w = std::thread{[&] { ios.run(); }}; });
  std::for_each(begin(worker), end(worker), [&](auto& w) { w.join(); });

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