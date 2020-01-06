#include "util.h"

using std::make_pair;
using std::make_shared;
using std::make_tuple;
using std::make_unique;
using std::map;
using std::shared_ptr;
using std::string;
using std::tuple;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

typedef struct {
    size_t capacity;
    int interval;
    int is_v2;
    int enable_fifo;
    int enable_lru;
    int enable_block;
    int enable_block_v2;
} config;

static config g_cfg;

class CacheStat {
public:
    struct Stat {
        string class_name;
        long count;
        long used;
        long capacity;
    };

    virtual ~CacheStat() = default;
    virtual void on_get(const string &key, const size_t length, bool is_hit) = 0;
    virtual Stat get_stat() const = 0;
};

class Cache {
public:
    virtual ~Cache() = default;
    virtual void get(const string &key, const size_t length) = 0;
};

template <typename Key> class FIFOCacheImpl {
public:
    struct CacheImpl {
        CacheImpl() = default;
        CacheImpl(Key key, size_t length)
            : key(key)
            , length(length)
        {
        }
        Key key;
        size_t length;
        CacheImpl *prev;
        CacheImpl *next;
    };

    FIFOCacheImpl(size_t capacity)
    {
        capacity_ = capacity;
        size_ = 0;
        head_.next = &tail_;
        tail_.prev = &head_;
    }

    tuple<bool, size_t> get(Key key)
    {
        auto it = vmap_.find(key);
        if (it == vmap_.end()) {
            return make_tuple(false, 0);
        } else {
            CacheImpl *c = it->second;
            return make_tuple(true, c->length);
        }
    }

    void set(Key key, size_t length)
    {
        auto it = vmap_.find(key);
        if (it != vmap_.end()) {
            CacheImpl *c = it->second;
            size_ -= c->length;
            c->length = length;
            size_ += c->length;
        } else {
            CacheImpl *c = new CacheImpl(key, length);
            cache_push(c);
            vmap_[key] = c;
        }

        while (capacity_ < size_) {
            CacheImpl *c = remove_last_cache();
            vmap_.erase(c->key);
            delete c;
        }
    }

    const long capacity() const { return capacity_; }

    const long used() const { return size_; }

    const long count() const { return vmap_.size(); }

private:
    void cache_push(CacheImpl *c)
    {
        c->next = head_.next;
        head_.next = c;
        c->next->prev = c;
        c->prev = &head_;
        size_ += c->length;
    }

    void cache_push(Key key, size_t length)
    {
        CacheImpl *c = new CacheImpl(key, length);
        cache_push(c);
    }

    void move_cache_to_head(CacheImpl *c)
    {
        cache_remove(c);
        cache_push(c);
    }

    void cache_remove(CacheImpl *c)
    {
        c->prev->next = c->next;
        c->next->prev = c->prev;
        size_ -= c->length;
    }

    CacheImpl *remove_last_cache()
    {
        CacheImpl *c = tail_.prev;
        cache_remove(c);
        return c;
    }

private:
    CacheImpl head_;
    CacheImpl tail_;
    size_t capacity_;
    size_t size_;
    unordered_map<Key, CacheImpl *> vmap_;
};

