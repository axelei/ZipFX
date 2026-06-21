#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "miniz_zip.h"
int main() {
    const char* zipPath = "D:\\temp\\ysoccer19_windows64.zip";
    // ENTRY NAME MUST USE FORWARD SLASHES (zip convention)
    const char* entry   = "ysoccer19_windows64/config.json";
    const char* dest    = "C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test\\config.json";
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));
    printf("Open\n");
    if (!mz_zip_reader_init_file(&archive, zipPath, 0)) { printf("FAIL: open\n"); return 1; }
    printf("Files:\n");
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); i++) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&archive, i, &st))
            printf("  [%u] '%s' dir=%d\n", i, st.m_filename, st.m_is_directory);
    }
    printf("Extract flat: '%s'\n -> '%s'\n", entry, dest);
    mz_bool ok = mz_zip_reader_extract_file_to_file(&archive, entry, dest, 0);
    printf("Result: %d\n", ok);
    FILE* f = fopen(dest, "rb");
    if (f) { printf("EXISTS\n"); fclose(f); }
    else   { printf("NOT FOUND\n"); }
    // Now with structure
    printf("\n--- With structure ---\n");
    const char* dest2 = "C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test2\\ysoccer19_windows64\\config.json";
    mkdir("C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_miniz_test2\\ysoccer19_windows64");
    ok = mz_zip_reader_extract_file_to_file(&archive, entry, dest2, 0);
    printf("Result: %d\n", ok);
    f = fopen(dest2, "rb");
    if (f) { printf("EXISTS\n"); fclose(f); }
    else   { printf("NOT FOUND\n"); }
    mz_zip_reader_end(&archive);
    return 0;
}
