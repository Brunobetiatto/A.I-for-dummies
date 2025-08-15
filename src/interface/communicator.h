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
