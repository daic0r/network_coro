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

class server {

  public:
  awaitable<void> handleMessages(asio::ip::tcp::socket sock, asio::ip::tcp::endpoint remoteEp) {
    using namespace ice::net;

    std::size_t nHash{};
    while (true) {
      asio::error_code ec{};

      message_header<message_type> msg; 
      co_await sock.async_read_some(asio::buffer(&msg.messageID, sizeof(msg.messageID)), asio::redirect_error(asio::use_awaitable, ec));
      co_await sock.async_read_some(asio::buffer(&msg.nSize, sizeof(msg.nSize)), asio::redirect_error(asio::use_awaitable, ec));

      switch (msg.messageID) {
        case message_type::CLIENT_HELLO:
          {
            assert((payload_definition<message_type, message_type::CLIENT_HELLO>::size_bytes == msg.nSize));

            message_payload<message_type> payload;
            payload.vBytes.resize(msg.nSize);

            auto nBytes = co_await sock.async_read_some(asio::buffer(payload.data(), payload.size()), asio::redirect_error(asio::use_awaitable, ec));
            payload.nPos = payload.size();

            auto hello = payload.read<message_type::CLIENT_HELLO>();
            std::cout << "[SERVER] CLIENT_HELLO received. Payload is '" << std::get<0>(hello) << "'." << std::endl;
          }
          {
            std::cout << "[SERVER] Transmitting server handshake...\n";
            message_header<message_type> msg{ message_type::SERVER_HANDSHAKE };
            auto vHandshake = generateHandshake();
            msg.nSize = sizeof(std::size_t) + vHandshake.size();
            std::cout << "[SERVER] Sending payload of " << msg.nSize << " bytes.\n";
            auto strHandshake = std::string_view{ vHandshake.data(), vHandshake.size() };
            std::cout << "[SERVER] '" << strHandshake << "'\n";
            nHash = std::hash<std::string_view>{}(strHandshake);

            co_await sock.async_write_some(asio::buffer(&msg.messageID, sizeof(msg.messageID)), asio::use_awaitable);
            co_await sock.async_write_some(asio::buffer(&msg.nSize, sizeof(msg.nSize)), asio::use_awaitable);

            message_payload<message_type> payload;
            payload << vHandshake;

            co_await sock.async_write_some(asio::buffer(payload.data(), payload.size()), asio::use_awaitable);
          }
          break;
        case message_type::CLIENT_HANDSHAKE:
          {
            assert((payload_definition<message_type, message_type::CLIENT_HANDSHAKE>::size_bytes == msg.nSize));

            message_payload<message_type> payload;
            payload.vBytes.resize(msg.nSize);

            auto nBytes = co_await sock.async_read_some(asio::buffer(payload.data(), payload.size()), asio::redirect_error(asio::use_awaitable, ec));
            payload.nPos = payload.size();

            auto theHashTuple = payload.read<message_type::CLIENT_HANDSHAKE>();
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
          std::cout << "[SERVER] Received message type not handled yet.\n";
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

  awaitable<void> run(asio::io_context& ctx, asio::ip::tcp::endpoint ep) {
    std::vector<ice::net::connection<ice::net::message_type>> vConns;

    asio::ip::tcp::acceptor acc{ ctx, ep.protocol() };
    acc.bind(ep);
    acc.listen(128);
    std::cout << "[SERVER] Listening on " << acc.local_endpoint() << "\n";
    while(true) {
      auto sock = co_await acc.async_accept(asio::use_awaitable);
      auto remoteEp = sock.remote_endpoint();
      std::cout << "[SERVER] Connection from " << remoteEp << ".\n";
      auto ex = sock.get_executor();
      vConns.emplace_back(std::move(sock));
      //asio::co_spawn(ex, handleMessages(std::move(sock), remoteEp), asio::detached);
    }
    co_return;
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


int main() {
    asio::io_context ctx;

    asio::io_context::work idleWork{ ctx };

    std::thread thr{ [&ctx]() { ctx.run(); } };

	asio::error_code ec{};

	asio::ip::tcp::endpoint ep{ asio::ip::address_v4::any(), 60000 };

  server serv{};
    
    asio::co_spawn(ctx, serv.run(ctx, ep), asio::detached);

    int i;
    std::cin >> i;

    ctx.stop();
    if (thr.joinable())
        thr.join();



}
