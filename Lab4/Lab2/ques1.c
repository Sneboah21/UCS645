#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>

#define EPSILON 1.0    // Depth of potential well
#define SIGMA 1.0      // Distance at which potential is zero
#define CUTOFF 2.5     // Cutoff distance (in units of sigma)

typedef struct{
    double x, y, z;
}Vector3D;

void initialize_particles(Vector3D *positions, int N, double box_size){
    for (int i = 0;i < N;i++){
        positions[i].x = ((double)rand() / RAND_MAX) * box_size;
        positions[i].y = ((double)rand() / RAND_MAX) * box_size;
        positions[i].z = ((double)rand() / RAND_MAX) * box_size;
    }
}

void compute_forces_serial(Vector3D *positions,Vector3D *forces,int N,double *total_energy){
    double energy = 0.0;
    double cutoff_sq = CUTOFF*CUTOFF;
    
    for (int i = 0;i<N;i++){
        forces[i].x = forces[i].y = forces[i].z = 0.0;
    }
  
    for (int i = 0;i < N-1;i++){
        for (int j = i+1;j<N;j++){
            
            double dx = positions[i].x - positions[j].x;
            double dy = positions[i].y - positions[j].y;
            double dz = positions[i].z - positions[j].z;
            
            double r_sq = dx*dx + dy*dy + dz*dz;
            
            if (r_sq < cutoff_sq){
                double r = sqrt(r_sq);
                double sigma_over_r = SIGMA/r;
                double sr6 = pow(sigma_over_r,6);
                double sr12 = sr6*sr6;
                
                double pair_energy = 4.0*EPSILON*(sr12-sr6);
                energy += pair_energy;
                
                double force_mag = 24.0*EPSILON*(2.0*sr12-sr6)/r_sq;
                
                double fx = force_mag * dx;
                double fy = force_mag * dy;
                double fz = force_mag * dz;
                
                forces[i].x += fx;
                forces[i].y += fy;
                forces[i].z += fz;
                
                forces[j].x -= fx;
                forces[j].y -= fy;
                forces[j].z -= fz;
            }
        }
    }
    
    *total_energy = energy;
}

void compute_forces_parallel_atomic(Vector3D *positions,Vector3D *forces,int N,double *total_energy){
    double energy = 0.0;
    double cutoff_sq = CUTOFF * CUTOFF;
    
    #pragma omp parallel for
    for (int i = 0;i<N;i++){
        forces[i].x = forces[i].y = forces[i].z = 0.0;
    }
    
    #pragma omp parallel for reduction(+:energy) schedule(dynamic, 10)
    for (int i = 0;i<N-1;i++){
        for (int j = i + 1;j<N;j++){
            double dx = positions[i].x - positions[j].x;
            double dy = positions[i].y - positions[j].y;
            double dz = positions[i].z - positions[j].z;
            
            double r_sq = dx*dx + dy*dy + dz*dz;
            
            if (r_sq < cutoff_sq){
                double r = sqrt(r_sq);
                double sigma_over_r = SIGMA/r;
                double sr6 = pow(sigma_over_r, 6);
                double sr12 = sr6 * sr6;
                
                double pair_energy = 4.0*EPSILON*(sr12-sr6);
                energy += pair_energy;
                
                double force_mag = 24.0*EPSILON*(2.0*sr12-sr6)/r_sq;
                
                double fx = force_mag * dx;
                double fy = force_mag * dy;
                double fz = force_mag * dz;
                
                // Atomic operations to prevent race conditions
                #pragma omp atomic
                forces[i].x += fx;
                #pragma omp atomic
                forces[i].y += fy;
                #pragma omp atomic
                forces[i].z += fz;
                
                #pragma omp atomic
                forces[j].x -= fx;
                #pragma omp atomic
                forces[j].y -= fy;
                #pragma omp atomic
                forces[j].z -= fz;
            }
        }
    }
    
    *total_energy = energy;
}

