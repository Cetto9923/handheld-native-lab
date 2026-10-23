#include "nso_diagnostic.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: nso_diagnostic <authorized-uncompressed.nso>\n";
        return 2;
    }
    const std::string report = ffrs::DiagnoseNso(argv[1]);
    std::cout << report << '\n';
    return report.find("LOADED_DIAGNOSTIC_ONLY") == std::string::npos ? 1 : 0;
}
