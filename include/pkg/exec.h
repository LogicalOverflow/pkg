#pragma once

#include <sstream>

#include "boost/filesystem.hpp"

#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <boost/process/start_dir.hpp>

#include "fmt/format.h"

namespace pkg {

struct exec_result {
  std::string out_, err_;
  int return_code_;
};

template <typename... Args>
exec_result exec(boost::filesystem::path const& working_directory,
                 char const* command, Args... args) {
  namespace bp = boost::process;

  auto cmd = fmt::format(command, args...);

  bp::ipstream out, err;
  bp::child c(cmd, bp::start_dir = working_directory, bp::std_out > out,
              bp::std_err > err);

  std::string line;
  std::stringstream out_ss, err_ss;
  while (true) {
    if (!out && !err) {
      break;
    }

    if (out) {
      std::getline(out, line);
      if (!line.empty()) {
        out_ss << line << "\n";
        continue;
      }
    }

    if (err) {
      std::getline(err, line);
      if (!line.empty()) {
        err_ss << line << "\n";
        continue;
      }
    }
  }

  c.wait();

  if (c.exit_code() != 0) {
    printf("COMMAND [%s] failed\n", cmd.c_str());
    printf("ERROR %d\n", c.exit_code());
    printf("OUTPUT:\n%s\n", out_ss.str().c_str());
    printf("ERROR:\n%s\n", err_ss.str().c_str());
    throw std::runtime_error("exit code != 0");
  }

  exec_result r;
  r.out_ = out_ss.str();
  r.err_ = err_ss.str();
  r.return_code_ = c.exit_code();
  return r;
}

}  // namespace pkg