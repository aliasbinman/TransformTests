#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <emmintrin.h>  // SSE2
#include <malloc.h>     // _aligned_malloc / _aligned_free

class ThreadPool {
public:
    explicit ThreadPool(int n) : count_(n), workers_(n) {
        for (int i = 0; i < n; ++i) {
            workers_[i] = std::thread([this, i] {
                int last_gen = 0;
                while (true) {
                    std::unique_lock<std::mutex> lock(mu_);
                    cv_.wait(lock, [&] { return gen_ != last_gen || stop_; });
                    if (stop_) return;
                    last_gen = gen_;
                    auto t = task_;
                    lock.unlock();

                    t(i, count_);

                    lock.lock();
                    if (++done_count_ == count_) done_cv_.notify_all();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
            ++gen_;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    void run(std::function<void(int, int)> fn) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            task_ = std::move(fn);
            done_count_ = 0;
            ++gen_;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> lock(mu_);
        done_cv_.wait(lock, [&] { return done_count_ == count_; });
    }
    int count() const { return count_; }
private:
    int count_;
    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cv_, done_cv_;
    int done_count_ = 0;
    int gen_ = 0;
    bool stop_ = false;
    std::function<void(int, int)> task_;
};

struct Point2 {
    float x, y;
};

// AoSoA block: 8 points with x's and y's in parallel lanes.
// One block = 16 floats = 4 SSE registers (2 for x, 2 for y).
// alignas(16) so vector storage is suitable for _mm_stream_ps.
struct alignas(16) Point2_8 {
    float x[8];
    float y[8];
};

// 3x2 affine matrix stored row-major:
// | m00 m01 m02 |   maps (x,y) -> (m00*x + m01*y + m02,
// | m10 m11 m12 |                  m10*x + m11*y + m12)
struct Mat3x2 {
    float m00, m01, m02;
    float m10, m11, m12;
};

struct Method {
    const char* name;
    std::function<void()> run;
    std::function<void()> reset;  // optional: restore in-place buffer from pristine before timing
    std::function<Point2(std::size_t)> get;  // read transformed point i from this method's dst
};

static void transform_points(const Point2* src, Point2* dst, std::size_t n, const Mat3x2& m) {

    for (std::size_t i = 0; i < n; ++i) {
        const float x = src->x;
        const float y = src->y;
        dst->x = m.m00 * x + m.m01 * y + m.m02;
        dst->y = m.m10 * x + m.m11 * y + m.m12;
        src++;
        dst++;
    }
}

// SSE: process 4 points per iteration (2 SSE registers, each holding 2 points).
// Requires n % 4 == 0.
static void transform_points_sse(const Point2* src, Point2* dst, std::size_t n, const Mat3x2& m) {
    // [a, c, a, c]
    const __m128 col0 = _mm_setr_ps(m.m00, m.m10, m.m00, m.m10);
    // [b, d, b, d]
    const __m128 col1 = _mm_setr_ps(m.m01, m.m11, m.m01, m.m11);
    // [tx, ty, tx, ty]
    const __m128 trans = _mm_setr_ps(m.m02, m.m12, m.m02, m.m12);

    const float* sp = reinterpret_cast<const float*>(src);
    float* dp = reinterpret_cast<float*>(dst);

    for (std::size_t i = 0; i < n; i += 4) {
        // Load 4 points = 8 floats as two registers.
        __m128 p01 = _mm_loadu_ps(sp + 0);  // [x0, y0, x1, y1]
        __m128 p23 = _mm_loadu_ps(sp + 4);  // [x2, y2, x3, y3]

        // Broadcast x and y components within each pair.
        __m128 xx01 = _mm_shuffle_ps(p01, p01, _MM_SHUFFLE(2, 2, 0, 0));  // [x0,x0,x1,x1]
        __m128 yy01 = _mm_shuffle_ps(p01, p01, _MM_SHUFFLE(3, 3, 1, 1));  // [y0,y0,y1,y1]
        __m128 xx23 = _mm_shuffle_ps(p23, p23, _MM_SHUFFLE(2, 2, 0, 0));
        __m128 yy23 = _mm_shuffle_ps(p23, p23, _MM_SHUFFLE(3, 3, 1, 1));

        __m128 r01 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(xx01, col0), _mm_mul_ps(yy01, col1)), trans);
        __m128 r23 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(xx23, col0), _mm_mul_ps(yy23, col1)), trans);

        _mm_storeu_ps(dp + 0, r01);
        _mm_storeu_ps(dp + 4, r23);

        sp += 8;
        dp += 8;
    }
}

