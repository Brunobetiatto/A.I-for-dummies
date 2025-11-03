#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <gio/gio.h>

#ifdef G_OS_UNIX
  #include <signal.h>
  #include <unistd.h>
  #ifdef __linux__
    #include <execinfo.h>   // backtrace()
  #endif
#endif

#ifdef G_OS_WIN32
  #include <windows.h>
  #include <dbghelp.h>
  #if defined(_MSC_VER)
    #pragma comment(lib, "dbghelp.lib")
  #endif
#endif

#include <time.h>

/* ------------------------------------------------------------------------- */
/* Globals                                                                   */
/* ------------------------------------------------------------------------- */
static gchar  *g_log_path  = NULL;
static gchar  *g_dmp_path  = NULL;
static gchar  *g_header    = NULL;
static GMutex  g_log_mutex;

#ifdef G_OS_UNIX
static int     g_log_fd    = -1;     /* async-signal-safe writes */
#endif

/* ------------------------------------------------------------------------- */
/* Utility                                                                   */
/* ------------------------------------------------------------------------- */
static gchar *timestamp_now(void) {
    GDateTime *dt = g_date_time_new_now_local();
    gchar *s = g_date_time_format(dt, "%Y-%m-%d_%H-%M-%S");
    g_date_time_unref(dt);
    return s;
}

static void ensure_dir(const char *path) {
    if (!path) return;
    g_mkdir_with_parents(path, 0755);
}

static void open_log_files(const char *log_dir, const char *app_name) {
    gchar *ts = timestamp_now();
    g_log_path = g_build_filename(log_dir, g_strdup_printf("%s_%s.log", app_name, ts), NULL);
#ifdef G_OS_WIN32
    g_dmp_path = g_build_filename(log_dir, g_strdup_printf("%s_%s.dmp", app_name, ts), NULL);
#endif
    g_free(ts);

#ifdef G_OS_UNIX
    g_log_fd = g_open(g_log_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
#endif
}

static void write_line_unsafe(const char *line) {
#ifdef G_OS_UNIX
    if (g_log_fd >= 0) {
        (void)write(g_log_fd, line, strlen(line));
        (void)write(g_log_fd, "\n", 1);
        (void)fsync(g_log_fd);
    }
#else
    /* Windows: normal stdio is fine here (we're not in a signal handler) */
    FILE *f = fopen(g_log_path, "ab");
    if (!f) return;
    fwrite(line, 1, strlen(line), f);
    fwrite("\n", 1, 1, f);
    fflush(f);
    fclose(f);
#endif
}

static void writef_line_safe(const char *prefix, const char *fmt, va_list ap) {
    char buf[4096];
    int n = 0;
    if (prefix) {
        n = g_snprintf(buf, sizeof(buf), "%s", prefix);
        if (n < 0 || n >= (int)sizeof(buf)) n = 0;
    }
    g_vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    write_line_unsafe(buf);
}

void backlogger_log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_mutex_lock(&g_log_mutex);
    writef_line_safe("", fmt, ap);
    g_mutex_unlock(&g_log_mutex);
    va_end(ap);
}

/* ------------------------------------------------------------------------- */
/* GLib log redirection                                                       */
/* ------------------------------------------------------------------------- */
static void glib_log_to_file(const gchar *domain,
                             GLogLevelFlags level,
                             const gchar *message,
                             gpointer user_data) {
    (void)user_data;
    const char *lvl = "LOG";
    if (level & G_LOG_LEVEL_ERROR)    lvl = "ERROR";
    if (level & G_LOG_LEVEL_CRITICAL) lvl = "CRITICAL";
    if (level & G_LOG_LEVEL_WARNING)  lvl = "WARNING";
    if (level & G_LOG_LEVEL_MESSAGE)  lvl = "MESSAGE";
    if (level & G_LOG_LEVEL_INFO)     lvl = "INFO";
    if (level & G_LOG_LEVEL_DEBUG)    lvl = "DEBUG";

    g_mutex_lock(&g_log_mutex);
    char when[64]; {
        time_t t = time(NULL);
        #ifdef G_OS_WIN32
                struct tm tmv;
                localtime_s(&tmv, &t);
        #else
                struct tm tmv; localtime_r(&t, &tmv);
        #endif
                strftime(when, sizeof(when), "%H:%M:%S", &tmv);
    }
    char line[4096];
    g_snprintf(line, sizeof(line), "[%s] [%s] %s%s%s",
               when, lvl, domain ? domain : "", domain ? ": " : "", message ? message : "");
    write_line_unsafe(line);
    g_mutex_unlock(&g_log_mutex);

    /* Crash on ERROR only; for CRITICAL just log (we also dump a stack below). */
    // if (level & G_LOG_LEVEL_ERROR) {
    // #ifdef G_OS_UNIX
    //     raise(SIGABRT);
    // #else
    //     abort();
    // #endif
    //     return;
    // }

    /* Optional: dump a best-effort stack for CRITICAL without exiting */
    if (level & G_LOG_LEVEL_CRITICAL) {
    #ifdef G_OS_WIN32
        void* stack[64]; USHORT frames = CaptureStackBackTrace(0, G_N_ELEMENTS(stack), stack, NULL);
        FILE *f = fopen(g_log_path, "ab");
        if (f) {
            fprintf(f, "\r\n[stack] CRITICAL backtrace:\r\n");
            for (USHORT i = 0; i < frames; ++i) fprintf(f, "  [%02u] %p\r\n", i, stack[i]);
            fclose(f);
        }
    #else
    #ifdef __linux__
        void* bt[64]; int n = backtrace(bt, 64);
        backtrace_symbols_fd(bt, n, g_log_fd);
    #endif
    #endif
    }

}

