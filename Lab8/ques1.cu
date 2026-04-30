#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cuda_runtime.h>
#include <time.h>

#define CUDA_CHECK(call) {                              \                   
    cudaError_t err = call;                              \          
    if(err != cudaSuccess){                                \         
        fprintf(stderr,"CUDA error %s:%d: %s\n",           \       
            __FILE__,__LINE__,cudaGetErrorString(err));    \      
        exit(EXIT_FAILURE);                                 \     
    }                                                        \     
}


__global__ void vectorAdd(const float* A, const float* B, float* C, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) C[i] = A[i] + B[i];
}

__global__ void vectorScale(float* A, float scalar, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N) A[i] *= scalar;
}



__global__ void squaredDiff(const float* A, const float* B, float* C, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        float d = A[i] - B[i];
        C[i] = d * d;
    }
}


void cpuVectorAdd(const float* A, const float* B, float* C, int N){
    for(int i = 0; i < N; i++) C[i] = A[i] + B[i];
}


void diy_launch_config(int N){
    int threads_per_block[] = {64, 128, 256, 512, 1024};
    printf("\n=== Launch Config Analysis (N=2^20=%d) ===\n", N);
    printf("%-20s %-15s %-15s\n", "ThreadsPerBlock", "BlockCount", "Coverage");
    for(int i = 0; i < 5; i++){
        int tpb = threads_per_block[i];
        int blocks = (N + tpb - 1) / tpb;
        int covered = blocks * tpb;
        printf("%-20d %-15d %-15d\n", tpb, blocks, covered);
    }
}


