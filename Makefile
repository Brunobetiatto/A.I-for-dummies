# ===== Auto-detect MSYS2 toolchain (MINGW64 or UCRT64) and use pkg-config =====
SHELL := cmd

# Candidates
MSYS2_MINGW64 := C:/msys64/mingw64
MSYS2_UCRT64  := C:/msys64/ucrt64

# Pick prefix by what actually exists (first gcc found wins)
ifeq ($(wildcard $(MSYS2_MINGW64)/bin/gcc.exe),)
  ifeq ($(wildcard $(MSYS2_UCRT64)/bin/gcc.exe),)
    $(error MSYS2 gcc not found. Install one: \
      pacman -S mingw-w64-x86_64-gcc   (MINGW64) \
      or pacman -S mingw-w64-ucrt-x86_64-gcc   (UCRT64))
  else
    MSYS2_PREFIX := $(MSYS2_UCRT64)
  endif
else
  MSYS2_PREFIX := $(MSYS2_MINGW64)
endif

# Ensure pkg-config exists in the same prefix
ifeq ($(wildcard $(MSYS2_PREFIX)/bin/pkg-config.exe),)
  $(error pkg-config not found in $(MSYS2_PREFIX). Install it with: \
    pacman -S mingw-w64-x86_64-pkg-config   (MINGW64) \
    or pacman -S mingw-w64-ucrt-x86_64-pkg-config   (UCRT64))
endif

CC         := "$(MSYS2_PREFIX)/bin/gcc.exe"
WINDRES    := "$(MSYS2_PREFIX)/bin/windres.exe"
PKG_CONFIG := "$(MSYS2_PREFIX)/bin/pkg-config.exe"

# Make pkg-config see .pc files even from PowerShell/cmd
export PKG_CONFIG_PATH := $(MSYS2_PREFIX)/lib/pkgconfig;$(MSYS2_PREFIX)/share/pkgconfig
export PKG_CONFIG_LIBDIR := $(MSYS2_PREFIX)/lib/pkgconfig

CFLAGS   := -std=c11 -O2 -Wall -Wextra -MMD -MP
LDFLAGS  := -mwindows
TARGET   := AI-for-dummies.exe

# Pull all correct GTK flags (includes HarfBuzz, Pango, Cairo, etc.)
GTKPKG := gtk+-3.0
CFLAGS  += $(shell $(PKG_CONFIG) --cflags $(GTKPKG))
LDFLAGS += $(shell $(PKG_CONFIG) --libs   $(GTKPKG))

# Extra libs: curl (pkg-config), cJSON (manual)
CURLPKG := libcurl
CFLAGS  += $(shell $(PKG_CONFIG) --cflags $(CURLPKG))
LDFLAGS += $(shell $(PKG_CONFIG) --libs   $(CURLPKG)) -lcjson

# Sources and objects
SRC := $(wildcard src/*.c) $(wildcard src/*/*.c)
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

# Icon resource
RES := build/icon.res

# Backslash helper for cmd mkdir/rmdir
bs = $(subst /,\,$1)

.PHONY: all clean rebuild run doctor
all: $(TARGET)

# Show which toolchain you’re using and the flags detected
doctor:
	@echo Using prefix: $(MSYS2_PREFIX)
	@$(PKG_CONFIG) --version
	@$(PKG_CONFIG) --cflags $(GTKPKG)
	@$(PKG_CONFIG) --libs   $(GTKPKG)

$(TARGET): $(OBJ) $(RES) | build
	$(CC) $(OBJ) $(RES) -o $@ $(LDFLAGS)

build/%.o: src/%.c
	@if not exist "$(call bs,$(dir $@))" mkdir "$(call bs,$(dir $@))"
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): icon.rc AI-for-dummies.ico | build
	@if not exist "$(call bs,$(dir $@))" mkdir "$(call bs,$(dir $@))"
	$(WINDRES) -O coff icon.rc -o $(RES)