// SSE SoA: x and y stored in separate parallel arrays.
// Processes 4 points per iteration. Requires n % 4 == 0.
static void transform_points_sse_soa(const float* src_x, const float* src_y,
                                     float* dst_x, float* dst_y,
                                     std::size_t n, const Mat3x2& m) {
    const __m128 va  = _mm_set1_ps(m.m00);
    const __m128 vb  = _mm_set1_ps(m.m01);
    const __m128 vtx = _mm_set1_ps(m.m02);
    const __m128 vc  = _mm_set1_ps(m.m10);
    const __m128 vd  = _mm_set1_ps(m.m11);
    const __m128 vty = _mm_set1_ps(m.m12);

    for (std::size_t i = 0; i < n; i += 4) {
        __m128 x = _mm_loadu_ps(src_x + i);
        __m128 y = _mm_loadu_ps(src_y + i);

        __m128 ox = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x), _mm_mul_ps(vb, y)), vtx);
        __m128 oy = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x), _mm_mul_ps(vd, y)), vty);

        _mm_storeu_ps(dst_x + i, ox);
        _mm_storeu_ps(dst_y + i, oy);
    }
}

// SSE SoA with non-temporal (streaming) stores: skip write-allocate so writes
// don't read dst cache lines first. Helps when working set exceeds LLC.
// dst_x and dst_y must be 16-byte aligned. Requires n % 4 == 0.
static void transform_points_sse_soa_stream(const float* src_x, const float* src_y,
                                            float* dst_x, float* dst_y,
                                            std::size_t n, const Mat3x2& m) {
    const __m128 va  = _mm_set1_ps(m.m00);
    const __m128 vb  = _mm_set1_ps(m.m01);
    const __m128 vtx = _mm_set1_ps(m.m02);
    const __m128 vc  = _mm_set1_ps(m.m10);
    const __m128 vd  = _mm_set1_ps(m.m11);
    const __m128 vty = _mm_set1_ps(m.m12);

    for (std::size_t i = 0; i < n; i += 4) {
        __m128 x = _mm_loadu_ps(src_x + i);
        __m128 y = _mm_loadu_ps(src_y + i);

        __m128 ox = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x), _mm_mul_ps(vb, y)), vtx);
        __m128 oy = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x), _mm_mul_ps(vd, y)), vty);

        _mm_stream_ps(dst_x + i, ox);
        _mm_stream_ps(dst_y + i, oy);
    }
    _mm_sfence();
}

// SSE AoSoA with non-temporal stores. dst must be 16-byte aligned
// (Point2_8 is alignas(16), so block-stride keeps every x/y subrange aligned).
// Requires n % 8 == 0.
static void transform_points_sse_aosoa_stream(const Point2_8* src, Point2_8* dst,
                                              std::size_t n, const Mat3x2& m) {
    const __m128 va  = _mm_set1_ps(m.m00);
    const __m128 vb  = _mm_set1_ps(m.m01);
    const __m128 vtx = _mm_set1_ps(m.m02);
    const __m128 vc  = _mm_set1_ps(m.m10);
    const __m128 vd  = _mm_set1_ps(m.m11);
    const __m128 vty = _mm_set1_ps(m.m12);

    const std::size_t blocks = n / 8;
    for (std::size_t b = 0; b < blocks; ++b) {
        const float* sx = src[b].x;
        const float* sy = src[b].y;
        float* dx = dst[b].x;
        float* dy = dst[b].y;

        __m128 x0 = _mm_load_ps(sx + 0);
        __m128 x1 = _mm_load_ps(sx + 4);
        __m128 y0 = _mm_load_ps(sy + 0);
        __m128 y1 = _mm_load_ps(sy + 4);

        __m128 ox0 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x0), _mm_mul_ps(vb, y0)), vtx);
        __m128 ox1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x1), _mm_mul_ps(vb, y1)), vtx);
        __m128 oy0 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x0), _mm_mul_ps(vd, y0)), vty);
        __m128 oy1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x1), _mm_mul_ps(vd, y1)), vty);

        _mm_stream_ps(dx + 0, ox0);
        _mm_stream_ps(dx + 4, ox1);
        _mm_stream_ps(dy + 0, oy0);
        _mm_stream_ps(dy + 4, oy1);
    }
    _mm_sfence();
}

// SSE AoSoA: process one Point2_8 block per outer iter (8 points = 2 SSE passes).
// Requires n % 8 == 0.
static void transform_points_sse_aosoa(const Point2_8* src, Point2_8* dst,
                                       std::size_t n, const Mat3x2& m) {
    const __m128 va  = _mm_set1_ps(m.m00);
    const __m128 vb  = _mm_set1_ps(m.m01);
    const __m128 vtx = _mm_set1_ps(m.m02);
    const __m128 vc  = _mm_set1_ps(m.m10);
    const __m128 vd  = _mm_set1_ps(m.m11);
    const __m128 vty = _mm_set1_ps(m.m12);

    const std::size_t blocks = n / 8;
    for (std::size_t b = 0; b < blocks; ++b) {
        const float* sx = src[b].x;
        const float* sy = src[b].y;
        float* dx = dst[b].x;
        float* dy = dst[b].y;

        __m128 x0 = _mm_loadu_ps(sx + 0);
        __m128 x1 = _mm_loadu_ps(sx + 4);
        __m128 y0 = _mm_loadu_ps(sy + 0);
        __m128 y1 = _mm_loadu_ps(sy + 4);

        __m128 ox0 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x0), _mm_mul_ps(vb, y0)), vtx);
        __m128 ox1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(va, x1), _mm_mul_ps(vb, y1)), vtx);
        __m128 oy0 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x0), _mm_mul_ps(vd, y0)), vty);
        __m128 oy1 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vc, x1), _mm_mul_ps(vd, y1)), vty);

        _mm_storeu_ps(dx + 0, ox0);
        _mm_storeu_ps(dx + 4, ox1);
        _mm_storeu_ps(dy + 0, oy0);
        _mm_storeu_ps(dy + 4, oy1);
    }
}

