#include "concurrencpp/concurrencpp.h"

#include "infra/tester.h"
#include "infra/assertions.h"

#include <iostream>

namespace concurrencpp::tests {
    void test_atomic_wait();

    void test_atomic_wait_for_timeout_1();
    void test_atomic_wait_for_timeout_2();
    void test_atomic_wait_for_timeout_3();
    void test_atomic_wait_for_success();
    void test_atomic_wait_for();

    void test_atomic_notify_one();
    void test_atomic_notify_all();

}  // namespace concurrencpp::tests

using namespace concurrencpp::tests;

void concurrencpp::tests::test_atomic_wait() {
    std::atomic_int flag {0};
    std::atomic_bool woken {false};

    std::thread waiter([&] {
        concurrencpp::details::atomic_wait(flag, 0, std::memory_order_acquire);
        woken = true;
    });

    for (size_t i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert_false(woken.load());
    }

    // notify was called, but value hadn't changed
    for (size_t i = 0; i < 5; i++) {
        concurrencpp::details::atomic_notify_one(flag);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert_false(woken.load());
    }

    // value had changed, but notify wasn't called
    flag = 1;
    for (size_t i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert_false(woken.load());
    }

    concurrencpp::details::atomic_notify_one(flag);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    assert_true(woken.load());

    waiter.join();
}

void concurrencpp::tests::test_atomic_wait_for_timeout_1() {
    std::cout << "test_atomic_wait_for_timeout_1" << std::endl;
    // timeout has reached
    std::atomic_int flag {0};
    constexpr auto timeout_ms = 100;

    const auto before = std::chrono::high_resolution_clock::now();
    const auto result =
        concurrencpp::details::atomic_wait_for(flag, 0, std::chrono::milliseconds(timeout_ms), std::memory_order_acquire);
    const auto after = std::chrono::high_resolution_clock::now();
    const auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    assert_equal(result, concurrencpp::details::atomic_wait_status::timeout);
    assert_bigger_equal(time_diff, timeout_ms);
}

void concurrencpp::tests::test_atomic_wait_for_timeout_2() { 
    std::cout << "test_atomic_wait_for_timeout_2" << std::endl;
    // notify was called, value hasn't changed
    std::atomic_int flag {0};
    constexpr auto timeout_ms = 200;

    std::thread modifier([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms / 2));
        concurrencpp::details::atomic_notify_one(flag);
    });

    const auto before = std::chrono::high_resolution_clock::now();
    const auto result =
        concurrencpp::details::atomic_wait_for(flag, 0, std::chrono::milliseconds(timeout_ms), std::memory_order_acquire);
    const auto after = std::chrono::high_resolution_clock::now();
    const auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    assert_equal(result, concurrencpp::details::atomic_wait_status::timeout);
    assert_bigger_equal(time_diff, timeout_ms - 25);
    assert_smaller_equal(time_diff, timeout_ms + 35);

    modifier.join();
}

void concurrencpp::tests::test_atomic_wait_for_timeout_3() { 
    std::cout << "test_atomic_wait_for_timeout_3" << std::endl;
    // value had changed, notify wasn't called,
    std::atomic_int flag {0};
    constexpr auto timeout_ms = 200;

    std::thread modifier([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms / 2));
        flag = 1;
    });

    const auto before = std::chrono::high_resolution_clock::now();
    const auto result =
        concurrencpp::details::atomic_wait_for(flag, 0, std::chrono::milliseconds(timeout_ms), std::memory_order_acquire);
    const auto after = std::chrono::high_resolution_clock::now();
    const auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    // note: value did change, so it's ok to receive <<ok>> instead of timeout
    assert_equal(result, concurrencpp::details::atomic_wait_status::ok);
    assert_bigger_equal(time_diff, timeout_ms - 25);
    assert_smaller_equal(time_diff, timeout_ms + 35);

    modifier.join();
}

void concurrencpp::tests::test_atomic_wait_for_success() { 
    std::cout << "test_atomic_wait_for_success" << std::endl;
    std::atomic_int flag {0};
    constexpr auto timeout_ms = 400;
    constexpr auto modify_ms = timeout_ms / 4;

    std::thread modifier([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(modify_ms));
        flag = 1;
        concurrencpp::details::atomic_notify_one(flag);
    });

    const auto before = std::chrono::high_resolution_clock::now();
    const auto result =
        concurrencpp::details::atomic_wait_for(flag, 0, std::chrono::milliseconds(timeout_ms), std::memory_order_acquire);
    const auto after = std::chrono::high_resolution_clock::now();
    const auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();

    assert_equal(result, concurrencpp::details::atomic_wait_status::ok);
    assert_bigger_equal(time_diff, modify_ms - 25);
    assert_smaller_equal(time_diff, modify_ms + 35);

    modifier.join();
}

void concurrencpp::tests::test_atomic_wait_for() {
    test_atomic_wait_for_timeout_1();
    test_atomic_wait_for_timeout_2();
    test_atomic_wait_for_timeout_3();
    test_atomic_wait_for_success();
}

void concurrencpp::tests::test_atomic_notify_one() {
    std::thread waiters[5];
    std::atomic_size_t woken = 0;
    std::atomic_int flag = 0;

    for (auto& waiter : waiters) {
        waiter = std::thread([&] {
            concurrencpp::details::atomic_wait(flag, 0, std::memory_order_relaxed);
            woken.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    flag = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert_equal(woken.load(), 0);

    for (size_t i = 0; i < std::size(waiters); i++) {
        concurrencpp::details::atomic_notify_one(flag);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        assert_equal(woken.load(), i + 1);
    }

    for (auto& waiter : waiters) {
        waiter.join();
    }
}

void concurrencpp::tests::test_atomic_notify_all() {
    std::thread waiters[5];
    std::atomic_size_t woken = 0;
    std::atomic_int flag = 0;

    for (auto& waiter : waiters) {
        waiter = std::thread([&] {
            concurrencpp::details::atomic_wait(flag, 0, std::memory_order_relaxed);
            woken.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    flag = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert_equal(woken.load(), 0);

    concurrencpp::details::atomic_notify_all(flag);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert_equal(woken.load(), std::size(waiters));

    for (auto& waiter : waiters) {
        waiter.join();
    }
}

int main() {
    tester tester("atomic_wait test");

    tester.add_step("wait", test_atomic_wait);
    tester.add_step("wait_for", test_atomic_wait_for);
    tester.add_step("notify_one", test_atomic_notify_one);
    tester.add_step("notify_all", test_atomic_notify_all);

    tester.launch_test();
    return 0;
}
