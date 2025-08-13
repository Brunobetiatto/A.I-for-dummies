#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Definição da estrutura da pilha
typedef struct Stack {
    int top;          // Índice do topo
    int capacity;     // Capacidade máxima
    int* array;       // Array para armazenar os elementos
} Stack;

// 1. Cria uma nova pilha
Stack* createStack(int capacity) {
    Stack* stack = (Stack*)malloc(sizeof(Stack));
    stack->capacity = capacity;
    stack->top = -1;  // -1 indica pilha vazia
    stack->array = (int*)malloc(stack->capacity * sizeof(int));
    return stack;
}

// 2. Verifica se a pilha está cheia
bool isFull(Stack* stack) {
    return stack->top == stack->capacity - 1;
}

// 3. Verifica se a pilha está vazia
bool isEmpty(Stack* stack) {
    return stack->top == -1;
}

// 4. Adiciona elemento no topo (push)
void push(Stack* stack, int item) {
    // Verifica se a pilha existe
    if (stack == NULL) {
        printf("Erro: Pilha inválida!\n");
        return;
    }
    
    // Redimensiona se necessário
    if (isFull(stack)) {
        printf("Pilha cheia! Aumentando capacidade de %d para %d...\n", 
               stack->capacity, stack->capacity * 2);
        
        // Calcula nova capacidade (limita o máximo para evitar estouro)
        int nova_capacidade = stack->capacity * 2;
        
        // Verifica estouro de capacidade máxima
        if (nova_capacidade < stack->capacity) { // Overflow de inteiro
            printf("Erro: Capacidade máxima atingida! Não é possível empilhar %d\n", item);
            return;
        }
        
        // Realoca com ponteiro temporário
        int* novo_array = realloc(stack->array, nova_capacidade * sizeof(int));
        
        // Verifica falha na alocação
        if (novo_array == NULL) {
            printf("Erro crítico: Falha ao realocar memória! Não é possível empilhar %d\n", item);
            return;
        }
        
        // Atualiza a pilha
        stack->array = novo_array;
        stack->capacity = nova_capacidade;
    }
    
    // Adiciona o item
    stack->array[++stack->top] = item;
    printf("%d empilhado (topo: %d, capacidade: %d)\n", 
           item, stack->top, stack->capacity);
}

// 5. Remove elemento do topo (pop)
int pop(Stack* stack) {
    if (isEmpty(stack)) {
        printf("Pilha vazia!\n");
        return INT_MIN; // Valor especial para erro
    }
    return stack->array[stack->top--];
}

// 6. Retorna o elemento do topo sem remover (peek)
int peek(Stack* stack) {
    if (isEmpty(stack)) {
        printf("Pilha vazia!\n");
        return INT_MIN;
    }
    return stack->array[stack->top];
}

// 7. Exibe todo o conteúdo da pilha
void display(Stack* stack) {
    if (isEmpty(stack)) {
        printf("Pilha vazia!\n");
        return;
    }
    
    printf("Conteúdo da pilha (do topo para base):\n");
    for (int i = stack->top; i >= 0; i--) {
        printf("[%d] -> %d\n", i, stack->array[i]);
    }
}

// 8. Libera a memória da pilha
void destroyStack(Stack* stack) {
    free(stack->array);
    free(stack);
}

// Programa principal para testar a pilha
void pilha_teste() {
    // Cria pilha com capacidade inicial para 3 elementos
    Stack* stack = createStack(3);

    // Testando operações
    push(stack, 10);
    push(stack, 20);
    push(stack, 30);
    push(stack, 40); // Deve aumentar capacidade automaticamente

    display(stack);

    printf("\nElemento no topo: %d\n", peek(stack));
    printf("%d desempilhado\n", pop(stack));
    printf("%d desempilhado\n", pop(stack));

    display(stack);

    destroyStack(stack);
}