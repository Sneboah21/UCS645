#include <iostream>
#include <vector>
#include <algorithm>
#include <omp.h>
#include <cstring>
#include <iomanip>

using namespace std;

const int MATCH = 2;
const int MISMATCH = -1;
const int GAP = -2;

string generate_dna_sequence(int length){
    const char bases[] = {'A', 'C', 'G', 'T'};
    string sequence;
    for (int i = 0;i < length;i++) {
        sequence += bases[rand() % 4];
    }
    return sequence;
}

// Scoring function
int score(char a, char b){
    return (a == b) ? MATCH : MISMATCH;
}

void smith_waterman_serial(const string& seq1,const string& seq2,vector<vector<int>>& H,int& max_score) {
    int m = seq1.length();
    int n = seq2.length();
    max_score = 0;
    
    for (int i = 0;i <= m;i++) H[i][0] = 0;
    for (int j = 0;j <= n;j++) H[0][j] = 0;
    
    for (int i = 1;i <= m;i++){
        for (int j = 1;j <= n;j++){
            int match = H[i-1][j-1] + score(seq1[i-1], seq2[j-1]);
            int delete_gap = H[i-1][j] + GAP;
            int insert_gap = H[i][j-1] + GAP;
            
            H[i][j] = max({0, match, delete_gap, insert_gap});
            max_score = max(max_score, H[i][j]);
        }
    }
}

// Task 1 & 2: Parallel with row-wise parallelization (handles dependencies)
void smith_waterman_parallel_rows(const string& seq1,const string& seq2,vector<vector<int>>& H,int& max_score){
    int m = seq1.length();
    int n = seq2.length();
    max_score = 0;
    
    for (int i = 0;i <= m;i++) H[i][0] = 0;
    for (int j = 0;j <= n;j++) H[0][j] = 0;
    
    // Process row by row (each row depends on previous)
    for (int i = 1;i <= m;i++){
        int local_max = 0;
        
        #pragma omp parallel for reduction(max:local_max) schedule(static)
        for (int j = 1;j <= n;j++) {
            int match = H[i-1][j-1] + score(seq1[i-1], seq2[j-1]);
            int delete_gap = H[i-1][j] + GAP;
            int insert_gap = H[i][j-1] + GAP;
            
            H[i][j] = max({0, match, delete_gap, insert_gap});
            local_max = max(local_max, H[i][j]);
        }
        
        max_score = max(max_score, local_max);
    }
}

// Task 4: Wavefront (Anti-diagonal) Parallelization
void smith_waterman_wavefront(const string& seq1,const string& seq2,vector<vector<int>>& H,int& max_score){
    int m = seq1.length();
    int n = seq2.length();
    max_score = 0;
    
    for (int i = 0; i <= m; i++) H[i][0] = 0;
    for (int j = 0; j <= n; j++) H[0][j] = 0;
    
    for (int diag = 2;diag <= m + n;diag++){
        int start_i = max(1, diag - n);
        int end_i = min(m, diag - 1);
        
        int local_max = 0;
        
        #pragma omp parallel for reduction(max:local_max) schedule(dynamic, 10)
        for (int i = start_i;i <= end_i;i++){
            int j = diag - i;
            if (j >= 1 && j <= n){
                int match = H[i-1][j-1] + score(seq1[i-1], seq2[j-1]);
                int delete_gap = H[i-1][j] + GAP;
                int insert_gap = H[i][j-1] + GAP;
                
                H[i][j] = max({0, match, delete_gap, insert_gap});
                local_max = max(local_max, H[i][j]);
            }
        }
        
        max_score = max(max_score, local_max);
    }
}

void print_computational_metrics(int m, int n, double time_seconds) {
    
    long long num_cells = (long long)m * n;
    long long total_ops = num_cells * 5;
    double mops = total_ops / (time_seconds * 1e6); // Million operations per second
    
    long long total_bytes = num_cells * 18;
    double bandwidth_mb_s = total_bytes / (time_seconds * 1e6); // MB/s
    double bandwidth_gb_s = bandwidth_mb_s / 1024.0; // GB/s
    
    // Computational intensity
    double comp_intensity = (double)total_ops / total_bytes;
    
    cout << "\n========== Computational Metrics ==========\n";
    cout << "Total cells computed:  " << num_cells << "\n";
    cout << "Total operations:      " << fixed << setprecision(2) 
         << (total_ops / 1e6) << " million\n";
    cout << "Performance:           " << mops << " MOPS (Million Ops/s)\n";
    cout << "Memory accessed:       " << (total_bytes / 1e6) << " MB\n";
    cout << "Memory bandwidth:      " << setprecision(2) << bandwidth_gb_s << " GB/s\n";
    cout << "Comp. intensity:       " << setprecision(3) << comp_intensity << " ops/byte\n";
    cout << "===========================================\n\n";
}

// Task 3: Test different scheduling strategies
void test_scheduling_strategies(const string& seq1, const string& seq2, int m, int n) {
    cout << "\n========== Scheduling Strategy Comparison ==========\n";
    
    vector<vector<int>> H(m+1, vector<int>(n+1));
    int max_score;
    
    // Static scheduling
    double start = omp_get_wtime();
    smith_waterman_parallel_rows(seq1, seq2, H, max_score);
    double time_static = omp_get_wtime() - start;
    cout << "Row-wise (Static):    " << fixed << setprecision(6) << time_static << " s (Score: " << max_score << ")\n";
    print_computational_metrics(m, n, time_static);
    
    // Wavefront with dynamic scheduling
    fill(H.begin(), H.end(), vector<int>(n+1, 0));
    start = omp_get_wtime();
    smith_waterman_wavefront(seq1, seq2, H, max_score);
    double time_wavefront = omp_get_wtime() - start;
    cout << "Wavefront (Dynamic):  " << time_wavefront << " s (Score: " << max_score << ")\n";
    print_computational_metrics(m, n, time_wavefront);
    
    cout << "====================================================\n\n";
}