// Requires n % 4 == 0.
static void transform_points_unroll4(const Point2* src, Point2* dst, std::size_t n, const Mat3x2& m) {
    const float a = m.m00, b = m.m01, tx = m.m02;
    const float c = m.m10, d = m.m11, ty = m.m12;

    for (std::size_t i = 0; i < n; i += 4) {
        const float x0 = src[0].x, y0 = src[0].y;
        const float x1 = src[1].x, y1 = src[1].y;
        const float x2 = src[2].x, y2 = src[2].y;
        const float x3 = src[3].x, y3 = src[3].y;

        dst[0].x = a * x0 + b * y0 + tx;
        dst[0].y = c * x0 + d * y0 + ty;
        dst[1].x = a * x1 + b * y1 + tx;
        dst[1].y = c * x1 + d * y1 + ty;
        dst[2].x = a * x2 + b * y2 + tx;
        dst[2].y = c * x2 + d * y2 + ty;
        dst[3].x = a * x3 + b * y3 + tx;
        dst[3].y = c * x3 + d * y3 + ty;

        src += 4;
        dst += 4;
    }
}

int main(int argc, char** argv) {
    bool verbose = false;
    bool do_verify = false;
    bool do_cold = false;
    bool do_warm = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--verify") == 0) {
            do_verify = true;
        } else if (std::strcmp(argv[i], "--cold") == 0) {
            do_cold = true;
        } else if (std::strcmp(argv[i], "--no-warm") == 0) {
            do_warm = false;
        }
    }

    const std::size_t sizes[] = { 1'000, 10'000, 100'000, 1'000'000, 10'000'000 };
    constexpr int SIZE_COUNT = sizeof(sizes) / sizeof(sizes[0]);
    constexpr std::size_t MAX_N = 10'000'000;
    constexpr int ITERATIONS = 21;        // warm: drop iter 0 as warmup, average remaining 20
    constexpr int WARMUP_ITERS = 1;
    static_assert(MAX_N % 8 == 0, "AoSoA requires MAX_N % 8 == 0");

    std::vector<Point2> src(MAX_N);
    std::vector<Point2> dst(MAX_N);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
    for (std::size_t i = 0; i < MAX_N; ++i) {
        src[i].x = dist(rng);
        src[i].y = dist(rng);
    }

    // SoA mirror.
    std::vector<float> src_x(MAX_N), src_y(MAX_N);
    std::vector<float> dst_x(MAX_N), dst_y(MAX_N);
    for (std::size_t i = 0; i < MAX_N; ++i) {
        src_x[i] = src[i].x;
        src_y[i] = src[i].y;
    }

    // 16-byte aligned dst buffers for stream-store variant.
    auto aligned_free = [](float* p) { _aligned_free(p); };
    std::unique_ptr<float, decltype(aligned_free)> dst_x_aligned(
        static_cast<float*>(_aligned_malloc(MAX_N * sizeof(float), 16)), aligned_free);
    std::unique_ptr<float, decltype(aligned_free)> dst_y_aligned(
        static_cast<float*>(_aligned_malloc(MAX_N * sizeof(float), 16)), aligned_free);

    // AoSoA mirror.
    const std::size_t max_blocks = MAX_N / 8;
    std::vector<Point2_8> src_aosoa(max_blocks);
    std::vector<Point2_8> dst_aosoa(max_blocks);
    for (std::size_t b = 0; b < max_blocks; ++b) {
        for (int k = 0; k < 8; ++k) {
            src_aosoa[b].x[k] = src[b * 8 + k].x;
            src_aosoa[b].y[k] = src[b * 8 + k].y;
        }
    }

    // Example: rotate ~30deg, scale 1.5, translate (10, -20).
    const float c = 0.8660254f * 1.5f;
    const float s = 0.5f * 1.5f;
    Mat3x2 mat{ c, -s, 10.0f,
                s,  c, -20.0f };

    std::size_t cur_n = 0;

    // Per-layout getters: read point i back out of that method's dst buffer.
    auto get_aos      = [&](std::size_t i) -> Point2 { return dst[i]; };
    auto get_soa      = [&](std::size_t i) -> Point2 { return { dst_x[i], dst_y[i] }; };
    auto get_soa_alig = [&](std::size_t i) -> Point2 { return { dst_x_aligned.get()[i], dst_y_aligned.get()[i] }; };
    auto get_aosoa    = [&](std::size_t i) -> Point2 { return { dst_aosoa[i / 8].x[i % 8], dst_aosoa[i / 8].y[i % 8] }; };

    // ===== Out-of-place methods (read src, write dst) =====
    std::vector<Method> methods;
    methods.push_back({
        "scalar",
        [&]{ transform_points(src.data(), dst.data(), cur_n, mat); },
        {},
        get_aos
    });
    methods.push_back({
        "unroll4",
        [&]{ transform_points_unroll4(src.data(), dst.data(), cur_n, mat); },
        {},
        get_aos
    });
    methods.push_back({
        "sse",
        [&]{ transform_points_sse(src.data(), dst.data(), cur_n, mat); },
        {},
        get_aos
    });
    methods.push_back({
        "sse_soa",
        [&]{ transform_points_sse_soa(src_x.data(), src_y.data(),
                                      dst_x.data(), dst_y.data(), cur_n, mat); },
        {},
        get_soa
    });
    methods.push_back({
        "sse_aosoa",
        [&]{ transform_points_sse_aosoa(src_aosoa.data(), dst_aosoa.data(), cur_n, mat); },
        {},
        get_aosoa
    });
    methods.push_back({
        "sse_soa_stream",
        [&]{ transform_points_sse_soa_stream(src_x.data(), src_y.data(),
                                             dst_x_aligned.get(), dst_y_aligned.get(),
                                             cur_n, mat); },
        {},
        get_soa_alig
    });
    methods.push_back({
        "sse_aosoa_stream",
        [&]{ transform_points_sse_aosoa_stream(src_aosoa.data(), dst_aosoa.data(), cur_n, mat); },
        {},
        get_aosoa
    });

    // ===== In-place methods (dst == src) =====
    // Working buffers used as both src and dst, restored from pristine before each run.
    std::vector<Point2>  ip_aos(MAX_N);
    std::vector<float>   ip_x(MAX_N), ip_y(MAX_N);
    std::vector<Point2_8> ip_aosoa(max_blocks);
    std::unique_ptr<float, decltype(aligned_free)> ip_x_aligned(
        static_cast<float*>(_aligned_malloc(MAX_N * sizeof(float), 16)), aligned_free);
    std::unique_ptr<float, decltype(aligned_free)> ip_y_aligned(
        static_cast<float*>(_aligned_malloc(MAX_N * sizeof(float), 16)), aligned_free);

    // Pure rotation (det=1), no translation. Safe for 100 in-place compositions.
    const float ang = 3.14159265f / 6.0f;  // 30 degrees
    const float cr = std::cos(ang);
    const float sr = std::sin(ang);
    Mat3x2 mat_ip{ cr, -sr, 0.0f,
                   sr,  cr, 0.0f };

    auto reset_aos      = [&]{ std::memcpy(ip_aos.data(),         src.data(),       cur_n * sizeof(Point2)); };
    auto reset_soa      = [&]{ std::memcpy(ip_x.data(),           src_x.data(),     cur_n * sizeof(float));
                               std::memcpy(ip_y.data(),           src_y.data(),     cur_n * sizeof(float)); };
    auto reset_aosoa    = [&]{ std::memcpy(ip_aosoa.data(),       src_aosoa.data(), (cur_n / 8) * sizeof(Point2_8)); };
    auto reset_soa_alig = [&]{ std::memcpy(ip_x_aligned.get(),    src_x.data(),     cur_n * sizeof(float));
                               std::memcpy(ip_y_aligned.get(),    src_y.data(),     cur_n * sizeof(float)); };

    // Per-layout getters for in-place buffers.
    auto get_ip_aos      = [&](std::size_t i) -> Point2 { return ip_aos[i]; };
    auto get_ip_soa      = [&](std::size_t i) -> Point2 { return { ip_x[i], ip_y[i] }; };
    auto get_ip_soa_alig = [&](std::size_t i) -> Point2 { return { ip_x_aligned.get()[i], ip_y_aligned.get()[i] }; };
    auto get_ip_aosoa    = [&](std::size_t i) -> Point2 { return { ip_aosoa[i / 8].x[i % 8], ip_aosoa[i / 8].y[i % 8] }; };

    std::vector<Method> methods_ip;
    methods_ip.push_back({
        "scalar",
        [&]{ transform_points(ip_aos.data(), ip_aos.data(), cur_n, mat_ip); },
        reset_aos,
        get_ip_aos
    });
    methods_ip.push_back({
        "unroll4",
        [&]{ transform_points_unroll4(ip_aos.data(), ip_aos.data(), cur_n, mat_ip); },
        reset_aos,
        get_ip_aos
    });
    methods_ip.push_back({
        "sse",
        [&]{ transform_points_sse(ip_aos.data(), ip_aos.data(), cur_n, mat_ip); },
        reset_aos,
        get_ip_aos
    });
    methods_ip.push_back({
        "sse_soa",
        [&]{ transform_points_sse_soa(ip_x.data(), ip_y.data(),
                                      ip_x.data(), ip_y.data(), cur_n, mat_ip); },
        reset_soa,
        get_ip_soa
    });
    methods_ip.push_back({
        "sse_aosoa",
        [&]{ transform_points_sse_aosoa(ip_aosoa.data(), ip_aosoa.data(), cur_n, mat_ip); },
        reset_aosoa,
        get_ip_aosoa
    });
    methods_ip.push_back({
        "sse_soa_stream",
        [&]{ transform_points_sse_soa_stream(ip_x_aligned.get(), ip_y_aligned.get(),
                                             ip_x_aligned.get(), ip_y_aligned.get(),
                                             cur_n, mat_ip); },
        reset_soa_alig,
        get_ip_soa_alig
    });
    methods_ip.push_back({
        "sse_aosoa_stream",
        [&]{ transform_points_sse_aosoa_stream(ip_aosoa.data(), ip_aosoa.data(), cur_n, mat_ip); },
        reset_aosoa,
        get_ip_aosoa
    });

    // ----- Thread pools + threaded method variants -----
    ThreadPool pool10(10);
    ThreadPool pool4(4);

    // chunk_fn(begin, count) operates on [begin, begin+count) points.
    // Per-thread chunk is rounded down to multiple of 8 (AoSoA constraint);
    // the last thread absorbs the remainder.
    auto make_threaded = [&](ThreadPool& pool,
                             std::function<void(std::size_t, std::size_t)> chunk_fn) {
        return [&pool, &cur_n, chunk_fn = std::move(chunk_fn)] {
            pool.run([&, chunk_fn](int tid, int nth) {
                std::size_t per = cur_n / nth;
                per = (per / 8) * 8;
                std::size_t begin = static_cast<std::size_t>(tid) * per;
                std::size_t count = (tid == nth - 1) ? (cur_n - begin) : per;
                chunk_fn(begin, count);
            });
        };
    };

    auto make_oop_methods = [&](ThreadPool& p) {
        std::vector<Method> v;
        v.push_back({ "scalar",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points(src.data() + b, dst.data() + b, c, mat); }),
            {}, get_aos });
        v.push_back({ "unroll4",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_unroll4(src.data() + b, dst.data() + b, c, mat); }),
            {}, get_aos });
        v.push_back({ "sse",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse(src.data() + b, dst.data() + b, c, mat); }),
            {}, get_aos });
        v.push_back({ "sse_soa",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_soa(src_x.data()+b, src_y.data()+b,
                                         dst_x.data()+b, dst_y.data()+b, c, mat); }),
            {}, get_soa });
        v.push_back({ "sse_aosoa",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_aosoa(src_aosoa.data() + b/8,
                                           dst_aosoa.data() + b/8, c, mat); }),
            {}, get_aosoa });
        v.push_back({ "sse_soa_stream",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_soa_stream(src_x.data()+b, src_y.data()+b,
                                                dst_x_aligned.get()+b, dst_y_aligned.get()+b,
                                                c, mat); }),
            {}, get_soa_alig });
        v.push_back({ "sse_aosoa_stream",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_aosoa_stream(src_aosoa.data() + b/8,
                                                  dst_aosoa.data() + b/8, c, mat); }),
            {}, get_aosoa });
        return v;
    };

    auto make_ip_methods = [&](ThreadPool& p) {
        std::vector<Method> v;
        v.push_back({ "scalar",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points(ip_aos.data()+b, ip_aos.data()+b, c, mat_ip); }),
            reset_aos, get_ip_aos });
        v.push_back({ "unroll4",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_unroll4(ip_aos.data()+b, ip_aos.data()+b, c, mat_ip); }),
            reset_aos, get_ip_aos });
        v.push_back({ "sse",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse(ip_aos.data()+b, ip_aos.data()+b, c, mat_ip); }),
            reset_aos, get_ip_aos });
        v.push_back({ "sse_soa",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_soa(ip_x.data()+b, ip_y.data()+b,
                                         ip_x.data()+b, ip_y.data()+b, c, mat_ip); }),
            reset_soa, get_ip_soa });
        v.push_back({ "sse_aosoa",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_aosoa(ip_aosoa.data()+b/8, ip_aosoa.data()+b/8, c, mat_ip); }),
            reset_aosoa, get_ip_aosoa });
        v.push_back({ "sse_soa_stream",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_soa_stream(ip_x_aligned.get()+b, ip_y_aligned.get()+b,
                                                ip_x_aligned.get()+b, ip_y_aligned.get()+b,
                                                c, mat_ip); }),
            reset_soa_alig, get_ip_soa_alig });
        v.push_back({ "sse_aosoa_stream",
            make_threaded(p, [&](std::size_t b, std::size_t c){
                transform_points_sse_aosoa_stream(ip_aosoa.data()+b/8,
                                                  ip_aosoa.data()+b/8, c, mat_ip); }),
            reset_aosoa, get_ip_aosoa });
        return v;
    };

    std::vector<Method> methods_mt10    = make_oop_methods(pool10);
    std::vector<Method> methods_ip_mt10 = make_ip_methods(pool10);
    std::vector<Method> methods_mt4     = make_oop_methods(pool4);
    std::vector<Method> methods_ip_mt4  = make_ip_methods(pool4);

    // ----- Cache-thrash buffer for cold-cache bench -----
    // Sized to exceed typical LLC (i7-14700HX has 33 MB L3). 128 MB guarantees
    // every line of working-set + dst gets evicted between iterations.
    constexpr std::size_t CACHE_THRASH_BYTES = 128ull * 1024 * 1024;
    std::vector<unsigned char> thrash_buf(CACHE_THRASH_BYTES, 0);
    auto thrash_cache = [&]{
        // Touch one byte per 64-byte line. Increment forces a write (dirty line)
        // so the cache controller must evict, not just invalidate.
        unsigned char* p = thrash_buf.data();
        for (std::size_t i = 0; i < CACHE_THRASH_BYTES; i += 64) {
            p[i] += 1;
        }
    };

    // ----- Benchmark runner -----
    using Grid = std::vector<std::vector<double>>;
    auto bench = [&](const std::vector<Method>& ms) {
        const int mc = static_cast<int>(ms.size());
        Grid grid(mc, std::vector<double>(SIZE_COUNT));
        for (int si = 0; si < SIZE_COUNT; ++si) {
            cur_n = sizes[si];
            for (int mi = 0; mi < mc; ++mi) {
                const Method& method = ms[mi];
                if (method.reset) method.reset();
                double total_ms = 0.0;
                for (int it = 0; it < ITERATIONS; ++it) {
                    auto t0 = std::chrono::high_resolution_clock::now();
                    method.run();
                    auto t1 = std::chrono::high_resolution_clock::now();
                    // Skip first WARMUP_ITERS samples: cold-cache cost on iter 0
                    // would otherwise inflate the mean of a "warm" measurement.
                    if (it >= WARMUP_ITERS) {
                        total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
                    }
                }
                grid[mi][si] = total_ms / (ITERATIONS - WARMUP_ITERS);
            }
        }
        return grid;
    };

    // Cold-cache bench: thrash 128 MB scratch buffer before EACH timed iter so
    // every method runs against an evicted src/dst. Reset happens each iter too
    // because thrash itself wrote to memory (and we want a clean starting state
    // for in-place methods). Thrash + reset time is NOT included in the sample.
    // Fewer iterations than warm bench since each iter costs ~5-10 ms of thrash.
    constexpr int COLD_ITERATIONS = 20;
    auto bench_cold = [&](const std::vector<Method>& ms) {
        const int mc = static_cast<int>(ms.size());
        Grid grid(mc, std::vector<double>(SIZE_COUNT));
        for (int si = 0; si < SIZE_COUNT; ++si) {
            cur_n = sizes[si];
            for (int mi = 0; mi < mc; ++mi) {
                const Method& method = ms[mi];
                double total_ms = 0.0;
                for (int it = 0; it < COLD_ITERATIONS; ++it) {
                    if (method.reset) method.reset();
                    thrash_cache();
                    auto t0 = std::chrono::high_resolution_clock::now();
                    method.run();
                    auto t1 = std::chrono::high_resolution_clock::now();
                    total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
                }
                grid[mi][si] = total_ms / COLD_ITERATIONS;
            }
        }
        return grid;
    };

    // ----- Verify: every method must match scalar bit-for-bit (or within float eps). -----
    // Picks one N, runs scalar to capture reference, then runs each method and
    // compares its dst (via Method::get) elementwise. Reports max absolute diff
    // in x or y, plus the worst index and both points there.
    auto verify = [&](const char* label, const std::vector<Method>& ms) {
        const std::size_t N = 1'000'000;  // big enough to exercise vector tails
        cur_n = N;
        std::printf("\n=== Verify: %s (N=%zu) ===\n", label, N);
        if (ms.empty()) return;

        // Reference = scalar (assumed at index 0).
        const Method& ref = ms[0];
        if (ref.reset) ref.reset();
        ref.run();
        std::vector<Point2> reference(N);
        for (std::size_t i = 0; i < N; ++i) reference[i] = ref.get(i);

        for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
            const Method& m = ms[mi];
            if (m.reset) m.reset();
            m.run();

            double max_diff = 0.0;
            std::size_t worst = 0;
            std::size_t mismatches = 0;
            for (std::size_t i = 0; i < N; ++i) {
                Point2 p = m.get(i);
                double dx = std::fabs(static_cast<double>(p.x) - static_cast<double>(reference[i].x));
                double dy = std::fabs(static_cast<double>(p.y) - static_cast<double>(reference[i].y));
                double d = dx > dy ? dx : dy;
                if (d > 0.0) ++mismatches;
                if (d > max_diff) { max_diff = d; worst = i; }
            }
            if (max_diff == 0.0) {
                std::printf("  %-18s IDENTICAL to scalar\n", m.name);
            } else {
                Point2 mp = m.get(worst);
                std::printf("  %-18s max_abs_diff=%.3e  mismatches=%zu/%zu  worst idx=%zu  scalar=(%.7f,%.7f)  method=(%.7f,%.7f)\n",
                            m.name, max_diff, mismatches, N, worst,
                            reference[worst].x, reference[worst].y,
                            mp.x, mp.y);
            }
        }
    };

    auto write_csv = [&](const char* filename, const std::vector<Method>& ms, const Grid& g) {
        FILE* f = std::fopen(filename, "w");
        if (!f) { std::printf("[warn] cannot write %s\n", filename); return; }
        // Top row: "method" header then sizes across.
        std::fprintf(f, "method");
        for (int si = 0; si < SIZE_COUNT; ++si) std::fprintf(f, ",%zu", sizes[si]);
        std::fprintf(f, "\n");
        // One row per method, times across sizes.
        for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
            std::fprintf(f, "%s", ms[mi].name);
            for (int si = 0; si < SIZE_COUNT; ++si) {
                std::fprintf(f, ",%.6f", g[mi][si]);
            }
            std::fprintf(f, "\n");
        }
        std::fclose(f);
        std::printf("[csv] wrote %s\n", filename);
    };

    // Percent of scalar (row 0) for this grid; lower is better. Rounded to int.
    auto pct_vs_scalar = [](const Grid& g, int mi, int si) -> int {
        double s = g[0][si];
        if (s <= 0.0) return 0;
        double p = g[mi][si] / s * 100.0;
        if (p < 0.0) p = 0.0;
        if (p > 999999.0) p = 999999.0;
        return static_cast<int>(p + 0.5);
    };

    auto print_grid_iters = [&](const char* label, int iters, const std::vector<Method>& ms, const Grid& g) {
        std::printf("\n=== %s (ms with (NN%%) vs scalar, %d iterations) ===\n", label, iters);
        std::printf("  %-18s", "method \\ N");
        for (int si = 0; si < SIZE_COUNT; ++si) std::printf(" %18zu", sizes[si]);
        std::printf("\n");
        for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
            std::printf("  %-18s", ms[mi].name);
            for (int si = 0; si < SIZE_COUNT; ++si) {
                std::printf("  %8.4f (%5d%%)", g[mi][si], pct_vs_scalar(g, mi, si));
            }
            std::printf("\n");
        }
    };
    auto print_grid = [&](const char* label, const std::vector<Method>& ms, const Grid& g) {
        print_grid_iters(label, ITERATIONS - WARMUP_ITERS, ms, g);
    };

    auto print_grid_speedup_iters = [&](const char* label, int iters, int nth,
                                        const std::vector<Method>& ms, const Grid& g) {
        std::printf("\n=== %s (ms with (NN%%) vs scalar, %d iterations, %d threads) ===\n",
                    label, iters, nth);
        std::printf("  %-18s", "method \\ N");
        for (int si = 0; si < SIZE_COUNT; ++si) std::printf(" %18zu", sizes[si]);
        std::printf("\n");
        for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
            std::printf("  %-18s", ms[mi].name);
            for (int si = 0; si < SIZE_COUNT; ++si) {
                std::printf("  %8.4f (%5d%%)", g[mi][si], pct_vs_scalar(g, mi, si));
            }
            std::printf("\n");
        }
    };
    auto print_grid_speedup = [&](const char* label, int nth,
                                  const std::vector<Method>& ms,
                                  const Grid& g, const Grid& /*baseline*/) {
        print_grid_speedup_iters(label, ITERATIONS - WARMUP_ITERS, nth, ms, g);
    };

    std::printf("Benchmarking %d method(s) across %d size(s), %d iters (+%d warmup discarded)%s\n",
                static_cast<int>(methods.size()), SIZE_COUNT,
                ITERATIONS - WARMUP_ITERS, WARMUP_ITERS,
                verbose ? " [verbose]" : "");

    // ----- Verify pass: every method must match scalar. -----
    if (do_verify) {
        verify("Out-of-place (1 thread)",          methods);
        verify("In-place (1 thread)",              methods_ip);
        verify("Out-of-place (4 threads)",         methods_mt4);
        verify("In-place (4 threads)",             methods_ip_mt4);
        verify("Out-of-place (10 threads)",        methods_mt10);
        verify("In-place (10 threads)",            methods_ip_mt10);
    }

    if (do_warm) {
        Grid g_oop = bench(methods);
        print_grid("Out-of-place (1 thread)", methods, g_oop);
        write_csv("results_oop_1t.csv", methods, g_oop);

        Grid g_ip  = bench(methods_ip);
        print_grid("In-place (1 thread, rotation-only mat)", methods_ip, g_ip);
        write_csv("results_ip_1t.csv", methods_ip, g_ip);

        Grid g_oop_mt4 = bench(methods_mt4);
        print_grid_speedup("Out-of-place (4 threads)", 4, methods_mt4, g_oop_mt4, g_oop);
        write_csv("results_oop_4t.csv", methods_mt4, g_oop_mt4);

        Grid g_ip_mt4 = bench(methods_ip_mt4);
        print_grid_speedup("In-place (4 threads, rotation-only mat)", 4, methods_ip_mt4, g_ip_mt4, g_ip);
        write_csv("results_ip_4t.csv", methods_ip_mt4, g_ip_mt4);

        Grid g_oop_mt10 = bench(methods_mt10);
        print_grid_speedup("Out-of-place (10 threads)", 10, methods_mt10, g_oop_mt10, g_oop);
        write_csv("results_oop_10t.csv", methods_mt10, g_oop_mt10);

        Grid g_ip_mt10  = bench(methods_ip_mt10);
        print_grid_speedup("In-place (10 threads, rotation-only mat)", 10, methods_ip_mt10, g_ip_mt10, g_ip);
        write_csv("results_ip_10t.csv", methods_ip_mt10, g_ip_mt10);

        // Long-form combined CSV (good for pivots / scripted plotting).
        FILE* lf = std::fopen("results_long.csv", "w");
        if (lf) {
            std::fprintf(lf, "table,threads,inplace,method,N,avg_ms\n");
            auto dump = [&](const char* table, int threads, int inplace,
                            const std::vector<Method>& ms, const Grid& g) {
                for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
                    for (int si = 0; si < SIZE_COUNT; ++si) {
                        std::fprintf(lf, "%s,%d,%d,%s,%zu,%.6f\n",
                                     table, threads, inplace,
                                     ms[mi].name, sizes[si], g[mi][si]);
                    }
                }
            };
            dump("oop_1t",  1,  0, methods,         g_oop);
            dump("ip_1t",   1,  1, methods_ip,      g_ip);
            dump("oop_4t",  4,  0, methods_mt4,     g_oop_mt4);
            dump("ip_4t",   4,  1, methods_ip_mt4,  g_ip_mt4);
            dump("oop_10t", 10, 0, methods_mt10,    g_oop_mt10);
            dump("ip_10t",  10, 1, methods_ip_mt10, g_ip_mt10);
            std::fclose(lf);
            std::printf("[csv] wrote results_long.csv\n");
        }
    }

    // ----- Cold-cache pass: 128 MB scratch thrashed before each iter. -----
    if (do_cold) {
        std::printf("\n############ COLD CACHE (thrash %zu MB between iters) ############\n",
                    CACHE_THRASH_BYTES / (1024 * 1024));

        Grid c_oop = bench_cold(methods);
        print_grid_iters("COLD Out-of-place (1 thread)", COLD_ITERATIONS, methods, c_oop);
        write_csv("results_cold_oop_1t.csv", methods, c_oop);

        Grid c_ip  = bench_cold(methods_ip);
        print_grid_iters("COLD In-place (1 thread, rotation-only mat)", COLD_ITERATIONS, methods_ip, c_ip);
        write_csv("results_cold_ip_1t.csv", methods_ip, c_ip);

        Grid c_oop_mt4 = bench_cold(methods_mt4);
        print_grid_speedup_iters("COLD Out-of-place (4 threads)", COLD_ITERATIONS, 4, methods_mt4, c_oop_mt4);
        write_csv("results_cold_oop_4t.csv", methods_mt4, c_oop_mt4);

        Grid c_ip_mt4 = bench_cold(methods_ip_mt4);
        print_grid_speedup_iters("COLD In-place (4 threads, rotation-only mat)", COLD_ITERATIONS, 4, methods_ip_mt4, c_ip_mt4);
        write_csv("results_cold_ip_4t.csv", methods_ip_mt4, c_ip_mt4);

        Grid c_oop_mt10 = bench_cold(methods_mt10);
        print_grid_speedup_iters("COLD Out-of-place (10 threads)", COLD_ITERATIONS, 10, methods_mt10, c_oop_mt10);
        write_csv("results_cold_oop_10t.csv", methods_mt10, c_oop_mt10);

        Grid c_ip_mt10  = bench_cold(methods_ip_mt10);
        print_grid_speedup_iters("COLD In-place (10 threads, rotation-only mat)", COLD_ITERATIONS, 10, methods_ip_mt10, c_ip_mt10);
        write_csv("results_cold_ip_10t.csv", methods_ip_mt10, c_ip_mt10);

        FILE* lf = std::fopen("results_cold_long.csv", "w");
        if (lf) {
            std::fprintf(lf, "table,threads,inplace,method,N,avg_ms\n");
            auto dump = [&](const char* table, int threads, int inplace,
                            const std::vector<Method>& ms, const Grid& g) {
                for (int mi = 0; mi < static_cast<int>(ms.size()); ++mi) {
                    for (int si = 0; si < SIZE_COUNT; ++si) {
                        std::fprintf(lf, "%s,%d,%d,%s,%zu,%.6f\n",
                                     table, threads, inplace,
                                     ms[mi].name, sizes[si], g[mi][si]);
                    }
                }
            };
            dump("cold_oop_1t",  1,  0, methods,         c_oop);
            dump("cold_ip_1t",   1,  1, methods_ip,      c_ip);
            dump("cold_oop_4t",  4,  0, methods_mt4,     c_oop_mt4);
            dump("cold_ip_4t",   4,  1, methods_ip_mt4,  c_ip_mt4);
            dump("cold_oop_10t", 10, 0, methods_mt10,    c_oop_mt10);
            dump("cold_ip_10t",  10, 1, methods_ip_mt10, c_ip_mt10);
            std::fclose(lf);
            std::printf("[csv] wrote results_cold_long.csv\n");
        }
    }

    return 0;
}
