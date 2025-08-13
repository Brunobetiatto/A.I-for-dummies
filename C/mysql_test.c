#include <stdio.h>
#include <stdlib.h>
#include <mysql/jdbc.h>
#include <jdbc/mysql_connecti.h>

int main() {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    const char *server = "localhost";     // Servidor do MySQL
    const char *user = "root";            // Usuário
    const char *password = "senha";       // Senha
    const char *database = "meubanco";    // Nome do banco

    // Inicializa conexão
    conn = mysql_init(NULL);

    if (conn == NULL) {
        fprintf(stderr, "Falha ao inicializar MySQL: %s\n", mysql_error(conn));
        exit(1);
    }

    // Conecta ao banco
    if (mysql_real_connect(conn, server, user, password, database, 0, NULL, 0) == NULL) {
        fprintf(stderr, "Erro na conexão: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }

    // Executa uma query
    if (mysql_query(conn, "SELECT id, nome FROM usuarios")) {
        fprintf(stderr, "Erro na consulta: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }

    // Pega o resultado
    res = mysql_store_result(conn);

    if (res == NULL) {
        fprintf(stderr, "Erro ao armazenar resultado: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }

    int num_fields = mysql_num_fields(res);

    // Imprime resultados
    while ((row = mysql_fetch_row(res))) {
        for (int i = 0; i < num_fields; i++) {
            printf("%s ", row[i] ? row[i] : "NULL");
        }
        printf("\n");
    }

    // Libera memória e fecha conexão
    mysql_free_result(res);
    mysql_close(conn);

    return 0;
}
