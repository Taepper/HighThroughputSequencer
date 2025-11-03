#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

const size_t NUM_THREADS = 16;
const size_t NUM_THREADS_PER_COMBINER = 4;
const size_t NUM_COMBINERS = 4;
static_assert(NUM_THREADS == NUM_THREADS_PER_COMBINER * NUM_COMBINERS);
const size_t COUNT_PER_THREAD = 10000000;

uint64_t TOTAL_OPERATIONS = COUNT_PER_THREAD * NUM_THREADS;

std::atomic<uint64_t> counter_simple{0};

uint64_t getAndIncrementCas() {
    return counter_simple++;
}

std::chrono::milliseconds caseSimple() {
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
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto duration = millis.count();

    double seconds = static_cast<double>(duration) / 1000.0;
    double throughput = static_cast<double>(TOTAL_OPERATIONS) / seconds;

    std::cout << "\n=== Results Simple CAS ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_simple.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
    return millis;
}

std::atomic<uint64_t> counter_lock{0};

uint64_t getAndIncrementLock() {
    static std::mutex mutex;
    std::lock_guard guard(mutex);
    return counter_lock++;
}

std::chrono::milliseconds caseLock() {
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

    auto end_time = std::chrono::high_resolution_clock::now();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto duration = millis.count();

    double seconds = static_cast<double>(duration) / 1000.0;
    double throughput = static_cast<double>(TOTAL_OPERATIONS) / seconds;

    std::cout << "\n=== Results Lock ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_lock.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
    return millis;
}

std::atomic<uint64_t> counter_combiner{0};

template <size_t NUMBER>
class Combiner {
    std::array<std::atomic<bool>, NUMBER> interested;
    std::atomic<uint32_t> queued;
    std::mutex lock;
    std::array<std::atomic<uint64_t>, NUMBER> sequence_numbers;

   public:
    Combiner() {
        queued = 0;
        for (auto& val : interested) {
            val = false;
        }
    }

    uint64_t getAndIncrement(size_t my_id) {
        interested.at(my_id).store(true);
        queued.fetch_add(1);

        while (true) {
            std::unique_lock guard(lock);

            // happy path, someone else already got the number for me:
            if (!interested[my_id]) {
                uint64_t my_sequence_number = sequence_numbers[my_id];
                return my_sequence_number;
            }

            uint64_t numbers_needed_total = queued.exchange(0);

            if (numbers_needed_total == 0) {
                // Rare race happened, that my "queue value" was taken,
                // but I did not get selected as the "interested" one (not
                // atomic), and then ALSO afterward, I got the lock and got
                // "queued = 0" before the "stealer" increased the queued count
                // (and that change was visible to me). Note that raising
                // interest and increasing the queue count is not atomic
                // together. This is fine, because this is very rare, I could
                // bypass the synchronization and directly grab a number from
                // the shared atomic, but this would require that the queued
                // counter can go negative. Instead, I can also just retry
                // grabbing the lock
                continue;
            }

            uint64_t distribute_range_lower = counter_combiner.fetch_add(numbers_needed_total);
            uint64_t distribute_range_upper = distribute_range_lower + numbers_needed_total;

            uint64_t my_sequence_number = distribute_range_lower;

            uint64_t current_number_to_distribute = my_sequence_number + 1;
            for (size_t i = 0; i < NUMBER && current_number_to_distribute < distribute_range_upper;
                 i++) {
                if (i != my_id && interested[i]) {
                    interested[i] = false;
                    sequence_numbers[i] = current_number_to_distribute++;
                }
            }
            interested[my_id] = false;
            return my_sequence_number;
        }
    }
};

std::chrono::milliseconds caseCombiner() {
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
                auto& my_combiner = combiners.at(combiner_i);
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
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto duration = millis.count();

    double seconds = static_cast<double>(duration) / 1000.0;
    double throughput = static_cast<double>(TOTAL_OPERATIONS) / seconds;

    std::cout << "\n=== Results Combiner ===\n";
    std::cout << "Total sum: " << total.load() << '\n';
    std::cout << "Duration: " << seconds << " seconds\n";
    std::cout << "Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec\n";
    std::cout << "Throughput: " << (throughput / 1000000.0) << " million ops/sec\n";
    std::cout << "Final counter value: " << counter_combiner.load() << "\n";
    std::cout << "Threads: " << NUM_THREADS << "\n";
    return millis;
}

void printTableHeader() {
    std::cout << "| Implementation | Duration | Throughput (ops/sec) | Throughput (M ops/sec) | "
                 "Relative Performance |\n"
              << "|------------|----------|---------------------|------------------|---------------"
                 "------|\n";
}

void printTableLine(std::string implementation_name,
                    std::chrono::milliseconds time,
                    std::chrono::milliseconds min_time) {
    double throughput =
        (static_cast<double>(TOTAL_OPERATIONS) * 1000.0) / static_cast<double>(time.count());
    double min_throughput =
        (static_cast<double>(TOTAL_OPERATIONS) * 1000.0) / static_cast<double>(min_time.count());
    std::cout << std::format("| {} | {}.{:03} | {} | {} | {:.2f} |\n", implementation_name,
                             time.count() / 1000, time.count() % 1000, throughput,
                             throughput / 1000000, throughput / min_throughput);
}

}  // namespace

int main() {
    std::chrono::milliseconds lock_time = caseLock();
    std::chrono::milliseconds simple_time = caseSimple();
    std::chrono::milliseconds combiner_time = caseCombiner();

    std::chrono::milliseconds min_time = std::min({lock_time, simple_time, combiner_time});

    printTableHeader();
    printTableLine("Lock", lock_time, min_time);
    printTableLine("Simple CAS", simple_time, min_time);
    printTableLine("Combiner", combiner_time, min_time);

    return 0;
}