void compute_forces_parallel_optimized(Vector3D *positions,Vector3D *forces,int N,double *total_energy){
    double energy = 0.0;
    double cutoff_sq = CUTOFF * CUTOFF;
    
    for (int i = 0;i<N;i++) {
        forces[i].x = forces[i].y = forces[i].z = 0.0;
    }
    
    #pragma omp parallel reduction(+:energy)
    {
        int num_threads = omp_get_num_threads();
        int tid = omp_get_thread_num();
        
        Vector3D *local_forces = (Vector3D *)calloc(N, sizeof(Vector3D));
        
        #pragma omp for schedule(dynamic, 5)
        for (int i = 0;i<N-1;i++){
            for (int j = i + 1;j<N;j++){
                double dx = positions[i].x - positions[j].x;
                double dy = positions[i].y - positions[j].y;
                double dz = positions[i].z - positions[j].z;
                
                double r_sq = dx*dx + dy*dy + dz*dz;
                
                if (r_sq < cutoff_sq) {
                    double r = sqrt(r_sq);
                    double sigma_over_r = SIGMA/r;
                    double sr6 = pow(sigma_over_r, 6);
                    double sr12 = sr6*sr6;
                    
                    double pair_energy = 4.0*EPSILON*(sr12-sr6);
                    energy += pair_energy;
                    
                    double force_mag = 24.0 * EPSILON*(2.0*sr12-sr6)/r_sq;
                    
                    double fx = force_mag * dx;
                    double fy = force_mag * dy;
                    double fz = force_mag * dz;
                    
                    // Update local forces (no race condition)
                    local_forces[i].x += fx;
                    local_forces[i].y += fy;
                    local_forces[i].z += fz;
                    
                    local_forces[j].x -= fx;
                    local_forces[j].y -= fy;
                    local_forces[j].z -= fz;
                }
            }
        }
        
        // Merge local forces into global array with critical section
        #pragma omp critical
        {
            for (int i = 0; i < N; i++) {
                forces[i].x += local_forces[i].x;
                forces[i].y += local_forces[i].y;
                forces[i].z += local_forces[i].z;
            }
        }
        
        free(local_forces);
    }
    
    *total_energy = energy;
}

// NEW: Calculate and print FLOPS and memory bandwidth
void print_computational_metrics(int N, double time_seconds) {
    
    long long num_pairs = (long long)N * (N - 1) / 2;
    long long total_flops = num_pairs * 40;
    double gflops = total_flops / (time_seconds * 1e9);
    
    long long total_bytes = num_pairs * 144;
    double bandwidth_gb_s = total_bytes / (time_seconds * 1e9);
    
    double arithmetic_intensity = (double)total_flops / total_bytes;
    
    printf("\n========== Computational Metrics ==========\n");
    printf("Total pairs computed:  %lld\n", num_pairs);
    printf("Total FLOPs:           %.2f GFLOPs\n", total_flops / 1e9);
    printf("Performance:           %.2f GFLOPS/s\n", gflops);
    printf("Memory accessed:       %.2f GB\n", total_bytes / 1e9);
    printf("Memory bandwidth:      %.2f GB/s\n", bandwidth_gb_s);
    printf("Arithmetic intensity:  %.2f FLOPs/byte\n", arithmetic_intensity);
    printf("===========================================\n\n");
}

void print_performance_metrics(int N,int num_threads,double t_serial,double t_parallel,double energy_serial,double energy_parallel){

    double speedup = t_serial/t_parallel;
    double efficiency = speedup/num_threads;
    double cost = num_threads*t_parallel;
    
    long long num_pairs = (long long)N*(N-1)/2;
    double throughput = num_pairs/t_parallel;
    
    printf("\n========== Performance Metrics ==========\n");
    printf("N = %d particles, Threads = %d\n", N, num_threads);
    printf("Serial Time (T1):      %.6f s\n", t_serial);
    printf("Parallel Time (Tp):    %.6f s\n", t_parallel);
    printf("Speedup S(p):          %.2f\n", speedup);
    printf("Efficiency E(p):       %.2f%% (>70%% is good)\n", efficiency * 100);
    printf("Cost (p × Tp):         %.6f s (ideal = %.6f s)\n", cost, t_serial);
    printf("Throughput:            %.2e pairs/s\n", throughput);
    printf("Energy (serial):       %.6f\n", energy_serial);
    printf("Energy (parallel):     %.6f\n", energy_parallel);
    printf("Energy difference:     %.2e\n",fabs(energy_serial - energy_parallel));
    printf("=========================================\n\n");
}

