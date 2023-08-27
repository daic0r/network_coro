#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/coroutine.hpp>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <iostream>
#include <random>
#include <syncstream>
#include <system_error>
#include <thread>
#include <coroutine>
#include <chrono>
#include <optional>
#include <future>
#include <list>
#include <variant>
#include <cassert>
#include "message.h"
#include "connection.h"

using asio::awaitable;

namespace ice {

  namespace net {

    template<Enumeration T>
    class server {
      std::list<std::shared_ptr<connection<T>>> m_lConnections{};
      asio::io_context& m_ctx;
      asio::ip::tcp::endpoint m_endpoint;
      asio::ip::tcp::acceptor m_acceptor;

      public:
        server(asio::io_context& ctx, asio::ip::tcp::endpoint ep) : m_ctx{ ctx }, m_endpoint{ ep }, m_acceptor{ ctx, ep.protocol() }
        {
          m_acceptor.bind(ep);
          m_acceptor.listen(128);
        }
        awaitable<void> handleConnectionMessages(std::shared_ptr<connection<T>> conn) { //asio::ip::tcp::socket sock, asio::ip::tcp::endpoint remoteEp) {
          using namespace ice::net;

          std::size_t nHash{};

          auto msgCoro = conn->message(m_ctx.get_executor());

          while (true) {
            std::cout << "[SERVER] Waiting for message...\n";
            std::optional<ice::net::message<T>> msgOpt;
            try {
              msgOpt = co_await msgCoro.async_resume(asio::use_awaitable);
              if (not msgOpt) {
                std::clog << "[SERVER] Detected disconnect. Exiting message handling loop.\n";
                break;
              }
            }
            catch (const std::exception& ex) {
              std::clog << "Exception during async_resume(): " << ex.what() << "\n";
              break;
            }
            auto msg = msgOpt.value();

            /*
            asio::error_code ec{};

            message_header<system_message> msg; 
            co_await sock.async_read_some(asio::buffer(&msg.messageID, sizeof(msg.messageID)), asio::redirect_error(asio::use_awaitable, ec));
            co_await sock.async_read_some(asio::buffer(&msg.nSize, sizeof(msg.nSize)), asio::redirect_error(asio::use_awaitable, ec));
            */

            switch (msg.header.rawID()) {
              case system_message::CLIENT_HELLO:
                {
                  if (not (payload_definition<system_message, system_message::CLIENT_HELLO>::size_bytes == msg.header.nSize)) {
                    std::cout << "[SERVER] CLIENT_HELLO message size is invalid. Expected " << payload_definition<system_message, system_message::CLIENT_HELLO>::size_bytes << " bytes, got " << msg.header.nSize << " bytes.\n";
                  }

                  /*
                  message_payload payload;
                  payload.vBytes.resize(msg.nSize);

                  auto nBytes = co_await sock.async_read_some(asio::buffer(payload.data(), payload.size()), asio::redirect_error(asio::use_awaitable, ec));
                  payload.nPos = payload.size();
                  */

                  auto hello = msg.payload.template read<system_message, system_message::CLIENT_HELLO>();
                  std::cout << "[SERVER] CLIENT_HELLO received. Payload is '" << std::get<0>(hello) << "'." << std::endl;
                }
                {
                  std::cout << "[SERVER] Transmitting server handshake...\n";
                  message<system_message> msg{ system_message::SERVER_HANDSHAKE };
                  auto vHandshake = generateHandshake();
                  msg.header.nSize = sizeof(std::size_t) + vHandshake.size();
                  std::cout << "[SERVER] Sending payload of " << msg.header.nSize << " bytes.\n";
                  auto strHandshake = std::string_view{ vHandshake.data(), vHandshake.size() };
                  std::cout << "[SERVER] '" << strHandshake << "'\n";
                  nHash = std::hash<std::string_view>{}(strHandshake);

                  /*
                  co_await sock.async_write_some(asio::buffer(&msg.messageID, sizeof(msg.messageID)), asio::use_awaitable);
                  co_await sock.async_write_some(asio::buffer(&msg.nSize, sizeof(msg.nSize)), asio::use_awaitable);
                  */

                  msg.payload.clear();
                  msg.payload << vHandshake;

                  co_await conn->send(msg);

                  //co_await sock.async_write_some(asio::buffer(payload.data(), payload.size()), asio::use_awaitable);
                }
                break;
              case system_message::CLIENT_HANDSHAKE:
                {
                  assert((payload_definition<system_message, system_message::CLIENT_HANDSHAKE>::size_bytes == msg.header.nSize));

                  /*
                  message_payload payload;
                  payload.vBytes.resize(msg.nSize);

                  auto nBytes = co_await sock.async_read_some(asio::buffer(payload.data(), payload.size()), asio::redirect_error(asio::use_awaitable, ec));
                  payload.nPos = payload.size();
                  */

                  auto theHashTuple = msg.payload.template read<system_message, system_message::CLIENT_HANDSHAKE>();
                  const auto theHash = std::get<0>(theHashTuple);

                  std::cout << "[SERVER] Client answered with " << theHash << "\n";

                  std::cout << "[SERVER] Client handshake response is ";
                  if (nHash == theHash)
                    std::cout << "VALID";
                  else
                    std::cout << "INVALID (expected " << nHash << ")\n";
                  std::cout << "\n";
                }
                break;
              default:
                std::cout << "[SERVER] Received message type not handled yet: " << msg.header.rawID() << "\n";
            }

            /*
               do {
            //std::array<char, 4096> arBuf;
            //nBytes = co_await sock.async_read_some(asio::buffer(arBuf), asio::redirect_error(asio::use_awaitable, ec));
            std::cout << "[SERVER] Receiving data packet of size " << nBytes << " bytes.\n";
            vBuf.reserve(vBuf.capacity() + nBytes);
            vBuf.insert(vBuf.end(), arBuf.begin(), std::next(arBuf.begin(), nBytes));
            } while (not ec and nBytes > 0);
            std::cout << "[SERVER] Received message from " << remoteEp << ":\n\n";
            std::string_view sv{ vBuf.data(), vBuf.size() };
            std::cout << sv << "\n";
            */
          }
          co_return;
        }

