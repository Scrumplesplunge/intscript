import "util/check.h";
import <fstream>;
import <iostream>;
import <optional>;
import <string>;
import <map>;
import <span>;
import <variant>;
import <vector>;
import compiler.ast;
import compiler.codegen;
import compiler.parser;
import as.parser;
import as.encode;
import intcode;
import util.io;
import util.value_ptr;

template <typename... Ts> struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts> overload(Ts...) -> overload<Ts...>;

struct flag {
  using load_bool = void();
  using load_value = void(const char*);

  std::string_view name;
  std::optional<const char*> value;
  std::string_view description;
  std::variant<load_bool*, load_value*> load;
};

struct {
  bool debug;
  std::span<char*> positional;
} args;

void show_usage_and_exit();

constexpr flag flags[] = {
  {"help", {}, "Displays the usage information.", show_usage_and_exit},
  {"debug", {}, "Show executed instructions", +[]() { args.debug = true; }},
};

void show_usage_and_exit() {
  std::cout << "Built on " __DATE__ " at " __TIME__ "\n\nFlags:\n"; 
  for (const flag& f : flags) {
    std::cout << "  --" << f.name << "\t" << f.description;
    if (f.value) std::cout << " Default value: " << std::quoted(*f.value);
    std::cout << "\n";
  }
  std::exit(0);
}

void read_options(int& argc, char**& argv) {
  for (const flag& f : flags) {
    if (auto* load = std::get_if<flag::load_value*>(&f.load)) {
      (*load)(f.value.value());
    }
  }
  bool options_done = false;
  int j = 1;
  for (int i = 1; i < argc; i++) {
    std::string_view argument = argv[i];
    if (options_done || !argument.starts_with("--")) {
      argv[j++] = argv[i];
    } else if (argument == "--") {
      options_done = true;
    } else {
      for (const flag& f : flags) {
        if (argument.substr(2) == f.name) {
          std::visit(overload{
            [&](flag::load_bool* load) { load(); },
            [&](flag::load_value* load) {
              if (++i < argc && !std::string_view(argv[i]).starts_with("--")) {
                load(argv[i]);
              } else {
                std::cerr << "Missing argument for --" << f.name << ".\n";
                std::exit(1);
              }
            },
          }, f.load);
        }
      }
    }
  }
  argc = j;
  args.positional = std::span<char*>(argv, argc);
}

std::vector<program::value_type> load(const char* filename) {
  auto extension = std::filesystem::path(filename).extension();
  if (extension == ".ic") {
    std::vector<program::value_type> buffer(program::max_size);
    auto data = program::load(contents(filename), buffer);
    buffer.resize(data.size());
    return buffer;
  } else if (extension == ".asm") {
    return as::encode(as::parse(filename, contents(filename)));
  } else if (extension == ".is") {
    auto code = compiler::load(filename);
    return as::encode(compiler::generate(code));
  } else {
    std::cerr << "Unknown extension " << std::quoted(extension.c_str())
              << ", must be \".ic\", \".asm\", or \".is\".\n";
    std::exit(1);
  }
}

int main(int argc, char* argv[]) {
  read_options(argc, argv);
  if (args.positional.size() != 2) {
    std::cerr << "Usage: run <filename>\n";
    return 1;
  }
  program program(load(argv[1]));
  while (!program.done()) {
    switch (program.resume()) {
      case program::ready:
        std::cerr << "Program paused for no reason.\n";
        std::abort();
      case program::waiting_for_input:
        program.provide_input(std::cin.get());
        break;
      case program::output:
        std::cout.put(program.get_output());
        break;
      case program::halt:
        return 0;
    }
  }
}
