#include <stdio.h>
#include <stdlib.h>
int input() {
    int a;
    scanf("%d", &a);
    return a;
}

void output(int a) { printf("%d\r\n", a); }

void outputFloat(float a) { printf("%f\r\n", a); }

void neg_idx_except() {
    printf("negative index exception\r\n");
    exit(0);
}
