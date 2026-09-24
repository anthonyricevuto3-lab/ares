#include "ares/application.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        return ares::run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
