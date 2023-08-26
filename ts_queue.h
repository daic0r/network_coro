#ifndef TS_QUEUE_H
#define TS_QUEUE_H

#include <deque>
#include <mutex>

namespace ice {
  namespace net {
    
    template<typename T>
      class ts_queue {
        std::deque<T> m_qQueue{};
        mutable std::mutex m_mutex{};

      public:
        ts_queue() = default;
        ts_queue(const ts_queue<T>& rhs)
        {
          std::scoped_lock lock{ rhs.m_mutex };
          m_qQueue = rhs.m_qQueue;
        }
        ts_queue& operator=(const ts_queue<T>& rhs) {
          std::scoped_lock lock{ m_mutex, rhs.m_mutex };
          m_qQueue = rhs.m_qQueue;
          return *this;
        }
        ts_queue(ts_queue<T>&& rhs) noexcept {
          std::scoped_lock lock{ rhs.m_mutex };
          m_qQueue = std::move(rhs.m_qQueue);
        }
        ts_queue& operator=(ts_queue<T>&& rhs) noexcept {
          rhs.swap(*this);
          return *this;
        }
        ~ts_queue() = default;

        void swap(ts_queue<T>& rhs) noexcept {
          std::scoped_lock lock{ m_mutex, rhs.m_mutex };
          std::swap(m_qQueue, rhs.m_qQueue);
        }
        friend void swap(ts_queue<T>& lhs, ts_queue<T>& rhs) noexcept {
          lhs.swap(rhs);
        }
        
        void push_back(const T& item) {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.push_back(item);
        }

        void push_front(const T& item) {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.push_front(item);
        }

        void pop_front() {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.pop_front();
        }

        void pop_back() {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.pop_back();
        }

        T& front() {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.front();
        }
        T const& back() {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.back();
        }
        T const& front() const {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.front();
        }

        T const& back() const {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.back();
        }

        bool empty() const noexcept {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.empty();
        }

        std::size_t size() const noexcept {
          std::scoped_lock lock{ m_mutex };
          return m_qQueue.size();
        }

        void clear() noexcept {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.clear();
        }

        void remove(T const& item) {
          std::scoped_lock lock{ m_mutex };
          m_qQueue.erase(std::remove(m_qQueue.begin(), m_qQueue.end(), item), m_qQueue.end());
        }

      };

  }
}

#endif
