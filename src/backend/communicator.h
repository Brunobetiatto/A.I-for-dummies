#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h> 
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <gtk/gtk.h>
#include "../interface/debug_window.h"

#ifndef COMMUNICATOR_H
#define COMMUNICATOR_H

struct ResponseData {
    char *data;
    size_t size;
};

#define BASE_URL "http://localhost:5000/uploads"
#define DATASET_DIR "datasets"

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
bool api_get_dataset_by_name_(const char *filename, char **response);

static char *g_auth_token = NULL;


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

void communicator_set_token(const char *token) {
    if (g_auth_token) { free(g_auth_token); g_auth_token = NULL; }
    if (token && *token) {
        g_auth_token = strdup(token);
    }
}

void communicator_clear_token(void) {
    if (g_auth_token) { free(g_auth_token); g_auth_token = NULL; }
}

/* Provide accessor for api_request to use */
const char* communicator_get_token(void) {
    return g_auth_token;
}

/* api_request: faz request HTTP e retorna resposta (malloc'd) ou NULL.
   Usa g_auth_token se presente para enviar Authorization: Bearer <token>
*/
char* api_request(const char *method, const char *endpoint, const char *data) {
    CURL *curl;
    CURLcode res;
    struct ResponseData chunk;
    struct curl_slist *headers = NULL;

    chunk.data = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl) {
        debug_log("!! api_request: curl_easy_init() failed");
        free(chunk.data);
        curl_global_cleanup();
        return NULL;
    }

    char url[1024];
    snprintf(url, sizeof(url), "http://localhost:5000%s", endpoint);

    debug_log(">> API_REQUEST: %s %s", method, url);
    if (data) debug_log(">> Payload: %s", data);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    if (g_strcmp0(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (data) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(data));
        }
    }

    /* headers: Content-Type JSON + optional Authorization */
    headers = curl_slist_append(headers, "Content-Type: application/json");

    extern char *g_auth_token; /* declarado em communicator.c/h */
    if (g_auth_token && *g_auth_token) {
        char auth_hdr[1024];
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", g_auth_token);
        debug_log(">> Header: %s", auth_hdr);
        headers = curl_slist_append(headers, auth_hdr);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* timeout and safety options (optional but recommended) */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        debug_log("!! curl_easy_perform() failed: %s", curl_easy_strerror(res));
        free(chunk.data);
        chunk.data = NULL;
    } else {
        debug_log("<< API_RESPONSE: %s", chunk.data ? chunk.data : "(null)");
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return chunk.data;
}



bool api_get_user_avatar_to_temp(int user_id, char **out_path);

/* Internal: list of temp files to cleanup at exit */
static GPtrArray *g_temp_files_array = NULL;
static gboolean g_temp_files_registered = FALSE;

static void communicator_cleanup_temp_files(void) {
    if (!g_temp_files_array) return;
    for (guint i = 0; i < g_temp_files_array->len; ++i) {
        char *p = (char*) g_temp_files_array->pdata[i];
        if (p) {
            /* Best-effort remove */
            remove(p);
            g_free(p);
        }
    }
    g_ptr_array_free(g_temp_files_array, TRUE);
    g_temp_files_array = NULL;
    g_temp_files_registered = FALSE;
}

static void communicator_register_tempfile(const char *path) {
    if (!path) return;
    if (!g_temp_files_array) {
        g_temp_files_array = g_ptr_array_new_with_free_func(g_free);
    }
    g_ptr_array_add(g_temp_files_array, g_strdup(path));
    if (!g_temp_files_registered) {
        atexit(communicator_cleanup_temp_files);
        g_temp_files_registered = TRUE;
        /* seed rand for name uniqueness */
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    }
}

/* helper: map content-type to extension */
static const char* _content_type_to_ext(const char *ct) {
    if (!ct) return ".tmp";
    if (g_strrstr(ct, "png")) return ".png";
    if (g_strrstr(ct, "jpeg") || g_strrstr(ct, "jpg")) return ".jpg";
    if (g_strrstr(ct, "gif")) return ".gif";
    if (g_strrstr(ct, "bmp")) return ".bmp";
    if (g_strrstr(ct, "svg")) return ".svg";
    if (g_strrstr(ct, "webp")) return ".webp";
    return ".tmp";
}

