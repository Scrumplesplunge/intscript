import <string_view>;
import <fstream>;
import <variant>;
import <optional>;
import <vector>;
import <iomanip>;
import <iostream>;
import <map>;
import <span>;
import as.ast;
import as.encode;
import compiler.ast;
import compiler.codegen;
import compiler.parser;

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
  const char* input;
  const char* output;
  enum { assembly, intcode } output_type;
  std::span<char*> positional;
} args;

void show_usage_and_exit();

constexpr flag flags[] = {
  {"help", {}, "Displays the usage information.", show_usage_and_exit},
  {"input", "-", "File to read from.", +[](const char* x) { args.input = x; }},
  {"output", "-", "File to write to.", +[](const char* x) { args.output = x; }},
  {"output_type", "intcode", "Output format (assembly or intcode).",
   +[](const char* x) {
     if (x == std::string_view("assembly")) {
       args.output_type = args.assembly;
     } else if (x == std::string_view("intcode")) {
       args.output_type = args.intcode;
     } else {
       std::cerr << "Invalid output type.\n";
       std::exit(1);
     }
   }},
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

int main(int argc, char* argv[]) {
  read_options(argc, argv);
  auto code = compiler::load(args.input);
  auto compiled = compiler::generate(code);
  std::ofstream file;
  std::ostream* output;
  if (args.output == std::string_view("-")) {
    output = &std::cout;
  } else {
    file.open(args.output);
    if (!file.good()) {
      std::cerr << "Could not open " << std::quoted(args.output)
                << " for writing.\n";
      return 1;
    }
    output = &file;
  }
  if (args.output_type == args.assembly) {
    for (auto& x : compiled) *output << x << '\n';
  } else {
    auto encoded = encode(compiled);
    bool first = true;
    for (auto x : encoded) {
      if (first) {
        first = false;
      } else {
        *output << ',';
      }
      *output << x;
    }
    *output << '\n';
  }
}
