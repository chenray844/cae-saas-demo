#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <iostream>

int main(int argc, char *argv[]) {
  {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_level(spdlog::level::debug);
  }
  spdlog::debug(__func__);
  return EXIT_SUCCESS;
}