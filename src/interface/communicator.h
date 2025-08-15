// communicator.h

#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32

#include <Windows.h>

#elif _MAC || _UNIX32

#define GetCurrentDirectory getcwd 

#else

#warning "Sistema operacional não reconhecido!"

exit(EXIT_FAILURE)

#endif

// flag: --DEBUG
char *DEBUG_FLAG    = "--DEBUG";
BOOL DEBUG          = FALSE;

// conecta a construção do comando (build_cmd_from_cwd)
// com a execução e leitura do PIPE juntos para produzir
// um bash continuamente atualizado e consistente com as
// informações presentes na base de dados no arquivo .PY.
DWORD 
connect_py_C(
    DWORD buff_size,
    WCHAR *cwd
);

// apenas para testes. feito para gravar a informação
// de um PIPE (console aberto onde se está executando
// instruções do CMD) em um arquivo. proof-of-concept
// que demonstra que podemos gravar informações de um
// banco de dados qualquer
WCHAR
*run_cmd_out(
    const WCHAR *cwd,
    const WCHAR *cmd
);

// constrói um comando que executa, no diretório em
// que este código se encontra, um arquivo .PY
BOOL
build_cmd_from_cwd(
    WCHAR *cwd,
    WCHAR *cmd,
    DWORD cmd_size
);

#include <string.h>

// helper primariamente para a função Sleep que recebe um DWORD 
// representando millisegundos. apenas para legibilidade do
// código
#define SECONDS(x) (x * 1000)

DWORD connect_py_C(DWORD buff_size, WCHAR *cwd){
    FreeConsole();
    BOOL success = AllocConsole();
    if (success) {
        FILE* pConsole;
        freopen_s(&pConsole, "CONOUT$", "w", stdout); 
        freopen_s(&pConsole, "CONIN$",  "r", stdin);   
        freopen_s(&pConsole, "CONERR$", "w", stderr); 
        
        //////////////////////////////////
        HWND hWnd = GetConsoleWindow();
        ShowWindow(hWnd, DEBUG);
        /////////////////////////////////

        for (;;){
            GetCurrentDirectoryW(buff_size, cwd);

            WCHAR cmd[MAX_PATH * 2];
            
            if (build_cmd_from_cwd(cwd, cmd, sizeof(cmd))){
                run_cmd_out(cwd, cmd);
            } else {
                wprintf(L"[ERROR] Problema encontrado construindo comando \"%ls\" a"
                        L"partir do diretorio (CWD) \"%ls\"!\n\nAbortando...\n",
                        cmd, cwd);

                Sleep(SECONDS(5));

                exit(EXIT_FAILURE);
            }

            if ((GetAsyncKeyState(VK_RETURN) & 0x8000) 
                && DEBUG){
                return 0;
            }
        }
        
        FreeConsole();
    } else {
        MessageBoxW(NULL, 
            L"Falha ao alocar console!", 
            L"Erro", 
            MB_OK | MB_ICONERROR);
        
        return -1;
    }
    return 1;
}

BOOL build_cmd_from_cwd(WCHAR *cwd, WCHAR *cmd, DWORD cmd_size){
    const WCHAR 
    *call = L"py",
    *file = L"./python/connectors/db_connector.py",
    *argv = L"";

    if (_snwprintf_s(cmd, cmd_size, _TRUNCATE, 
        L"%ls \"%ls\\%ls\" %ls", 
        call, cwd, file, argv) < 0) {
        return FALSE;
    }

    return TRUE;
}

WCHAR *run_cmd_out(const WCHAR *cwd, const WCHAR *cmd){
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return NULL;

    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    WCHAR *cmdline = _wcsdup(cmd);
    DWORD flags = CREATE_NO_WINDOW; // <- key bit

    BOOL ok = CreateProcessW(
        NULL,                 // application (NULL => from cmdline)
        cmdline,              // command line (writable)
        NULL, NULL,           // process/thread security
        TRUE,                 // inherit handles (for our pipe)
        flags,                // no window
        NULL,                 // environment
        (cwd && *cwd) ? cwd : NULL, // current directory
        &si, &pi
    );
    free(cmdline);

    CloseHandle(hWrite);

    if (!ok) { CloseHandle(hRead); return NULL; }

    BYTE  buf[4096];
    DWORD got = 0;
    size_t cap = 0, len = 0;
    char *acc = NULL;

    for (;;) {
        BOOL r = ReadFile(hRead, buf, sizeof(buf), &got, NULL);
        if (!r || got == 0) break;
        if (len + got + 1 > cap) {
            cap = (len + got + 1) * 2 + 1024;
            acc = (char*)realloc(acc, cap);
        }
        memcpy(acc + len, buf, got);
        len += got;
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!acc) {
        WCHAR *empty = (WCHAR*)calloc(1, sizeof(WCHAR));
        return empty;
    }
    acc[len] = '\0';

    int wlen = MultiByteToWideChar(CP_UTF8, 0, acc, (int)len, NULL, 0);
    WCHAR *wout = (WCHAR*)malloc((wlen + 1) * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, acc, (int)len, wout, wlen);
    wout[wlen] = 0;
    free(acc);
    return wout;
}





typedef struct {
    PROCESS_INFORMATION pi;
    HANDLE hStdoutRd;  // parent reads
    HANDLE hStdinWr;   // parent writes
    BOOL   running;
} BackendProc;