/* ------------------------------------------------------------------------- */
/* POSIX crash handler                                                        */
/* ------------------------------------------------------------------------- */
#ifdef G_OS_UNIX
static void posix_signal_handler(int sig, siginfo_t *si, void *uap) {
    (void)uap;
    char hdr[256];
    g_snprintf(hdr, sizeof(hdr),
               "\n=== CRASH (signal %d) === addr=%p ===", sig, si ? si->si_addr : NULL);
    write_line_unsafe(hdr);

#ifdef __linux__
    void *bt[64];
    int n = backtrace(bt, 64);
    write_line_unsafe("Backtrace:");
    /* backtrace_symbols_fd is async-signal-friendly enough for crash dumps */
    backtrace_symbols_fd(bt, n, g_log_fd);
#else
    write_line_unsafe("Backtrace not available on this UNIX; rebuild on Linux/macOS.");
#endif

    write_line_unsafe("=== END CRASH ===");

    /* Restore default and re-raise so debuggers can catch it too */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_posix_handlers(void) {
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posix_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    int sigs[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL,
#ifdef SIGBUS
                   SIGBUS,
#endif
                 };
    for (guint i = 0; i < G_N_ELEMENTS(sigs); ++i)
        sigaction(sigs[i], &sa, NULL);
}
#endif

/* ------------------------------------------------------------------------- */
/* Windows crash handler                                                      */
/* ------------------------------------------------------------------------- */
#ifdef G_OS_WIN32
static void write_windows_stacktrace(EXCEPTION_POINTERS *ep) {
    HANDLE hProc = GetCurrentProcess();
    SymInitialize(hProc, NULL, TRUE);

    void* stack[62];
    USHORT frames = CaptureStackBackTrace(0, G_N_ELEMENTS(stack), stack, NULL);

    FILE *f = fopen(g_log_path, "ab");
    if (f) {
        fprintf(f, "\r\nBacktrace (Windows):\r\n");
        for (USHORT i = 0; i < frames; ++i) {
            DWORD64 addr = (DWORD64)(stack[i]);
            char buf[sizeof(SYMBOL_INFO) + 256];
            PSYMBOL_INFO sym = (PSYMBOL_INFO)buf;
            memset(sym, 0, sizeof(buf));
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;

            DWORD64 disp = 0;
            if (SymFromAddr(hProc, addr, &disp, sym)) {
                fprintf(f, "  %02u: %s + 0x%llx [0x%llx]\r\n",
                        i, sym->Name, (unsigned long long)disp, (unsigned long long)addr);
            } else {
                fprintf(f, "  %02u: 0x%llx\r\n", i, (unsigned long long)addr);
            }
        }
        fflush(f);
        fclose(f);
    }

    /* Minidump */
    HANDLE hFile = CreateFileA(g_dmp_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                          &mei, NULL, NULL);
        CloseHandle(hFile);
    }
}

static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep) {
    FILE *f = fopen(g_log_path, "ab");
    if (f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "\r\n=== CRASH (Windows) === code=0x%08lx at %p ===\r\n",
                ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        fprintf(f, "time=%04d-%02d-%02d %02d:%02d:%02d\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fclose(f);
    }

    write_windows_stacktrace(ep);

    f = fopen(g_log_path, "ab");
    if (f) { fprintf(f, "=== END CRASH ===\r\n"); fclose(f); }

    /* Terminate so CI or shells see a failure code */
    ExitProcess((UINT)ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER; /* not reached */
}

static void install_windows_handler(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled_filter);
}
#endif

/* ------------------------------------------------------------------------- */
/* API                                                                        */
/* ------------------------------------------------------------------------- */
void backlogger_init(const char *log_dir, const char *app_name) {
    g_return_if_fail(log_dir && *log_dir && app_name && *app_name);

    ensure_dir(log_dir);
    open_log_files(log_dir, app_name);

    /* header */
    gchar *ts = timestamp_now();
    g_header = g_strdup_printf("=== %s start %s ===", app_name, ts);
    g_free(ts);
    write_line_unsafe(g_header);

    /* route GLib/GTK messages to file */
    g_log_set_default_handler(glib_log_to_file, NULL);

#ifdef G_OS_UNIX
    install_posix_handlers();
#elif defined(G_OS_WIN32)
    install_windows_handler();
#endif
}
