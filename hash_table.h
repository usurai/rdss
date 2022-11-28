#pragma once

#include "memory.h"

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <vector>
#include <xxhash.h>

namespace rdss {

template<typename KeyType, typename ValueType>
struct HashTableEntry {
    using pointer = HashTableEntry*;
    KeyType key;
    ValueType value;
    pointer next = nullptr;
};

template<typename KeyType, typename ValueType>
class HashTable {
public:
    using EntryType = HashTableEntry<KeyType, ValueType>;
    using EntryPointer = EntryType::pointer;
    using BucketVector = std::vector<EntryPointer, Mallocator<EntryPointer>>;

public:
    HashTable() { std::srand(std::time(nullptr)); }
    ~HashTable() { Clear(); }

    // TODO: parameter
    std::pair<EntryPointer, bool> Insert(KeyType key, ValueType value) {
        return Add(std::move(key), std::move(value), false);
    }

    std::pair<EntryPointer, bool> InsertOrAssign(KeyType key, ValueType value) {
        return Add(std::move(key), std::move(value), true);
    }

    EntryPointer Find(const KeyType& key) {
        if (buckets_[0].empty()) {
            return nullptr;
        }
        auto bucket = FindBucket(key);
        return FindEntryInBucket(bucket, key);
    }

    EntryPointer GetRandomEntry() {
        if (buckets_[0].empty()) {
            return nullptr;
        }
        if (!IsRehashing()) {
            EntryPointer bucket{nullptr};
            while (bucket == nullptr) {
                const auto bucket_index = std::rand() % buckets_[0].size();
                bucket = buckets_[0][bucket_index];
            }
            return GetRandomEntryInBucket(bucket);
        }
        // TODO: rehash case
        return nullptr;
    }

    // const ValueType& GetValue(KeyType key);

    bool Erase(const KeyType& key) {
        auto bucket = FindBucket(key);
        const auto erased = EraseEntryInBucket(bucket, key);
        if (erased) {
            --entries_;
        }
        return erased;
    }

    size_t Count() const { return entries_; }

    size_t BucketCount() const { return buckets_[0].size(); }

    double LoadFactor() const {
        if (BucketCount() == 0) {
            return 0;
        }
        return static_cast<double>(Count()) / BucketCount();
    }

    void Clear() {
        for (auto& buckets : buckets_) {
            for (auto& bucket : buckets) {
                if (bucket == nullptr) {
                    continue;
                }
                auto entry = bucket;
                while (entry) {
                    auto next = entry->next;
                    entry->~HashTableEntry();
                    entry_allocator_.deallocate(entry, 1);
                    entry = next;
                }
            }
        }
    }

    bool IsRehashing() const { return (rehash_index_ >= 0); }

    // TODO: void RehashStep(size_t steps) const;
    // TODO: iterate
    // TODO: mem-related APIs

private:
    std::pair<EntryPointer, bool> Add(KeyType key, ValueType value, bool replace) {
        if (auto entry = Find(key)) {
            if (!replace) {
                return {entry, false};
            }
            entry->value = std::move(value);
            return {entry, true};
        }

        if (Expand() == ExpandResult::FAIL) {
            return {nullptr, false};
        }

        auto bucket = FindBucket(key);
        auto result_pointer = InsertIntoBucket(bucket, std::move(key), std::move(value));
        ++entries_;
        return {result_pointer, true};
    }

    uint64_t Hash(const KeyType& key) { return XXH64(key.data(), key.size(), 0); }

    // Assumes the table is not empty.
    BucketVector::iterator FindBucket(const KeyType& key) {
        const auto hash = Hash(key);
        const int32_t index = hash % buckets_[0].size();
        if (index >= rehash_index_) {
            return buckets_[0].begin() + index;
        }
        assert(index < static_cast<int32_t>(buckets_[1].size()));
        return buckets_[1].begin() + (hash % buckets_[1].size());
    }

    EntryPointer FindEntryInBucket(BucketVector::iterator bucket, const KeyType& key) {
        if (*bucket == nullptr) {
            return nullptr;
        }

        auto entry = *bucket;
        while (entry->key != key) {
            if (entry->next == nullptr) {
                return nullptr;
            }
            entry = entry->next;
        }
        return entry;
    }

    EntryPointer GetRandomEntryInBucket(EntryPointer bucket) {
        size_t bucket_length{1};
        auto entry = bucket;
        while (entry->next) {
            ++bucket_length;
            entry = entry->next;
        }
        const size_t target_entry = rand() % bucket_length;
        entry = bucket;
        for (size_t i = 0; i < target_entry; ++i) {
            entry = entry->next;
        }
        return entry;
    }

    bool EraseEntryInBucket(BucketVector::iterator bucket, const KeyType& key) {
        if (*bucket == nullptr) {
            return false;
        }
        auto entry = *bucket;
        if (entry->key == key) {
            *bucket = entry->next;
            entry->~HashTableEntry();
            entry_allocator_.deallocate(entry, 1);
            return true;
        }

        EntryPointer* prev_next = &(entry->next);
        entry = entry->next;
        while (entry->key != key) {
            if (entry->next == nullptr) {
                return false;
            }
            prev_next = &(entry->next);
            entry = entry->next;
        }
        *prev_next = entry->next;
        entry->~HashTableEntry();
        entry_allocator_.deallocate(entry, 1);
        return true;
    }

    // Insert entry with 'key' and 'value' to the head of the bucket.This assumes 'bucket' doesn't
    // contains an entry that has equivalent key to 'key'.
    EntryPointer InsertIntoBucket(BucketVector::iterator bucket, KeyType key, ValueType value) {
        // TODO: exception handling
        auto* mem = entry_allocator_.allocate(1);
        auto entry = new (mem) EntryType();
        assert(entry != nullptr);
        entry->key = std::move(key);
        entry->value = std::move(value);
        entry->next = *bucket;
        *bucket = entry;
        return entry;
    }

    enum class ExpandResult { NoNeed, SUCCESS, FAIL };
    ExpandResult Expand() {
        if (!NeedsToExpand()) {
            return ExpandResult::NoNeed;
        }

        assert(buckets_[1].empty());
        // TODO: what if cannot allocate
        buckets_[1].resize(buckets_[0].size() * 2, nullptr);
        Rehash();
        buckets_[0] = std::move(buckets_[1]);
        return ExpandResult::SUCCESS;
    }

    bool NeedsToExpand() {
        if (buckets_[0].empty()) {
            // TODO: parameterize this.
            buckets_[0].resize(4, nullptr);
            return false;
        }
        if (entries_ >= buckets_[0].size()) {
            return true;
        }
        return false;
    }

    void Rehash() {
        for (auto bucket = buckets_[0].begin(); bucket != buckets_[0].end(); ++bucket) {
            if (*bucket == nullptr) {
                continue;
            }
            auto entry = *bucket;
            while (entry) {
                auto* next_entry = entry->next;
                const auto hash = Hash(entry->key);
                auto target_bucket = buckets_[1].begin() + (hash % buckets_[1].size());
                entry->next = *target_bucket;
                *target_bucket = entry;
                entry = next_entry;
            }
        }
    }

private:
    Mallocator<EntryType> entry_allocator_;
    BucketVector buckets_[2];
    size_t entries_ = 0;
    int32_t rehash_index_ = -1;
};

} // namespace rdss