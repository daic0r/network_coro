#ifndef CONNECTION_H
#define CONNECTION_H

#include <asio.hpp>
#include <asio/coroutine.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include "message.h"
#include <bits/chrono.h>
#include <deque>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

namespace ice {
  namespace net {

    template<typename T>
      class connection {
        static inline std::uint32_t s_nIDCounter{};

        std::uint32_t m_nID{};
        asio::ip::tcp::socket m_socket;
        std::deque<ice::net::message<T>> m_qMessagesIn;
        std::chrono::steady_clock::time_point m_tpDeadline{};
        bool m_bConnected{};

        public:
          connection(asio::ip::tcp::socket sock) 
            : m_nID{ s_nIDCounter++ },
            m_socket{ std::move(sock) },
            m_tpDeadline{ std::chrono::steady_clock::now() + 5s },
            m_bConnected{true}
          {
            asio::co_spawn(m_socket.get_executor(), [this]() -> asio::awaitable<void> {
                  for (;;) {
                    auto ret = co_await (watchdog() || readMessage());

                    static_assert(std::is_same_v<decltype(ret), std::variant<asio::awaitable<void>, ice::net::message<T>>>);

                    if (ret.index() == 0) {
                      m_bConnected = false;
                      std::cout << "Connection timed out.\n";
                      break;
                    }
                    auto& msg = std::get<ice::net::message<T>>(ret);
                    if (msg.header.messageID == ice::net::message_type::CLIENT_HEARTBEAT) {
                      m_tpDeadline = std::chrono::steady_clock::now() + 5s;
                      continue;
                    }
                    m_qMessagesIn.push_back(msg);
                  }
                }, asio::detached);
          }

          void send(ice::net::message<T> const& msg) {
            asio::co_spawn(m_socket.get_executor(), [this, &msg]() -> asio::awaitable<void> {
                [[maybe_unused]] auto const nBytes = co_await m_socket.async_write_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
                [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_write_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);
                if (msg.header.nSize > 0) {
                  [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_write_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
                  }
              }, asio::detached);
          }

          bool connected() const noexcept { return m_bConnected; }

        private:
          asio::awaitable<void> watchdog() {
            asio::steady_timer timer{ m_socket.get_executor() };

            auto now = std::chrono::steady_clock::now();
            while (m_tpDeadline > now) {

              timer.expires_at(m_tpDeadline);
              co_await timer.async_wait(asio::use_awaitable);

              now = std::chrono::steady_clock::now();
            }

            co_return;
          }

          asio::awaitable<ice::net::message<T>> readMessage() const {
            ice::net::message<T> msg{};
            [[maybe_unused]] auto const nBytes = co_await m_socket.async_read_some(asio::buffer(&msg.header.messageID, sizeof(msg.header.messageID)), asio::use_awaitable);
            [[maybe_unused]] auto const nBytes2 = co_await m_socket.async_read_some(asio::buffer(&msg.header.nSize, sizeof(msg.header.nSize)), asio::use_awaitable);

            if (msg.header.nSize > 0) {
              msg.payload.resize(msg.header.nSize);
              [[maybe_unused]] auto const nBytes3 = co_await m_socket.async_read_some(asio::buffer(msg.payload.data(), msg.payload.size()), asio::use_awaitable);
            }

            co_return msg;
          }

      };
  }
}

#endif // !CONNECTION_H
