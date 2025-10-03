#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <gtk/gtk.h>
#include "../interface/debug_window.h"

struct ResponseData {
    char *data;
    size_t size;
};



bool api_list_tables(char **response);
bool api_dump_table(const char *table_name, char **response);
bool api_forgot_password(const char *email, char **response);
bool api_verify_reset_code(const char *email, const char *code, char **response);
bool api_reset_password(const char *reset_token, const char *new_password, char **response);
bool api_describe_table(const char *table_name, char **response);
bool api_create_user(const char *nome, const char *email, const char *password, char **response);
bool api_login(const char *email, const char *password, char **response);
bool api_get_user_by_id(int user_id, char **response);
bool api_get_user_datasets(int user_id, char **response);



char* process_api_response(const char *api_response);   // processa a resposta da API e formata para o main.c
WCHAR* run_api_command(const WCHAR *command);           // executa comandos via API



size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct ResponseData *mem = (struct ResponseData *)userp;
    
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if(!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    
    return realsize;
}

char* api_request(const char *method, const char *endpoint, const char *data) {
    CURL *curl;
    CURLcode res;
    struct ResponseData chunk;

    chunk.data = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        char url[256];
        snprintf(url, sizeof(url), "http://localhost:5000%s", endpoint);

        // 🔹 LOG de saída
        debug_log(">> API_REQUEST: %s %s", method, url);
        if (data) debug_log(">> Payload: %s", data);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        if(strcmp(method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if(data) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            }
        }

        // Set headers for JSON
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            debug_log("!! curl_easy_perform() failed: %s", curl_easy_strerror(res));
            free(chunk.data);
            chunk.data = NULL;
        } else {
            // 🔹 LOG de entrada (resposta bruta)
            debug_log("<< API_RESPONSE: %s", chunk.data ? chunk.data : "(null)");
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return chunk.data;
}


// Funções para substituir as antigas funcionalidades
bool api_list_tables(char **response) {
    *response = api_request("GET", "/tables", NULL);
    return (*response != NULL);
}

bool api_dump_table(const char *table_name, char **response) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/table/%s", table_name);
    *response = api_request("GET", endpoint, NULL);
    return (*response != NULL);
}

// Adicione estas funções para os novos endpoints de recuperação de senha
bool api_forgot_password(const char *email, char **response) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "email", email);
    
    char *data = cJSON_PrintUnformatted(json);
    *response = api_request("POST", "/forgot-password", data);
    
    cJSON_Delete(json);
    free(data);
    return (*response != NULL);
}

bool api_verify_reset_code(const char *email, const char *code, char **response) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "email", email);
    cJSON_AddStringToObject(json, "code", code);
    
    char *data = cJSON_PrintUnformatted(json);
    *response = api_request("POST", "/verify-reset-code", data);
    
    cJSON_Delete(json);
    free(data);
    return (*response != NULL);
}

bool api_reset_password(const char *reset_token, const char *new_password, char **response) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "reset_token", reset_token);
    cJSON_AddStringToObject(json, "new_password", new_password);
    
    char *data = cJSON_PrintUnformatted(json);
    *response = api_request("POST", "/reset-password", data);
    
    cJSON_Delete(json);
    free(data);
    return (*response != NULL);
}

bool api_describe_table(const char *table_name, char **response) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/schema/%s", table_name);
    *response = api_request("GET", endpoint, NULL);
    return (*response != NULL);
}

bool api_create_user(const char *nome, const char *email, const char *password, char **response) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "nome", nome);
    cJSON_AddStringToObject(json, "email", email);
    cJSON_AddStringToObject(json, "password", password);
    
    char *data = cJSON_PrintUnformatted(json);
    *response = api_request("POST", "/user", data);
    
    cJSON_Delete(json);
    free(data);
    return (*response != NULL);
}

bool api_login(const char *email, const char *password, char **response) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "email", email);
    cJSON_AddStringToObject(json, "password", password);
    
    char *data = cJSON_PrintUnformatted(json);
    *response = api_request("POST", "/login", data);
    
    cJSON_Delete(json);
    free(data);
    return (*response != NULL);
}

