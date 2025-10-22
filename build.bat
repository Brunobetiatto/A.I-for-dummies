@echo off
set "MSYS2=C:\msys64\mingw64"
set "PATH=%MSYS2%\bin;%PATH%"
set "PKG_CONFIG_PATH=%MSYS2%\lib\pkgconfig;%MSYS2%\share\pkgconfig"
set "PKG_CONFIG_LIBDIR=%MSYS2%\lib\pkgconfig"
make %*
make bundle-ntldd