#include "concurrencpp/platform_defs.h"
#include "concurrencpp/threads/atomic_wait.h"

#include <cassert>

#if defined(CRCPP_WIN_OS)

#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>

#    pragma comment(lib, "Synchronization.lib")

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int32_t old) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(old), INFINITE);
    }

    void atomic_wait_for_native(void* atom, int32_t old, std::chrono::milliseconds ms) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(old), static_cast<DWORD>(ms.count()));
    }

    void atomic_notify_one_native(void* atom) noexcept {
        ::WakeByAddressSingle(atom);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        ::WakeByAddressAll(atom);
    }
}  // namespace concurrencpp::details

#elif defined(CRCPP_UNIX_OS) || defined(CRCPP_FREE_BSD_OS)

#    include <ctime>

#    include <unistd.h>
#    include <linux/futex.h>
#    include <sys/syscall.h>

namespace concurrencpp::details {
    int futex(void* addr, int32_t op, int32_t old, const timespec* ts) noexcept {
        return ::syscall(SYS_futex, addr, op, old, ts, nullptr, 0);
    }

    timespec ms_to_time_spec(size_t ms) noexcept {
        timespec req;
        req.tv_sec = static_cast<time_t>(ms / 1000);
        req.tv_nsec = (ms % 1000) * 1'000'000;
        return req;
    }

    void atomic_wait_native(void* atom, int32_t old) noexcept {
        futex(atom, FUTEX_WAIT_PRIVATE, old, nullptr);
    }

    void atomic_wait_for_native(void* atom, int32_t old, std::chrono::milliseconds ms) noexcept {
        auto spec = ms_to_time_spec(ms.count());
        futex(atom, FUTEX_WAIT_PRIVATE, old, &spec);
    }

    void atomic_notify_one_native(void* atom) noexcept {
        futex(atom, FUTEX_WAKE_PRIVATE, 1, nullptr);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        futex(atom, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr);
    }
}  // namespace concurrencpp::details

#else

#    include "concurrencpp/threads/cache_line.h"

#    include <mutex>
#    include <condition_variable>

#    include <cstring>
#    include <cassert>

namespace concurrencpp::details {
    class atomic_wait_table {

       private:
        constexpr static size_t k_wait_table_size = 257;

        struct wait_context {
            const void* const storage_ptr;
            wait_context* next = nullptr;
            wait_context* prev = nullptr;
            std::condition_variable cv;

            wait_context(const void* storage_ptr) : storage_ptr(storage_ptr) {}
        };

        struct alignas(CRCPP_CACHE_LINE_ALIGNMENT) wait_table_bucket {
            std::mutex lock;
            wait_context* head = nullptr;

            void push_front_wait_ctx(wait_context* ctx) noexcept {
                assert(ctx != nullptr);
                assert(ctx->next == nullptr);
                assert(ctx->prev == nullptr);

                ctx->next = head;

                if (head != nullptr) {
                    head->prev = ctx;
                }

                head = ctx;
            }

            void remove_wait_ctx(wait_context* ctx) noexcept {
                assert(ctx != nullptr);
                assert(head != nullptr);

                if (head == ctx) {
                    head = ctx->next;
                }

                if (ctx->next != nullptr) {
                    ctx->next->prev = ctx->prev;
                }

                if (ctx->prev != nullptr) {
                    ctx->prev->next = ctx->next;
                }
            }
        };

        struct scoped_wait_context : public wait_context {
            wait_table_bucket& parent_bucket;

            scoped_wait_context(wait_table_bucket& parent_bucket, const void* storage_ptr) :
                wait_context(storage_ptr), parent_bucket(parent_bucket) {
                parent_bucket.push_front_wait_ctx(this);
            }

            ~scoped_wait_context() noexcept {
                parent_bucket.remove_wait_ctx(this);
            }
        };

       private:
        wait_table_bucket m_wait_table[k_wait_table_size];

        wait_table_bucket& get_bucket_for(const void* const storage_ptr) noexcept {
            const std::hash<std::uintptr_t> hasher;
            const auto address = reinterpret_cast<std::uintptr_t>(storage_ptr);
            const auto index = hasher(address) % k_wait_table_size;
            return m_wait_table[index];
        }

       public:
        void notify_one(const void* const storage_ptr) {
            auto& bucket = get_bucket_for(storage_ptr);

            std::unique_lock<std::mutex> lock(bucket.lock);
            for (auto cursor = bucket.head; cursor != nullptr; cursor = cursor->next) {
                if (cursor->storage_ptr == storage_ptr) {
                    cursor->cv.notify_all();
                    break;
                }
            }
        }

        void notify_all(const void* const storage_ptr) {
            auto& bucket = get_bucket_for(storage_ptr);

            std::unique_lock<std::mutex> guard(bucket.lock);
            for (auto cursor = bucket.head; cursor != nullptr; cursor = cursor->next) {
                if (cursor->storage_ptr == storage_ptr) {
                    cursor->cv.notify_all();
                }
            }
        }

        void wait(const void* const storage_ptr, const void* const comparand, size_t size) {
            auto& bucket = get_bucket_for(storage_ptr);

            std::unique_lock<std::mutex> lock(bucket.lock);
            scoped_wait_context context(bucket, storage_ptr);

            if (std::memcmp(storage_ptr, comparand, size) != 0) {
                return;
            }

            context.cv.wait(lock);
        }

        void wait(const void* const storage_ptr, const void* const comparand, size_t size, std::chrono::milliseconds timeout_ms) {
            auto& bucket = get_bucket_for(storage_ptr);

            std::unique_lock<std::mutex> lock(bucket.lock);
            scoped_wait_context context(bucket, storage_ptr);

            if (std::memcmp(storage_ptr, comparand, size) != 0) {
                return;
            }

            context.cv.wait_for(lock, timeout_ms);
        }

        static atomic_wait_table& instance() {
            static atomic_wait_table s_atomic_wait_table;
            return s_atomic_wait_table;
        }
    };

    void atomic_wait_native(void* atom, int32_t old) noexcept {
        try {
            atomic_wait_table::instance().wait(atom, &old, sizeof(old));
        } catch (...) {
        }
    }

    void atomic_wait_for_native(void* atom, int32_t old, std::chrono::milliseconds ms) noexcept {
        try {
            atomic_wait_table::instance().wait(atom, &old, sizeof(old), ms);
        } catch (...) {
        }
    }

    void atomic_notify_one_native(void* atom) noexcept {
        try {
            atomic_wait_table::instance().notify_one(atom);
        } catch (...) {
        }
    }

    void atomic_notify_all_native(void* atom) noexcept {
        try {
            atomic_wait_table::instance().notify_all(atom);
        } catch (...) {
        }
    }
}  // namespace concurrencpp::details

#endif
