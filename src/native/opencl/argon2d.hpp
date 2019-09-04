/**
MIT License

Copyright (c) 2016 Ondrej Mosnáček

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
* Argon2d
* refined version of https://gitlab.com/omos/argon2-gpu
*/
#include <string>

std::string srcArgon2d{R"====(
#define ARGON2_BLOCK_SIZE 1024
#define ARGON2_QWORDS_IN_BLOCK (ARGON2_BLOCK_SIZE / 8)
#define MEMORY_COST 512

#define THREADS_PER_LANE 32

#define u64_build(hi, lo)   (as_ulong((uint2)((lo), (hi))))
#define u64_lo(x)   (as_uint2(x).s0)
#define u64_hi(x)   (as_uint2(x).s1)

struct block_g
{
    ulong data[ARGON2_QWORDS_IN_BLOCK];
};

struct block_th
{
    ulong a, b, c, d;
};

#define ROUND1_IDX(x) (((thread & 0x1c) << 2) | (x << 2) | (thread & 0x3))
#define ROUND2_IDX(x) (((thread & 0x1c) << 2) | (x << 2) | ((thread + x) & 0x3))
#define ROUND3_IDX(x) ((x << 5) | ((thread & 0x2) << 3) | ((thread & 0x1c) >> 1) | (thread & 0x1))
#define ROUND4_IDX(x) ((x << 5) | (((thread + x) & 0x2) << 3) | ((thread & 0x1c) >> 1) | ((thread + x) & 0x1))

#define IDX_X(r, x) (r == 1 ? (ROUND1_IDX(x)) : (r == 2 ? (ROUND2_IDX(x)) : (r == 3 ? (ROUND3_IDX(x)) : ROUND4_IDX(x))))
#define IDX_A(r) (IDX_X(r, 0))
#define IDX_B(r) (IDX_X(r, 1))
#define IDX_C(r) (IDX_X(r, 2))
#define IDX_D(r) (IDX_X(r, 3))

void move_block(struct block_th *dst, const struct block_th *src)
{
    *dst = *src;
}

void xor_block(struct block_th *dst, const struct block_th *src)
{
    dst->a ^= src->a;
    dst->b ^= src->b;
    dst->c ^= src->c;
    dst->d ^= src->d;
}

void load_block_global(struct block_th *dst, __global const struct block_g *src, uint thread)
{
    dst->a = src->data[0 * THREADS_PER_LANE + thread];
    dst->b = src->data[1 * THREADS_PER_LANE + thread];
    dst->c = src->data[2 * THREADS_PER_LANE + thread];
    dst->d = src->data[3 * THREADS_PER_LANE + thread];
}

void load_block_local(struct block_th *dst, __local const struct block_g *src, uint thread)
{
    dst->a = src->data[0 * THREADS_PER_LANE + thread];
    dst->b = src->data[1 * THREADS_PER_LANE + thread];
    dst->c = src->data[2 * THREADS_PER_LANE + thread];
    dst->d = src->data[3 * THREADS_PER_LANE + thread];
}

void load_block_xor_global(struct block_th *dst, __global const struct block_g *src, uint thread)
{
    dst->a ^= src->data[0 * THREADS_PER_LANE + thread];
    dst->b ^= src->data[1 * THREADS_PER_LANE + thread];
    dst->c ^= src->data[2 * THREADS_PER_LANE + thread];
    dst->d ^= src->data[3 * THREADS_PER_LANE + thread];
}

void load_block_xor_local(struct block_th *dst, __local const struct block_g *src, uint thread)
{
    dst->a ^= src->data[0 * THREADS_PER_LANE + thread];
    dst->b ^= src->data[1 * THREADS_PER_LANE + thread];
    dst->c ^= src->data[2 * THREADS_PER_LANE + thread];
    dst->d ^= src->data[3 * THREADS_PER_LANE + thread];
}

void store_block_global(__global struct block_g *dst, const struct block_th *src, uint thread)
{
    dst->data[0 * THREADS_PER_LANE + thread] = src->a;
    dst->data[1 * THREADS_PER_LANE + thread] = src->b;
    dst->data[2 * THREADS_PER_LANE + thread] = src->c;
    dst->data[3 * THREADS_PER_LANE + thread] = src->d;
}

void store_block_local(__local struct block_g *dst, const struct block_th *src, uint thread)
{
    dst->data[0 * THREADS_PER_LANE + thread] = src->a;
    dst->data[1 * THREADS_PER_LANE + thread] = src->b;
    dst->data[2 * THREADS_PER_LANE + thread] = src->c;
    dst->data[3 * THREADS_PER_LANE + thread] = src->d;
}

#ifdef cl_amd_media_ops
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
ulong rotr64(ulong x, ulong n)
{
    uint lo = u64_lo(x);
    uint hi = u64_hi(x);
    uint r_lo, r_hi;
    if (n < 32) {
        r_lo = amd_bitalign(hi, lo, (uint)n);
        r_hi = amd_bitalign(lo, hi, (uint)n);
    } else {
        r_lo = amd_bitalign(lo, hi, (uint)n - 32);
        r_hi = amd_bitalign(hi, lo, (uint)n - 32);
    }
    return u64_build(r_hi, r_lo);
}
#else
ulong rotr64(ulong x, ulong n)
{
    return rotate(x, 64 - n);
}
#endif

ulong f(ulong x, ulong y)
{
    uint xlo = u64_lo(x);
    uint ylo = u64_lo(y);
    return x + y + 2 * u64_build(mul_hi(xlo, ylo), xlo * ylo);
}

void g(struct block_th *block)
{
    ulong a, b, c, d;
    a = block->a;
    b = block->b;
    c = block->c;
    d = block->d;

    a = f(a, b);
    d = rotr64(d ^ a, 32);
    c = f(c, d);
    b = rotr64(b ^ c, 24);
    a = f(a, b);
    d = rotr64(d ^ a, 16);
    c = f(c, d);
    b = rotr64(b ^ c, 63);

    block->a = a;
    block->b = b;
    block->c = c;
    block->d = d;
}

void shuffle_block(struct block_th *block, __local struct block_g *buf, uint thread)
{
    // Transpose 1
    store_block_local(buf, block, thread);
    barrier(CLK_LOCAL_MEM_FENCE);
    block->a = buf->data[IDX_A(1)];
    block->b = buf->data[IDX_B(1)];
    block->c = buf->data[IDX_C(1)];
    block->d = buf->data[IDX_D(1)];

    g(block);

    // Shuffle 1, index of A doesn't change
    buf->data[IDX_B(1)] = block->b;
    buf->data[IDX_C(1)] = block->c;
    buf->data[IDX_D(1)] = block->d;
    barrier(CLK_LOCAL_MEM_FENCE);
    block->b = buf->data[IDX_B(2)];
    block->c = buf->data[IDX_C(2)];
    block->d = buf->data[IDX_D(2)];

    g(block);

    // Shuffle 2
    buf->data[IDX_A(2)] = block->a;
    buf->data[IDX_B(2)] = block->b;
    buf->data[IDX_C(2)] = block->c;
    buf->data[IDX_D(2)] = block->d;
    barrier(CLK_LOCAL_MEM_FENCE);
    block->a = buf->data[IDX_A(3)];
    block->b = buf->data[IDX_B(3)];
    block->c = buf->data[IDX_C(3)];
    block->d = buf->data[IDX_D(3)];

    g(block);

    // Shuffle 3, index of A doesn't change
    buf->data[IDX_B(3)] = block->b;
    buf->data[IDX_C(3)] = block->c;
    buf->data[IDX_D(3)] = block->d;
    barrier(CLK_LOCAL_MEM_FENCE);
    block->b = buf->data[IDX_B(4)];
    block->c = buf->data[IDX_C(4)];
    block->d = buf->data[IDX_D(4)];

    g(block);

    // Transpose 2 + XOR
    buf->data[IDX_A(4)] = block->a;
    buf->data[IDX_B(4)] = block->b;
    buf->data[IDX_C(4)] = block->c;
    buf->data[IDX_D(4)] = block->d;
    barrier(CLK_LOCAL_MEM_FENCE);
    load_block_local(block, buf, thread);
}

uint compute_ref_index(__local struct block_g *block, uint curr_index)
{
    ulong v = block->data[0];
    uint ref_index = u64_lo(v);
    uint ref_area_size = curr_index; // -1
    ref_index = mul_hi(ref_index, ref_index);
    return ref_area_size - 1 - mul_hi(ref_area_size, ref_index);
}

__kernel
__attribute__((reqd_work_group_size(32, JOBS_PER_BLOCK, 1)))
void argon2(__local struct block_g *shmem, __global struct block_g *memory)
{
    uint job_id = get_global_id(1);
    uint warp   = get_local_id(1);
    uint thread = get_local_id(0);
    uint nonces_per_run = get_global_size(1);

    __local struct block_g *cache = &shmem[warp * CACHE_SIZE];

    memory += job_id;

    struct block_th tmp, prev, evicted;

    load_block_global(&tmp, memory, thread);
    load_block_global(&prev, memory + nonces_per_run, thread);

    // cache first blocks
    store_block_local(cache, &tmp, thread);
    store_block_local(cache + 1, &prev, thread);

    uint ref_index = 0;

    for (uint curr_index = 2; curr_index < MEMORY_COST; curr_index++)
    {
        uint ref_offset = curr_index - ref_index;
        if (ref_offset <= CACHE_SIZE)
        {
            load_block_xor_local(&prev, cache + ref_index % CACHE_SIZE, thread);
        }
        else
        {
            load_block_xor_global(&prev, memory + ref_index * nonces_per_run, thread);
        }

        __local struct block_g *curr_cache = cache + (curr_index % CACHE_SIZE);

        load_block_local(&evicted, curr_cache, thread);

        move_block(&tmp, &prev);
        shuffle_block(&prev, curr_cache, thread);
        xor_block(&prev, &tmp);

        store_block_local(curr_cache, &prev, thread);
        barrier(CLK_LOCAL_MEM_FENCE);

        ref_index = compute_ref_index(curr_cache, curr_index); // next block ref_index

        if (curr_index > CACHE_SIZE + 1)
        {
            store_block_global(memory + (curr_index - CACHE_SIZE) * nonces_per_run, &evicted, thread);
        }
    }

    store_block_global(memory + (MEMORY_COST - 1) * nonces_per_run, &prev, thread);
}
)===="};