/* write callback to save to FILE* (used above in file download func too) */
static size_t write_file_callback_curl(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

/* Implementation: download avatar into tmp dir and return path via out_path (caller g_free) */
bool api_get_user_avatar_to_temp(int user_id, char **out_path) {
    if (!out_path) return false;
    *out_path = NULL;

    CURL *curl = NULL;
    CURLcode res;
    bool ok = false;
    FILE *fp = NULL;
    char tmp_path[1024] = {0};
    char final_path[1024] = {0};
    char url[512];
    struct curl_slist *headers = NULL;

    snprintf(url, sizeof(url), "http://localhost:5000/user/%d/avatar", user_id);

    const char *tmpdir = g_get_tmp_dir();
    if (!tmpdir) tmpdir = "/tmp";

    /* create unique temp filename (no extension initially) */
    unsigned int rnd = (unsigned int)rand();
    time_t t = time(NULL);
    snprintf(tmp_path, sizeof(tmp_path), "%s/aifd_avatar_%d_%u_%lu.tmp",
             tmpdir, user_id, rnd, (unsigned long)t);

    fp = fopen(tmp_path, "wb");
    if (!fp) {
        debug_log("api_get_user_avatar_to_temp: failed to open temp file '%s' errno=%d", tmp_path, errno);
        return false;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        remove(tmp_path);
        debug_log("api_get_user_avatar_to_temp: curl_easy_init failed");
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback_curl);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    /* set a modest timeout */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    /* perform */
    res = curl_easy_perform(curl);

    /* obtain content-type if available */
    char *content_type = NULL;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    }

    curl_easy_cleanup(curl);
    fclose(fp);

    /* headers: Content-Type JSON + optional Authorization */
    headers = curl_slist_append(headers, "Content-Type: application/json");

    extern char *g_auth_token; /* declarado em communicator.c/h */
    if (g_auth_token && *g_auth_token) {
        char auth_hdr[1024];
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", g_auth_token);
        debug_log(">> Header: %s", auth_hdr);
        headers = curl_slist_append(headers, auth_hdr);
    }

    if (res != CURLE_OK) {
        debug_log("api_get_user_avatar_to_temp: curl failed: %s", curl_easy_strerror(res));
        remove(tmp_path);
        curl_global_cleanup();
        return false;
    }

    /* determine extension */
    const char *ext = _content_type_to_ext(content_type);
    /* build final filename by replacing .tmp with ext (or appending if .tmp not present) */
    if (g_str_has_suffix(tmp_path, ".tmp")) {
        size_t base_len = strlen(tmp_path) - 4;
        snprintf(final_path, sizeof(final_path), "%.*s%s", (int)base_len, tmp_path, ext);
        /* rename file */
        if (rename(tmp_path, final_path) != 0) {
            /* rename failed: keep original tmp name */
            debug_log("api_get_user_avatar_to_temp: rename failed errno=%d, keeping %s", errno, tmp_path);
            g_snprintf(final_path, sizeof(final_path), "%s", tmp_path);
        }
    } else {
        /* just append extension */
        snprintf(final_path, sizeof(final_path), "%s%s", tmp_path, ext);
        if (rename(tmp_path, final_path) != 0) {
            /* if rename fails, try to copy fallback (rare) */
            debug_log("api_get_user_avatar_to_temp: rename append failed errno=%d", errno);
            /* leave tmp_path as-is */
            snprintf(final_path, sizeof(final_path), "%s", tmp_path);
        }
    }

    /* register file for cleanup at exit */
    communicator_register_tempfile(final_path);

    /* success: return path */
    *out_path = g_strdup(final_path);
    ok = true;

    curl_global_cleanup();
    return ok;
}

