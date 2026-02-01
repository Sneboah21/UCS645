#include <stdio.h>
#include <omp.h>
#include <stdlib.h>

#define N 65536 //2^16 elements

int main()
{
    double *X, *Y;
    double a = 2.0;
    double start, seq_time, par_time, speedup, efficiency;
    X = (double*)malloc(N*sizeof(double));
    Y = (double*)malloc(N*sizeof(double));
    if(X == NULL || Y == NULL)
    {
        printf("Memory not allocated!");
        return 1;
    }
    for(int i=0;i<N;i++)
    {
        X[i] = i*1.0;
        Y[i] = i*0.5;
    }
    //Sequential
    start = omp_get_wtime();
    for(int i=0;i<N;i++)
    {
    	X[i] = a*X[i]+Y[i];
    }
    seq_time = omp_get_wtime()-start;
    printf("Sequential time: %f\n", seq_time);
    
    printf("\nThreads	Time(s)	   Speedup	Efficiency\n");
    //Parallel 
    for(int num_thread = 2;num_thread<=16;num_thread++)
    {
        for(int i=0;i<N;i++)
        {
            X[i] = i*1.0;
        }
        omp_set_num_threads(num_thread);
        start = omp_get_wtime();

        #pragma omp parallel for
        for(int i=0;i<N;i++)
        {
            X[i] = a*X[i]+Y[i];
        }
        par_time = omp_get_wtime()-start;
        speedup = seq_time/par_time;
        efficiency = (speedup/num_thread)*100.0;
        printf("%7d	%7f	%7.2f	%12.2f\n",num_thread,par_time,speedup,efficiency);
    }
    printf("X[0] = %f\n", X[0]);
    free(X);
    free(Y);
    return 0;
    
 }  
 
