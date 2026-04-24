#include <iostream>
#include <vector>
#include <cmath>
#include <omp.h>
#include <iomanip>
#include <algorithm>

using namespace std;

class HeatDiffusion2D {
private:
    int N;                          // Grid size (N x N)
    double L;                       // Physical size of plate
    double alpha;                   // Thermal diffusivity
    double dx, dy;                  // Grid spacing
    double dt;                      // Time step
    int num_steps;                  // Number of time steps
    
    vector<vector<double>> T;       // Current temperature
    vector<vector<double>> T_new;   // Next temperature (no race conditions)
    
public:
    HeatDiffusion2D(int grid_size, double length, double diffusivity, 
                    double time_step, int steps) 
        : N(grid_size), L(length), alpha(diffusivity), dt(time_step), 
          num_steps(steps){
        
        dx = L / (N - 1);
        dy = L / (N - 1);
        
        T.resize(N, vector<double>(N, 0.0));
        T_new.resize(N, vector<double>(N, 0.0));
        
        initialize_conditions();
    }
    
    void initialize_conditions() {
        int center = N / 2;
        int radius = N / 10;
        
        for (int i = center - radius; i < center + radius; i++) {
            for (int j = center - radius; j < center + radius; j++) {
                if (i >= 0 && i < N && j >= 0 && j < N) {
                    T[i][j] = 100.0;  // Hot spot at 100°C
                }
            }
        }
        
        for (int i = 0; i < N; i++) {
            T[i][0] = T[i][N-1] = 0.0;
            T[0][i] = T[N-1][i] = 0.0;
        }
    }
    
    // Serial version (baseline)
    double simulate_serial() {
        double start = omp_get_wtime();
        
        for (int step = 0; step < num_steps; step++) {
            // Hint 1: Update has dependencies only on previous time step
            for (int i = 1; i < N - 1; i++) {
                for (int j = 1; j < N - 1; j++) {
                    double d2Tdx2 = (T[i+1][j] - 2*T[i][j] + T[i-1][j]) / (dx * dx);
                    double d2Tdy2 = (T[i][j+1] - 2*T[i][j] + T[i][j-1]) / (dy * dy);
                    
                    T_new[i][j] = T[i][j] + alpha * dt * (d2Tdx2 + d2Tdy2);
                }
            }
            
            // Swap arrays for next iteration
            swap(T, T_new);
        }
        
        return omp_get_wtime() - start;
    }
    
    // Parallel version - basic parallelization
    double simulate_parallel_basic() {
        double start = omp_get_wtime();
        
        for (int step = 0; step < num_steps; step++) {
            #pragma omp parallel for collapse(2) schedule(static)
            for (int i = 1;i < N - 1;i++){
                for (int j = 1; j < N - 1; j++){
                    double d2Tdx2 = (T[i+1][j] - 2*T[i][j] + T[i-1][j]) / (dx * dx);
                    double d2Tdy2 = (T[i][j+1] - 2*T[i][j] + T[i][j-1]) / (dy * dy);
                    
                    T_new[i][j] = T[i][j] + alpha * dt * (d2Tdx2 + d2Tdy2);
                }
            }
            
            swap(T, T_new);
        }
        
        return omp_get_wtime() - start;
    }
    
    double simulate_parallel_tiled(int tile_size) {
        double start = omp_get_wtime();
        
        for (int step = 0; step < num_steps; step++) {
            #pragma omp parallel for collapse(2) schedule(static)
            for (int ii = 1; ii < N - 1; ii += tile_size) {
                for (int jj = 1; jj < N - 1; jj += tile_size) {
                    int i_end = min(ii + tile_size, N - 1);
                    int j_end = min(jj + tile_size, N - 1);
                    
                    for (int i = ii; i < i_end; i++) {
                        for (int j = jj; j < j_end; j++) {
                            double d2Tdx2 = (T[i+1][j] - 2*T[i][j] + T[i-1][j]) / (dx * dx);
                            double d2Tdy2 = (T[i][j+1] - 2*T[i][j] + T[i][j-1]) / (dy * dy);
                            
                            T_new[i][j] = T[i][j] + alpha * dt * (d2Tdx2 + d2Tdy2);
                        }
                    }
                }
            }
            
            swap(T, T_new);
        }
        
        return omp_get_wtime() - start;
    }
    