template <typename Key> class LRUCacheImpl {
public:
    struct CacheImpl {
        CacheImpl() = default;
        CacheImpl(Key key, size_t length)
            : key(key)
            , length(length)
        {
        }
        Key key;
        size_t length;
        CacheImpl *prev;
        CacheImpl *next;
    };

    LRUCacheImpl(size_t capacity)
    {
        capacity_ = capacity;
        size_ = 0;
        head_.next = &tail_;
        tail_.prev = &head_;
    }

    tuple<bool, size_t> get(Key key)
    {
        auto it = vmap_.find(key);
        if (it == vmap_.end()) {
            return make_tuple(false, 0);
        } else {
            CacheImpl *c = it->second;
            move_cache_to_head(c);
            return make_tuple(true, c->length);
        }
    }

    void set(Key key, size_t length)
    {
        auto it = vmap_.find(key);
        if (it != vmap_.end()) {
            CacheImpl *c = it->second;
            size_ -= c->length;
            c->length = length;
            size_ += c->length;
            move_cache_to_head(c);
        } else {
            CacheImpl *c = new CacheImpl(key, length);
            cache_push(c);
            vmap_[key] = c;
        }

        while (capacity_ < size_) {
            CacheImpl *c = remove_last_cache();
            vmap_.erase(c->key);
            delete c;
        }
    }

    const long capacity() const { return capacity_; }

    const long used() const { return size_; }

    const long count() const { return vmap_.size(); }

private:
    void cache_push(CacheImpl *c)
    {
        c->next = head_.next;
        head_.next = c;
        c->next->prev = c;
        c->prev = &head_;
        size_ += c->length;
    }

    void cache_push(Key key, size_t length)
    {
        CacheImpl *c = new CacheImpl(key, length);
        cache_push(c);
    }

    void move_cache_to_head(CacheImpl *c)
    {
        cache_remove(c);
        cache_push(c);
    }

    void cache_remove(CacheImpl *c)
    {
        c->prev->next = c->next;
        c->next->prev = c->prev;
        size_ -= c->length;
    }

    CacheImpl *remove_last_cache()
    {
        CacheImpl *c = tail_.prev;
        cache_remove(c);
        return c;
    }

private:
    CacheImpl head_;
    CacheImpl tail_;
    size_t capacity_;
    size_t size_;
    unordered_map<Key, CacheImpl *> vmap_;
};

template <typename Key> class BlockCacheImpl {
public:
    struct BlockImpl;

    struct CacheImpl {
        CacheImpl() = default;
        CacheImpl(Key key, size_t length)
            : key(key)
            , length(length)
        {
        }

        Key key;
        size_t length;
        CacheImpl *next;
        BlockImpl *block = nullptr;
    };

    struct BlockImpl {
        CacheImpl *first = nullptr;
        size_t used = 0;
        BlockImpl *next = nullptr;
        BlockImpl *prev = nullptr;
    };

    BlockCacheImpl(size_t capacity)
    {
        capacity_ = capacity;
        size_ = 0;
        head_.next = &tail_;
        head_.prev = &tail_;
        tail_.prev = &head_;
        tail_.next = &head_;
    }

    tuple<bool, size_t> get(Key key)
    {
        auto it = vmap_.find(key);
        if (it == vmap_.end()) {
            return make_tuple(false, 0);
        } else {
            CacheImpl *c = it->second;
            if (c->block != wblock_) {
                block_remove(c->block);
                block_push(c->block);
            }
            return make_tuple(true, c->length);
        }
    }

    void set(Key key, size_t length)
    {
        auto it = vmap_.find(key);
        if (it != vmap_.end()) {
            CacheImpl *c = it->second;
            size_ -= c->length;
            c->length = length;
            size_ += c->length;
        } else {
            cache_push(key, length);
        }

        while (capacity_ < size_) {
            if (tail_.prev == &head_) {
                drop_block(wblock_);
                wblock_ = nullptr;
            } else {
                BlockImpl *b = tail_.prev;
                block_remove(b);
                drop_block(b);
            }
        }
    }

    const long capacity() const { return capacity_; }

    const long used() const { return size_; }

    const long count() const { return vmap_.size(); }

private:
    void cache_push(Key key, size_t length)
    {
        CacheImpl *c = new CacheImpl(key, length);
        if (wblock_ == nullptr) {
            wblock_ = new BlockImpl;
        }

        if (c->length + wblock_->used > 64 * 1048576) {
            block_push(wblock_);
            wblock_ = new BlockImpl;
        }

        c->next = wblock_->first;
        wblock_->first = c;
        wblock_->used += length;
        c->block = wblock_;

        size_ += length;
        vmap_[key] = c;
    }

    void block_push(BlockImpl *b)
    {
        b->next = head_.next;
        head_.next = b;
        b->next->prev = b;
        b->prev = &head_;
    }

    void block_remove(BlockImpl *b)
    {
        b->prev->next = b->next;
        b->next->prev = b->prev;
    }

    void drop_cache(CacheImpl *c)
    {
        vmap_.erase(c->key);
        size_ -= c->length;
        delete c;
    }

    void drop_block(BlockImpl *b)
    {
        while (1) {
            CacheImpl *c = b->first;
            if (c == nullptr) {
                break;
            }

            b->first = c->next;
            drop_cache(c);
        }

        delete b;
    }

private:
    BlockImpl *wblock_;
    size_t size_;
    size_t capacity_;
    BlockImpl head_;
    BlockImpl tail_;
    unordered_map<Key, CacheImpl *> vmap_;
};

