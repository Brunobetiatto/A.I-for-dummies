#include "cmd_runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BOOL DEBUG = FALSE;

#define SECONDS(x) (x * 1000)

DWORD update_cwd(DWORD buff_size, WCHAR *cwd) {
    FreeConsole();
    BOOL success = AllocConsole();
    if (success) {
        FILE* pConsole;
        freopen_s(&pConsole, "CONOUT$", "w", stdout); 
        freopen_s(&pConsole, "CONIN$", "r", stdin);   
        freopen_s(&pConsole, "CONERR$", "w", stderr); 
        
        HWND hWnd = GetConsoleWindow();
        ShowWindow(hWnd, DEBUG);

        for (;;) {
            GetCurrentDirectoryW(buff_size, cwd);

            WCHAR cmd[MAX_PATH * 2];
            
            if (build_cmd_from_cwd(cwd, cmd, sizeof(cmd))) {
                run_cmd_out(cwd);
            } else {
                wprintf(L"[ERROR] Problema encontrado construindo comando \"%ls\" a partir do diretorio (CWD) \"%ls\"!\n\nAbortando...\n",
                        cmd, cwd);
                Sleep(SECONDS(5));
                exit(EXIT_FAILURE);
            }

            if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                return 0;
            }
        }
        
        FreeConsole();
    } else {
        MessageBoxW(NULL, 
            L"Falha ao alocar console!", 
            L"Erro", 
            MB_OK | MB_ICONERROR);
    }
}

BOOL build_cmd_from_cwd(WCHAR *cwd, WCHAR *cmd, DWORD cmd_size) {
    const WCHAR 
    *call = L"py",
    *file = L"main.py",
    *argv = L"";

    if (_snwprintf_s(cmd, cmd_size, _TRUNCATE, 
        L"%s \"%ls\\%s\" %s", 
        call, cwd, file, argv) < 0) {
        return FALSE;
    }

    return TRUE;
}


wchar_t *run_cmd_out(const wchar_t *cmd) {
    FILE *fp = _wpopen(cmd, L"rt"); 
    if (!fp) {
        fwprintf(stderr, L"Erro ao executar comando.\n");
        return NULL;
    }

    wchar_t *result = malloc(1 * sizeof(wchar_t));
    result[0] = L'\0';
    size_t total_len = 0;

    char buf8[1024];
    wchar_t buffer[1024];

    while (fgets(buf8, sizeof(buf8), fp) != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, buf8, -1, buffer, _countof(buffer));

        size_t chunk_len = wcslen(buffer);
        result = realloc(result, (total_len + chunk_len + 1) * sizeof(wchar_t));
        wcscpy(result + total_len, buffer);
        total_len += chunk_len;
    }

    _pclose(fp);
    return result;
}


