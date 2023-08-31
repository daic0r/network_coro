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
#include <functional>

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
        ts_queue<ice::net::message<T>> m_qMessagesOut;
        std::chrono::steady_clock::time_point m_tpDeadline{};
        bool m_bConnected{};
        //std::binary_semaphore m_messageAvailableSemaphore{0};
        std::function<void(std::shared_ptr<connection>&)> m_fnOnDisconnect{};
        std::atomic<bool> m_bDone{};
        ts_queue<std::move_only_function<void(ice::net::message<T>)>> m_qfnMessageReturners{};
      

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
          //m_messageAvailableSemaphore.release();
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
                if (not msgOpt.has_value()) {
                  //std::clog << "FATAL: Empty message received\n";
                  continue;
                }
                auto& msg = msgOpt.value();
                if (msg.header.rawID() == ice::net::system_message::HEARTBEAT) {
                  std::cout << "Received heartbeat\n";
                  m_tpDeadline = std::chrono::steady_clock::now() + 10s;
                  continue;
                } else {
                  std::clog << "Received message with ID " << msg.header.rawID() << "\n";
                }
                m_qMessagesIn.push_back(std::move(msg));
                if (not m_qfnMessageReturners.empty()) {
                  std::clog << "Calling message handler\n";
                  auto func = std::move(m_qfnMessageReturners.front());
                  m_qfnMessageReturners.pop_front();
                  auto msg = std::move(m_qMessagesIn.front());
                  m_qMessagesIn.pop_front();
                  std::move(func)(std::move(msg));
                }
                std::clog << m_qMessagesIn.size() << " messages queued now.\n";
              }
              catch (...) {
                std::clog << "I THREW ON MESSAGE RETRIEVAL\n";
              }
            }
          }
          co_return;
        }

        template<typename CompletionToken>
        auto message(CompletionToken&& token) {
          return asio::async_initiate<CompletionToken, void(ice::net::message<T>)>(
              [self = this->shared_from_this()](auto&& handler) mutable {
                if (self->m_bDone.load(std::memory_order_acquire)) {
                  std::clog << "FATAL: Connection is done\n";
                  return;
                }
                if (not self->m_qMessagesIn.empty()) {
                  auto msg = std::move(self->m_qMessagesIn.front());
                  self->m_qMessagesIn.pop_front();
                  // Calling the handler means returning the value to the
                  // coroutine that co_awaits message().
                  std::move(handler)(std::move(msg));
                } else {
                  std::clog << "Adding message handler\n";
                  //self->m_fnMessageGetter = std::move(handler);
                  self->m_qfnMessageReturners.emplace_back(std::move(handler));
                }
              },
              token);
        }

        /*
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
        */

        template<typename U>
        void send(ice::net::message<U> msg) {
          std::clog << "Queuing message (msg ID " << msg.header.rawID() << ")\n";
          m_qMessagesOut.push_back(std::move(msg));
          if (m_qMessagesOut.size() == 1)
            asio::co_spawn(m_ioContext, sendMessages(), asio::detached);
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
            send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });
          }
          catch (...) {
            co_return;
          }

          while (not m_bDone.load(std::memory_order_acquire)) {
            try {
              co_await timer.async_wait(asio::use_awaitable);
              send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });
            }
            catch (...) {
              co_return;
            }

            timer.expires_after(8s);
          }

          co_return;
        }

        asio::awaitable<void> sendMessages() {
          auto self = this->shared_from_this();
          try {
            while (not self->m_qMessagesOut.empty()) {
              auto msg = std::move(self->m_qMessagesOut.front());
              [[maybe_unused]] auto nBytes = co_await asio::async_write(self->m_socket, asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
              nBytes = co_await asio::async_write(self->m_socket, asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);
              if (msg.header.nSize > 0) {
                nBytes = co_await asio::async_write(self->m_socket, asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
              }
              self->m_qMessagesOut.pop_front();
              std::clog << self->m_qMessagesOut.size() << " messages left in queue\n";
              if (not self->m_qMessagesOut.empty()) {
                std::clog << "Sending next message (msg ID " << self->m_qMessagesOut.front().header.rawID() << ")\n";
                //asio::co_spawn(self->m_ioContext, sendMessage(), asio::detached);
              }
            }
          }
          catch (...) {
            std::clog << "sendMessage() threw exception\n";
            co_return;
          }
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
          catch (const std::exception& e) {
            std::clog << "readMessage() threw exception: " << e.what() << "\n";
            co_return std::nullopt;
          }

          co_return msg;
        }

      };
  }
}

#endif // !CONNECTION_H