void measureBandwidth(){
    size_t sizes_mb[] = {1, 8, 64, 256, 512};
    printf("\n=== Memory Bandwidth Benchmark ===\n");
    printf("%-12s %-15s %-15s %-15s %-15s\n",
           "Size(MB)", "H2D_ms", "D2H_ms", "H2D_GB/s", "D2H_GB/s");

    for(int s = 0; s < 5; s++){
        size_t bytes = sizes_mb[s] * 1024 * 1024;
        float *h_data = (float*)malloc(bytes);
        float *d_data;
        CUDA_CHECK(cudaMalloc(&d_data, bytes));

        for(size_t i = 0; i < bytes/sizeof(float); i++) h_data[i] = (float)i;

        cudaEvent_t start, stop;
        CUDA_CHECK(cudaEventCreate(&start));
        CUDA_CHECK(cudaEventCreate(&stop));

        // H2D
        CUDA_CHECK(cudaEventRecord(start));
        CUDA_CHECK(cudaMemcpy(d_data, h_data, bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaEventRecord(stop));
        CUDA_CHECK(cudaEventSynchronize(stop));
        float h2d_ms; CUDA_CHECK(cudaEventElapsedTime(&h2d_ms, start, stop));

        // D2H
        CUDA_CHECK(cudaEventRecord(start));
        CUDA_CHECK(cudaMemcpy(h_data, d_data, bytes, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaEventRecord(stop));
        CUDA_CHECK(cudaEventSynchronize(stop));
        float d2h_ms; CUDA_CHECK(cudaEventElapsedTime(&d2h_ms, start, stop));

        float h2d_bw = (bytes / 1e9) / (h2d_ms / 1e3);
        float d2h_bw = (bytes / 1e9) / (d2h_ms / 1e3);

        printf("%-12zu %-15.3f %-15.3f %-15.2f %-15.2f\n",
               sizes_mb[s], h2d_ms, d2h_ms, h2d_bw, d2h_bw);

        free(h_data); CUDA_CHECK(cudaFree(d_data));
        CUDA_CHECK(cudaEventDestroy(start));
        CUDA_CHECK(cudaEventDestroy(stop));
    }
}


__global__ void divergentKernel(float* A, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        if(threadIdx.x % 2 == 0)
            A[i] = A[i] * 2.0f + 1.0f;
        else
            A[i] = A[i] * 3.0f - 1.0f;
    }
}

__global__ void branchFreeKernel(float* A, int N){
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if(i < N){
        int is_even = 1 - (threadIdx.x & 1);  // 1 if even, 0 if odd
        float scale = 2.0f + (float)(1 - is_even); // 2.0 or 3.0
        float bias  = (float)is_even - (float)(1 - is_even); // +1 or -1
        A[i] = A[i] * scale + bias;
    }
}


int main(){

    measureBandwidth();

    int N_launch = 1 << 20;
    diy_launch_config(N_launch);

    int Ns[] = {1<<10, 1<<14, 1<<18, 1<<22, 1<<26};
    int num_sizes = 5;
    int THREADS = 256;

    printf("\n=== CPU vs GPU Speedup Table ===\n");
    printf("%-10s %-12s %-12s %-12s %-10s\n",
           "N", "CPU_ms", "GPU_ms", "H2D_ms", "Speedup");

    for(int s = 0; s < num_sizes; s++){
        int N = Ns[s];
        size_t bytes = N * sizeof(float);

        float *hA = (float*)malloc(bytes);
        float *hB = (float*)malloc(bytes);
        float *hC = (float*)malloc(bytes);
        for(int i = 0; i < N; i++){ hA[i] = (float)i; hB[i] = (float)(N-i); }

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        cpuVectorAdd(hA, hB, hC, N);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        float cpu_ms = (t1.tv_sec - t0.tv_sec)*1e3 + (t1.tv_nsec - t0.tv_nsec)/1e6;

        float *dA, *dB, *dC;
        CUDA_CHECK(cudaMalloc(&dA, bytes));
        CUDA_CHECK(cudaMalloc(&dB, bytes));
        CUDA_CHECK(cudaMalloc(&dC, bytes));

        cudaEvent_t eStart, eStop;
        CUDA_CHECK(cudaEventCreate(&eStart));
        CUDA_CHECK(cudaEventCreate(&eStop));

        CUDA_CHECK(cudaEventRecord(eStart));
        CUDA_CHECK(cudaMemcpy(dA, hA, bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(dB, hB, bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaEventRecord(eStop));
        CUDA_CHECK(cudaEventSynchronize(eStop));
        float h2d_ms; CUDA_CHECK(cudaEventElapsedTime(&h2d_ms, eStart, eStop));

        int blocks = (N + THREADS - 1) / THREADS;
        CUDA_CHECK(cudaEventRecord(eStart));
        vectorAdd<<<blocks, THREADS>>>(dA, dB, dC, N);
        CUDA_CHECK(cudaEventRecord(eStop));
        CUDA_CHECK(cudaEventSynchronize(eStop));
        float gpu_ms; CUDA_CHECK(cudaEventElapsedTime(&gpu_ms, eStart, eStop));

        float speedup = cpu_ms / gpu_ms;
        printf("%-10d %-12.4f %-12.4f %-12.4f %-10.2f\n",
               N, cpu_ms, gpu_ms, h2d_ms, speedup);

        free(hA); free(hB); free(hC);
        CUDA_CHECK(cudaFree(dA)); CUDA_CHECK(cudaFree(dB)); CUDA_CHECK(cudaFree(dC));
        CUDA_CHECK(cudaEventDestroy(eStart)); CUDA_CHECK(cudaEventDestroy(eStop));
    }

    int N_warp = 1 << 22;
    size_t wb = N_warp * sizeof(float);
    float *hW = (float*)malloc(wb);
    for(int i = 0; i < N_warp; i++) hW[i] = 1.0f;
    float *dW; CUDA_CHECK(cudaMalloc(&dW, wb));
    CUDA_CHECK(cudaMemcpy(dW, hW, wb, cudaMemcpyHostToDevice));

    int wblocks = (N_warp + THREADS - 1) / THREADS;
    cudaEvent_t eS, eE;
    CUDA_CHECK(cudaEventCreate(&eS)); CUDA_CHECK(cudaEventCreate(&eE));

    CUDA_CHECK(cudaEventRecord(eS));
    divergentKernel<<<wblocks, THREADS>>>(dW, N_warp);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float div_ms; CUDA_CHECK(cudaEventElapsedTime(&div_ms, eS, eE));

    CUDA_CHECK(cudaMemcpy(dW, hW, wb, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaEventRecord(eS));
    branchFreeKernel<<<wblocks, THREADS>>>(dW, N_warp);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float bf_ms; CUDA_CHECK(cudaEventElapsedTime(&bf_ms, eS, eE));

    printf("\n=== Warp Divergence Experiment (N=2^22) ===\n");
    printf("Divergent kernel:    %.4f ms\n", div_ms);
    printf("Branch-free kernel:  %.4f ms\n", bf_ms);
    printf("Divergence overhead: %.2fx\n", div_ms / bf_ms);

    free(hW); CUDA_CHECK(cudaFree(dW));
    return 0;
}