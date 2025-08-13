#include "stack.h"

Stack* createStack(int capacity) {
    Stack* stack = (Stack*)malloc(sizeof(Stack));
    stack->capacity = capacity;
    stack->top = -1;
    stack->array = (int*)malloc(stack->capacity * sizeof(int));
    return stack;
}

bool isFull(Stack* stack) {
    return stack->top == stack->capacity - 1;
}

bool isEmpty(Stack* stack) {
    return stack->top == -1;
}

void push(Stack* stack, int item) {
    if (stack == NULL) return;
    
    if (isFull(stack)) {
        int nova_capacidade = stack->capacity * 2;
        int* novo_array = realloc(stack->array, nova_capacidade * sizeof(int));
        if (!novo_array) {
            fprintf(stderr, "Erro ao realocar memória!\n");
            return;
        }
        stack->array = novo_array;
        stack->capacity = nova_capacidade;
    }
    stack->array[++stack->top] = item;
}

int pop(Stack* stack) {
    if (isEmpty(stack)) {
        return INT_MIN;
    }
    return stack->array[stack->top--];
}

int peek(Stack* stack) {
    if (isEmpty(stack)) {
        return INT_MIN;
    }
    return stack->array[stack->top];
}

char* stackToString(Stack* stack) {
    if (isEmpty(stack)) {
        char* str = malloc(20);
        sprintf(str, "Pilha vazia");
        return str;
    }
    
    char* str = malloc(stack->capacity * 10);
    str[0] = '\0';
    
    for (int i = stack->top; i >= 0; i--) {
        char temp[20];
        sprintf(temp, "[%d] %d\n", i, stack->array[i]);
        strcat(str, temp);
    }
    return str;
}

void destroyStack(Stack* stack) {
    if (stack) {
        free(stack->array);
        free(stack);
    }
}