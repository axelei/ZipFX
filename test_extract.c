#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "miniz_zip.h"
int main() {
    const char* zipPath = "D:\\temp\\ysoccer19_windows64.zip";
    const char* entry   = "ysoccer19_windows64/config.json";
    const char* dest    = "C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_test\\out\\config.json";
    mkdir("C:\\Users\\axele\\AppData\\Local\\Temp\\zipfx_test\\out");
    mz_zip_archive archive = {0};
    mz_zip_reader_init_file(&archive, zipPath, MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY);
    printf("Files: %u\n", mz_zip_reader_get_num_files(&archive));
    mz_bool ok = mz_zip_reader_extract_file_to_file(&archive, entry, dest, 0);
    printf("Extract result: %d\n", ok);
    if (ok) {
        FILE* f = fopen(dest, "rb");
        if (f) { char b[256]={}; fread(b,1,255,f); printf("OK: %.50s\n",b); fclose(f); }
        else { printf("File not found on disk\n"); }
    }
    mz_zip_reader_end(&archive);
    return ok ? 0 : 1;
}
