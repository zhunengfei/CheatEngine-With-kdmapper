#pragma once
#include <immintrin.h>
#include <vector>
#include <cstdint>
#include <type_traits>

enum class SimdOp { Equal, Greater, Less, NotEqual };

template<typename T>
struct SimdKernel {
    // 整数比较适配
    static inline __m256i cmp_int(__m256i a, __m256i b, SimdOp op) {
        if constexpr (sizeof(T) == 1) { // Int8
            if (op == SimdOp::Equal) return _mm256_cmpeq_epi8(a, b);
            if (op == SimdOp::Greater) return _mm256_cmpgt_epi8(a, b);
            if (op == SimdOp::Less) return _mm256_cmpgt_epi8(b, a);
        }
        else if constexpr (sizeof(T) == 2) { // Int16
            if (op == SimdOp::Equal) return _mm256_cmpeq_epi16(a, b);
            if (op == SimdOp::Greater) return _mm256_cmpgt_epi16(a, b);
            if (op == SimdOp::Less) return _mm256_cmpgt_epi16(b, a);
        }
        else if constexpr (sizeof(T) == 4) { // Int32
            if (op == SimdOp::Equal) return _mm256_cmpeq_epi32(a, b);
            if (op == SimdOp::Greater) return _mm256_cmpgt_epi32(a, b);
            if (op == SimdOp::Less) return _mm256_cmpgt_epi32(b, a);
        }
        else if constexpr (sizeof(T) == 8) { // Int64
            if (op == SimdOp::Equal) return _mm256_cmpeq_epi64(a, b);
            if (op == SimdOp::Greater) return _mm256_cmpgt_epi64(a, b);
            if (op == SimdOp::Less) return _mm256_cmpgt_epi64(b, a);
        }
        return _mm256_setzero_si256();
    }

    // 浮点比较适配 (区分 float 和 double)
    static inline __m256 cmp_fp_ps(__m256 a, __m256 b, SimdOp op) {
        // 必须使用 switch 或 if-else 并在每个分支硬编码指令
        switch (op) {
        case SimdOp::Equal:   return _mm256_cmp_ps(a, b, _CMP_EQ_OQ);
        case SimdOp::Greater: return _mm256_cmp_ps(a, b, _CMP_GT_OQ);
        case SimdOp::Less:    return _mm256_cmp_ps(a, b, _CMP_LT_OQ);
        case SimdOp::NotEqual: return _mm256_cmp_ps(a, b, _CMP_NEQ_OQ);
        default: return _mm256_setzero_ps();
        }
    }

    static inline __m256d cmp_fp_pd(__m256d a, __m256d b, SimdOp op) {
        switch (op) {
        case SimdOp::Equal:   return _mm256_cmp_pd(a, b, _CMP_EQ_OQ);
        case SimdOp::Greater: return _mm256_cmp_pd(a, b, _CMP_GT_OQ);
        case SimdOp::Less:    return _mm256_cmp_pd(a, b, _CMP_LT_OQ);
        case SimdOp::NotEqual: return _mm256_cmp_pd(a, b, _CMP_NEQ_OQ);
        default: return _mm256_setzero_pd();
        }
    }
};

class SimdScanner {
public:
    /**
     * @brief 统一 SIMD 扫描入口
     * @tparam T 数据类型 (int32_t, float 等)
     * @param op 扫描逻辑 (ExactValue, GreaterThan, LessThan)
     * @param align 对齐要求，由 ScanRequest 传入
     */
    template<typename T>
    static void execute(const uint8_t* cur, const uint8_t* tgt, size_t size,
        uint64_t base, size_t align, SimdOp op, std::vector<uint64_t>& res) {

        const size_t step = 32;
        const size_t typeSize = sizeof(T);
        const size_t effectiveAlign = (align > 0) ? align : typeSize;

        for (size_t i = 0; i + step <= size; i += step) {
            __m256i vC = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + i));
            __m256i vT = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(tgt + i));
            uint32_t mask = 0;

            if constexpr (std::is_same_v<T, float>) {
                mask = _mm256_movemask_ps(SimdKernel<T>::cmp_fp_ps(_mm256_castsi256_ps(vC), _mm256_castsi256_ps(vT), op));
            }
            else if constexpr (std::is_same_v<T, double>) {
                mask = _mm256_movemask_pd(SimdKernel<T>::cmp_fp_pd(_mm256_castsi256_pd(vC), _mm256_castsi256_pd(vT), op));
            }
            else {
                mask = _mm256_movemask_epi8(SimdKernel<T>::cmp_int(vC, vT, op));
            }

            if (mask != 0) {
                // 这里的位遍历需要根据类型调整步长
                for (size_t j = 0; j < step; j += typeSize) {
                    uint64_t addr = base + i + j;
                    if (addr % effectiveAlign == 0) {
                        bool hit = false;
                        if constexpr (std::is_same_v<T, float>) hit = (mask & (1 << (j / 4)));
                        else if constexpr (std::is_same_v<T, double>) hit = (mask & (1 << (j / 8)));
                        else hit = (mask & (1 << j));

                        if (hit) res.push_back(addr);
                    }
                }
            }
        }

        // 2. 标量补丁：处理区域末尾不足 32 字节的部分
        for (size_t i = (size / step) * step; i + typeSize <= size; i += typeSize) {
            uint64_t addr = base + i;
            if (addr % effectiveAlign == 0) {
                T v1, v2;
                std::memcpy(&v1, cur + i, typeSize);
                std::memcpy(&v2, tgt + i, typeSize);

                bool match = false;
                if (op == SimdOp::Equal) match = (v1 == v2);
                else if (op == SimdOp::Greater) match = (v1 > v2);
                else if (op == SimdOp::Less) match = (v1 < v2);

                if (match) res.push_back(addr);
            }
        }
    }

    /**
     * @brief 使用 SIMD 查找第一个匹配字符的所有偏移量
     */
    static void findFirstChar(const uint8_t* cur, size_t size, uint8_t targetChar, std::vector<size_t>& offsets) {
        const size_t step = 32;
        __m256i vTarget = _mm256_set1_epi8(static_cast<char>(targetChar));

        for (size_t i = 0; i + step <= size; i += step) {
            __m256i vData = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + i));
            uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(vData, vTarget));

            while (mask != 0) {
                unsigned long bitPos;
                if (_BitScanForward(&bitPos, mask)) { // 找到 mask 中第一个 1 的位置
                    offsets.push_back(i + bitPos);
                    mask &= ~(1 << bitPos); // 清除已处理的位
                }
            }
        }
    }


};