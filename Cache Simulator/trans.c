/*************************************************************
 * Name - Sudhir Kumar Vijay
 * Andrew ID - svijay 
 * 
 * DESCRIPTION:
 * This program implements the transpose function to minimize 
 * the number of cache misses.  
 *************************************************************/

/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"
#include "contracts.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 * will be graded on for Part B of the assignment. Do not change
 * the description string "Transpose submission", as the driver
 * searches for that string to identify the transpose function to
 * be graded. The REQUIRES and ENSURES from 15-122 are included
 * for your convenience. They can be removed if you like.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    REQUIRES(M > 0);
    REQUIRES(N > 0);
    /* Defining temporary and loop variables */        
    int i, j, t0, t1, t2, t3, t4, t5, t6, t7, i1, j1;
    /*  Transpose for 32 X 32
        Using the concept of blocking to store 8x8 chunks of A 
        into the cache and assigning them to temporary local 
        variables. These local variables are assigned to a column in
        B when its 8 rows are loaded into the cache. 8X8 looked like
        the right thing to do,considering that the each line in cache 
        can hold 8 integers.    
    */ 
    if ((M == 32) && (N == 32)) {
        for (i = 0; i < N; i=i+8) {
            for (j = 0; j < M; j=j+8) {
                for (i1 = i; i1 < i+8; i1=i1+1) {
                        t0 = A[i1][j];
                        t1 = A[i1][j+1];
                        t2 = A[i1][j+2];
                        t3 = A[i1][j+3];
                        t4 = A[i1][j+4];
                        t5 = A[i1][j+5];
                        t6 = A[i1][j+6];
                        t7 = A[i1][j+7];

                        B[j][i1] = t0;
                        B[j+1][i1] = t1;
                        B[j+2][i1] = t2;
                        B[j+3][i1] = t3;
                        B[j+4][i1] = t4;
                        B[j+5][i1] = t5;
                        B[j+6][i1] = t6;
                        B[j+7][i1] = t7;
                }
            }
        }    
    }     
    /*  Transpose for 64 X 64
        Traversing the array in 8X8 blocks and then using blocking 
        in terms of 4X4 blocks. I used 4 temporary variables to store
        the elements of A row-wise and using these 4 variables 
        to populate the corresponding 4 columns of B. This reduces 
        the number of conflict misses and gives us better utilization 
        than a blind 8X8 traversal like the 32X32 case. 
    */ 
    else if((M==64) && (N == 64)) {
           for (j = 0; j < M; j=j+8) {
            for (i = 0; i < N; i=i+8) {
                for (i1 = i; i1 < i+8; i1=i1+1) {
                        t0= A[i1][j];
                        t1= A[i1][j+1];
                        t2= A[i1][j+2];
                        t3= A[i1][j+3];
                    
                        B[j][i1]  = t0;
                        B[j+1][i1]= t1;
                        B[j+2][i1]= t2;
                        B[j+3][i1]= t3;
                }
                for (i1 = i+7; i1 > i-1; i1=i1-1) {
                        t0= A[i1][j+4];
                        t1= A[i1][j+5];
                        t2= A[i1][j+6];
                        t3= A[i1][j+7];
                    
                        B[j+4][i1]= t0;
                        B[j+5][i1]= t1;
                        B[j+6][i1]= t2;
                        B[j+7][i1]= t3;
                }
            }
        }    
    }
   /*   Transpose for 61 X 67
        Traversing the 61X67 matrix in chunks of 8X8 blocks to 
        maximize utilization of the 32X8 cache (integral) blocks.
        Exiting the loops once the edge conditions are reached
        (N = 67 and M = 61). I guess this should work, considering
        that the target number of misses is much higher than the 
        other two cases. 
   */ 
    else if((M==61) && (N == 67)) {
        for (i = 0; i < N; i=i+8) {
            for (j = 0; j < M; j=j+8) {
                for (j1 = j; (j1 < j+8) && (j1 < M); j1++){
                    for (i1 = i; (i1 < i+8) && (i1 < N); i1++){
                        B[j1][i1] = A[i1][j1];
                    }
                }
            }
        }
    }   

    ENSURES(is_transpose(M, N, A, B));
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the 
 * cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    REQUIRES(M > 0);
    REQUIRES(N > 0);

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

    ENSURES(is_transpose(M, N, A, B));
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}
