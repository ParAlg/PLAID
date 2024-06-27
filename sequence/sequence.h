//
// Created by peter on 6/4/24.
//

#ifndef SORTING_SEQUENCE_H
#define SORTING_SEQUENCE_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "parlay/delayed_sequence.h"
#include "parlay/primitives.h"
#include "absl/log/log.h"
#include "absl/log/check.h"

#include "utils/file_info.h"

struct StreamingSequenceBlock {
    void *data;
    size_t start_index;
    size_t n;
};

bool Compare(const StreamingSequenceBlock &a, const StreamingSequenceBlock &b) {
    return a.start_index < b.start_index;
}

/**
 * FIXME: maybe this is bettered called an ordered file reader?
 */
template <typename T>
struct StreamingSequence {
    std::priority_queue<StreamingSequenceBlock, std::vector<StreamingSequenceBlock>, decltype(&Compare)> pq;
    std::mutex pq_mutex;
    std::condition_variable pq_consumer_cond, pq_producer_cond;
    size_t next_index;

    StreamingSequence() {
        next_index = 0;
    }

    StreamingSequenceBlock NextBlock() {
        StreamingSequenceBlock result;
        std::unique_lock<std::mutex> lock(pq_mutex);
        while (true) {
            if (!pq.empty() && (result = pq.top()).start_index == next_index) {
                pq.pop();
                pq_producer_cond.notify_one();
                next_index += result.n;
                if (!pq.empty() && pq.top().start_index == next_index) {
                    pq_consumer_cond.notify_one();
                }
                return result;
            }
            pq_consumer_cond.wait(lock);
        }
    }
};

template <typename T>
StreamingSequence<T> GetSequence(const std::vector<FileInfo> &files) {
    size_t total_size = 0;
    for (const auto &f : files) {
        total_size += f.true_size;
    }
    size_t n = total_size / sizeof(T);
    CHECK(total_size > 0);
}

#endif //SORTING_SEQUENCE_H