template <typename Key> class BlockCacheImplV2 {
public:
    struct BlockImpl;

    struct CacheImpl {
        CacheImpl() = default;
        CacheImpl(Key key, size_t length)
            : key(key)
            , length(length)
        {
        }

        Key key;
        size_t length;
        CacheImpl *next;
        BlockImpl *block = nullptr;
    };

    struct BlockImpl {
        BlockImpl() { score = tv_now_usec(); }

        CacheImpl *first = nullptr;
        uint64_t key = 0;
        uint64_t score = 0;
        size_t used = 0;
        BlockImpl *n = nullptr;
        BlockImpl *next = nullptr;
        BlockImpl *prev = nullptr;
    };

    BlockCacheImplV2(size_t capacity)
    {
        capacity_ = capacity;
        size_ = 0;
        head_.next = &tail_;
        head_.prev = &tail_;
        tail_.prev = &head_;
        tail_.next = &head_;
    }

    tuple<bool, size_t> get(Key key)
    {
        auto it = vmap_.find(key);
        if (it == vmap_.end()) {
            return make_tuple(false, 0);
        } else {
            CacheImpl *c = it->second;
            if (c->block != wblock_) {
                block_remove(c->block);
                block_update_score(c->block, c->length);
                block_push(c->block);
            }
            return make_tuple(true, c->length);
        }
    }

    void set(Key key, size_t length)
    {
        auto it = vmap_.find(key);
        if (it != vmap_.end()) {
            CacheImpl *c = it->second;
            size_ -= c->length;
            c->length = length;
            size_ += c->length;
        } else {
            cache_push(key, length);
        }

        while (capacity_ < size_) {
            if (tail_.prev == &head_) {
                drop_block(wblock_);
                wblock_ = nullptr;
            } else {
                BlockImpl *b = block_map_.begin()->second;
                block_remove(b);
                drop_block(b);
            }
        }
    }

    const long capacity() const { return capacity_; }

    const long used() const { return size_; }

    const long count() const { return vmap_.size(); }

private:
    void cache_push(Key key, size_t length)
    {
        CacheImpl *c = new CacheImpl(key, length);
        if (wblock_ == nullptr) {
            wblock_ = new BlockImpl;
        }

        if (c->length + wblock_->used > 64 * 1048576) {
            block_push(wblock_);
            wblock_ = new BlockImpl;
        }

        c->next = wblock_->first;
        wblock_->first = c;
        wblock_->used += length;
        c->block = wblock_;

        size_ += length;
        vmap_[key] = c;
    }

    void block_update_score(BlockImpl *b, size_t length)
    {
        long delta = tv_now_usec() - b->score;
        if (delta <= 0) {
            return;
        }

        long new_delta = length * delta / (b->used + length);
        if (new_delta > delta) {
            delta = new_delta;
        }

        b->score += new_delta;
    }

    void block_push(BlockImpl *b)
    {
        b->next = head_.next;
        head_.next = b;
        b->next->prev = b;
        b->prev = &head_;
        b->key = b->score;

        b->n = nullptr;
        if (block_map_.find(b->key) == block_map_.end()) {
            block_map_[b->key] = b;
        } else {
            b->n = block_map_[b->key];
            block_map_[b->key] = b;
        }
    }

    void block_remove(BlockImpl *b)
    {
        b->prev->next = b->next;
        b->next->prev = b->prev;

        assert(block_map_.find(b->key) != block_map_.end());
        BlockImpl *p = block_map_[b->key];
        for (BlockImpl *prev = nullptr; p; prev = p, p = p->n) {
            if (p != b) {
                continue;
            }
            if (p->n) {
                if (prev) {
                    prev->n = p->n;
                } else {
                    block_map_[b->key] = p->n;
                }
            } else {
                if (prev) {
                    prev->n = nullptr;
                } else {
                    block_map_.erase(b->key);
                }
            }
        }
    }

    void drop_cache(CacheImpl *c)
    {
        vmap_.erase(c->key);
        size_ -= c->length;
        delete c;
    }

    void drop_block(BlockImpl *b)
    {
        while (1) {
            CacheImpl *c = b->first;
            if (c == nullptr) {
                break;
            }

            b->first = c->next;
            drop_cache(c);
        }

        delete b;
    }

private:
    BlockImpl *wblock_;
    size_t size_;
    size_t capacity_;
    BlockImpl head_;
    BlockImpl tail_;
    map<uint64_t, BlockImpl *> block_map_;
    unordered_map<Key, CacheImpl *> vmap_;
};

