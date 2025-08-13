#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

typedef struct Stack {
    int top;
    int capacity;
    int* array;
} Stack;

Stack* createStack(int capacity);
bool isFull(Stack* stack);
bool isEmpty(Stack* stack);
void push(Stack* stack, int item);
int pop(Stack* stack);
int peek(Stack* stack);
void display(Stack* stack);
void destroyStack(Stack* stack);
char* stackToString(Stack* stack);

#endif