        awaitable<void> run() {
          std::vector<ice::net::connection<ice::net::system_message>> vConns;

          //m_acceptor.bind(ep);
          //m_acceptor.listen(128);
          std::cout << "[SERVER] Listening on " << m_acceptor.local_endpoint() << "\n";
          while(true) {
            asio::ip::tcp::socket sock{ m_ctx };
            try {
              sock = co_await m_acceptor.async_accept(asio::use_awaitable);
            }
            catch (...) {
              std::clog << "[SERVER] Acceptor closed.\n";
              break;
            }
            auto remoteEp = sock.remote_endpoint();
            std::cout << "[SERVER] Connection from " << remoteEp << ".\n";
            auto ex = sock.get_executor();
            
            auto& connPtr = m_lConnections.emplace_back(std::make_shared<connection<T>>(m_ctx, std::move(sock), [this](std::shared_ptr<connection<T>>& connPtr) mutable {
              std::cout << "[SERVER] Connection closed.\n";
              m_lConnections.remove(connPtr);
              std::clog << "[SERVER] I still have " << m_lConnections.size() << " active connections\n";
            }));
            connPtr->start();
            //asio::co_spawn(ctx, conn.listen(), asio::detached);
            asio::co_spawn(ex, handleConnectionMessages(connPtr), asio::detached);
          }
          co_return;
        }

        void shutDown() {
          m_acceptor.cancel();
          m_acceptor.close();
        }

      private:

        static std::vector<char> generateHandshake() {
          std::uniform_int_distribution<> dist(33,126); // Exclamation point through tilde
          std::random_device rd;
          std::mt19937 gen(rd());

          const auto nLen = dist(gen);
          std::vector<char> vRet;
          vRet.reserve(nLen);
          for (std::size_t i = 0; i < nLen; ++i) {
            vRet.push_back(static_cast<char>(dist(gen)));
          }

          return vRet;
        }

    };

} // namespace net

} // namespace ice
  //
enum class my_message {
  ROLL_DICE
};


int main() {
    asio::io_context ctx;

//    asio::io_context::work idleWork{ ctx };


	asio::error_code ec{};

	asio::ip::tcp::endpoint ep{ asio::ip::address_v4::any(), 60000 };

  ice::net::server<my_message> serv{ctx, ep};
    
    asio::co_spawn(ctx, serv.run(), asio::detached);

    std::vector<std::thread> vThreads;
    for (int i = 0; i < 3; ++i)
      vThreads.emplace_back([&ctx] { ctx.run(); });

    int i;
    std::cin >> i;

    std::clog << "[SERVER] Stopping context.\n";

    serv.shutDown();

    ctx.stop();
    for (auto& thread : vThreads)
      thread.join();

}