// Implementação:
bool api_get_user_by_id(int user_id, char **response) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/user/%d", user_id);
    *response = api_request("GET", endpoint, NULL);
    return (*response != NULL);
}

bool api_get_user_datasets(int user_id, char **response) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/user/%d/datasets", user_id);
    *response = api_request("GET", endpoint, NULL);
    return (*response != NULL);
}

char* process_api_response(const char *api_response) {
    cJSON *json = cJSON_Parse(api_response);
    if (!json) {
        return strdup("ERR Invalid JSON response\n");
    }
    
    cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
    if (!cJSON_IsString(status)) {
        cJSON_Delete(json);
        return strdup("ERR No status in response\n");
    }
    
    // aqui processamos as respostas corretas dos arquivos recebidos 
    // a partir da resposta da API, em formato JSON. 
    if (strcmp(status->valuestring, "OK") == 0) {
        cJSON *tables   = cJSON_GetObjectItemCaseSensitive(json, "tables");
        cJSON *columns  = cJSON_GetObjectItemCaseSensitive(json, "columns");
        cJSON *data     = cJSON_GetObjectItemCaseSensitive(json, "data");
        cJSON *user     = cJSON_GetObjectItemCaseSensitive(json, "user");
        cJSON *schema   = cJSON_GetObjectItemCaseSensitive(json, "schema");
        
        if (tables && cJSON_IsArray(tables)) {
            // List tables response
            GString *result = g_string_new("OK ");
            cJSON *table;
            cJSON_ArrayForEach(table, tables) {
                if (cJSON_IsString(table)) {
                    g_string_append(result, table->valuestring);
                    g_string_append_c(result, ',');
                }
            }
            // Remove trailing comma
            if (result->len > 3) {
                result->str[result->len - 1] = '\0';
            }
            g_string_append(result, "\n\x04\n");
            cJSON_Delete(json);
            return g_string_free(result, FALSE);
        }
        else if (columns && data && cJSON_IsArray(columns) && cJSON_IsArray(data)) {
            // Table data response
            GString *result = g_string_new("OK");
            
            // Add columns
            cJSON *column;
            int col_count = 0;
            cJSON_ArrayForEach(column, columns) {
                if (col_count > 0) g_string_append_c(result, ',');
                g_string_append(result, column->valuestring);
                col_count++;
            }
            g_string_append_c(result, '\n');
            
            // Add data rows
            cJSON *row;
            cJSON_ArrayForEach(row, data) {
                if (cJSON_IsArray(row)) {
                    cJSON *cell;
                    int cell_count = 0;
                    cJSON_ArrayForEach(cell, row) {
                        if (cell_count > 0) g_string_append_c(result, ',');
                        if (cJSON_IsString(cell)) {
                            g_string_append(result, cell->valuestring);
                        } else if (cJSON_IsNumber(cell)) {
                            char num_str[32];
                            snprintf(num_str, sizeof(num_str), "%d", cell->valueint);
                            g_string_append(result, num_str);
                        }
                        cell_count++;
                    }
                    g_string_append_c(result, '\n');
                }
            }
            g_string_append(result, "\x04\n");
            cJSON_Delete(json);
            return g_string_free(result, FALSE);
        }
        else if (user && cJSON_IsObject(user)) {
            // User response
            cJSON *id = cJSON_GetObjectItemCaseSensitive(user, "id");
            cJSON *nome = cJSON_GetObjectItemCaseSensitive(user, "nome");
            cJSON *email = cJSON_GetObjectItemCaseSensitive(user, "email");
            
            char *result = malloc(256);
            snprintf(result, 256, "OK %d|%s|%s\n\x04\n", 
                    id->valueint, nome->valuestring, email->valuestring);
            cJSON_Delete(json);
            return result;
        }
        else if (cJSON_GetObjectItemCaseSensitive(json, "reset_token")) {
            // Verify reset code response
            cJSON *reset_token = cJSON_GetObjectItemCaseSensitive(json, "reset_token");
            if (cJSON_IsString(reset_token)) {
                char *result = malloc(strlen(reset_token->valuestring) + 10);
                snprintf(result, strlen(reset_token->valuestring) + 10, "OK %s\n\x04\n", reset_token->valuestring);
                cJSON_Delete(json);
                return result;
            }
        }
        else if (cJSON_GetObjectItemCaseSensitive(json, "message")) {
            // Simple success message response
            cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "message");
            if (cJSON_IsString(message)) {
                char *result = malloc(strlen(message->valuestring) + 10);
                snprintf(result, strlen(message->valuestring) + 10, "OK %s\n\x04\n", message->valuestring);
                cJSON_Delete(json);
                return result;
            }
        }
        else if (schema && cJSON_IsArray(schema)) {
            // Schema response
            GString *result = g_string_new("OK Field,Type,Null,Key,Default,Extra\n");
            
            cJSON *field;
            cJSON_ArrayForEach(field, schema) {
                if (cJSON_IsObject(field)) {
                    cJSON *field_name = cJSON_GetObjectItemCaseSensitive(field, "Field");
                    cJSON *type = cJSON_GetObjectItemCaseSensitive(field, "Type");
                    cJSON *null = cJSON_GetObjectItemCaseSensitive(field, "Null");
                    cJSON *key = cJSON_GetObjectItemCaseSensitive(field, "Key");
                    cJSON *default_val = cJSON_GetObjectItemCaseSensitive(field, "Default");
                    cJSON *extra = cJSON_GetObjectItemCaseSensitive(field, "Extra");
                    
                    g_string_append_printf(result, "%s,%s,%s,%s,%s,%s\n",
                                          field_name->valuestring,
                                          type->valuestring,
                                          null->valuestring,
                                          key->valuestring,
                                          default_val ? default_val->valuestring : "",
                                          extra->valuestring);
                }
            }
            g_string_append(result, "\x04\n");
            cJSON_Delete(json);
            return g_string_free(result, FALSE);
        }
    }
    else {
        // Error response
        cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "message");
        if (cJSON_IsString(message)) {
            char *result = malloc(strlen(message->valuestring) + 10);
            snprintf(result, strlen(message->valuestring) + 10, "ERR %s\n\x04\n", message->valuestring);
            cJSON_Delete(json);
            return result;
        }
    }
    
    cJSON_Delete(json);
    return strdup("ERR Unknown response format\n\x04\n");
}

