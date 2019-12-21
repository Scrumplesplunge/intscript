import "util/check.h";
import <fstream>;
import <iostream>;
import <string>;
import <span>;
import intcode;
import util.io;

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: run <filename>\n";
    return 1;
  }
  program::buffer buffer;
  program program(program::load(init(argc, argv), buffer));
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
