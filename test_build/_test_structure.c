#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "miniz_zip.h"
int main() {
    const char* zipPath = "D:\\temp\\ysoccer19_windows64.zip";
    const char* entry   = "ysoccer19_windows64\\config.json";
    const char* dest    = "C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test\\config.json";
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));
    printf("Open\n");
    if (!mz_zip_reader_init_file(&archive, zipPath, 0)) { printf("FAIL: open\n"); return 1; }
    printf("Extract: '%s'\n -> '%s'\n", entry, dest);
    mz_bool ok = mz_zip_reader_extract_file_to_file(&archive, entry, dest, 0);
    printf("Result: %d\n", ok);
    FILE* f = fopen(dest, "rb");
    if (f) { printf("FILE EXISTS: %s\n", dest); fclose(f); }
    else   { printf("FILE NOT FOUND\n"); }
    mz_zip_reader_end(&archive);
    // Test WITH subdir in dest
    printf("\n--- With subdir structure ---\n");
    memset(&archive, 0, sizeof(archive));
    mz_zip_reader_init_file(&archive, zipPath, 0);
    const char* dest2 = "C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test\\ysoccer19_windows64\\config.json";
    mkdir("C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test\\ysoccer19_windows64");
    ok = mz_zip_reader_extract_file_to_file(&archive, entry, dest2, 0);
    printf("Result: %d\n", ok);
    f = fopen(dest2, "rb");
    if (f) { printf("FILE EXISTS: %s\n", dest2); fclose(f); }
    else   { printf("FILE NOT FOUND\n"); }
    mz_zip_reader_end(&archive);
    return 0;
}
