#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

static long num_steps = 100000;
double step;

int main() 
{
    int i, actual_threads = 0;
    double x, pi, sum = 0.0;
    double start, end;
    
    step = 1.0/(double)num_steps;
    
    printf("Calculation of PI using Numerical Integration\n");
    printf("Number of steps: %ld\n", num_steps);
    
    omp_set_num_threads(4);
    
    start = omp_get_wtime();
    
    #pragma omp parallel for reduction(+:sum) private(x)
    for (i=0; i<num_steps; i++) 
    {
        x = (i+0.5)*step;
        sum = sum+4.0/(1.0+x*x);
        
        if (i == 0)
        {
            actual_threads = omp_get_num_threads();
        }
    }
    
    pi = step*sum;
    end = omp_get_wtime();
    
    printf("Calculated PI = %.15f\n", pi);
    printf("Actual PI     = 3.141592653589793\n");
    printf("Error         = %.15f\n", pi - 3.141592653589793);
    printf("Time taken    = %f seconds\n", end - start);
    printf("Threads used  = %d\n", actual_threads);
    
    return 0;
}