bool api_update_user_with_avatar(int user_id,
                                 const char *nome,
                                 const char *bio,
                                 const char *email,
                                 const char *avatar_path,
                                 char **response) {
    CURL *curl = NULL;
    CURLcode res;
    struct ResponseData chunk;
    char url[512];

    if (!response) return false;
    *response = NULL;

    chunk.data = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        free(chunk.data);
        curl_global_cleanup();
        return false;
    }

    /* Endpoint: POST /user/<id>/avatar */
    snprintf(url, sizeof(url), "http://localhost:5000/user/%d/avatar", user_id);
    debug_log(">> API_UPDATE_USER_AVATAR: %s", url);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* multipart/form-data */
    curl_mime *form = curl_mime_init(curl);
    if (!form) {
        debug_log("!! api_update_user_with_avatar: curl_mime_init failed");
        free(chunk.data);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }

    /* avatar file part (if provided) */
    if (avatar_path && avatar_path[0] != '\0') {
        curl_mimepart *filep = curl_mime_addpart(form);
        curl_mime_name(filep, "avatar");
        curl_mime_filedata(filep, avatar_path);
    }

    /* nome */
    if (nome && nome[0] != '\0') {
        curl_mimepart *np = curl_mime_addpart(form);
        curl_mime_name(np, "nome");
        curl_mime_data(np, nome, CURL_ZERO_TERMINATED);
    }

    /* bio */
    if (bio && bio[0] != '\0') {
        curl_mimepart *bp = curl_mime_addpart(form);
        curl_mime_name(bp, "bio");
        curl_mime_data(bp, bio, CURL_ZERO_TERMINATED);
    }

    /* email */
    if (email && email[0] != '\0') {
        curl_mimepart *ep = curl_mime_addpart(form);
        curl_mime_name(ep, "email");
        curl_mime_data(ep, email, CURL_ZERO_TERMINATED);
    }

    /* user_id (forte redundância, já está no path, mas enviar como campo também não faz mal) */
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%d", user_id);
    curl_mimepart *uidp = curl_mime_addpart(form);
    curl_mime_name(uidp, "user_id");
    curl_mime_data(uidp, uid_str, CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    /* executa */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        debug_log("!! api_update_user_with_avatar curl failed: %s", curl_easy_strerror(res));
        free(chunk.data);
        chunk.data = NULL;
    } else {
        debug_log("<< API_RESPONSE (update avatar): %s", chunk.data ? chunk.data : "(null)");
    }

    curl_mime_free(form);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    *response = chunk.data; /* caller must free */

    return (*response != NULL);
}


// Funções para substituir as antigas funcionalidades
bool api_list_tables(char **response) {
    *response = api_request("GET", "/tables", NULL);
    return (*response != NULL);
}

bool api_dump_table(const char *table_name, char **response) {
    if (!table_name || !*table_name) {
        debug_log("api_dump_table: invalid table name");
        return false;
    }

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/table/%s", table_name);

    *response = api_request("GET", endpoint, NULL);

    if (*response == NULL) {
        debug_log("api_dump_table: no response from API");
        return false;
    }

    debug_log("api_dump_table: got response");
    return true;
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

bool api_delete_user(int user_id, char **response) {
    if (user_id <= 0) return false;

    char endpoint[64];
    snprintf(endpoint, sizeof(endpoint), "/delete/%d", user_id);

    char *resp = api_request("DELETE", endpoint, NULL);

    /* se recebeu HTML "Not Found", tenta com slash final */
    if (resp && strstr(resp, "Not Found")) {
        free(resp);
        snprintf(endpoint, sizeof(endpoint), "/delete/%d/", user_id);
        resp = api_request("DELETE", endpoint, NULL);
    }

    if (!resp) {
        *response = NULL;
        return false;
    }

    *response = resp; // caller deve free()

    /* verificação rígida do JSON de sucesso */
    if (strstr(resp, "\"status\":\"OK\"") != NULL) {
        return true;
    }

    return false;
}


bool api_delete_dataset(int dataset_id, char **response) {
    if (dataset_id <= 0) return false;

    char endpoint[64];
    snprintf(endpoint, sizeof(endpoint), "/delete/dataset/%d", dataset_id);

    char *resp = api_request("DELETE", endpoint, NULL);

    /* se recebeu HTML "Not Found", tenta com slash final */
    if (resp && strstr(resp, "Not Found")) {
        free(resp);
        snprintf(endpoint, sizeof(endpoint), "/delete/dataset/%d/", dataset_id);
        resp = api_request("DELETE", endpoint, NULL);
    }

    if (!resp) {
        *response = NULL;
        return false;
    }

    *response = resp; // caller deve free()

    /* verificação rígida do JSON de sucesso */
    if (strstr(resp, "\"status\":\"OK\"") != NULL) {
        return true;
    }

    return false;
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
bool api_get_dataset_by_name_(const char *filename, char **response){
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/dataset/%s", filename);
    *response = api_request("GET", endpoint, NULL);
    return (*response != NULL);
}

/* --- helper: simple percent-encode --- */
static char *url_encode_simple(const char *s) {
    if (!s) return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            g_string_append_c(out, c);
        } else {
            g_string_append_printf(out, "%%%02X", c);
        }
    }
    char *ret = g_string_free(out, FALSE);
    return ret;
}

