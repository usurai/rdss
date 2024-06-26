// Copyright (c) usurai.
// Licensed under the MIT license.
#pragma once

#include "base/memory.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <limits.h>
#include <memory>
#include <vector>
#include <xxhash.h>

namespace rdss::detail {

static size_t rev(size_t v) {
    unsigned long s = CHAR_BIT * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0UL;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

static size_t NextIndex(size_t index, size_t size) {
    int i;
    for (i = 31; i >= 0; --i) {
        if ((size >> i) & 1) {
            break;
        }
    }

    const auto mask = (1UL << i) - 1;
    index |= ~mask;
    index = rev(index);
    ++index;
    index = rev(index);
    return index;
}

} // namespace rdss::detail

namespace rdss {

template<typename Allocator>
class HashTableKey {
public:
    using String = std::basic_string<char, std::char_traits<char>, Allocator>;
    using LastAccessTimeDuration = std::chrono::duration<uint32_t, std::milli>;
    using LastAccessTimePoint
      = std::chrono::time_point<std::chrono::steady_clock, LastAccessTimeDuration>;

    explicit HashTableKey(std::string_view sv)
      : data_(sv) {}

    std::string_view StringView() const { return std::string_view(data_); }

    bool Equals(std::string_view rhs) const { return !data_.compare(rhs); }

    void SetLRU(LastAccessTimePoint lru) { lru_ = lru; }

    LastAccessTimePoint GetLRU() const { return lru_; }

private:
    // TODO: Give it a more reasonable name.
    LastAccessTimePoint lru_;
    const String data_;
};

template<typename ValueType, typename Allocator>
class HashTableEntry {
public:
    using Pointer = HashTableEntry*;

    using ThisType = HashTableEntry<ValueType, Allocator>;
    // TODO: Revisit this.
    using EntryAllocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<ThisType>;

    using CharAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<char>;
    using KeyType = HashTableKey<CharAllocator>;
    using KeyPointer = std::shared_ptr<KeyType>;
    using KeyAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<KeyType>;

public:
    static HashTableEntry* Create() {
        VLOG(1) << "Creating new TableEntry.";
        auto* mem = EntryAllocator().allocate(1);
        auto entry = new (mem) ThisType();
        return entry;
    }

    static void Destroy(Pointer entry) {
        entry->~HashTableEntry();
        EntryAllocator().deallocate(entry, 1);
    }

    void SetKey(std::string_view sv) {
        VLOG(1) << "Setting key in TableEntry with value:" << sv;
        key = std::allocate_shared<KeyType>(KeyAllocator(), sv);
    }

    KeyType* GetKey() { return key.get(); }

    KeyPointer CopyKey() { return key; }

    KeyPointer key = nullptr;
    // TODO: value can be shared string, or inlined int, and needs to be extented to data structure
    // like set and list.
    ValueType value;
    Pointer next = nullptr;

private:
    HashTableEntry() = default;
    ~HashTableEntry() = default;
};

template<typename ValueType, typename Allocator = Mallocator<ValueType>>
class HashTable {
public:
    using EntryType = HashTableEntry<ValueType, Allocator>;
    using EntryPointer = EntryType::Pointer;
    using EntryPointerAllocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<EntryPointer>;
    using BucketVector = std::vector<EntryPointer, EntryPointerAllocator>;

public:
    HashTable() { std::srand(static_cast<unsigned int>(time(nullptr))); }
    ~HashTable() { Clear(); }

    /// Searches for entry with 'key' in the HashTable, and if 'create_on_missing' is set, creates
    /// entry if no such entry is found.
    /// If 'create_on_missing' is true, returns {entry for 'key', if entry already exists}.
    /// If 'create_on_missing' is false, returns {newly created entry for 'key', false}.
    std::pair<EntryPointer, bool>
    FindOrCreate(std::string_view key, bool create_on_missing, bool create_shared_key = true) {
        if (buckets_[0].empty()) {
            if (!create_on_missing) {
                return {nullptr, false};
            }
            Expand();
        }

        auto bucket = FindBucket(key);
        EntryPointer entry{nullptr};
        if ((entry = FindEntryInBucket(bucket, key)) != nullptr) {
            return {entry, true};
        }
        if (create_on_missing) {
            if (Expand() != ExpandResult::kNoExpand) {
                bucket = FindBucket(key);
            }
            entry = CreateEntryInBucket(bucket, key, create_shared_key);
            ++entries_;
        }
        return {entry, false};
    }