void test_scheduling_strategies(Vector3D *positions,Vector3D *forces,int N,int num_threads){
    double energy;
    
    printf("\n========== Scheduling Strategy Comparison ==========\n");
    
    // Static scheduling
    omp_set_schedule(omp_sched_static, 0);
    double start = omp_get_wtime();
    compute_forces_parallel_optimized(positions,forces,N,&energy);
    double time_static = omp_get_wtime()-start;
    printf("Static Scheduling:    %.6f s (Energy: %.6f)\n", time_static, energy);
    print_computational_metrics(N, time_static);
    
    // Dynamic scheduling (chunk=10)
    omp_set_schedule(omp_sched_dynamic, 10);
    start = omp_get_wtime();
    compute_forces_parallel_optimized(positions,forces,N,&energy);
    double time_dynamic = omp_get_wtime()-start;
    printf("Dynamic (chunk=10):   %.6f s (Energy: %.6f)\n", time_dynamic, energy);
    print_computational_metrics(N, time_dynamic);
    
    // Guided scheduling
    omp_set_schedule(omp_sched_guided, 0);
    start = omp_get_wtime();
    compute_forces_parallel_optimized(positions, forces, N, &energy);
    double time_guided = omp_get_wtime() - start;
    printf("Guided Scheduling:    %.6f s (Energy: %.6f)\n", time_guided, energy);
    print_computational_metrics(N, time_guided);
    
    printf("====================================================\n\n");
}

int main() {
    int N = 2000; 
    double box_size = 20.0;
    
    printf("=======================================================\n");
    printf("Molecular Dynamics - Lennard-Jones Force Calculation\n");
    printf("=======================================================\n");
    printf("Max threads available: %d\n\n", omp_get_max_threads());
  
    Vector3D *positions = (Vector3D*)malloc(N*sizeof(Vector3D));
    Vector3D *forces = (Vector3D*)malloc(N*sizeof(Vector3D));
    
    srand(42);
    initialize_particles(positions, N, box_size);
    
    // Serial version (baseline)
    printf("Running serial version...\n");
    double energy_serial;
    double start = omp_get_wtime();
    compute_forces_serial(positions,forces,N,&energy_serial);
    double t_serial = omp_get_wtime()-start;
    printf("Serial time: %.6f s, Energy: %.6f\n", t_serial, energy_serial);
    print_computational_metrics(N, t_serial);
    
    // Test atomic version
    printf("\nRunning parallel version (atomic)...\n");
    double energy_atomic;
    start = omp_get_wtime();
    compute_forces_parallel_atomic(positions, forces, N, &energy_atomic);
    double t_atomic = omp_get_wtime() - start;
    printf("Atomic time: %.6f s, Energy: %.6f\n", t_atomic, energy_atomic);
    print_computational_metrics(N, t_atomic);
    
    // Test optimized version (recommended)
    printf("\nRunning parallel version (optimized with private arrays)...\n");
    double energy_optimized;
    start = omp_get_wtime();
    compute_forces_parallel_optimized(positions, forces, N, &energy_optimized);
    double t_optimized = omp_get_wtime() - start;
    printf("Optimized time: %.6f s, Energy: %.6f\n", t_optimized, energy_optimized);
    print_computational_metrics(N, t_optimized);
    
    int num_threads = omp_get_max_threads();
    print_performance_metrics(N,num_threads,t_serial,t_optimized,energy_serial,energy_optimized);
    
    // Strong scaling study
    printf("========== Strong Scaling Study ==========\n");
    printf("%-10s %-15s %-10s %-12s %-12s\n", "Threads", "Time (s)", "Speedup", "Efficiency", "GFLOPS/s");
    printf("------------------------------------------------------------\n");
    
    for (int threads = 1; threads <= omp_get_max_threads(); threads *= 2) {
        omp_set_num_threads(threads);
        double energy_temp;
        start = omp_get_wtime();
        compute_forces_parallel_optimized(positions, forces, N, &energy_temp);
        double t_parallel = omp_get_wtime() - start;
        
        double speedup = t_serial / t_parallel;
        double efficiency = speedup / threads;
        
        // Calculate GFLOPS for this run
        long long num_pairs = (long long)N * (N - 1) / 2;
        long long total_flops = num_pairs * 40;
        double gflops = total_flops / (t_parallel * 1e9);
        
        printf("%-10d %-15.6f %-10.2f %-12.2f%% %-12.2f\n", 
               threads, t_parallel, speedup, efficiency * 100, gflops);
    }
    printf("==========================================================\n");
    
    omp_set_num_threads(omp_get_max_threads());
    test_scheduling_strategies(positions, forces, N, omp_get_max_threads());
    
    free(positions);
    free(forces);
    
    printf("Computation complete!\n");
    return 0;
}

