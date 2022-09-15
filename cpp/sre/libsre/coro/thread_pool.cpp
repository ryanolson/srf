#include "sre/coro/thread_pool.hpp"

#include "sre/coro/thread_local_state.hpp"
#include "sre/trace/trace.hpp"

#include <iostream>

namespace sre::coro {

thread_local ThreadPool* ThreadPool::m_self{nullptr};

auto ThreadPool::from_current_thread() -> ThreadPool*
{
    return m_self;
}

ThreadPool::Operation::Operation(ThreadPool& tp) noexcept : m_thread_pool(tp) {}

auto ThreadPool::Operation::await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> void
{
    // create span to measure the time spent in the scheduler
    m_span = sre::trace::get_tracer()->StartSpan("schedule to thread_pool");
    // suspend thread local state
    ThreadLocalState::suspend_coro_thread_local_state();

    // capture the coroutine handle and schedule it to be resumed
    m_awaiting_coroutine = awaiting_coroutine;
    m_thread_pool.schedule_impl(m_awaiting_coroutine);

    // void return on await_suspend suspends the _this_ coroutine, which is now scheduled on the
    // thread pool and returns control to the caller.
}

auto ThreadPool::Operation::await_resume() noexcept -> void
{
    // restore thread local state
    ThreadLocalState::resume_coro_thread_local_state();
    // complete the scheduling
    m_span->End();
}

ThreadPool::ThreadPool(Options opts) : m_opts(std::move(opts))
{
    m_threads.reserve(m_opts.thread_count);

    for (uint32_t i = 0; i < m_opts.thread_count; ++i)
    {
        m_threads.emplace_back([this, i](std::stop_token st) { executor(std::move(st), i); });
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

auto ThreadPool::schedule() -> Operation
{
    if (!m_shutdown_requested.load(std::memory_order::relaxed))
    {
        m_size.fetch_add(1, std::memory_order::release);
        return Operation{*this};
    }

    throw std::runtime_error("coro::ThreadPool is shutting down, unable to schedule new tasks.");
}

auto ThreadPool::resume(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    m_size.fetch_add(1, std::memory_order::release);
    schedule_impl(handle);
}

auto ThreadPool::shutdown() noexcept -> void
{
    // Only allow shutdown to occur once.
    if (!m_shutdown_requested.exchange(true, std::memory_order::acq_rel))
    {
        for (auto& thread : m_threads)
        {
            thread.request_stop();
        }

        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
}

auto ThreadPool::executor(std::stop_token stop_token, std::size_t idx) -> void
{
    if (m_opts.on_thread_start_functor != nullptr)
    {
        m_opts.on_thread_start_functor(idx);
    }

    m_self = this;

    while (!stop_token.stop_requested())
    {
        // Wait until the queue has operations to execute or shutdown has been requested.
        while (true)
        {
            std::unique_lock<std::mutex> lk{m_wait_mutex};
            m_wait_cv.wait(lk, stop_token, [this] { return !m_queue.empty(); });
            if (m_queue.empty())
            {
                lk.unlock();  // would happen on scope destruction, but being explicit/faster(?)
                break;
            }

            auto handle = m_queue.front();
            m_queue.pop_front();

            lk.unlock();  // Not needed for processing the coroutine.

            handle.resume();
            m_size.fetch_sub(1, std::memory_order::release);
        }
    }

    m_self = nullptr;

    if (m_opts.on_thread_stop_functor != nullptr)
    {
        m_opts.on_thread_stop_functor(idx);
    }
}

auto ThreadPool::schedule_impl(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle == nullptr)
    {
        return;
    }

    {
        std::scoped_lock lk{m_wait_mutex};
        m_queue.emplace_back(handle);
    }

    m_wait_cv.notify_one();
}

}  // namespace sre::coro