    /// Insert if 'key' not exists. Returns {entry of 'key', inserted}.
    std::pair<EntryPointer, bool> Insert(std::string_view key, ValueType value) {
        auto [entry, exists] = FindOrCreate(key, true);
        if (!exists) {
            entry->value = std::move(value);
        }
        return {entry, !exists};
    }

    /// Insert if 'key' not exists, overwrite if exists. Returns {entry of 'key', overwritten}. The
    /// actual key is a shared_ptr constructed with string_view 'key'.
    std::pair<EntryPointer, bool> Upsert(std::string_view key, ValueType value) {
        auto [entry, exists] = FindOrCreate(key, true);
        entry->value = std::move(value);
        return {entry, exists};
    }

    /// Provides the same functionality as above, but with already constructed key shared_ptr. This
    /// is used for a second table that shares the key object with the first table.
    std::pair<EntryPointer, bool> Upsert(EntryType::KeyPointer key_ptr, ValueType value) {
        auto [entry, exists] = FindOrCreate(key_ptr->StringView(), true, false);
        if (exists) {
            assert(entry->key.get() == key_ptr.get());
        } else {
            entry->key = key_ptr;
        }
        entry->value = std::move(value);
        return {entry, exists};
    }

    EntryPointer Find(std::string_view key) {
        auto [entry, _] = FindOrCreate(key, false);
        return entry;
    }

    EntryPointer GetRandomEntry() {
        if (buckets_[0].empty()) {
            return nullptr;
        }

        EntryPointer bucket{nullptr};
        while (bucket == nullptr) {
            const size_t rand = static_cast<size_t>(std::rand());
            const size_t bucket_index = rand % buckets_[0].size();
            if (static_cast<int32_t>(bucket_index) >= rehash_index_) {
                bucket = buckets_[0][bucket_index];
            } else {
                assert(IsRehashing());
                bucket = buckets_[1][rand % buckets_[1].size()];
            }
        }
        return GetRandomEntryInBucket(bucket);
    }

    bool Erase(std::string_view key) {
        if (buckets_[0].empty()) {
            return false;
        }
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
                    EntryType::Destroy(entry);
                    entry = next;
                }
            }
        }
        rehash_index_ = -1;
    }

    bool IsRehashing() const { return (rehash_index_ >= 0); }

    size_t TraverseBucket(size_t bucket_index, auto func) {
        // TODO: support rehashing.
        if (IsRehashing()) {
            return 0;
        }

        assert(bucket_index < buckets_[0].size());
        auto entry = buckets_[0][bucket_index];

        bucket_index = detail::NextIndex(bucket_index, buckets_[0].size());

        while (entry != nullptr) {
            func(entry);
            entry = entry->next;
        }
        return bucket_index;
    }

    /// Rehashes 'buckets_to_rehash' non-empty buckets, or 10 * 'buckets_to_rehash'. Returns if
    /// rehashing has finished. If all the buckets have been rehashed, move the second bucket vector
    /// to the first.
    bool RehashSome(size_t buckets_to_rehash) {
        assert(IsRehashing());
        assert(rehash_index_ != static_cast<int32_t>(buckets_[0].size()));
        assert(buckets_to_rehash != 0);

        auto empty_buckets_allowed = buckets_to_rehash * 10;
        while (true) {
            const auto moved = RehashBucket(buckets_[0].begin() + rehash_index_);

            if (++rehash_index_ == static_cast<int32_t>(buckets_[0].size())) {
                rehash_index_ = -1;
                buckets_[0] = std::move(buckets_[1]);
                return true;
            }

            if (moved == 0) {
                if (--empty_buckets_allowed == 0) {
                    return false;
                }
            } else {
                if (--buckets_to_rehash == 0) {
                    return false;
                }
            }
        }
    }

    // TODO: iterate
    // TODO: mem-related APIs

