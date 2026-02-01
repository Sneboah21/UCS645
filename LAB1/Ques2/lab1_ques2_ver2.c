#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

#define N 1000

int main()
{
    double **A,**B,**B_T,**C;
    int i,j,k;
    double start, seq_time, par_time, speedup, efficiency;
    
    A = (double**)malloc(N*sizeof(double*));
    B = (double**)malloc(N*sizeof(double*));
    B_T = (double**)malloc(N*sizeof(double*));
    C = (double**)malloc(N*sizeof(double*));
    
    for (i = 0; i < N; i++) {
        A[i] = (double*)malloc(N*sizeof(double));
        B[i] = (double*)malloc(N*sizeof(double));
        B_T[i] = (double*)malloc(N*sizeof(double));
        C[i] = (double*)malloc(N*sizeof(double));
    }
    
    for (i = 0;i < N;i++) 
    {
        for (j = 0;j < N;j++) 
        {
            A[i][j] = i+j;
            B[i][j] = i-j;
        }
    }
    //Transposing B for memory efficiency
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < N; j++) 
        {
            B_T[j][i] = B[i][j];
        }
    }
    
    printf("2D Threading (Nested)\n");
    
    // Sequential
    start = omp_get_wtime();
    for (i = 0; i < N; i++) 
    {
        for (j = 0; j < N; j++) 
        {
            C[i][j] = 0.0;
            for (k = 0; k < N; k++) 
            {
                C[i][j] += A[i][k] * B_T[j][k];
            }
        }
    }
    seq_time = omp_get_wtime()-start;
    printf("Sequential time: %f\n", seq_time);
    
    printf("Threads   Time(s)   Speedup   Efficiency(%%)\n");
    
    // Parallel
    for (int num_threads = 2;num_threads <= 16;num_threads++) {
        omp_set_num_threads(num_threads);
        
        start = omp_get_wtime();
        
        #pragma omp parallel for collapse(2) private(k)
        for (i = 0; i < N; i++)
        {
            for (j = 0; j < N; j++) 
            {
                C[i][j] = 0.0;
                for (k = 0; k < N; k++) 
                {
                    C[i][j] += A[i][k] * B_T[j][k];
                }
            }
        }
        
        par_time = omp_get_wtime()-start;
        speedup = seq_time/par_time;
        efficiency = (speedup/num_threads)*100.0;
        
        printf("%7d   %7.3f   %7.2f   %12.2f\n", num_threads, par_time, speedup, efficiency);
    }
    
    printf("C[0][0] = %f\n", C[0][0]);
    
    //Free memory
    for (i = 0; i < N; i++) {
        free(A[i]);
        free(B[i]);
        free(B_T[i]);
        free(C[i]);
    }
    free(A);
    free(B);
    free(B_T);
    free(C);
    
    return 0;
}
