// main.c

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

// atualizar as informações do diretório CWD
DWORD 
update_cwd(
    DWORD buff_size,
    WCHAR *cwd
);

BOOL 
run_cmd_out(
    WCHAR *cwd,
    WCHAR *cmd
);

BOOL
build_cmd_from_cwd(
    WCHAR *cwd,
    WCHAR *cmd,
    DWORD cmd_size
);

int
parse_and_read(
    WCHAR *cwd
);

#include <string.h>

int main(int argc, char *argv[]){
    DEBUG = strncmp(argv[argc - 1], DEBUG_FLAG, 8) == 0
            ? TRUE 
            : FALSE;

    WCHAR cwd[MAX_PATH];

    update_cwd(sizeof(cwd), cwd);

    return 0;
}

// helper primariamente para a função Sleep que recebe um DWORD 
// representando millisegundos. apenas para legibilidade do
// código
#define SECONDS(x) (x * 1000)

DWORD update_cwd(DWORD buff_size, WCHAR *cwd){
    FreeConsole();
    BOOL success = AllocConsole();
    if (success) {
        FILE* pConsole;
        freopen_s(&pConsole, "CONOUT$", "w", stdout); 
        freopen_s(&pConsole, "CONIN$", "r", stdin);   
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
                wprintf(L"[ERROR] Problema encontrado construindo comando \"%ls\" a partir do diretorio (CWD) \"%ls\"!\n\nAbortando...\n",
                        cmd, cwd);

                Sleep(SECONDS(5));

                exit(EXIT_FAILURE);
            }

            if (GetAsyncKeyState(VK_RETURN) & 0x8000){
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

BOOL build_cmd_from_cwd(WCHAR *cwd, WCHAR *cmd, DWORD cmd_size){
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


BOOL run_cmd_out(WCHAR *cwd, WCHAR *cmd){
    FILE *fp, *fw;
    WCHAR buffer[1024], cwdBuffer[1024];;

    fp = _wpopen(cmd, L"rt");
    if (fp == NULL) {
        perror("Erro ao rodar CMD");
        exit(EXIT_FAILURE);
    }

    fw = _wfopen(cwdBuffer, L"w");
    if (fw == NULL) {
        perror("Erro ao criar arquivo .txt");
        exit(EXIT_FAILURE);
    }

    if (_snwprintf_s(cwdBuffer, _countof(cwdBuffer), _TRUNCATE,
            L"%ls\\novo.txt", 
            cwd) < 0) {
            return FALSE;
        }

    while (fgetws(buffer, _countof(buffer), fp) != NULL) {
        fwprintf(fw, L"%s", buffer);
    }

    fflush(fw);
    _pclose(fp); 
    fclose(fw);

    return TRUE;
}