private:
    uint64_t Hash(std::string_view key) { return XXH64(key.data(), key.size(), 0); }

    EntryPointer CreateEntryInBucket(
      BucketVector::iterator bucket, std::string_view key, bool create_shared_key) {
        auto* entry = EntryType::Create();
        if (create_shared_key) {
            entry->SetKey(key);
        }
        entry->next = *bucket;
        *bucket = entry;
        return entry;
    }

    // Assumes the table is not empty.
    BucketVector::iterator FindBucket(std::string_view key) {
        if (IsRehashing()) {
            RehashSome(1);
        }

        const auto hash = Hash(key);
        const int32_t index = static_cast<int32_t>(hash % buckets_[0].size());
        if (index >= rehash_index_) {
            return buckets_[0].begin() + index;
        }
        assert(index < static_cast<int32_t>(buckets_[1].size()));
        return buckets_[1].begin() + static_cast<int32_t>(hash % buckets_[1].size());
    }

    EntryPointer FindEntryInBucket(BucketVector::iterator bucket, std::string_view key) {
        if (*bucket == nullptr) {
            return nullptr;
        }

        auto entry = *bucket;
        while (!entry->GetKey()->Equals(key)) {
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
        const size_t target_entry = static_cast<size_t>(rand()) % bucket_length;
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
        while (!entry->GetKey()->Equals(key)) {
            if (entry->next == nullptr) {
                return false;
            }
            prev_next = &(entry->next);
            entry = entry->next;
        }
        *prev_next = entry->next;
        // TODO: ditto.
        EntryType::Destroy(entry);
        return true;
    }

    enum class ExpandResult { kNoExpand, kRehashing, kExpandDone };

    ExpandResult Expand() {
        if (IsRehashing()) {
            // return (RehashSome(1) ? ExpandResult::kExpandDone : ExpandResult::kRehashing);
            return ExpandResult::kRehashing;
        }

        if (!NeedsToExpand()) {
            return ExpandResult::kNoExpand;
        }

        assert(buckets_[1].empty());
        VLOG(1) << "BucketVector: resizing to " << buckets_[0].size() * 2;
        buckets_[1].resize(buckets_[0].size() * 2);
        StartRehashing();
        return (IsRehashing() ? ExpandResult::kRehashing : ExpandResult::kExpandDone);
    }

    bool NeedsToExpand() {
        if (buckets_[0].empty()) {
            VLOG(1) << "BucketVector: init resize: 4";
            buckets_[0].resize(4, nullptr);
            return false;
        }
        // TODO: More on this later.
        if (entries_ >= buckets_[0].size()) {
            return true;
        }
        return false;
    }

    void StartRehashing() {
        assert(!IsRehashing());
        assert(!buckets_[1].empty());

        rehash_index_ = 0;
        RehashSome(1);
    }

    size_t RehashBucket(BucketVector::iterator bucket) {
        if (*bucket == nullptr) {
            return 0;
        }
        auto entry = *bucket;
        size_t num_rehashed{0};
        while (entry) {
            auto* next_entry = entry->next;
            const uint64_t hash = Hash(entry->GetKey()->StringView());
            auto target_bucket = buckets_[1].begin()
                                 + static_cast<int32_t>(hash % buckets_[1].size());
            entry->next = *target_bucket;
            *target_bucket = entry;
            entry = next_entry;
            ++num_rehashed;
        }
        *bucket = nullptr;
        return num_rehashed;
    }

private:
    BucketVector buckets_[2];
    size_t entries_ = 0;
    int32_t rehash_index_ = -1;
};

} // namespace rdss
