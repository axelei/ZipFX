Building 7z.so for macOS
=========================

From p7zip source (tested on macOS arm64 and x86_64):

    git clone https://github.com/p7zip-project/p7zip.git
    cd p7zip/CPP/7zip/Bundles/Format7zF
    make -f makefile.gcc -j
    make -f makefile.gcc install INSTALL_PREFIX=/tmp/7z-out

The .so will be at `/tmp/7z-out/lib/7z.so`. Copy it to:

    lib/macos/arm64/lib7z.so   (for Apple Silicon)
    lib/macos/x64/lib7z.so     (for Intel Macs)

Note: both are built from the same source — only the architecture differs.
Use `lipo -info 7z.so` to verify the architecture after building.