void print_performance_metrics(int m, int n, int num_threads, 
                              double t_serial, double t_parallel,
                              int score_serial, int score_parallel) {
    double speedup = t_serial / t_parallel;
    double efficiency = speedup / num_threads;
    double cost = num_threads * t_parallel;
    
    long long num_cells = (long long)m * n;
    double throughput = num_cells / t_parallel;
    
    cout << "\n========== Performance Metrics ==========\n";
    cout << "Matrix size: " << m << " x " << n << " (" << num_cells << " cells)\n";
    cout << "Threads: " << num_threads << "\n";
    cout << "Serial Time (T1):      " << fixed << setprecision(6) << t_serial << " s\n";
    cout << "Parallel Time (Tp):    " << t_parallel << " s\n";
    cout << "Speedup S(p):          " << setprecision(2) << speedup << "\n";
    cout << "Efficiency E(p):       " << (efficiency * 100) << "% (>70% is good)\n";
    cout << "Cost (p × Tp):         " << setprecision(6) << cost << " s (ideal = " << t_serial << " s)\n";
    cout << "Throughput:            " << setprecision(2) << (throughput / 1e6) << " M cells/s\n";
    cout << "Max Score (serial):    " << score_serial << "\n";
    cout << "Max Score (parallel):  " << score_parallel << "\n";
    cout << "=========================================\n\n";
}

int main() {
    int m = 2000;  // Length of sequence 1
    int n = 2000;  // Length of sequence 2
    
    cout << "=======================================================\n";
    cout << "Smith-Waterman Local Sequence Alignment\n";
    cout << "UCS645 Assignment 2 - Question 2\n";
    cout << "=======================================================\n";
    cout << "Max threads available: " << omp_get_max_threads() << "\n\n";
    
    // Generate random DNA sequences
    srand(42);
    string seq1 = generate_dna_sequence(m);
    string seq2 = generate_dna_sequence(n);
    
    cout << "Sequence 1 length: " << m << "\n";
    cout << "Sequence 2 length: " << n << "\n";
    cout << "Matrix cells: " << (long long)m * n << "\n\n";
    
    vector<vector<int>> H(m+1, vector<int>(n+1));
    
    // Serial version
    cout << "Running serial version...\n";
    int max_score_serial;
    double start = omp_get_wtime();
    smith_waterman_serial(seq1, seq2, H, max_score_serial);
    double t_serial = omp_get_wtime() - start;
    cout << "Serial time: " << fixed << setprecision(6) << t_serial 
         << " s, Max Score: " << max_score_serial << "\n";
    print_computational_metrics(m, n, t_serial);
    
    // Parallel row-wise version
    cout << "\nRunning parallel version (row-wise)...\n";
    fill(H.begin(), H.end(), vector<int>(n+1, 0));
    int max_score_parallel;
    start = omp_get_wtime();
    smith_waterman_parallel_rows(seq1, seq2, H, max_score_parallel);
    double t_parallel_rows = omp_get_wtime() - start;
    cout << "Parallel (rows) time: " << t_parallel_rows 
         << " s, Max Score: " << max_score_parallel << "\n";
    print_computational_metrics(m, n, t_parallel_rows);
    
    // Wavefront version
    cout << "\nRunning parallel version (wavefront)...\n";
    fill(H.begin(), H.end(), vector<int>(n+1, 0));
    int max_score_wavefront;
    start = omp_get_wtime();
    smith_waterman_wavefront(seq1, seq2, H, max_score_wavefront);
    double t_wavefront = omp_get_wtime() - start;
    cout << "Wavefront time: " << t_wavefront 
         << " s, Max Score: " << max_score_wavefront << "\n";
    print_computational_metrics(m, n, t_wavefront);
    
    // Performance metrics
    int num_threads = omp_get_max_threads();
    print_performance_metrics(m, n, num_threads, t_serial, t_wavefront,
                             max_score_serial, max_score_wavefront);
    
    // Strong scaling study
    cout << "========== Strong Scaling Study ==========\n";
    cout << left << setw(10) << "Threads" << setw(15) << "Time (s)" 
         << setw(10) << "Speedup" << setw(12) << "Efficiency" 
         << setw(12) << "MOPS\n";
    cout << "------------------------------------------------------------\n";
    
    for (int threads = 1; threads <= omp_get_max_threads(); threads *= 2) {
        omp_set_num_threads(threads);
        fill(H.begin(), H.end(), vector<int>(n+1, 0));
        int temp_score;
        
        start = omp_get_wtime();
        smith_waterman_wavefront(seq1, seq2, H, temp_score);
        double t_parallel = omp_get_wtime() - start;
        
        double speedup = t_serial / t_parallel;
        double efficiency = speedup / threads;
        
        // Calculate MOPS for this run
        long long num_cells = (long long)m * n;
        long long total_ops = num_cells * 5;
        double mops = total_ops / (t_parallel * 1e6);
        
        cout << left << setw(10) << threads << setw(15) << setprecision(6) << t_parallel 
             << setw(10) << setprecision(2) << speedup 
             << setw(12) << setprecision(2) << (efficiency * 100) << "%"
             << setw(12) << setprecision(2) << mops << "\n";
    }
    cout << "==========================================================\n";
    
    // Test scheduling strategies
    omp_set_num_threads(omp_get_max_threads());
    test_scheduling_strategies(seq1, seq2, m, n);
    
    cout << "Computation complete!\n";
    return 0;
}

