#include <stdio.h>

void math() {
    printf("Hello, World!\n");
    int A = 5;
    int B = 10;
    for(int i = 0; i < 5; i++) {
        printf("Iteration %d: A = %d, B = %d\n", i, A, B);
        A += 1; // Increment A
        B += 2; // Increment B
    }
    int C = A + B; 
    printf("The sum of %d and %d is %d\n", A, B, C);
}
