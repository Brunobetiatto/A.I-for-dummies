#ifndef CMD_RUNNER_H
#define CMD_RUNNER_H
#include <windows.h>
#include <wchar.h>

// Define um tipo booleano compatível
#ifndef BOOL
#define BOOL int
#endif

DWORD update_cwd(DWORD buff_size, WCHAR *cwd);
wchar_t *run_cmd_out(const wchar_t *cmd);
BOOL build_cmd_from_cwd(WCHAR *cwd, WCHAR *cmd, DWORD cmd_size);


#endif
