#include "probe_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    bool output_json = true;
    bool output_summary = false;
    std::string output_file;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--summary") == 0) {
            output_summary = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            output_json = true;
        } else if ((std::strcmp(argv[i], "-o") == 0 || std::strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: probe_rg477m [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --json             Emit probe results in JSON format (default)\n";
            std::cout << "  --summary          Print high-level readable summary to stdout\n";
            std::cout << "  -o, --output PATH  Write JSON output to the specified file path\n";
            std::cout << "  -h, --help         Display this help message\n";
            return 0;
        }
    }

    ffrs::ProbeContext ctx;
#if defined(__ANDROID__)
    ctx.execution_mode = "android_native_cli";
#else
    ctx.execution_mode = "host_macos";
#endif

    ffrs::FullProbeReport report = ffrs::RunProbe(ctx);
    std::string json_str = ffrs::SerializeProbeReportToJson(report);

    if (!output_file.empty()) {
        std::ofstream ofs(output_file);
        if (ofs.is_open()) {
            ofs << json_str << "\n";
            std::cerr << "[probe_rg477m] Report written to: " << output_file << "\n";
        } else {
            std::cerr << "[probe_rg477m] Failed to open file for writing: " << output_file << "\n";
            return 1;
        }
    }

    if (output_summary) {
        ffrs::PrintReportSummary(report);
    }

    if (output_json && !output_summary) {
        std::cout << json_str << "\n";
    }

    return 0;
}