static size_t write_file_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

bool api_get_dataset_by_name(const char *filename, char **response) {
    if (!filename || !*filename) return false;

    CURL *curl;
    CURLcode res;
    bool success = false;

    // monta URL completa
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", BASE_URL, filename);

    // monta caminho local
    char local_path[512];
    snprintf(local_path, sizeof(local_path), "%s/%s", DATASET_DIR, filename);

    // cria diretório datasets/ se não existir


    #ifdef _WIN32
        wchar_t wdir[64];
        MultiByteToWideChar(CP_UTF8, 0, DATASET_DIR, -1, wdir, 64);
        _wmkdir(wdir);
    #else
        mkdir(DATASET_DIR, 0755);
    #endif

    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        *response = strdup("{\"status\":\"ERROR\",\"message\":\"Falha ao abrir arquivo local.\"}");
        return false;
    }

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        fclose(fp);

        if (res == CURLE_OK) {
            success = true;
            *response = strdup("{\"status\":\"OK\",\"message\":\"Dataset salvo em datasets/\"}");
        } else {
            remove(local_path); // apaga arquivo incompleto
            *response = strdup("{\"status\":\"ERROR\",\"message\":\"Falha ao baixar dataset.\"}");
        }
    } else {
        fclose(fp);
        *response = strdup("{\"status\":\"ERROR\",\"message\":\"Falha ao inicializar CURL.\"}");
    }

    return success;
}


