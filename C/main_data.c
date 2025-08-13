#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

int main() {
    FILE *fp;
    char comando[256];
    int n;
    double valor;

#ifdef _WIN32
    snprintf(comando, sizeof(comando), "python get_data.py");
#else
    snprintf(comando, sizeof(comando), "python3 get_data.py");
#endif

    fp = popen(comando, "r");
    if (!fp) {
        perror("Erro ao executar Python");
        return 1;
    }

    if (fscanf(fp, "%d", &n) != 1) {
        fprintf(stderr, "Erro lendo quantidade\n");
        pclose(fp);
        return 1;
    }

    printf("Recebendo %d valores:\n", n);

    for (int i = 0; i < n; i++) {
        if (fscanf(fp, "%lf", &valor) != 1) {
            fprintf(stderr, "Erro lendo valor %d\n", i);
            break;
        }
        printf("Valor %d: %.2f\n", i + 1, valor);
    }

    pclose(fp);
    return 0;
}