    double simulate_parallel_dynamic(int chunk_size) {
        double start = omp_get_wtime();
        
        for (int step = 0; step < num_steps; step++) {
            #pragma omp parallel for collapse(2) schedule(dynamic, chunk_size)
            for (int i = 1; i < N - 1; i++) {
                for (int j = 1; j < N - 1; j++) {
                    double d2Tdx2 = (T[i+1][j] - 2*T[i][j] + T[i-1][j]) / (dx * dx);
                    double d2Tdy2 = (T[i][j+1] - 2*T[i][j] + T[i][j-1]) / (dy * dy);
                    
                    T_new[i][j] = T[i][j] + alpha * dt * (d2Tdx2 + d2Tdy2);
                }
            }
            
            swap(T, T_new);
        }
        
        return omp_get_wtime() - start;
    }
    
    double simulate_parallel_guided() {
        double start = omp_get_wtime();
        
        for (int step = 0; step < num_steps; step++) {
            #pragma omp parallel for collapse(2) schedule(guided)
            for (int i = 1; i < N - 1; i++) {
                for (int j = 1; j < N - 1; j++) {
                    double d2Tdx2 = (T[i+1][j] - 2*T[i][j] + T[i-1][j]) / (dx * dx);
                    double d2Tdy2 = (T[i][j+1] - 2*T[i][j] + T[i][j-1]) / (dy * dy);
                    
                    T_new[i][j] = T[i][j] + alpha * dt * (d2Tdx2 + d2Tdy2);
                }
            }
            
            swap(T, T_new);
        }
        
        return omp_get_wtime() - start;
    }
    
    double get_total_heat() {
        double total = 0.0;
        
        #pragma omp parallel for reduction(+:total) collapse(2)
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                total += T[i][j];
            }
        }
        
        return total;
    }
    
    double get_average_temperature() {
        return get_total_heat() / (N * N);
    }
    
    double get_center_temperature() {
        return T[N/2][N/2];
    }
    
    void reset() {
        fill(T.begin(), T.end(), vector<double>(N, 0.0));
        fill(T_new.begin(), T_new.end(), vector<double>(N, 0.0));
        initialize_conditions();
    }
    
    void print_stats() {
        cout << "  Center temperature: " << fixed << setprecision(2) 
             << get_center_temperature() << "°C\n";
        cout << "  Average temperature: " << get_average_temperature() << "°C\n";
        cout << "  Total heat: " << get_total_heat() << " units\n";
    }
    
    // Get grid size
    int get_N() const { return N; }
    int get_steps() const { return num_steps; }
};

void print_computational_metrics(int N, int steps, double time_seconds) {
    
    long long interior_points = (long long)(N-2) * (N-2);
    long long total_flops = interior_points * steps * 10;
    double gflops = total_flops / (time_seconds * 1e9);
    
    long long total_bytes = interior_points * steps * 6 * sizeof(double);
    double bandwidth_gb_s = total_bytes / (time_seconds * 1e9);
    
    // Arithmetic Intensity (FLOPS per byte)
    double arithmetic_intensity = (double)total_flops / total_bytes;
    
    cout << "\n========== Computational Metrics ==========\n";
    cout << "Total FLOPs:           " << fixed << setprecision(2) 
         << (total_flops / 1e9) << " GFLOPs\n";
    cout << "Performance:           " << gflops << " GFLOPS/s\n";
    cout << "Memory accessed:       " << (total_bytes / 1e9) << " GB\n";
    cout << "Memory bandwidth:      " << bandwidth_gb_s << " GB/s\n";
    cout << "Arithmetic intensity:  " << arithmetic_intensity << " FLOPs/byte\n";
    cout << "===========================================\n\n";
}

void print_performance_metrics(int N, int steps, int num_threads,
                              double t_serial, double t_parallel) {
    double speedup = t_serial / t_parallel;
    double efficiency = speedup / num_threads;
    double cost = num_threads * t_parallel;
    
    long long num_operations = (long long)(N-2) * (N-2) * steps;
    double throughput = num_operations / t_parallel;
    
    cout << "\n========== Performance Metrics ==========\n";
    cout << "Grid size: " << N << " x " << N << "\n";
    cout << "Time steps: " << steps << "\n";
    cout << "Threads: " << num_threads << "\n";
    cout << "Serial Time (T1):      " << fixed << setprecision(6) << t_serial << " s\n";
    cout << "Parallel Time (Tp):    " << t_parallel << " s\n";
    cout << "Speedup S(p):          " << setprecision(2) << speedup << "\n";
    cout << "Efficiency E(p):       " << (efficiency * 100) << "% (>70% is good)\n";
    cout << "Cost (p × Tp):         " << setprecision(6) << cost << " s (ideal = " << t_serial << " s)\n";
    cout << "Throughput:            " << setprecision(2) << (throughput / 1e6) << " M updates/s\n";
    cout << "=========================================\n\n";
}

