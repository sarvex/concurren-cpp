#include "concurrencpp/platform_defs.h"
#include "concurrencpp/threads/atomic_wait.h"

#include <cassert>

#if defined(CRCPP_WIN_OS)

#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>

#    pragma comment(lib, "Synchronization.lib")

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int32_t old) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(atom), INFINITE);
    }

    void atomic_wait_for_native(void* atom, int32_t old, std::chrono::milliseconds ms) noexcept {
        ::WaitOnAddress(atom, &old, sizeof(atom), static_cast<DWORD>(ms.count()));
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

#elif defined(CRCPP_MAC_OS)

extern "C" {
    int __ulock_wait(uint32_t op, void* addr, uint64_t value, uint32_t timeout);
    int __ulock_wake(uint32_t op, void* addr, uint64_t wake_value);
}  // extern "C"

enum ulock_flags {
	UL_COMPARE_AND_WAIT = 1,
	UL_COMPARE_AND_WAIT_64 = 5,
	ULF_WAKE_ALL = 0x00000100,
	ULF_NO_ERRNO = 0x01000000
};

namespace concurrencpp::details {
    void atomic_wait_native(void* atom, int32_t old) noexcept {
        __ulock_wait(UL_COMPARE_AND_WAIT_64 | ULF_NO_ERRNO, atom, old, 0);
    }

    void atomic_wait_for_native(void* atom, int32_t old, std::chrono::milliseconds ms) noexcept {
        __ulock_wait(UL_COMPARE_AND_WAIT_64 | ULF_NO_ERRNO, atom, old, ms.count());
    }

    void atomic_notify_one_native(void* atom) noexcept {
        __ulock_wake(UL_COMPARE_AND_WAIT_64 | ULF_NO_ERRNO, atom, 0);
    }

    void atomic_notify_all_native(void* atom) noexcept {
        __ulock_wake(UL_COMPARE_AND_WAIT_64 | ULF_WAKE_ALL | ULF_NO_ERRNO, atom, 0);
    }
}  // namespace concurrencpp::details

#endif