build:
	@if not exist "$(call bs,$@)" mkdir "$(call bs,$@)"

clean:
	-@if exist "$(call bs,build)" rmdir /S /Q "$(call bs,build)"
	-@if exist "$(TARGET)" del /Q "$(TARGET)"

rebuild: clean all
run: all
	.\$(TARGET)

-include $(DEP)

# --- Auto-detect MSYS2 prefix (mingw64 or ucrt64) ---
MSYS2_MINGW64 := C:/msys64/mingw64
MSYS2_UCRT64  := C:/msys64/ucrt64
ifeq ($(wildcard $(MSYS2_MINGW64)/bin),)
  MSYS2_PREFIX := $(MSYS2_UCRT64)
else
  MSYS2_PREFIX := $(MSYS2_MINGW64)
endif
RUNTIME := $(MSYS2_PREFIX)/bin
DIST    ?= dist

# Optional: ntldd for exact dependency copy (install with: pacman -S mingw-w64-x86_64-ntldd or ucrt variant)
NTLDD := $(MSYS2_PREFIX)/bin/ntldd.exe

# Minimal DLL set for GTK3 apps (names differ little across mingw/ucrt)
BUNDLE_DLLS := \
  libgtk-3-0.dll \
  libgdk-3-0.dll \
  libgobject-2.0-0.dll \
  libglib-2.0-0.dll \
  libgio-2.0-0.dll \
  libgdk_pixbuf-2.0-0.dll \
  libpango-1.0-0.dll \
  libpangocairo-1.0-0.dll \
  libatk-1.0-0.dll \
  libcairo-2.dll \
  libharfbuzz-0.dll \
  libfreetype-6.dll \
  libfontconfig-1.dll \
  libpng16-16.dll \
  zlib1.dll \
  libintl-8.dll \
  libiconv-2.dll \
  libgcc_s_seh-1.dll \
  libstdc++-6.dll \
  libwinpthread-1.dll

# Backslash helper for cmd.exe
bs = $(subst /,\,$1)

.PHONY: doctor bundle bundle-ntldd
doctor:
	@echo Using MSYS2 prefix: $(MSYS2_PREFIX)
	@echo Runtime bin:        $(RUNTIME)
	@echo Checking a few expected DLLs:
	@if exist "$(call bs,$(RUNTIME))/libgtk-3-0.dll" (echo  OK  libgtk-3-0.dll) else (echo  MISSING  libgtk-3-0.dll)
	@if exist "$(call bs,$(RUNTIME))/libglib-2.0-0.dll" (echo  OK  libglib-2.0-0.dll) else (echo  MISSING  libglib-2.0-0.dll)
	@if exist "$(call bs,$(RUNTIME))/libgobject-2.0-0.dll" (echo  OK  libgobject-2.0-0.dll) else (echo  MISSING  libgobject-2.0-0.dll)

bundle: all
	@echo Copying common GTK3 runtime DLLs from: $(RUNTIME)
	@for %%D in ($(BUNDLE_DLLS)) do if exist "$(call bs,$(RUNTIME))\%%D" (copy /Y "$(call bs,$(RUNTIME))\%%D" . >nul & echo   + %%D) else (echo   - skipped not found: %%D)
	@echo Ready: ./$(TARGET)

bundle-ntldd: all
	@if not exist "$(call bs,$(NTLDD))" (echo ntldd not found at $(NTLDD). Install it with pacman and try again. & exit /b 1)
	@if not exist "$(call bs,build)" mkdir "$(call bs,build)"
	@echo Resolving runtime DLLs via ntldd...
	@"$(call bs,$(NTLDD))" -R "$(TARGET)" | findstr /I "\\bin\\" > build\deps.tmp
	@for /f "tokens=3" %%P in (build\deps.tmp) do if exist "%%P" (copy /Y "%%P" . >nul & echo   + %%~nxP)
	@del /q build\deps.tmp
	@echo Ready: ./$(TARGET)

