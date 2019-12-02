#include "util.h"
#include <cmath>
#include <random>

using std::default_random_engine;
using std::make_shared;
using std::make_tuple;
using std::make_unique;
using std::map;
using std::shared_ptr;
using std::sort;
using std::string;
using std::to_string;
using std::tuple;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

using hash_func_type = uint32_t (*)(const string &);

uint32_t murmur_hash2(const string &s)
{
    const char *data = s.data();
    size_t len = s.length();

    uint32_t h, k;

    h = 0 ^ len;

    while (len >= 4) {
        k = data[0];
        k |= data[1] << 8;
        k |= data[2] << 16;
        k |= data[3] << 24;

        k *= 0x5bd1e995;
        k ^= k >> 24;
        k *= 0x5bd1e995;

        h *= 0x5bd1e995;
        h ^= k;

        data += 4;
        len -= 4;
    }

    switch (len) {
    case 3:
        h ^= data[2] << 16;
        /* fall through */
    case 2:
        h ^= data[1] << 8;
        /* fall through */
    case 1:
        h ^= data[0];
        h *= 0x5bd1e995;
    }

    h ^= h >> 13;
    h *= 0x5bd1e995;
    h ^= h >> 15;

    return h;
}

uint32_t xorshiftmul32(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

class Hash {
public:
    explicit Hash(hash_func_type hash)
        : hash_(hash)
    {
    }
    virtual ~Hash() = default;
    virtual void init(vector<tuple<string, int>> nodes) = 0;
    virtual string get(const string &key) = 0;
    virtual string name() const = 0;

protected:
    hash_func_type hash_;
};

static bool vnode_compare(const tuple<uint32_t, int> &vnode1, const tuple<uint32_t, int> &vnode2)
{
    return std::get<0>(vnode1) < std::get<0>(vnode2);
}

class CHash : public Hash {
public:
    CHash(hash_func_type hash = murmur_hash2, int vnode_num = 160)
        : Hash(hash)
        , vnode_num_(vnode_num)
    {
    }

    virtual void get_vnodes(vector<tuple<string, int>> nodes)
    {
        for (int rnode_index = 0; rnode_index < nodes.size(); rnode_index++) {
            auto const &node = nodes[rnode_index];
            const string &name = std::get<0>(node);
            rnodes_.emplace_back(name);
            uint32_t sid = hash_(name);
            int num = std::get<1>(node) * vnode_num_;
            for (int i = 0; i < num; i++) {
                uint32_t id = sid * 256 * 16 + i;
                uint32_t h = hash_(string((const char *)&id, sizeof(id)));
                vnodes_.emplace_back(make_tuple(h, rnode_index));
            }
        }
    }

    void init(vector<tuple<string, int>> nodes) override
    {
        rnodes_.clear();
        vnodes_.clear();
        get_vnodes(nodes);

        elapsed e("sort");
        sort(vnodes_.begin(), vnodes_.end(), vnode_compare);
    }

    string get(const string &key) override
    {
        if (rnodes_.empty()) {
            return "";
        }

        tuple<uint32_t, int> v = { hash_(key), 0 };
        auto it = lower_bound(vnodes_.cbegin(), vnodes_.cend(), v, vnode_compare);
        if (it == vnodes_.cend()) {
            it = vnodes_.cbegin();
        }
        return rnodes_[std::get<1>(*it)];
    }

    string name() const override { return get_filt_type_name<decltype(*this)>(); }

protected:
    int vnode_num_;
    vector<string> rnodes_;
    vector<tuple<uint32_t, int>> vnodes_;
};

class CHash2 : public CHash {
public:
    CHash2(hash_func_type hash = murmur_hash2, int vnode_num = 160)
        : CHash(hash, vnode_num)
    {
    }

    void get_vnodes(vector<tuple<string, int>> nodes) override {
        hit_count_ = 0;
        miss_count_ = 0;

        for (int rnode_index = 0; rnode_index < nodes.size(); rnode_index++) {
            auto const &node = nodes[rnode_index];
            const string &name = std::get<0>(node);
            rnodes_.emplace_back(name);
            int num = std::get<1>(node) * vnode_num_;
            uint32_t sid = hash_(name);
            for (int i = 0; i < num; i++) {
                // string key = name + "_" + std::to_string(i);
                // uint32_t h = hash_(key);
                uint32_t h = xorshiftmul32(sid ^ (murmur_table_get(i)));
                vnodes_.emplace_back(make_tuple(h, rnode_index));
            }
        }

        log_debug("hit_count = %d miss_count = %d", hit_count_, miss_count_);
    }

    string name() const override { return get_filt_type_name<decltype(*this)>(); }

private:
    uint32_t murmur_table_get(int k)
    {
        if (murmur_table_.find(k) == murmur_table_.end()) {
            murmur_table_[k] = murmur_hash2(string((const char *)&k, sizeof(k)));
            miss_count_++;
        } else {
            hit_count_++;
        }

        return murmur_table_[k];
    }

private:
    int hit_count_ = 0;
    int miss_count_ = 0;
    unordered_map<int, uint32_t> murmur_table_;
};

struct Node {
public:
    Node(string n, uint64_t h, int w)
        : name(n)
        , hash(h)
        , weight(w)
    {
    }
    string name;
    uint64_t hash;
    int weight;
};

uint64_t hash64(const string &key)
{
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;
    const uint64_t seed = 0x1F0D3804;
    const size_t len = key.length();

    uint64_t h = seed ^ (len * m);

    const uint64_t *data = (const uint64_t *)key.data();
    const uint64_t *end = data + (len / 8);

    while (data != end) {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char *data2 = (const unsigned char *)data;

    switch (len & 7) {
    case 7:
        h ^= uint64_t(data2[6]) << 48;
    case 6:
        h ^= uint64_t(data2[5]) << 40;
    case 5:
        h ^= uint64_t(data2[4]) << 32;
    case 4:
        h ^= uint64_t(data2[3]) << 24;
    case 3:
        h ^= uint64_t(data2[2]) << 16;
    case 2:
        h ^= uint64_t(data2[1]) << 8;
    case 1:
        h ^= uint64_t(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

class HRWHash : public Hash {
public:
    HRWHash(hash_func_type hash = murmur_hash2)
        : Hash(hash)
    {
    }

    void init(vector<tuple<string, int>> nodes) override
    {
        nodes_.clear();

        std::transform(nodes.cbegin(), nodes.cend(), std::back_inserter(nodes_),
            [](const tuple<string, int> &node) {
                const auto &name = std::get<0>(node);
                return Node(name, hash64(name), std::get<1>(node));
            });
    }

    string get(const string &key) override
    {
        if (nodes_.empty()) {
            return "";
        }

        uint64_t khash = hash64(key);
        int max_index = 0;
        const Node &first_node = nodes_[0];
        uint64_t tmp1 = xorshiftmul64(khash ^ first_node.hash);
        double max_hash = -first_node.weight / std::log(((double)tmp1 / 0xFFFFFFFFFFFFFFFFUL));

        for (int i = 1; i < nodes_.size(); i++) {
            const Node &node = nodes_[i];
            tmp1 = xorshiftmul64(khash ^ node.hash);
            double tmp2 = -node.weight / std::log(((double)tmp1 / 0xFFFFFFFFFFFFFFFFUL));
            if (tmp2 > max_hash) {
                max_hash = tmp2;
                max_index = i;
            }
        }

        return nodes_[max_index].name;
    }

    string name() const override { return get_filt_type_name<decltype(*this)>(); }

private:
    uint64_t xorshiftmul64(uint64_t x)
    {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        return x * 2685821657736338717;
    }

private:
    vector<Node> nodes_;
};

template <int N = 65536> class YHash : public Hash {
public:
    YHash(hash_func_type hash = murmur_hash2)
        : Hash(hash)
    {
    }

    void init(vector<tuple<string, int>> nodes) override
    {
        nodes_.clear();
        memset(table_, 0, sizeof(table_));
        index_map_.clear();

        for (auto &node : nodes) {
            nodes_.emplace_back(std::get<0>(node), 0, std::get<1>(node));
            index_map_[std::get<0>(node)] = nodes_.size() - 1;
        }

        hrw_.init(nodes);
    }

    string get(const string &key) override
    {
        int k = hash_(key) % array_size(table_);
        int idx = table_[k];
        if (idx == 0) {
            auto s = hrw_.get(to_string(k));
            table_[k] = index_map_[s] + 1;
            idx = table_[k];
        }
        return nodes_[--idx].name;
    }

    string name() const override { return get_filt_type_name<decltype(*this)>(); }

private:
    HRWHash hrw_;
    int table_[N];
    vector<Node> nodes_;
    unordered_map<string, int> index_map_;
};

const static uint32_t fleaSeed = 0xf1ea5eed;
const static uint32_t fleaRot1 = 27;
const static uint32_t fleaRot2 = 17;
const static uint32_t fleaInitRounds = 3;

tuple<uint32_t, uint32_t, uint32_t, uint32_t> flea_init(uint64_t key)
{
    uint32_t seed = uint32_t((key >> 32) ^ key);
    uint32_t a = fleaSeed;
    uint32_t b = seed;
    uint32_t c = seed;
    uint32_t d = seed;

    for (uint32_t i = 0; i < fleaInitRounds; i++) {
        uint32_t e = a - (b << fleaRot1);
        a = b ^ (c << fleaRot2);
        b = c + d;
        c = d + e;
        d = e + a;
    }

    return { a, b, c, d };
    // return make_tuple(a, b, c, d);
}

tuple<uint32_t, uint32_t, uint32_t, uint32_t> flea_round(
    tuple<uint32_t, uint32_t, uint32_t, uint32_t> abcd)
{
    auto [a, b, c, d] = abcd;
    uint32_t e = a - (b << fleaRot1);
    a = b ^ (c << fleaRot2);
    b = c + d;
    c = d + e;
    d = e + a;
    return { a, b, c, d };
}

class CompactAnchor {
public:
    CompactAnchor(int cap)
        : A(cap, 0)
        , K(cap, 0)
        , W(cap, 0)
        , L(cap, 0)
        , R(cap, 0)
        , N(cap)
    {
        for (int i = 0; i < cap; i++) {
            K[i] = i;
            W[i] = i;
            L[i] = i;
        }
    }

    uint16_t get_bucket(uint64_t key)
    {
        auto [ha, hb, hc, hd] = flea_init(key);
        uint16_t b = hd % A.size();
        while (A[b] > 0) {
            auto [ha2, hb2, hc2, hd2] = flea_round({ ha, hb, hc, hd });
            ha = ha2;
            hb = hb2;
            hc = hc2;
            hd = hd2;
            uint16_t h = hd % A[b];
            while (A[h] >= A[b]) {
                h = K[h];
            }
            b = h;
        }
        return b;
    }

private:
    vector<uint16_t> A;
    vector<uint16_t> K;
    vector<uint16_t> W;
    vector<uint16_t> L;
    vector<uint16_t> R;
    int N;
};

class AnchorHash : public Hash {
public:
    AnchorHash(hash_func_type hash = murmur_hash2)
        : Hash(hash)
    {
    }

    void init(vector<tuple<string, int>> nodes) override
    {
        anchor_ = make_unique<CompactAnchor>(nodes.size());

        std::transform(nodes.cbegin(), nodes.cend(), std::back_inserter(nodes_),
            [](const tuple<string, int> &node) {
                const auto name = std::get<0>(node);
                return name;
            });
    }

    string get(const string &key) override { return nodes_[anchor_->get_bucket(hash_(key))]; }

    string name() const override { return get_filt_type_name<decltype(*this)>(); }

private:
    unique_ptr<CompactAnchor> anchor_;
    vector<string> nodes_;
};

string get_random_string()
{
    static default_random_engine e;
    return to_string(e());
}

vector<string> get_sequential_string_array(int num)
{
    vector<string> ret;

    for (int i = 0; i < num; i++) {
        ret.emplace_back(std::to_string(i));
    }

    return ret;
}

vector<string> get_random_string_array(int num)
{
    vector<string> ret;

    while (num-- > 0) {
        ret.emplace_back(get_random_string());
    }

    return ret;
}

vector<string> do_check_consistency_1(
    shared_ptr<Hash> hash, vector<string> strs, vector<tuple<string, int>> nodes)
{
    hash->init(nodes);
    vector<string> results;
    map<string, int> result_map;
    for (const auto &str : strs) {
        results.emplace_back(hash->get(str));
        result_map[results.back()]++;
    }
    for (const auto &res : result_map) {
        log_info(
            "%s: %d, %.03f", res.first.data(), res.second, (double)res.second / results.size());
    }
    return results;
}

void do_compare_results(vector<string> results1, vector<string> results2)
{
    int same = 0;
    int count = results1.size();
    assert(results2.size() == count);

    for (int i = 0; i < count; i++) {
        if (results2[i] == results1[i]) {
            same++;
        }
    }

    log_info("same is %d, count is %d, ratio is %0.02f", same, count, double(same) / count);
}

void check_consistency_1(shared_ptr<Hash> hash)
{
    int same1 = 0;
    int same2 = 0;
    int count = 100000;
    vector<string> strs = get_sequential_string_array(count);

    vector<string> results1 = do_check_consistency_1(hash, strs,
        { { "127.0.0.1", 9 }, { "127.0.0.2", 8 }, { "127.0.0.3", 6 }, { "127.0.0.4", 2 },
            { "127.0.0.5", 3 }, { "127.0.0.6", 5 } });
    vector<string> results2 = do_check_consistency_1(hash, strs,
        { { "127.0.0.1", 18 }, { "127.0.0.2", 16 }, { "127.0.0.3", 12 }, { "127.0.0.4", 4 },
            { "127.0.0.5", 6 }, { "127.0.0.6", 10 } });
    vector<string> results3 = do_check_consistency_1(hash, strs,
        { { "127.0.0.1", 18 }, { "127.0.0.2", 16 }, { "127.0.0.3", 12 }, { "127.0.0.4", 4 },
            { "127.0.0.5", 6 } });

    do_compare_results(results1, results2);
    do_compare_results(results1, results3);
}

void check_consistency_2()
{
    const int count = 100000;
    vector<string> strs = get_sequential_string_array(count);

    auto results1 = do_check_consistency_1(make_shared<YHash<16384>>(), strs,
        { { "127.0.0.1", 9 }, { "127.0.0.2", 8 }, { "127.0.0.3", 6 }, { "127.0.0.4", 2 },
            { "127.0.0.5", 3 }, { "127.0.0.6", 5 } });
    auto results2 = do_check_consistency_1(make_shared<YHash<32768>>(), strs,
        { { "127.0.0.1", 9 }, { "127.0.0.2", 8 }, { "127.0.0.3", 6 }, { "127.0.0.4", 2 },
            { "127.0.0.5", 3 }, { "127.0.0.6", 5 } });
    auto results3 = do_check_consistency_1(make_shared<YHash<65536>>(), strs,
        { { "127.0.0.1", 9 }, { "127.0.0.2", 8 }, { "127.0.0.3", 6 }, { "127.0.0.4", 2 },
            { "127.0.0.5", 3 }, { "127.0.0.6", 5 } });
    auto results4 = do_check_consistency_1(make_shared<YHash<16385>>(), strs,
        { { "127.0.0.1", 9 }, { "127.0.0.2", 8 }, { "127.0.0.3", 6 }, { "127.0.0.4", 2 },
            { "127.0.0.5", 3 }, { "127.0.0.6", 5 } });

    do_compare_results(results1, results2);
    do_compare_results(results1, results3);
    do_compare_results(results2, results3);
    do_compare_results(results1, results4);
}

void bench_hash(shared_ptr<Hash> hash, vector<string> strs, int node_count)
{
    string name = hash->name() + ":node_count=" + to_string(node_count);

    vector<tuple<string, int>> nodes;
    for (int i = 0; i < node_count; i++) {
        nodes.emplace_back(make_tuple("127.0.0." + to_string(i + 1), 5));
    }

    struct timeval tv_start = tv_now();
    hash->init(nodes);
    struct timeval tv_end = tv_now();
    double ms_taken = tv_sub_msec_double(tv_end, tv_start);

    log_info("%s init taken %.02fms", name.data(), ms_taken);

    tv_start = tv_now();
    for (const auto &str : strs) {
        hash->get(str);
    }
    tv_end = tv_now();

    ms_taken = tv_sub_msec_double(tv_end, tv_start);
    log_info("%s count=%lu taken %.02fms, %.02fus/op", name.data(), strs.size(), ms_taken,
        ms_taken * 1000 / strs.size());
}

void bench_strs(vector<shared_ptr<Hash>> hashs, vector<string> strs)
{
    for (const auto &hash : hashs) {
        bench_hash(hash, strs, 5);
        bench_hash(hash, strs, 50);
        bench_hash(hash, strs, 100);
        bench_hash(hash, strs, 250);
        bench_hash(hash, strs, 500);
    }
}

void bench(vector<shared_ptr<Hash>> hashs, int count)
{
    vector<string> strs = get_sequential_string_array(count);
    bench_strs(hashs, strs);
}

void bench_same_strs(vector<shared_ptr<Hash>> hashs, int count)
{
    vector<string> random_strs = get_random_string_array(1);
    string random_str = random_strs[0];
    vector<string> strs(count, random_str);
    bench_strs(hashs, strs);
}

void do_test_weight(shared_ptr<Hash> hash, vector<tuple<string, int>> nodes, vector<string> strs)
{
    hash->init(nodes);

    unordered_map<string, int> result_map;
    unordered_map<string, int> weight_map;

    int total_weight = std::accumulate(nodes.cbegin(), nodes.cend(), 0,
        [](int a, const tuple<string, int> &node) { return std::get<1>(node) + a; });

    for (const auto &str : strs) {
        result_map[hash->get(str)]++;
    }

    double total_diff = 0;

    for (const auto &node : nodes) {
        const string name = std::get<0>(node);
        const int weight = std::get<1>(node);
        double actual = result_map[name] / (double)strs.size();
        double expect = weight / (double)total_weight;
        double diff = actual - expect;
        if (diff < 0) {
            diff = -diff;
        }
        total_diff += diff;

        log_debug("%s, actual %0.2f%%, expect %0.2f%%, ratio %.02f", hash->name().data(),
            actual * 100, expect * 100, actual / expect);
    }

    log_info("%s, node_count = %d, total diff is %0.2f%%", hash->name().data(), nodes.size(),
        total_diff * 100);
}

void test_weight(
    vector<shared_ptr<Hash>> hashs, const int node_count = 50, bool enable_random_weight = false)
{
    vector<string> strs = get_sequential_string_array(1000000);
    vector<tuple<string, int>> nodes;
    for (int i = 0; i < node_count; i++) {
        int weight = 1;
        if (enable_random_weight) {
            weight += random() % 8;
        }

        nodes.emplace_back(make_tuple("127.0.0." + to_string(i + 1), weight));
    }

    for (auto &hash : hashs) {
        do_test_weight(hash, nodes, strs);
    }
}

int main(int argc, char **argv)
{
    for (const auto &c : vector<int>{ 5, 50, 100, 500 }) {
        test_weight(
            { make_shared<YHash<4096>>(), make_shared<YHash<16384>>(), make_shared<YHash<32768>>(),
                make_shared<YHash<65536>>(), make_shared<YHash<655360>>(), make_shared<CHash>(),
                make_shared<CHash2>(), make_shared<HRWHash>() },
            c);
    }

    check_consistency_2();

    check_consistency_1(make_shared<CHash>());
    check_consistency_1(make_shared<CHash2>());
    check_consistency_1(make_shared<HRWHash>());
    check_consistency_1(make_shared<YHash<>>());
    check_consistency_1(make_shared<YHash<16384>>());

    for (const auto &c : vector<int>{ 100000, 500000, 1000000 }) {
        bench({ make_shared<CHash>(), make_shared<CHash2>(), make_shared<HRWHash>(),
                  make_shared<YHash<>>(), make_shared<YHash<16384>>() },
            c);
    }

    bench_same_strs(
        { make_shared<CHash>(), make_shared<HRWHash>(), make_shared<YHash<>>() }, 1000000);

    return 0;
}
