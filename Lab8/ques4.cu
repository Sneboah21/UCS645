#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cuda_runtime.h>
#include <cublas_v2.h>

#define CUDA_CHECK(call) {                                       \
    cudaError_t err = call;                                      \
    if(err != cudaSuccess){                                      \
        fprintf(stderr,"CUDA error %s:%d: %s\n",                \
            __FILE__,__LINE__,cudaGetErrorString(err));          \
        exit(EXIT_FAILURE);                                      \
    }                                                            \
}

#define TILE_SIZE 16
#define BLOCK_SIZE 256


__global__ void tiledMatMul(const float* A, const float* B, float* C,
                             int M, int N, int K){
    __shared__ float tileA[TILE_SIZE][TILE_SIZE];
    __shared__ float tileB[TILE_SIZE][TILE_SIZE];

    int row = blockIdx.y * TILE_SIZE + threadIdx.y;
    int col = blockIdx.x * TILE_SIZE + threadIdx.x;

    float sum = 0.0f;

    for(int t = 0; t < (K + TILE_SIZE - 1) / TILE_SIZE; t++){

        int aCol = t * TILE_SIZE + threadIdx.x;
        tileA[threadIdx.y][threadIdx.x] = (row < M && aCol < K) ?
            A[row * K + aCol] : 0.0f;

        int bRow = t * TILE_SIZE + threadIdx.y;
        tileB[threadIdx.y][threadIdx.x] = (bRow < K && col < N) ?
            B[bRow * N + col] : 0.0f;

        __syncthreads();
        for(int k = 0; k < TILE_SIZE; k++)
            sum += tileA[threadIdx.y][k] * tileB[k][threadIdx.x];
        __syncthreads();
    }

    if(row < M && col < N) C[row * N + col] = sum;
}


__global__ void naiveMatMul(const float* A, const float* B, float* C,
                             int M, int N, int K){
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    if(row < M && col < N){
        float sum = 0.0f;
        for(int k = 0; k < K; k++) sum += A[row*K+k] * B[k*N+col];
        C[row*N+col] = sum;
    }
}


__global__ void maxPool2x2(const float* input, float* output,
                            int N, int C, int H, int W){
    int out_h = H / 2, out_w = W / 2;
    int total = N * C * out_h * out_w;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if(idx >= total) return;

    int w_out = idx % out_w;
    int h_out = (idx / out_w) % out_h;
    int c     = (idx / (out_w * out_h)) % C;
    int n     = idx / (out_w * out_h * C);

    int h_in = h_out * 2, w_in = w_out * 2;
    float max_val = -1e30f;
    for(int dh = 0; dh < 2; dh++)
        for(int dw = 0; dw < 2; dw++){
            float v = input[((n*C + c)*H + (h_in+dh))*W + (w_in+dw)];
            max_val = fmaxf(max_val, v);
        }
    output[idx] = max_val;
}


__global__ void batchNormInference(const float* input, float* output,
                                    const float* gamma, const float* beta,
                                    const float* mean, const float* var,
                                    int N, int C, int H, int W, float eps){
    int total = N * C * H * W;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if(idx >= total) return;

    int c = (idx / (H * W)) % C;
    output[idx] = gamma[c] * (input[idx] - mean[c]) / sqrtf(var[c] + eps) + beta[c];
}

void benchmarkGEMM(){
    cublasHandle_t handle;
    cublasCreate(&handle);

    int sizes[] = {128, 256, 512, 1024, 2048};
    printf("\n=== GEMM Benchmark ===\n");
    printf("%-6s %-12s %-12s %-12s %-12s %-12s %-12s\n",
           "Size", "Naive_ms", "Tiled_ms", "cuBLAS_ms",
           "Naive_GFLOPS", "Tiled_GFLOPS", "cuBLAS_GFLOPS");

    for(int s = 0; s < 5; s++){
        int M = sizes[s], N = sizes[s], K = sizes[s];
        size_t bytes_A = M * K * sizeof(float);
        size_t bytes_B = K * N * sizeof(float);
        size_t bytes_C = M * N * sizeof(float);
        double flops = 2.0 * M * N * K;

        float *dA, *dB, *dC;
        CUDA_CHECK(cudaMalloc(&dA, bytes_A));
        CUDA_CHECK(cudaMalloc(&dB, bytes_B));
        CUDA_CHECK(cudaMalloc(&dC, bytes_C));

        float *hA = (float*)malloc(bytes_A);
        float *hB = (float*)malloc(bytes_B);
        for(int i = 0; i < M*K; i++) hA[i] = (float)rand()/RAND_MAX;
        for(int i = 0; i < K*N; i++) hB[i] = (float)rand()/RAND_MAX;
        CUDA_CHECK(cudaMemcpy(dA, hA, bytes_A, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(dB, hB, bytes_B, cudaMemcpyHostToDevice));

        cudaEvent_t eS, eE;
        CUDA_CHECK(cudaEventCreate(&eS)); CUDA_CHECK(cudaEventCreate(&eE));

        dim3 block2D(TILE_SIZE, TILE_SIZE);
        dim3 grid2D((N+TILE_SIZE-1)/TILE_SIZE, (M+TILE_SIZE-1)/TILE_SIZE);

        CUDA_CHECK(cudaEventRecord(eS));
        naiveMatMul<<<grid2D, block2D>>>(dA, dB, dC, M, N, K);
        CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
        float naive_ms; CUDA_CHECK(cudaEventElapsedTime(&naive_ms, eS, eE));

        CUDA_CHECK(cudaEventRecord(eS));
        tiledMatMul<<<grid2D, block2D>>>(dA, dB, dC, M, N, K);
        CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
        float tiled_ms; CUDA_CHECK(cudaEventElapsedTime(&tiled_ms, eS, eE));

        float alpha = 1.0f, beta_v = 0.0f;
        CUDA_CHECK(cudaEventRecord(eS));
        cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                    N, M, K, &alpha, dB, N, dA, K, &beta_v, dC, N);
        CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
        float cublas_ms; CUDA_CHECK(cudaEventElapsedTime(&cublas_ms, eS, eE));

        printf("%-6d %-12.3f %-12.3f %-12.3f %-12.2f %-12.2f %-12.2f\n",
               M,
               naive_ms,  tiled_ms,  cublas_ms,
               flops/naive_ms/1e6,  flops/tiled_ms/1e6,  flops/cublas_ms/1e6);

        free(hA); free(hB);
        CUDA_CHECK(cudaFree(dA)); CUDA_CHECK(cudaFree(dB)); CUDA_CHECK(cudaFree(dC));
        CUDA_CHECK(cudaEventDestroy(eS)); CUDA_CHECK(cudaEventDestroy(eE));
    }
    cublasDestroy(handle);
}


