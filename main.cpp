#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <array>

namespace {

static const size_t NUM_THREADS = 16;
static const size_t NUM_THREADS_PER_COMBINER = 4;
static const size_t NUM_COMBINERS = 4;
static_assert(NUM_THREADS == NUM_THREADS_PER_COMBINER * NUM_COMBINERS);
static const size_t COUNT_PER_THREAD = 10000000;

static std::atomic<uint64_t> counter_simple{0};

static uint64_t getAndIncrementCas(){
    return counter_simple++;
}

void caseSimple(){
    uint64_t total_operations = COUNT_PER_THREAD * NUM_THREADS;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    auto start_time = std::chrono::high_resolution_clock::now();

    std::atomic<uint64_t> total;

    // Launch threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([&total]() {
            uint64_t my_total = 0;
            for (uint64_t i = 0; i < COUNT_PER_THREAD; ++i) {
                my_total += getAndIncrementCas();
            }
            total += my_total;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    double seconds = duration / 1000.0;
    double throughput = total_operations / seconds;

    std::cout << "\n=== Results Simple CAS ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_simple.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
}


static std::atomic<uint64_t> counter_lock{0};

uint64_t getAndIncrementLock(){
    static std::mutex mutex;
    std::lock_guard guard(mutex);
    return counter_lock++;
}

void caseLock(){
    uint64_t total_operations = COUNT_PER_THREAD * NUM_THREADS;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    auto start_time = std::chrono::high_resolution_clock::now();

    std::atomic<uint64_t> total;

    // Launch threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([&total]() {
            uint64_t my_total = 0;
            for (uint64_t i = 0; i < COUNT_PER_THREAD; ++i) {
                my_total += getAndIncrementLock();
            }
            total += my_total;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - start_time).count();

    double seconds = duration / 1000.0;
    double throughput = total_operations / seconds;

    std::cout << "\n=== Results Lock ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_lock.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
}

static std::atomic<uint64_t> counter_combiner{0};

template <size_t NUMBER>
class Combiner{
    std::array<std::atomic<bool>, NUMBER> interested;
    std::atomic<uint32_t> queued;
    std::mutex lock;
    std::array<std::atomic<uint64_t>, NUMBER> sequence_numbers;

public:

    Combiner(){
        queued = 0;
        for(auto& val : interested){
            val = false;
        }
    }

    uint64_t getAndIncrement(size_t my_id){
        interested.at(my_id).store(true);
        queued.fetch_add(1);

        {
            std::unique_lock guard(lock);

            // happy path, someone else already got the number for me:
            if(!interested[my_id]){
                uint64_t my_sequence_number = sequence_numbers[my_id];
                return my_sequence_number;
            }

            uint64_t numbers_needed_total = queued.exchange(0);

            while(numbers_needed_total == 0){
                numbers_needed_total = queued.exchange(0);
            }

            uint64_t distribute_range_lower = counter_combiner.fetch_add(numbers_needed_total);
            uint64_t distribute_range_upper = distribute_range_lower + numbers_needed_total;

            uint64_t my_sequence_number = distribute_range_lower;

            uint64_t current_number_to_distribute = my_sequence_number + 1;
            for(size_t i = 0; i < NUMBER && current_number_to_distribute < distribute_range_upper; i++){
                if(i != my_id && interested[i]){
                    interested[i] = false;
                    sequence_numbers[i] = current_number_to_distribute++;
                }
            }
            interested[my_id] = false;
            return my_sequence_number;
        }
    }
};

void caseCombiner(){
    uint64_t total_operations = COUNT_PER_THREAD * NUM_THREADS;

    std::vector<std::unique_ptr<Combiner<NUM_THREADS_PER_COMBINER>>> combiners;
    combiners.reserve(NUM_COMBINERS);
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int combiner_i = 0; combiner_i < NUM_COMBINERS; combiner_i++) {
        auto combiner = std::make_unique<Combiner<NUM_THREADS_PER_COMBINER>>();
        combiners.emplace_back(std::move(combiner));
    }

    std::atomic<uint64_t> total;

    // Launch threads
    for (int combiner_i = 0; combiner_i < NUM_COMBINERS; combiner_i++) {
        for (int thread_i = 0; thread_i < NUM_THREADS_PER_COMBINER; thread_i++) {
            threads.emplace_back([thread_i, combiner_i, &combiners, &total]() {
                uint64_t my_total = 0;
                auto &my_combiner = combiners.at(combiner_i);
                for (uint64_t i = 0; i < COUNT_PER_THREAD; ++i) {
                    my_total += my_combiner->getAndIncrement(thread_i);
                }
                total += my_total;
            });
        }
    }

    for (auto& thread : threads) {
        thread.join();
    }


    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    double seconds = duration / 1000.0;
    double throughput = total_operations / seconds;

    std::cout << "\n=== Results Combiner ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_combiner.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
}
}  // namespace

int main() {
    caseLock();
    caseSimple();
    caseCombiner();
    return 0;
}
