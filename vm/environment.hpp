#ifndef RBX_ENVIRONMENT_HPP
#define RBX_ENVIRONMENT_HPP

#include <string>
#include <stdexcept>

#include "vm.hpp"
#include "config_parser.hpp"
#include "configuration.hpp"

namespace rubinius {

  class ConfigParser;
  class QueryAgent;

  class Environment {
  public:
    SharedState* shared;
    VM* state;
    QueryAgent* agent;

    ConfigParser  config_parser;
    Configuration config;

    Environment();
    ~Environment();

    void load_config_argv(int argc, char** argv);
    void load_argv(int argc, char** argv);
    void load_directory(std::string dir);
    void load_platform_conf(std::string dir);
    void load_conf(std::string path);
    void load_string(std::string str);
    void run_file(std::string path);
    void enable_preemption();
    void boot_vm();
    void halt();
    int exit_code();
    void start_signals();
    void start_agent(int port);
  };

}

#endif
