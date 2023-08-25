#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/coroutine.hpp>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include "message.h"
#include "ts_queue.h"
#include <bits/chrono.h>
#include <deque>
#include <chrono>
#include <iostream>
#include <asio/experimental/coro.hpp>
#include <semaphore>
#include <asio/experimental/awaitable_operators.hpp>

using namespace std::chrono_literals;
using asio::experimental::coro;
using namespace asio::experimental::awaitable_operators;

namespace ice {
  namespace net {

    template<typename T>
      class connection {
        static inline std::uint32_t s_nIDCounter{};

        std::uint32_t m_nID{};
        asio::io_context& m_ioContext;
        asio::ip::tcp::socket m_socket;
        ts_queue<ice::net::message<T>> m_qMessagesIn;
        std::chrono::steady_clock::time_point m_tpDeadline{};
        bool m_bConnected{};
        std::binary_semaphore m_messageAvailableSemaphore{0};
        std::function<void(connection&)> m_fnOnDisconnect{};

        public:
        connection(asio::io_context& ioContext)
          : m_nID{ s_nIDCounter++ },
          m_ioContext{ ioContext },
          m_socket{ m_ioContext }
        {}

        connection(asio::io_context& ioContext, asio::ip::tcp::socket sock, std::function<void(connection&)> fn)
          : m_nID{ s_nIDCounter++ },
          m_ioContext{ ioContext },
          m_socket{ std::move(sock) },
          m_tpDeadline{ std::chrono::steady_clock::now() + 5s },
          m_bConnected{m_socket.is_open()},
          m_fnOnDisconnect{std::move(fn)}
        {
          if (m_bConnected) {
            std::cout << "Spawning listener\n";
            asio::co_spawn(m_ioContext, listen(), asio::detached);
            asio::co_spawn(m_ioContext, heartbeat(), asio::detached);
          }
        }

        void setOnDisconnect(std::function<void(connection&)> fn) { m_fnOnDisconnect = std::move(fn); }

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
          asio::co_spawn(m_ioContext, listen(), asio::detached);
          asio::co_spawn(m_ioContext, heartbeat(), asio::detached);
          std::cout << "Spawning listener\n";
          co_return connected();
        }
        
        asio::awaitable<void> listen() {
          for (;;) {
            auto ret = co_await (readMessage() || watchdog());

            //static_assert(std::is_same_v<decltype(ret), std::variant<ice::net::message<T>, std::monostate>>);

            if (ret.index() == 1) {
              m_bConnected = false;
              m_socket.close();
              std::cout << "Connection timed out.\n";
              if (m_fnOnDisconnect) {
                std::cout << "Running disconnect handler\n";
                m_fnOnDisconnect(*this);
              }
              break;
            } else {
              auto& msg = std::get<ice::net::message<T>>(ret);
              if (msg.header.rawID() == ice::net::system_message::HEARTBEAT) {
                std::cout << "Received heartbeat\n";
                m_tpDeadline = std::chrono::steady_clock::now() + 10s;
                continue;
              }
              std::cout << "Received message\n";
              m_qMessagesIn.push_back(msg);
              m_messageAvailableSemaphore.release();
            }
          }
          co_return;
        }

        asio::experimental::coro<message<T>> message(asio::any_io_executor) {
          for (;;) {
            m_messageAvailableSemaphore.acquire();

            auto msg = m_qMessagesIn.front();
            m_qMessagesIn.pop_front();

            co_yield std::move(msg);
          }
        }

        template<typename U>
        asio::awaitable<void> send(ice::net::message<U> const& msg) {
          [[maybe_unused]] auto const nBytes = co_await m_socket.async_write_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
          [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_write_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);
          if (msg.header.nSize > 0) {
            [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_write_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
          }
          co_return;
        }

        bool connected() const noexcept { return m_bConnected; }
        bool hasMessage() const noexcept { return !m_qMessagesIn.empty(); }
        std::uint32_t id() const noexcept { return m_nID; }
        const asio::ip::tcp::socket& socket() const noexcept { return m_socket; }

        bool operator==(connection const& rhs) const noexcept { return m_nID == rhs.m_nID; }

        private:
        asio::awaitable<void> watchdog() {
          asio::steady_timer timer{ m_ioContext };

          auto now = std::chrono::steady_clock::now();

          while (m_tpDeadline > now) {

            timer.expires_at(m_tpDeadline);

            co_await timer.async_wait(asio::use_awaitable);

            now = std::chrono::steady_clock::now();
          }

          co_return;
        }

        asio::awaitable<void> heartbeat() {
          asio::steady_timer timer{ m_ioContext };
          timer.expires_after(8s);

          co_await send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });

          for (;;) {
            co_await timer.async_wait(asio::use_awaitable);
            co_await send(ice::net::message<system_message>{ ice::net::system_message::HEARTBEAT });
            timer.expires_after(8s);
          }

          co_return;
        }

        asio::awaitable<ice::net::message<T>> readMessage() {
          ice::net::message<T> msg{};
          [[maybe_unused]] auto const nBytes = co_await m_socket.async_read_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
          [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_read_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);

          if (msg.header.nSize > 0) {
            msg.payload.resize(msg.header.nSize);
            [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_read_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
            msg.payload.nPos = msg.payload.size();
          }

          co_return msg;
        }

      };
  }
}

#endif // !CONNECTION_H
