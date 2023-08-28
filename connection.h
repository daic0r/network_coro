#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/coroutine.hpp>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/system_error.hpp>
#include <asio/use_awaitable.hpp>
#include "message.h"
#include "ts_queue.h"
#include <atomic>
#include <bits/chrono.h>
#include <deque>
#include <chrono>
#include <exception>
#include <iostream>
#include <asio/experimental/coro.hpp>
#include <semaphore>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/as_tuple.hpp>
#include <memory>

using namespace std::chrono_literals;
using asio::experimental::coro;
using namespace asio::experimental::awaitable_operators;

namespace ice {
  namespace net {

    constexpr auto use_nothrow_awaitable = asio::experimental::as_tuple(asio::use_awaitable);

    template<typename T>
      class connection : public std::enable_shared_from_this<connection<T>> {
        static inline std::uint32_t s_nIDCounter{};

        std::uint32_t m_nID{};
        asio::io_context& m_ioContext;
        asio::ip::tcp::socket m_socket;
        ts_queue<ice::net::message<T>> m_qMessagesIn;
        std::chrono::steady_clock::time_point m_tpDeadline{};
        bool m_bConnected{};
        std::binary_semaphore m_messageAvailableSemaphore{0};
        std::function<void(std::shared_ptr<connection>&)> m_fnOnDisconnect{};
        std::atomic<bool> m_bDone{};

        public:
        connection(asio::io_context& ioContext)
          : m_nID{ s_nIDCounter++ },
          m_ioContext{ ioContext },
          m_socket{ m_ioContext }
        {}

        connection(asio::io_context& ioContext, asio::ip::tcp::socket sock, std::function<void(std::shared_ptr<connection>&)> fn)
          : m_nID{ s_nIDCounter++ },
          m_ioContext{ ioContext },
          m_socket{ std::move(sock) },
          m_tpDeadline{ std::chrono::steady_clock::now() + 5s },
          m_bConnected{m_socket.is_open()},
          m_fnOnDisconnect{std::move(fn)}
        {
        }

        void setOnDisconnect(std::function<void(std::shared_ptr<connection>&)> fn) { m_fnOnDisconnect = std::move(fn); }

        void start() {
          if (m_bConnected) {
            asio::co_spawn(m_ioContext, heartbeat(), [self= this->shared_from_this()](std::exception_ptr) {
                std::clog << "CONNECTION: Exception thrown from heartbeat()\n";
                if (self->connected())
                  self->disconnect();
              });
            asio::co_spawn(m_ioContext, listen(), [self= this->shared_from_this()](std::exception_ptr ptr) {
                std::clog << "CONNECTION: Exception thrown from listen()\n";
                if (self->connected())
                  self->disconnect();
              });
            std::clog << "Spawning listener\n";
          }
        }

        asio::awaitable<bool> connect(asio::ip::tcp::endpoint ep) {
          try {
            co_await m_socket.async_connect(ep, asio::use_awaitable);
          }
          catch (std::exception& e) {
            std::cerr << "Connection exception: " << e.what() << "\n";
            co_return false;
          }
          m_bConnected = true;
          m_tpDeadline = std::chrono::steady_clock::now() + 10s;
          start();
          co_return connected();
        }

        void disconnect() {
          auto self = this->shared_from_this();
          try {
            m_socket.cancel();
            m_socket.close();
          }
          catch (const asio::system_error&) {}
          m_bDone.store(true, std::memory_order_release);
          m_messageAvailableSemaphore.release();
          m_bConnected = false;
          if (m_fnOnDisconnect) {
            m_fnOnDisconnect(self);
          }
        }
        