__global__ void im2colKernel(const float* input, float* col,
                              int N, int C, int H, int W,
                              int kH, int kW, int H_out, int W_out,
                              int pad, int stride){
    int total = N * C * kH * kW * H_out * W_out;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if(idx >= total) return;

    int w_out = idx % W_out;
    int h_out = (idx / W_out) % H_out;
    int kw    = (idx / (W_out * H_out)) % kW;
    int kh    = (idx / (W_out * H_out * kW)) % kH;
    int c     = (idx / (W_out * H_out * kW * kH)) % C;
    int n     = idx / (W_out * H_out * kW * kH * C);

    int h_in = h_out * stride - pad + kh;
    int w_in = w_out * stride - pad + kw;

    float val = 0.0f;
    if(h_in >= 0 && h_in < H && w_in >= 0 && w_in < W)
        val = input[((n * C + c) * H + h_in) * W + w_in];

    int col_idx = ((n * (C * kH * kW) + (c * kH * kW + kh * kW + kw)) * H_out + h_out) * W_out + w_out;
    col[col_idx] = val;
}

int main(){
    printf("===== Assignment 8 Problem 4: CNN Layers =====\n");

    benchmarkGEMM();

    int bN=32, C_in=64, H=14, W=14;
    int total_feat = bN * C_in * H * W;
    size_t feat_bytes = total_feat * sizeof(float);

    float *d_input, *d_output;
    CUDA_CHECK(cudaMalloc(&d_input, feat_bytes));

    int H_out = H/2, W_out = W/2;
    size_t pool_bytes = bN * C_in * H_out * W_out * sizeof(float);
    CUDA_CHECK(cudaMalloc(&d_output, pool_bytes));

    cudaEvent_t eS, eE;
    CUDA_CHECK(cudaEventCreate(&eS)); CUDA_CHECK(cudaEventCreate(&eE));
    int pb = (bN*C_in*H_out*W_out + BLOCK_SIZE - 1) / BLOCK_SIZE;

    CUDA_CHECK(cudaEventRecord(eS));
    maxPool2x2<<<pb, BLOCK_SIZE>>>(d_input, d_output, bN, C_in, H, W);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float pool_ms; CUDA_CHECK(cudaEventElapsedTime(&pool_ms, eS, eE));

    float *d_gamma, *d_beta, *d_mean, *d_var, *d_bn_out;
    CUDA_CHECK(cudaMalloc(&d_gamma, C_in*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_beta,  C_in*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_mean,  C_in*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_var,   C_in*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_bn_out, feat_bytes));

    float ones[64], zeros[64];
    for(int i=0;i<64;i++){ones[i]=1.0f; zeros[i]=0.0f;}
    CUDA_CHECK(cudaMemcpy(d_gamma, ones,  C_in*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_beta,  zeros, C_in*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_mean,  zeros, C_in*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_var,   ones,  C_in*sizeof(float), cudaMemcpyHostToDevice));

    int bn_blocks = (total_feat + BLOCK_SIZE - 1) / BLOCK_SIZE;
    CUDA_CHECK(cudaEventRecord(eS));
    batchNormInference<<<bn_blocks, BLOCK_SIZE>>>(d_input, d_bn_out,
        d_gamma, d_beta, d_mean, d_var, bN, C_in, H, W, 1e-5f);
    CUDA_CHECK(cudaEventRecord(eE)); CUDA_CHECK(cudaEventSynchronize(eE));
    float bn_ms; CUDA_CHECK(cudaEventElapsedTime(&bn_ms, eS, eE));

    printf("\n=== CNN Layer Timing [32,64,14,14] ===\n");
    printf("%-15s %-12s\n", "Layer", "Time(ms)");
    printf("%-15s %-12.4f\n", "MaxPool(2x2)", pool_ms);
    printf("%-15s %-12.4f\n", "BatchNorm-Inf", bn_ms);
    printf("(Conv2D requires cuDNN — see PyTorch benchmark below)\n");

    CUDA_CHECK(cudaFree(d_input)); CUDA_CHECK(cudaFree(d_output));
    CUDA_CHECK(cudaFree(d_gamma)); CUDA_CHECK(cudaFree(d_beta));
    CUDA_CHECK(cudaFree(d_mean));  CUDA_CHECK(cudaFree(d_var));
    CUDA_CHECK(cudaFree(d_bn_out));
    return 0;
}