#pragma once

#include "memory.h"

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <vector>
#include <xxhash.h>

namespace rdss {

// TODO: templatize the Allocator
class HashTableKey {
public:
    using String = std::basic_string<char, std::char_traits<char>, Mallocator<char>>;

    explicit HashTableKey(std::string_view sv)
      : data_(sv) {}

    // TODO: rename to StringView()
    std::string_view Data() const { return std::string_view(data_); }

    bool Equals(std::string_view rhs) const { return !data_.compare(rhs); }

    void SetLRU(uint32_t lru) { lru_ = lru; }

    uint32_t GetLRU() const { return lru_; }

private:
    // TODO: Give it a more reasonable name.
    uint32_t lru_ = 0;
    const String data_;
};

template<typename ValueType>
struct HashTableEntry {
    using Pointer = HashTableEntry*;
    using KeyPointer = std::shared_ptr<HashTableKey>;

    KeyPointer key;

    // TODO: value can be shared string, or inlined int, and needs to be extented to data structure
    // like set and list.
    std::shared_ptr<ValueType> value;

    Pointer next = nullptr;
};

template<typename ValueType>
class HashTable {
public:
    using EntryType = HashTableEntry<ValueType>;
    using EntryPointer = EntryType::Pointer;
    using BucketVector = std::vector<EntryPointer, Mallocator<EntryPointer>>;

public:
    HashTable() { std::srand(static_cast<unsigned int>(time(nullptr))); }
    ~HashTable() { Clear(); }

    std::pair<EntryPointer, bool> Insert(std::string_view key, std::string_view value) {
        return Add(std::move(key), std::move(value), false);
    }

    std::pair<EntryPointer, bool> InsertOrAssign(std::string_view key, std::string_view value) {
        return Add(std::move(key), std::move(value), true);
    }

    EntryPointer Find(std::string_view key) {
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

    bool Erase(const std::string_view& key) {
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
        rehash_index_ = -1;
    }

    bool IsRehashing() const { return (rehash_index_ >= 0); }

    // TODO: void RehashStep(size_t steps) const;
    // TODO: iterate
    // TODO: mem-related APIs

private:
    std::pair<EntryPointer, bool> Add(std::string_view key, std::string_view value, bool replace) {
        if (auto entry = Find(key)) {
            if (!replace) {
                return {entry, false};
            }
            entry->value = std::make_shared<ValueType>(value);
            return {entry, true};
        }

        Expand();

        auto bucket = FindBucket(key);
        auto result_pointer = InsertIntoBucket(bucket, key, value);
        ++entries_;
        return {result_pointer, true};
    }

    uint64_t Hash(std::string_view key) { return XXH64(key.data(), key.size(), 0); }

    // Assumes the table is not empty.
    BucketVector::iterator FindBucket(std::string_view key) {
        const auto hash = Hash(key);
        const int32_t index = static_cast<int32_t>(hash % buckets_[0].size());
        if (index >= rehash_index_) {
            return buckets_[0].begin() + index;
        }
        assert(index < static_cast<int32_t>(buckets_[1].size()));
        return buckets_[1].begin() + (hash % buckets_[1].size());
    }

    EntryPointer FindEntryInBucket(BucketVector::iterator bucket, std::string_view key) {
        if (*bucket == nullptr) {
            return nullptr;
        }

        auto entry = *bucket;
        while (!entry->key->Equals(key)) {
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

    bool EraseEntryInBucket(BucketVector::iterator bucket, std::string_view key) {
        if (*bucket == nullptr) {
            return false;
        }
        EntryPointer* prev_next = &(*bucket);
        auto entry = *bucket;
        while (!entry->key->Equals(key)) {
            if (entry->next == nullptr) {
                return false;
            }
            prev_next = &(entry->next);
            entry = entry->next;
        }
        *prev_next = entry->next;
        // TODO: ditto.
        entry->~HashTableEntry();
        entry_allocator_.deallocate(entry, 1);
        return true;
    }

    // Insert entry with 'key' and 'value' to the head of the bucket.This assumes 'bucket' doesn't
    // contains an entry that has equivalent key to 'key'.
    EntryPointer
    InsertIntoBucket(BucketVector::iterator bucket, std::string_view key, std::string_view value) {
        // TODO: exception handling.
        // TODO: Make these into function.
        auto* mem = entry_allocator_.allocate(1);
        auto entry = new (mem) EntryType();
        assert(entry != nullptr);
        // TODO: shared_ptr should also be tracked by MemoryTracker.
        entry->key = std::make_shared<HashTableKey>(key);
        // TODO: value should be cared differently.
        entry->value = std::make_shared<ValueType>(value);
        entry->next = *bucket;
        *bucket = entry;
        return entry;
    }

    enum class ExpandResult { NoNeed, SUCCESS };
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
                const auto hash = Hash(entry->key->Data());
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