static uint64_t g_current_ticks = 0;

class CacheStatImpl : public CacheStat {
public:
    void on_get(const string &key, const size_t length, bool is_hit) override
    {
        log_debug("get key %s, length %d, result %s", key.data(), length, is_hit ? "HIT" : "MISS");

        if (is_hit) {
            hit_count_++;
            hit_length_ += length;
        } else {
            miss_count_++;
            miss_length_ += length;
        }

        interval_log_if_need();
    }

private:
    void interval_log_if_need()
    {
        if (g_current_ticks % g_cfg.interval == 0 && g_current_ticks > 0) {
            interval_log();
        }
    }

    void interval_log()
    {
        long hit_c = hit_count_ - last_hit_count_;
        long miss_c = miss_count_ - last_miss_count_;
        long hit_l = hit_length_ - last_hit_length_;
        long miss_l = miss_length_ - last_miss_length_;

        Stat st = get_stat();
        log_info("%s: hit_count = %ld, miss_count = %ld, rate is %0.02f%%", st.class_name.data(),
            hit_c, miss_c, (double)hit_c / (hit_c + miss_c) * 100);
        log_info("%s: hit_length = %s, miss_length = %s, rate is %0.02f%%", st.class_name.data(),
            get_size_str(hit_l).data(), get_size_str(miss_l).data(),
            (double)hit_l / (hit_l + miss_l) * 100);

        log_info("%s: count = %ld, in use %s/%s", st.class_name.data(), st.count,
            get_size_str(st.used).data(), get_size_str(st.capacity).data());

        last_hit_count_ = hit_count_;
        last_miss_count_ = miss_count_;
        last_hit_length_ = hit_length_;
        last_miss_length_ = miss_length_;
    }

protected:
    long hit_count_ = 0;
    long miss_count_ = 0;
    long hit_length_ = 0;
    long miss_length_ = 0;
    long last_hit_count_ = 0;
    long last_miss_count_ = 0;
    long last_hit_length_ = 0;
    long last_miss_length_ = 0;
};

template <typename Impl> class TCache : public Cache, public CacheStatImpl {
public:
    TCache(size_t cap)
        : impl_(cap)
    {
    }

    void get(const string &key, const size_t length) override
    {
        auto [is_hit, old_length] = impl_.get(key);
        if (!is_hit || old_length != length) {
            is_hit = false;
            impl_.set(key, length);
        }
        on_get(key, length, is_hit);
    }

    Stat get_stat() const override
    {
        Stat stat;
        stat.class_name = get_filt_type_name<decltype(*this)>();
        stat.capacity = impl_.capacity();
        stat.count = impl_.count();
        stat.used = impl_.used();
        return stat;
    }

private:
    Impl impl_;
};

using LRUCache = TCache<LRUCacheImpl<string>>;
using FIFOCache = TCache<FIFOCacheImpl<string>>;
using BlockCache = TCache<BlockCacheImpl<string>>;
using BlockCacheV2 = TCache<BlockCacheImplV2<string>>;

