#include "SpioCLI/CLI.hpp"

#include <string>
#include <vector>

int main(int argc, char **argv)
{
  std::vector<std::string> args;
  args.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0U);
  for (int i = 1; i < argc; ++i)
  {
    args.emplace_back(argv[i]);
  }

  return spio::RunCli(args);
}
