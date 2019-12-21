import <filesystem>;
import <fstream>;
import <iomanip>;
import <iostream>;
import <map>;
import <regex>;
import <set>;
import <string>;

namespace fs = std::filesystem;
using std::literals::operator""ms;

struct file_info {
  fs::file_time_type last_write_time;
  std::string module_name;
  std::set<std::string> dependencies;
  bool from_cache = false;
};

constexpr char module_cache[] = "build/module_cache";

const std::regex module_name_pattern{
    R"(export\s+module\s+([a-zA-Z0-9_.]+)\s*;)"};
const std::regex import_pattern{
    R"(import\s+([a-zA-Z0-9_.]+|<[a-zA-Z0-9_./]+>|"[a-zA-Z0-9_./]+")\s*;)"};

struct state {
  void update() {
    for (auto entry : fs::recursive_directory_iterator("src")) {
      if (!is_regular_file(entry.status()) && !is_symlink(entry.status())) {
        continue;
      }
      if (should_scan(entry.path())) scan(entry.path());
    }
    prune();
  }

  bool should_scan(const fs::path& file) {
    if (file.extension() != ".cc") return false;
    auto i = files.find(file);
    if (i == files.end()) return true;
    i->second.from_cache = false;
    auto last_write_time = fs::last_write_time(file);
    return last_write_time > i->second.last_write_time;
  }

  void scan(const fs::path& filename) {
    std::ifstream file(filename.c_str());
    if (!file.good()) {
      std::cerr << "error: can't open " << filename << '\n';
      return;
    }
    auto contents = std::string(std::istreambuf_iterator<char>(file), {});
    std::string module_name;
    if (std::smatch match; regex_search(contents, match, module_name_pattern)) {
      module_name = match[1];
    }
    auto begin =
        std::sregex_iterator(contents.begin(), contents.end(), import_pattern);
    auto end = std::sregex_iterator();
    std::set<std::string> dependencies;
    for (auto i = begin; i != end; ++i) {
      dependencies.insert((*i)[1]);
    }
    auto& file_info = files[filename];
    file_info.from_cache = false;
    file_info.last_write_time = fs::last_write_time(filename);
    file_info.module_name = module_name;
    file_info.dependencies = dependencies;
    if (module_name.empty()) {
      binaries.push_back(filename);
    } else {
      modules.emplace(module_name, filename);
    }
  }

  void prune() {
    for (auto i = files.begin(); i != files.end();) {
      if (i->second.from_cache) {
        i = files.erase(i);
      } else {
        ++i;
      }
    }
  }

  std::set<std::string> dependencies(std::string module) const {
    if (!modules.contains(module)) return {};
    return files.at(modules.at(module)).dependencies;
  }

  std::set<std::string> recursive_dependencies(fs::path file) const {
    const auto& dependencies = files.at(file).dependencies;
    std::set<std::string> all;
    for (const auto& d : dependencies) {
      if (!modules.contains(d)) continue;
      all.insert(d);
      for (const auto& d2 : recursive_dependencies(modules.at(d))) {
        if (modules.contains(d2)) all.insert(d2);
      }
    }
    return all;
  }

  std::map<fs::path, file_info> files;
  std::map<std::string, fs::path> modules;
  std::vector<fs::path> binaries;
};

state load_cache() {
  std::ifstream file(module_cache);
  if (!file.good()) return {};
  state state;
  fs::path name;
  while (file >> name) {
    file_info f;
    f.from_cache = true;
    long time;
    int num_dependencies;
    file >> std::quoted(f.module_name) >> time >> num_dependencies;
    if (!file) {
      file.clear(std::ios::failbit);
      std::string line;
      std::getline(file, line);
      std::cerr << "warning: bad module cache near " << std::quoted(line)
                << '\n';
      return {};
    }
    f.last_write_time = fs::file_time_type{} + time * 1ms;
    for (int i = 0; i < num_dependencies; i++) {
      std::string dependency;
      file >> std::quoted(dependency);
      if (!file) {
        file.clear(std::ios::failbit);
        std::string line;
        std::getline(file, line);
        std::cerr << "warning: bad module cache near " << std::quoted(line)
                  << '\n';
        return {};
      }
      f.dependencies.insert(dependency);
    }
    state.files.emplace(name, f);
    if (f.module_name.empty()) {
      state.binaries.push_back(name);
    } else {
      state.modules.emplace(f.module_name, name);
    }
  }
  return state;
}

void save_cache(const state& state) {
  std::ofstream file(module_cache);
  for (const auto& [name, info] : state.files) {
    long time = info.last_write_time.time_since_epoch() / 1ms + 1;
    file << name << " " << std::quoted(info.module_name) << " " << time << " "
         << info.dependencies.size();
    for (const auto& dependency : info.dependencies) {
      file << " " << std::quoted(dependency);
    }
    file << "\n";
  }
  if (!file.good()) {
    std::cerr << "warning: can't write module cache.\n";
    return;
  }
}

void emit_rules(state state, std::string_view mode) {
  // Emit make rules.
  for (const auto& [module, file] : state.modules) {
    // Rule to build the module interface for a module.
    std::cout << "build/" << mode << "/" << module << ".pcm: " << file.c_str();
    const auto& dependencies = state.files.at(file).dependencies;
    for (const auto& dependency : dependencies) {
      if (state.modules.contains(dependency)) {
        std::cout << " build/" << mode << "/" << dependency << ".pcm";
      }
    }
    // Rule to build the module itself.
    std::cout << "\nbuild/" << mode << "/" << module << ".o: " << file.c_str()
              << " |";
    for (const auto& dependency : dependencies) {
      if (state.modules.contains(dependency)) {
        std::cout << " build/" << mode << "/" << dependency << ".pcm";
      }
    }
    std::cout << "\n";
  }
  for (const auto& binary : state.binaries) {
    std::cout << "\nbuild/" << mode << "/" << binary.stem().c_str()
              << ".o: " << binary.c_str();
    for (const auto& dependency : state.files[binary].dependencies) {
      if (state.modules.contains(dependency)) {
        std::cout << " build/" << mode << "/" << dependency << ".pcm";
      }
    }
    std::cout << "\n";
    std::cout << "bin/" << mode << "/" << binary.stem().c_str() << ": build/"
              << mode << "/" << binary.stem().c_str() << ".o";
    for (const auto& dependency : state.recursive_dependencies(binary)) {
      std::cout << " build/" << mode << "/" << dependency << ".o";
    }
    std::cout << "\n\n";
  }
  std::cout << mode << ": ";
  for (const auto& binary : state.binaries) {
    std::cout << " bin/" << mode << "/" << binary.stem().c_str();
  }
  std::cout << "\n";
}

int main() {
  // Scan the source directory and update the dependency graph.
  auto state = load_cache();
  state.update();
  save_cache(state);

  // Emit make rules.
  emit_rules(state, "debug");
  emit_rules(state, "opt");
  std::cout << "all: opt debug\n";
}