/* wrapper que converte UTF-8 -> WCHAR, chama run_api_command, converte de volta */


WCHAR* run_api_command(const WCHAR *command) {
    // Convert command to UTF-8
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, command, -1, NULL, 0, NULL, NULL);
    char *utf8_command = malloc(utf8_size);
    WideCharToMultiByte(CP_UTF8, 0, command, -1, utf8_command, utf8_size, NULL, NULL);
    
    // Parse command
    char *token = strtok(utf8_command, " ");
    char *response = NULL;
    
    if (token && strcmp(token, "LIST") == 0) {
        api_list_tables(&response);
    }
    else if (token && strcmp(token, "DUMP") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            api_dump_table(token, &response);
        }
    }
    else if (token && strcmp(token, "SCHEMA") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            api_describe_table(token, &response);
        }
    }
    else if (token && strcmp(token, "CREATE_USER") == 0) {
        // Parse JSON data from command
        token = strtok(NULL, "");
        if (token) {
            cJSON *json = cJSON_Parse(token);
            if (json) {
                cJSON *nome = cJSON_GetObjectItemCaseSensitive(json, "nome");
                cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
                cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
                
                if (cJSON_IsString(nome) && cJSON_IsString(email) && cJSON_IsString(password)) {
                    api_create_user(nome->valuestring, email->valuestring, password->valuestring, &response);
                }
                cJSON_Delete(json);
            }
        }
    }
    else if (token && strcmp(token, "LOGIN") == 0) {
        // Parse JSON data from command
        token = strtok(NULL, "");
        if (token) {
            cJSON *json = cJSON_Parse(token);
            if (json) {
                cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
                cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
                
                if (cJSON_IsString(email) && cJSON_IsString(password)) {
                    api_login(email->valuestring, password->valuestring, &response);
                }
                cJSON_Delete(json);
            }
        }
    }
    else if (token && strcmp(token, "FORGOT_PASSWORD") == 0) {
        // Parse JSON data from command
        token = strtok(NULL, "");
        if (token) {
            cJSON *json = cJSON_Parse(token);
            if (json) {
                cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
                
                if (cJSON_IsString(email)) {
                    api_forgot_password(email->valuestring, &response);
                }
                cJSON_Delete(json);
            }
        }
    }
    else if (token && strcmp(token, "VERIFY_RESET_CODE") == 0) {
        // Parse JSON data from command
        token = strtok(NULL, "");
        if (token) {
            cJSON *json = cJSON_Parse(token);
            if (json) {
                cJSON *email = cJSON_GetObjectItemCaseSensitive(json, "email");
                cJSON *code = cJSON_GetObjectItemCaseSensitive(json, "code");
                
                if (cJSON_IsString(email) && cJSON_IsString(code)) {
                    api_verify_reset_code(email->valuestring, code->valuestring, &response);
                }
                cJSON_Delete(json);
            }
        }
    }
    else if (token && strcmp(token, "RESET_PASSWORD") == 0) {
        // Parse JSON data from command
        token = strtok(NULL, "");
        if (token) {
            cJSON *json = cJSON_Parse(token);
            if (json) {
                cJSON *reset_token = cJSON_GetObjectItemCaseSensitive(json, "reset_token");
                cJSON *new_password = cJSON_GetObjectItemCaseSensitive(json, "new_password");
                
                if (cJSON_IsString(reset_token) && cJSON_IsString(new_password)) {
                    api_reset_password(reset_token->valuestring, new_password->valuestring, &response);
                }
                cJSON_Delete(json);
            }
        }
    }

    if (token && strcmp(token, "GET_USER_JSON") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int uid = atoi(token);
            if (api_get_user_by_id(uid, &response)) {
                // convert raw JSON (response) to WCHAR and return directly
                int wchar_size = MultiByteToWideChar(CP_UTF8, 0, response, -1, NULL, 0);
                WCHAR *wchar_response = malloc(wchar_size * sizeof(WCHAR));
                MultiByteToWideChar(CP_UTF8, 0, response, -1, wchar_response, wchar_size);
                free(response);
                free(utf8_command);
                return wchar_response; // caller must free
            }
        }
        free(utf8_command);
        return NULL;
    }

    if (token && strcmp(token, "GET_USER_DATASETS_JSON") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int uid = atoi(token);
            if (api_get_user_datasets(uid, &response)) {
                int wchar_size = MultiByteToWideChar(CP_UTF8, 0, response, -1, NULL, 0);
                WCHAR *wchar_response = malloc(wchar_size * sizeof(WCHAR));
                MultiByteToWideChar(CP_UTF8, 0, response, -1, wchar_response, wchar_size);
                free(response);
                free(utf8_command);
                return wchar_response;
            }
        }
        free(utf8_command);
        return NULL;
    }
    
    free(utf8_command);
    
    if (response) {
        // Process API response
        char *formatted_response = process_api_response(response);
        free(response);
        
        // Convert to WCHAR
        int wchar_size = MultiByteToWideChar(CP_UTF8, 0, formatted_response, -1, NULL, 0);
        WCHAR *wchar_response = malloc(wchar_size * sizeof(WCHAR));
        MultiByteToWideChar(CP_UTF8, 0, formatted_response, -1, wchar_response, wchar_size);
        
        free(formatted_response);
        return wchar_response;
    }
    
    return NULL;
}