vector<string> split(const string &str, const string &delim)
{
    vector<string> tokens;
    size_t prev = 0, pos = 0;
    do {
        pos = str.find(delim, prev);
        if (pos == string::npos)
            pos = str.length();
        string token = str.substr(prev, pos - prev);
        if (!token.empty())
            tokens.push_back(token);
        prev = pos + delim.length();
    } while (pos < str.length() && prev < str.length());
    return tokens;
}

LRUCache *g_lru_cache = nullptr;
FIFOCache *g_fifo_cache = nullptr;
BlockCache *g_block_cache = nullptr;
BlockCacheV2 *g_block_cache_v2 = nullptr;

static void cache_get(const string &key, const long len)
{
    g_current_ticks++;

    if (g_cfg.enable_lru) {
        g_lru_cache->get(key, len);
    }

    if (g_cfg.enable_fifo) {
        g_fifo_cache->get(key, len);
    }

    if (g_cfg.enable_block) {
        g_block_cache->get(key, len);
    }

    if (g_cfg.enable_block_v2) {
        g_block_cache_v2->get(key, len);
    }
}

static void process_line(const string &line)
{
    const auto tokens = split(line, " ");
    if (tokens.size() != 2) {
        log_info("line %s is invalid", line.data());
        return;
    }

    const string &key = tokens[0];
    const string &lenstr = tokens[1];
    const long len = std::atol(lenstr.data());

    cache_get(key, len);
}

static size_t part_size(size_t entity_length, size_t part_index)
{
    if ((part_index + 1) * 1048576 <= entity_length) {
        return 1048576;
    }
    if (part_index * 1048576 > entity_length) {
        return 1048576;
    }
    return entity_length - part_index * 1048576;
}

static void process_line_v2(const string &line)
{
    const auto tokens = split(line, " ");
    if (tokens.size() != 4) {
        log_info("line %s is invalid", line.data());
        return;
    }

    const string &items = tokens[0];
    const string &hkey = tokens[1];
    const string &entity_lenstr = tokens[2];
    const string &name = tokens[3];
    const long entity_length = atol(entity_lenstr.data());

    for (const auto &slice : split(items, ",")) {
        const auto start_ends = split(slice, "-");
        if (start_ends.size() != 2) {
            log_info("start and end %s is invalid", slice.data());
        }

        const int start = atoi(start_ends[0].data());
        const int end = atoi(start_ends[1].data());

        for (int i = start; i <= end; i++) {
            string key = hkey + name + ":" + std::to_string(i);
            const long len = part_size(entity_length, i);
            cache_get(key, len);
        }
    }
}

static command_t cmds[] = { { "c", "cap", cmd_set_size, offsetof(config, capacity), "4G",
                                "set the capapcity of cache" },
    { "", "interval", cmd_set_int, offsetof(config, interval), "1000000" },
    { "", "fifo", cmd_set_bool, offsetof(config, enable_fifo), "on" },
    { "", "lru", cmd_set_bool, offsetof(config, enable_lru), "on" },
    { "", "block", cmd_set_bool, offsetof(config, enable_block), "on" },
    { "", "block_v2", cmd_set_bool, offsetof(config, enable_block_v2), "on" },
    { "", "v2", nullptr, offsetof(config, is_v2), nullptr, "" } };

int main(int argc, const char *argv[])
{
    char *errstr = nullptr;
    int rc = parse_command_args(argc, argv, &g_cfg, cmds, array_size(cmds), &errstr, nullptr);
    if (rc != 0) {
        log_fatal("parse command error: %s", errstr ? errstr : "");
    }
    free(errstr);

    g_lru_cache = new LRUCache(g_cfg.capacity);
    g_fifo_cache = new FIFOCache(g_cfg.capacity);
    g_block_cache = new BlockCache(g_cfg.capacity);
    g_block_cache_v2 = new BlockCacheV2(g_cfg.capacity);

    for (string line; std::getline(std::cin, line);) {
        if (g_cfg.is_v2) {
            process_line_v2(line);
        } else {
            process_line(line);
        }
    }

    return 0;
}
