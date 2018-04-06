/*
Copyright(c) 2018 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/

/** \example xsample02.cpp
  Counting sort using bit-vector and sparse vector to build histogram of unsigned ints.
  Benchmark compares different histogram buiding techniques using BitMagic and std::sort()
  
*/


#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include <chrono>
#include <algorithm>
#include <random>
#include <stdexcept>

#include <future>
#include <thread>

#include "bm.h"
#include "bmtimer.h"
#include "bmsparsevec.h"


// ----------------------------------------------------
// Global parameters and types
// ----------------------------------------------------

const unsigned  value_max = 1250000;
const unsigned  test_size = 100000000;

std::random_device rand_dev;
std::mt19937 gen(rand_dev());
std::uniform_int_distribution<> rand_dis(1, value_max); // generate uniform numebrs for [1, vector_max]


typedef bm::sparse_vector<bm::id_t, bm::bvector<> > sparse_vector_u32;


// timing storage for benchmarking
bm::chrono_taker::duration_map_type  timing_map;


static
void counting_sort_naive(sparse_vector_u32& sv_out, const std::vector<unsigned>& vin)
{
    for (auto v : vin)
    {
        auto count = sv_out.get(v);
        sv_out.set(v, count + 1);
    }
}


static
void counting_sort(sparse_vector_u32& sv_out, const std::vector<unsigned>& vin)
{
    for(auto v : vin)
        sv_out.inc(v);
}


inline 
unsigned counting_sort_subbatch(sparse_vector_u32* sv_out, const std::vector<unsigned>* vin)
{
    for (size_t i = 0; i < vin->size(); i++)
    {
        auto v = (*vin)[i];
        if ((v & 1) == 0)
            sv_out->inc(v);
    }
    return 0;
}

static
void counting_sort_parallel(sparse_vector_u32& sv_out, const std::vector<unsigned>& vin)
{
    sparse_vector_u32 sv_out2(bm::use_null);
    std::future<unsigned> f1 = std::async(std::launch::async, counting_sort_subbatch, &sv_out2, &vin);

    // process all even elements
    //
    for (size_t i = 0; i < vin.size(); i++)
    {
        auto v = vin[i];
        if (v & 1)
            sv_out.inc(v);
    }
    f1.wait();
    sv_out.join(sv_out2);
}

static
void print_sorted(const sparse_vector_u32& sv)
{
    const sparse_vector_u32::bvector_type* bv_null = sv.get_null_bvector();
    sparse_vector_u32::bvector_type::enumerator en = bv_null->first();
    
    for (; en.valid(); ++en)
    {
        unsigned v = *en;
        unsigned cnt = sv.get(v);
        for (unsigned j = 0; j < cnt; ++j)
        {
            std::cout << v << ", ";
        } // for
    } // for en
    std::cout << std::endl;
}

// build histogram using sorted vector
static
void build_histogram(sparse_vector_u32& sv_out, const std::vector<unsigned>& vin)
{
    if (vin.empty())
        return;
    unsigned start = vin[0];
    unsigned count = 0; // histogram counter
    for (auto v : vin)
    {
        if (v == start)
        {
            ++count;
        }
        else
        {
            sv_out.set(start, count);
            start = v; count = 1; 
        }
    }
    if (count)
    {
        sv_out.set(start, count);
    }
}


int main(void)
{
    try
    {
        // try simple input vector as a model
        //
        {
            std::vector<unsigned> v {10, 1, 5, 4, 8, 8, 8} ;
            sparse_vector_u32 r_sv(bm::use_null);  // result vector
            
            counting_sort(r_sv, v);

            print_sorted(r_sv); // 1, 4, 5, 8, 8, 8, 10,

            std::sort(v.begin(), v.end());
            sparse_vector_u32 h_sv(bm::use_null);  // histogram vector
            build_histogram(h_sv, v);
            if (!r_sv.equal(h_sv))
            {
                std::cerr << "Error: Histogram comparison failed!" << std::endl;
                print_sorted(h_sv);
                return 1;
            }

            sparse_vector_u32 p_sv(bm::use_null);
            counting_sort_parallel(p_sv, v);
            print_sorted(r_sv); 
        }
        
        // run benchmarks
        //
        std::vector<unsigned> v;
        
        // generate vector of random numbers
        for (unsigned i = 0; i < test_size; ++i)
        {
            v.push_back(rand_dis(gen));
        }
        std::cout << "test vector generation ok" << std::endl;

        sparse_vector_u32 r_sv(bm::use_null);
        sparse_vector_u32 h_sv(bm::use_null);
        sparse_vector_u32 n_sv(bm::use_null);
        sparse_vector_u32 p_sv(bm::use_null);

        {
            bm::chrono_taker tt1("1. counting sort ", 1, &timing_map);
            counting_sort(r_sv, v);
        }

        {
            bm::chrono_taker tt1("3. counting sort (naive) ", 1, &timing_map);
            counting_sort_naive(n_sv, v);
        }

        {
            bm::chrono_taker tt1("2. std::sort() + histogram", 1, &timing_map);
            std::sort(v.begin(), v.end());
            build_histogram(h_sv, v);
        }

        {
            bm::chrono_taker tt1("4. counting sort (parallel) ", 1, &timing_map);
            counting_sort_parallel(p_sv, v);
        }


        // quality assurance checks
        //
        if (!r_sv.equal(h_sv) || !n_sv.equal(h_sv)) 
        {
            std::cerr << "Error: Histogram comparison failed!" << std::endl;
            return 1;
        }
        if (!r_sv.equal(p_sv))
        {
            std::cerr << "Error: Histogram comparison failed for parallel sort!" << std::endl;
            return 1;
        }

        bm::chrono_taker::print_duration_map(timing_map, bm::chrono_taker::ct_ops_per_sec);

    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

