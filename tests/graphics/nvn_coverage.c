/* Coverage gate for the game's metadata-only NVN API inventory. */
#include "nvn/nvn_dispatch.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void quiet_missing(const char *name, void *user) { (void)name; (void)user; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: nvn_coverage <api-name-inventory>\n"); return 2; }
    FILE *input = fopen(argv[1], "r");
    if (!input) { perror("inventory"); return 2; }
    nvn_dispatch_init();
    nvn_dispatch_set_missing_callback(quiet_missing, NULL);
    char name[256];
    unsigned required = 0, resolved = 0;
    printf("{\"schema_version\":1,\"kind\":\"nvn_api_resolution\",\"missing\":[");
    while (fgets(name, sizeof(name), input)) {
        name[strcspn(name, "\r\n")] = '\0';
        if (!*name) continue;
        if (strncmp(name, "nvn", 3)) { fclose(input); return 2; }
        for (const unsigned char *c = (unsigned char *)name; *c; ++c)
            if (!isalnum(*c) && *c != '_') { fclose(input); return 2; }
        ++required;
        if (nvnBootstrapLoader(name)) ++resolved;
        else printf("%s\"%s\"", required - resolved > 1 ? "," : "", name);
    }
    int bad = ferror(input);
    fclose(input);
    if (bad || !required) return 2;
    printf("],\"required\":%u,\"resolved\":%u,\"complete\":%s,"
           "\"proves_rendering\":false}\n", required, resolved,
           required == resolved ? "true" : "false");
    /* Resolution is necessary, never sufficient, evidence for rendering. */
    return required == resolved ? 0 : 3;
}
