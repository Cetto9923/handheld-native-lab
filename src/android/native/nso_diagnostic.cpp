#include "nso_diagnostic.h"
#include "nso_loader.h"
#include "json_writer.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#if defined(FFRS_HAS_NVN_DIAGNOSTICS)
#include "nvn/nvn_dispatch.h"
#endif

namespace ffrs {
std::string DiagnoseNso(const std::string& path) {
    JsonWriter out;
    out.BeginObject();
    out.Key("mode"); out.String("load_and_relocate_only");
    out.Key("game_code_executed"); out.Bool(false);
    out.Key("nvn_dispatch"); out.BeginObject();
#if defined(FFRS_HAS_NVN_DIAGNOSTICS)
    nvn_dispatch_stats graphics {};
    out.Key("compiled"); out.Bool(true);
    out.Key("bootstrap_entry_available"); out.Bool(nvnBootstrapLoader("nvnBootstrapLoader") != nullptr);
    nvn_dispatch_get_stats(&graphics);
    out.Key("backend_functions_registered"); out.UInt(graphics.registered);
#else
    out.Key("compiled"); out.Bool(false);
#endif
    out.Key("rendering"); out.String("NOT_RUN");
    out.EndObject();
    out.Key("module_path"); out.String(path);
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        out.Key("status"); out.String("BLOCKED_MODULE");
        out.Key("error"); out.String("Place an authorized uncompressed NSO at this path; no module is packaged in the APK.");
        out.EndObject();
        return out.str();
    }
    out.Key("file_bytes"); out.UInt(static_cast<unsigned long long>(st.st_size));
    unsigned char header[256] {};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    uint32_t flags = 0;
    std::memcpy(&flags, header + 12, sizeof(flags));
    if (input.gcount() != sizeof(header) || std::memcmp(header, "NSO0", 4) != 0 || (flags & 7)) {
        out.Key("status"); out.String("INVALID_MODULE");
        out.Key("error"); out.String("Expected an uncompressed NSO0 image with a complete 256-byte header.");
        out.EndObject();
        return out.str();
    }
    char error[512] {};
    nso_module* module = nso_load_file(path.c_str(), nullptr, error, sizeof(error));
    if (!module) {
        out.Key("status"); out.String("LOAD_FAILED");
        out.Key("error"); out.String(error);
    } else {
        char build_id[33] {};
        nso_build_id(module, build_id);
        out.Key("status"); out.String("LOADED_DIAGNOSTIC_ONLY");
        out.Key("build_id_loader_format"); out.String(build_id);
        out.Key("imports"); out.UInt(nso_import_count(module));
        out.Key("imports_unresolved_by_loader"); out.UInt(nso_unresolved_count(module));
        out.Key("resolver"); out.String("none; imports are not executed");
        out.Key("rela_applied"); out.UInt(nso_reloc_count(module));
        out.Key("relr_applied"); out.UInt(nso_relr_count(module));
        unsigned unresolved = 0;
        unsigned imported = 0;
        out.Key("import_names_first_64"); out.BeginArray();
        for (uint32_t i = 1; ; ++i) {
            const char* name = nso_symbol_name(module, i);
            if (!name) break;
            if (nso_symbol_value(module, i) == UINT64_MAX) {
                if (imported++ < 64) out.String(name);
                if (!dlsym(RTLD_DEFAULT, name)) ++unresolved;
            }
        }
        out.EndArray();
        out.Key("imports_unresolved_by_host_dlsym"); out.UInt(unresolved);
        out.Key("compatibility_surface"); out.String("Host dlsym visibility is diagnostic; Switch imports and constructors are not validated.");
        nso_free(module);
    }
    out.EndObject();
    return out.str();
}
}
