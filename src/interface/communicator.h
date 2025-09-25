#ifndef COMMUNICATOR_H
#define COMMUNICATOR_H

#include <stdbool.h>  // Para bool
#include <wchar.h>
#include <windows.h> // Para DWORD

// Declarações consistentes com as definições
bool api_list_tables(char **response);
bool api_dump_table(const char *table_name, char **response);
bool api_forgot_password(const char *email, char **response);
bool api_verify_reset_code(const char *email, const char *code, char **response);
bool api_reset_password(const char *reset_token, const char *new_password, char **response);
bool api_describe_table(const char *table_name, char **response);
bool api_create_user(const char *nome, const char *email, const char *password, char **response);
bool api_login(const char *email, const char *password, char **response);

// Corrigir a declaração de connect_to_api para combinar com a definição
DWORD connect_to_api(DWORD buff_size, WCHAR *cwd);  // Se esta for a assinatura real
WCHAR* run_api_command(const WCHAR* command);

#endif