static BackendProc g_backend = {0};

static WCHAR* build_worker_cmdline(const WCHAR *db, const WCHAR *user, const WCHAR *pass,
                                   const WCHAR *host, int port)
{
    WCHAR cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);

    WCHAR portw[16];
    _snwprintf(portw, _countof(portw), L"%d", port);

    size_t cap = 4096;
    WCHAR *cmd = (WCHAR*)calloc(cap, sizeof(WCHAR));
    _snwprintf(cmd, cap-1,
        L"python \"%ls\\python\\connectors\\db_connector.py\" "
        L"--user \"%ls\" --password \"%ls\" --db \"%ls\" --host \"%ls\" --port %ls --serve",
        cwd, user, pass, db, host, portw);
    return cmd;
}

static BOOL read_line_until(HANDLE h, const char *needle, DWORD timeout_ms) {
    // Read until a line equal to needle is seen. For READY\n on boot.
    char buf[1];
    GString *line = g_string_new(NULL);
    DWORD start = GetTickCount();
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, 1, &got, NULL) || got == 0) {
            Sleep(1);
        } else {
            line = g_string_append_c(line, buf[0]);
            if (buf[0] == '\n') {
                if (strcmp(line->str, needle) == 0) { g_string_free(line, TRUE); return TRUE; }
                g_string_truncate(line, 0);
            }
        }
        if (timeout_ms && GetTickCount() - start > timeout_ms) {
            g_string_free(line, TRUE);
            return FALSE;
        }
    }
}

BOOL backend_start(const WCHAR *db, const WCHAR *user, const WCHAR *pass,
                   const WCHAR *host, int port)
{
    if (g_backend.running) return TRUE;

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    HANDLE childStdoutRd = NULL, childStdoutWr = NULL;
    HANDLE childStdinRd  = NULL, childStdinWr  = NULL;

    // stdout pipe (child writes, parent reads)
    if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0)) return FALSE;
    SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0);

    // stdin pipe (parent writes, child reads)
    if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0)) {
        CloseHandle(childStdoutRd); CloseHandle(childStdoutWr); return FALSE;
    }
    SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = childStdoutWr;
    si.hStdError  = childStdoutWr;
    si.hStdInput  = childStdinRd;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    WCHAR *cmdline = build_worker_cmdline(db, user, pass, host, port);
    BOOL ok = CreateProcessW(
        NULL, cmdline, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi
    );
    free(cmdline);

    // Close child-side pipe ends in parent
    CloseHandle(childStdoutWr);
    CloseHandle(childStdinRd);

    if (!ok) {
        CloseHandle(childStdoutRd); CloseHandle(childStdinWr);
        return FALSE;
    }

    g_backend.pi = pi;
    g_backend.hStdoutRd = childStdoutRd;
    g_backend.hStdinWr  = childStdinWr;
    g_backend.running   = TRUE;

    // Wait for READY\n
    if (!read_line_until(g_backend.hStdoutRd, "READY\n", 10000)) {
        // Failed to initialize — cleanup
        TerminateProcess(g_backend.pi.hProcess, 1);
        CloseHandle(g_backend.hStdoutRd);
        CloseHandle(g_backend.hStdinWr);
        CloseHandle(g_backend.pi.hThread);
        CloseHandle(g_backend.pi.hProcess);
        ZeroMemory(&g_backend, sizeof(g_backend));
        return FALSE;
    }

    return TRUE;
}

// Sends a command (must end with '\n' or we'll add it) and reads until <EOT>\n (0x04 '\n')
char* backend_request(const char *req_utf8, size_t *out_len_opt) {
    if (!g_backend.running) return NULL;

    // ensure newline
    GString *line = g_string_new(req_utf8);
    if (line->len == 0 || line->str[line->len-1] != '\n') g_string_append_c(line, '\n');

    DWORD written = 0;
    WriteFile(g_backend.hStdinWr, line->str, (DWORD)line->len, &written, NULL);
    g_string_free(line, TRUE);

    // read until EOT "\x04\n"
    GString *acc = g_string_new(NULL);
    char buf[4096];
    DWORD got = 0;
    for (;;) {
        if (!ReadFile(g_backend.hStdoutRd, buf, sizeof(buf), &got, NULL) || got == 0) break;
        g_string_append_len(acc, buf, got);
        if (acc->len >= 2 && acc->str[acc->len-2] == '\x04' && acc->str[acc->len-1] == '\n') {
            acc->str[acc->len-2] = '\0'; // strip <EOT>
            acc->len -= 2;
            break;
        }
    }
    if (out_len_opt) *out_len_opt = acc->len;
    return g_string_free(acc, FALSE); // returns malloc'd char*
}

void backend_stop(void) {
    if (!g_backend.running) return;
    DWORD written = 0;
    const char *quit = "QUIT\n";
    WriteFile(g_backend.hStdinWr, quit, (DWORD)strlen(quit), &written, NULL);

    WaitForSingleObject(g_backend.pi.hProcess, 2000);
    CloseHandle(g_backend.hStdinWr);
    CloseHandle(g_backend.hStdoutRd);
    CloseHandle(g_backend.pi.hThread);
    CloseHandle(g_backend.pi.hProcess);
    ZeroMemory(&g_backend, sizeof(g_backend));
}