// envia CSV + metadados: user_id, enviado_por_nome, enviado_por_email, nome, descricao
bool api_upload_csv_with_meta(const char *csv_path,
                              int user_id,
                              const char *enviado_por_nome,
                              const char *enviado_por_email,
                              const char *nome,
                              const char *descricao,
                              char **response) {
    CURL *curl = NULL;
    CURLcode res;
    struct ResponseData chunk;

    FILE *f = fopen(csv_path, "rb");
    if (!f) {
        debug_log("!! api_upload_csv: arquivo nao encontrado: %s", csv_path);
        *response = NULL;
        return false;
    }
    fclose(f);

    chunk.data = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        free(chunk.data);
        curl_global_cleanup();
        return false;
    }

    char url[512];
    snprintf(url, sizeof(url), "http://localhost:5000/datasets/upload"); // ajuste se necessário

    debug_log(">> API_UPLOAD_CSV_WITH_META: %s -> %s (user_id=%d)", csv_path, url, user_id);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    // Criar formulário multipart
    curl_mime *form = curl_mime_init(curl);
    if (!form) {
        debug_log("!! curl_mime_init falhou");
        free(chunk.data);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }

    // Parte do arquivo: campo "file"
    curl_mimepart *file_part = curl_mime_addpart(form);
    curl_mime_name(file_part, "file");
    curl_mime_filedata(file_part, csv_path);

    // user_id
    char user_id_str[32];
    snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
    curl_mimepart *uid_part = curl_mime_addpart(form);
    curl_mime_name(uid_part, "user_id");
    curl_mime_data(uid_part, user_id_str, CURL_ZERO_TERMINATED);

    // enviado_por_nome (se fornecido)
    if (enviado_por_nome && enviado_por_nome[0] != '\0') {
        curl_mimepart *ename = curl_mime_addpart(form);
        curl_mime_name(ename, "enviado_por_nome");
        curl_mime_data(ename, enviado_por_nome, CURL_ZERO_TERMINATED);
    }

    // enviado_por_email (se fornecido)
    if (enviado_por_email && enviado_por_email[0] != '\0') {
        curl_mimepart *eemail = curl_mime_addpart(form);
        curl_mime_name(eemail, "enviado_por_email");
        curl_mime_data(eemail, enviado_por_email, CURL_ZERO_TERMINATED);
    }

    // nome do dataset (opcional)
    if (nome && nome[0] != '\0') {
        curl_mimepart *npart = curl_mime_addpart(form);
        curl_mime_name(npart, "nome");
        curl_mime_data(npart, nome, CURL_ZERO_TERMINATED);
    }

    // descricao (opcional)
    if (descricao && descricao[0] != '\0') {
        curl_mimepart *dpart = curl_mime_addpart(form);
        curl_mime_name(dpart, "descricao");
        curl_mime_data(dpart, descricao, CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    // Executa
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        debug_log("!! curl_easy_perform() failed: %s", curl_easy_strerror(res));
        free(chunk.data);
        chunk.data = NULL;
    } else {
        debug_log("<< API_RESPONSE: %s", chunk.data ? chunk.data : "(null)");
    }

    curl_mime_free(form);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    *response = chunk.data; // caller libera
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
    else if (token && strcmp(token, "DELETE_USER") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int user_id = atoi(token);
            if (user_id > 0) {
                api_delete_user(user_id, &response);
            }
        }
    }
    else if (token && strcmp(token, "DELETE_DATASET") == 0) {
        token = strtok(NULL, " ");
        if (token) {
            int dataset_id = atoi(token);
            if (dataset_id > 0) {
                api_delete_dataset(dataset_id, &response);
            }
        }
    }
    // *** ADICIONADO: novo comando UPLOAD_CSV <caminho do arquivo (restante da linha)> ***
    else if (token && strcmp(token, "UPLOAD_CSV") == 0) {
        token = strtok(NULL, ""); // pega resto da linha (path e/ou pares key=value) — pode conter espaços
        if (token) {
            // trim leading spaces
            while (*token == ' ') token++;
            if (*token == '\0') {
                debug_log("UPLOAD_CSV: nothing after command");
            } else {
                char *rest = token;
                char *path = NULL;
                int user_id = 0;
                char *enviado_por_nome = NULL;
                char *enviado_por_email = NULL;
                char *nome_field = NULL;
                char *descricao = NULL;

                /* Caso 1: JSON object (recomendado) */
                if (rest[0] == '{') {
                    cJSON *j = cJSON_Parse(rest);
                    if (j) {
                        cJSON *jpath = cJSON_GetObjectItemCaseSensitive(j, "path");
                        if (!jpath) jpath = cJSON_GetObjectItemCaseSensitive(j, "file");
                        if (cJSON_IsString(jpath) && jpath->valuestring) path = g_strdup(jpath->valuestring);

                        cJSON *ju = cJSON_GetObjectItemCaseSensitive(j, "user_id");
                        if (cJSON_IsNumber(ju)) user_id = ju->valueint;
                        else {
                            cJSON *jus = cJSON_GetObjectItemCaseSensitive(j, "usuario_id");
                            if (cJSON_IsNumber(jus)) user_id = jus->valueint;
                            else if (cJSON_IsString(jus) && jus->valuestring) user_id = atoi(jus->valuestring);
                        }

                        cJSON *jn = cJSON_GetObjectItemCaseSensitive(j, "enviado_por_nome");
                        if (cJSON_IsString(jn) && jn->valuestring) enviado_por_nome = g_strdup(jn->valuestring);

                        cJSON *je = cJSON_GetObjectItemCaseSensitive(j, "enviado_por_email");
                        if (cJSON_IsString(je) && je->valuestring) enviado_por_email = g_strdup(je->valuestring);

                        cJSON *jnome = cJSON_GetObjectItemCaseSensitive(j, "nome");
                        if (cJSON_IsString(jnome) && jnome->valuestring) nome_field = g_strdup(jnome->valuestring);

                        cJSON *jdesc = cJSON_GetObjectItemCaseSensitive(j, "descricao");
                        if (cJSON_IsString(jdesc) && jdesc->valuestring) descricao = g_strdup(jdesc->valuestring);

                        cJSON_Delete(j);
                    } else {
                        debug_log("UPLOAD_CSV: invalid JSON payload");
                    }
                } else {
                    /* Caso 2: tokenized key=value pairs. First token sem '=' é tratado como path */
                    char *tmp = g_strdup(rest);
                    char *saveptr = NULL;
                    char *tk = strtok_r(tmp, " ", &saveptr);
                    if (tk) {
                        if (strchr(tk, '=') == NULL) {
                            path = g_strdup(tk);
                            tk = strtok_r(NULL, " ", &saveptr);
                        }
                    }
                    while (tk) {
                        char *eq = strchr(tk, '=');
                        if (eq) {
                            *eq = '\0';
                            char *k = tk;
                            char *v = eq + 1;

                            /* strip surrounding quotes if present */
                            size_t vlen = strlen(v);
                            if (vlen >= 2 && ((v[0] == '"' && v[vlen-1] == '"') || (v[0] == '\'' && v[vlen-1] == '\''))) {
                                v[vlen-1] = '\0';
                                v++;
                            }

                            if (g_strcmp0(k, "user_id") == 0 || g_strcmp0(k, "usuario_id") == 0) {
                                user_id = atoi(v);
                            } else if (g_strcmp0(k, "enviado_por_nome") == 0) {
                                g_free(enviado_por_nome); enviado_por_nome = g_strdup(v);
                            } else if (g_strcmp0(k, "enviado_por_email") == 0) {
                                g_free(enviado_por_email); enviado_por_email = g_strdup(v);
                            } else if (g_strcmp0(k, "nome") == 0) {
                                g_free(nome_field); nome_field = g_strdup(v);
                            } else if (g_strcmp0(k, "descricao") == 0) {
                                g_free(descricao); descricao = g_strdup(v);
                            } else if (g_strcmp0(k, "path") == 0 || g_strcmp0(k, "file") == 0) {
                                if (!path) path = g_strdup(v);
                            } else {
                                /* ignore unknown keys */
                            }
                        }
                        tk = strtok_r(NULL, " ", &saveptr);
                    }
                    g_free(tmp);
                }

                /* path é obrigatório para o upload */
                if (!path) {
                    debug_log("UPLOAD_CSV: missing path");
                } else {
                    /* Chama a função existente corretamente, passando &response para que ela aloque/retorne a resposta */
                    if (!api_upload_csv_with_meta(path,
                                                  user_id,
                                                  (enviado_por_nome && *enviado_por_nome) ? enviado_por_nome : NULL,
                                                  (enviado_por_email && *enviado_por_email) ? enviado_por_email : NULL,
                                                  (nome_field && *nome_field) ? nome_field : NULL,
                                                  (descricao && *descricao) ? descricao : NULL,
                                                  &response)) {
                        /* Se a função retornou false, response pode ou não ter sido preenchido; manter como está e deixar o fluxo tratar */
                        debug_log("UPLOAD_CSV: upload function returned failure");
                    }
                    /* NOTA: não liberar 'response' aqui — o fluxo posterior de run_api_command faz process_api_response(response) e depois free(response) */
                }

                /* liberar temporários alocados */
                if (path) g_free(path);
                if (enviado_por_nome) g_free(enviado_por_nome);
                if (enviado_por_email) g_free(enviado_por_email);
                if (nome_field) g_free(nome_field);
                if (descricao) g_free(descricao);
            }
        }
    }


    else if (token && strcmp(token, "GET_USER_AVATAR") == 0) {
        /* Accept either: GET_USER_AVATAR 29
                         GET_USER_AVATAR {"user_id":29}   */
        token = strtok(NULL, ""); /* pega resto da linha (pode ser JSON ou id) */
        if (token) {
            /* trim leading spaces */
            while (*token == ' ') token++;
            if (*token != '\0') {
                int uid = 0;
                char *tmp_path = NULL;

                /* Try simple numeric id first */
                char *endptr = NULL;
                long v = strtol(token, &endptr, 10);
                if (endptr != token && ( *endptr == '\0' || *endptr == ' ')) {
                    uid = (int)v;
                } else if (token[0] == '{') {
                    /* try parse JSON { "user_id": ... } */
                    cJSON *j = cJSON_Parse(token);
                    if (j) {
                        cJSON *ju = cJSON_GetObjectItemCaseSensitive(j, "user_id");
                        if (cJSON_IsNumber(ju)) {
                            uid = ju->valueint;
                        } else {
                            /* also accept string id in json */
                            cJSON *jus = cJSON_GetObjectItemCaseSensitive(j, "id");
                            if (cJSON_IsNumber(jus)) uid = jus->valueint;
                            else if (cJSON_IsString(jus) && jus->valuestring) uid = atoi(jus->valuestring);
                        }
                        cJSON_Delete(j);
                    } else {
                        /* not JSON and not numeric -> try atoi fallback */
                        uid = atoi(token);
                    }
                } else {
                    /* fallback: attempt atoi on token */
                    uid = atoi(token);
                }

                if (uid > 0) {
                    if (api_get_user_avatar_to_temp(uid, &tmp_path) && tmp_path) {
                        /* convert tmp_path (UTF-8) to WCHAR and return (caller frees) */
                        int wchar_size = MultiByteToWideChar(CP_UTF8, 0, tmp_path, -1, NULL, 0);
                        WCHAR *wchar_response = (WCHAR*)malloc((size_t)wchar_size * sizeof(WCHAR));
                        if (wchar_response) {
                            MultiByteToWideChar(CP_UTF8, 0, tmp_path, -1, wchar_response, wchar_size);
                        } else {
                            /* allocation failed */
                            g_free(tmp_path);
                            free(utf8_command);
                            return NULL;
                        }
                        g_free(tmp_path);
                        free(utf8_command);
                        return wchar_response;
                    } else {
                        /* download failed: return NULL so caller can handle */
                        if (tmp_path) g_free(tmp_path);
                        free(utf8_command);
                        return NULL;
                    }
                }
            }
        }
        free(utf8_command);
        return NULL;
    }

    else if (token && strcmp(token, "UPDATE_USER") == 0) {
        token = strtok(NULL, ""); /* resto é JSON */
        if (token) {
            /* token é JSON: {"user_id":29,"nome":"x","bio":"y","avatar":"/path/to/file"} */
            cJSON *j = cJSON_Parse(token);
            if (j) {
                cJSON *ju = cJSON_GetObjectItemCaseSensitive(j, "user_id");
                cJSON *jn = cJSON_GetObjectItemCaseSensitive(j, "nome");
                cJSON *jm = cJSON_GetObjectItemCaseSensitive(j, "email");
                cJSON *jb = cJSON_GetObjectItemCaseSensitive(j, "bio");
                cJSON *ja = cJSON_GetObjectItemCaseSensitive(j, "avatar");
                int uid = 0;
                const char *nome = NULL;
                const char *email = NULL;
                const char *bio = NULL;
                const char *avatar = NULL;
                if (cJSON_IsNumber(ju)) uid = ju->valueint;
                else if (cJSON_IsString(ju)) uid = atoi(ju->valuestring);
                if (cJSON_IsString(jn)) nome = jn->valuestring;
                if (cJSON_IsString(jb)) bio = jb->valuestring;
                if (cJSON_IsString(jm)) email = jm->valuestring;
                if (cJSON_IsString(ja)) avatar = ja->valuestring;

                if (uid > 0) {
                    api_update_user_with_avatar(uid, nome, bio, email, avatar, &response);
                } else {
                    debug_log("UPDATE_USER_AVATAR: missing/invalid user_id");
                }
                cJSON_Delete(j);
            } else {
                debug_log("UPDATE_USER_AVATAR: invalid JSON payload");
            }
        } else {
            debug_log("UPDATE_USER_AVATAR: no payload");
        }
    }

    else if (token && strcmp(token, "GET_DATASET") == 0) {
        // pega resto da linha (nome do dataset -- pode conter espaços)
        token = strtok(NULL, "");
        if (token) {
            // trim leading spaces
            while (*token == ' ') token++;
            if (*token != '\0') {
                api_get_dataset_by_name(token, &response);
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

    if (token && strcmp(token, "GET_DATASET_JSON") == 0) {
        // pega resto da linha (nome do dataset -- pode conter espaços)
        token = strtok(NULL, "");
        if (token) {
            // trim leading spaces
            while (*token == ' ') token++;
            if (*token != '\0') {
                if (api_get_dataset_by_name_(token, &response)) {
                    int wchar_size = MultiByteToWideChar(CP_UTF8, 0, response, -1, NULL, 0);
                    WCHAR *wchar_response = malloc(wchar_size * sizeof(WCHAR));
                    MultiByteToWideChar(CP_UTF8, 0, response, -1, wchar_response, wchar_size);
                    free(response);
                    free(utf8_command);
                    return wchar_response;
                }
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

#endif