void test_scheduling_strategies(HeatDiffusion2D& sim) {
    cout << "\n========== Scheduling Strategy Comparison ==========\n";
    
    int N = sim.get_N();
    int steps = sim.get_steps();
    
    // Static scheduling
    sim.reset();
    double time_static = sim.simulate_parallel_basic();
    cout << "Static Scheduling:    " << fixed << setprecision(6) << time_static << " s\n";
    sim.print_stats();
    print_computational_metrics(N, steps, time_static);
    
    // Dynamic scheduling
    sim.reset();
    double time_dynamic = sim.simulate_parallel_dynamic(50);
    cout << "\nDynamic (chunk=50):   " << time_dynamic << " s\n";
    sim.print_stats();
    print_computational_metrics(N, steps, time_dynamic);
    
    // Guided scheduling
    sim.reset();
    double time_guided = sim.simulate_parallel_guided();
    cout << "\nGuided Scheduling:    " << time_guided << " s\n";
    sim.print_stats();
    print_computational_metrics(N, steps, time_guided);
    
    // Tiled version
    sim.reset();
    double time_tiled = sim.simulate_parallel_tiled(32);
    cout << "\nTiled (tile=32):      " << time_tiled << " s\n";
    sim.print_stats();
    print_computational_metrics(N, steps, time_tiled);
    
    cout << "====================================================\n\n";
}

int main() {
    // Simulation parameters
    int N = 1000;          
    double L = 1.0;        
    double alpha = 0.01;   
    double dt = 0.0001;    
    int num_steps = 500;   
    
    cout << "=======================================================\n";
    cout << "2D Heat Diffusion Simulation (Finite Difference Method)\n";
    cout << "=======================================================\n";
    cout << "Max threads available: " << omp_get_max_threads() << "\n";
    cout << "Grid size: " << N << " x " << N << " (" << (long long)N*N << " points)\n";
    cout << "Time steps: " << num_steps << "\n";
    cout << "Total updates: " << (long long)(N-2)*(N-2)*num_steps << "\n\n";
    
    HeatDiffusion2D sim(N, L, alpha, dt, num_steps);
    
    // Serial simulation (baseline)
    cout << "Running serial simulation...\n";
    double t_serial = sim.simulate_serial();
    cout << "Serial time: " << fixed << setprecision(6) << t_serial << " s\n";
    sim.print_stats();
    print_computational_metrics(N, num_steps, t_serial);
    
    // Parallel basic
    sim.reset();
    cout << "\nRunning parallel simulation (basic)...\n";
    double t_parallel_basic = sim.simulate_parallel_basic();
    cout << "Parallel (basic) time: " << t_parallel_basic << " s\n";
    sim.print_stats();
    print_computational_metrics(N, num_steps, t_parallel_basic);
    
    // Parallel tiled (best performance)
    sim.reset();
    cout << "\nRunning parallel simulation (tiled for cache)...\n";
    double t_parallel_tiled = sim.simulate_parallel_tiled(32);
    cout << "Parallel (tiled) time: " << t_parallel_tiled << " s\n";
    sim.print_stats();
    print_computational_metrics(N, num_steps, t_parallel_tiled);
    
    // Performance metrics
    int num_threads = omp_get_max_threads();
    print_performance_metrics(N, num_steps, num_threads, t_serial, t_parallel_tiled);
    
    // Strong scaling study
    cout << "========== Strong Scaling Study ==========\n";
    cout << left << setw(10) << "Threads" << setw(15) << "Time (s)" 
         << setw(10) << "Speedup" << setw(12) << "Efficiency" 
         << setw(12) << "GFLOPS/s\n";
    cout << "-----------------------------------------------------------\n";
    
    for (int threads = 1; threads <= omp_get_max_threads(); threads *= 2) {
        omp_set_num_threads(threads);
        sim.reset();
        
        double t = sim.simulate_parallel_tiled(32);
        double speedup = t_serial / t;
        double efficiency = speedup / threads;
        
        // Calculate GFLOPS for this run
        long long interior_points = (long long)(N-2) * (N-2);
        long long total_flops = interior_points * num_steps * 10;
        double gflops = total_flops / (t * 1e9);
        
        cout << left << setw(10) << threads << setw(15) << setprecision(6) << t 
             << setw(10) << setprecision(2) << speedup 
             << setw(12) << setprecision(2) << (efficiency * 100) << "%"
             << setw(12) << setprecision(2) << gflops << "\n";
    }
    cout << "==========================================\n";
    
    // Test scheduling strategies
    omp_set_num_threads(omp_get_max_threads());
    test_scheduling_strategies(sim);
    
    cout << "Simulation complete!\n";
    
    return 0;
}