        asio::awaitable<void> listen() {
          auto self = this->shared_from_this();
          for (;;) {
            std::variant<std::optional<ice::net::message<T>>, std::monostate> ret;

            try {
              ret = co_await (readMessage() || watchdog());
            }
            catch (...) {
              std::clog << "readMessage() or watchdog() threw exception\n";
              break;
            }

            //static_assert(std::is_same_v<decltype(ret), std::variant<ice::net::message<T>, std::monostate>>);

            if (ret.index() == 1) {
              try {
                disconnect();
              }
              catch (...) {
                std::clog << "Disconnect on timeout failed.\n";
              }
              std::cout << "Connection timed out.\n";
              break;
            } else {
              //std::cout << "Received message, queue holding " << m_qMessagesIn.size() << " messages\n";
              try {
                auto& msgOpt = std::get<std::optional<ice::net::message<T>>>(ret);
                if (not msgOpt.has_value())
                  co_return;
                auto& msg = msgOpt.value();
                if (msg.header.rawID() == ice::net::system_message::HEARTBEAT) {
                  std::cout << "Received heartbeat\n";
                  m_tpDeadline = std::chrono::steady_clock::now() + 10s;
                  continue;
                } else {
                  std::clog << "Received message with ID " << msg.header.rawID() << "\n";
                }
                m_qMessagesIn.push_back(msg);
                if (m_qMessagesIn.size() == 1)
                  m_messageAvailableSemaphore.release();
                std::clog << m_qMessagesIn.size() << " messages queued now.\n";
              }
              catch (...) {
                std::clog << "I THREW ON MESSAGE RETRIEVAL\n";
              }
            }
          }
          co_return;
        }

        asio::experimental::coro<message<T>, void> message(asio::any_io_executor) {
          auto copy = this->shared_from_this();
          for (;;) {
            if (m_qMessagesIn.empty())
              m_messageAvailableSemaphore.acquire();

            if (m_bDone.load(std::memory_order_acquire))
              co_return;

            auto msg = m_qMessagesIn.front();
            m_qMessagesIn.pop_front();

            co_yield std::move(msg);
          }
          co_return;
        }

        template<typename U>
        asio::awaitable<void> send(ice::net::message<U> msg) {
          try {
            [[maybe_unused]] auto const nBytes = co_await m_socket.async_write_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
            [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_write_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);
            if (msg.header.nSize > 0) {
              [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_write_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
            }
          }
          catch (const std::exception& ex) {
            std::clog << "Exception during send(): " << ex.what() << "\n";
          }
          co_return;
        }

        bool connected() const noexcept { return m_bConnected; }
        bool hasMessage() const noexcept { return !m_qMessagesIn.empty(); }
        std::uint32_t id() const noexcept { return m_nID; }
        asio::ip::tcp::socket& socket() noexcept { return m_socket; }
        const asio::ip::tcp::socket& socket() const noexcept { return m_socket; }
        asio::io_context& context() noexcept { return m_ioContext; }
        const asio::io_context& context() const noexcept { return m_ioContext; }

        bool operator==(connection const& rhs) const noexcept { return m_nID == rhs.m_nID; }

        private:
        asio::awaitable<void> watchdog() {
          auto self = this->shared_from_this();
          asio::steady_timer timer{ m_ioContext };

          auto now = std::chrono::steady_clock::now();

          while (m_tpDeadline > now and not m_bDone.load(std::memory_order_acquire)) {

            timer.expires_at(m_tpDeadline);

            try {
              co_await timer.async_wait(asio::use_awaitable);
            }
            catch (...) {
              co_return;
            }

            now = std::chrono::steady_clock::now();
          }

          co_return;
        }

        asio::awaitable<void> heartbeat() {
          try {
            auto self = this->shared_from_this();
          }
          catch (const std::bad_weak_ptr&) {
            std::clog << "NO SHARED_PTR EXISTS YET\n";
            co_return;
          }

          asio::steady_timer timer{ m_ioContext };
          timer.expires_after(8s);

          try {
            co_await send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });
          }
          catch (...) {
            co_return;
          }

          while (not m_bDone.load(std::memory_order_acquire)) {
            try {
              co_await timer.async_wait(asio::use_awaitable);
              co_await send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });
            }
            catch (...) {
              co_return;
            }

            timer.expires_after(8s);
          }

          co_return;
        }

        asio::awaitable<std::optional<ice::net::message<T>>> readMessage() {
          auto self = this->shared_from_this();
          ice::net::message<T> msg{};
          try {
            [[maybe_unused]] auto const nBytes = co_await m_socket.async_read_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
            assert(nBytes == sizeof(msg.header.messageID));
            [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_read_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);
            assert(nBytes2 == sizeof(msg.header.nSize));

            if (msg.header.nSize > 0) {
              msg.payload.resize(msg.header.nSize);
              [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_read_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
              msg.payload.nPos = msg.payload.size();
            }
          }
          catch (...) {
            co_return std::nullopt;
          }

          co_return msg;
        }

      };
  }
}

#endif // !CONNECTION_H
