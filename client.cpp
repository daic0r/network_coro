#include <asio.hpp>
#include <asio/error_code.hpp>
#include <asio/experimental/coro.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/system_error.hpp>
#include <asio/use_awaitable.hpp>
#include <exception>
#include <iostream>
#include <system_error>
#include <thread>
#include <coroutine>
#include <chrono>
#include <optional>
#include <future>
#include "message.h"
#include "connection.h"

using asio::awaitable;
using asio::use_awaitable;

/*
template<typename Result>
struct awaitable {
    std::thread& thread_;
    Result& result_;
    std::function<Result()> func_;

    template<typename F>
    awaitable(F&& func, std::thread& t, Result& r) : func_{ std::forward<F>(func) }, thread_{ t }, result_{ r } {}
    
	bool await_ready() const noexcept { return false; }
	bool await_suspend(std::coroutine_handle<> callingCoro) {
        auto fut = std::async(std::launch::async, 
            [callingCoro](std::function<Result()> func, std::reference_wrapper<Result> res) {
                res.get() = func();
                callingCoro.resume();
        }, std::move(func_), std::ref(result_));
        return true;
	}
	void await_resume() noexcept {
	}
};

awaitable<int> calculate_it(std::thread& t, int& result) {
	return awaitable<int>{ []() { 
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(10s); 
		return 42; 
	}, t, result };
}
*/

namespace ice {
  /*
    class socket {
        asio::io_context& m_context;
        asio::ip::tcp::socket m_socket;
        std::coroutine_handle<> m_coro;

        public:
        struct base_awaitable {
            socket& m_sock;

            bool await_ready() const noexcept { return false; }
        };
        template<typename T = void, typename = void> struct awaitable;
        template<typename T>
            struct awaitable<T, std::enable_if_t<not std::is_same_v<T, void>>> : base_awaitable {
                std::function<void(std::optional<T>&)> m_func;
                std::optional<T> m_value;

                template<typename F>
                    awaitable(socket& sock, F&& func) : base_awaitable{ sock }, m_func{ std::forward<F>(func) } {}

                bool await_suspend(std::coroutine_handle<> coro) noexcept {
                    m_sock.setCoroHandle(coro);
                    m_func(m_value); 
                    return true;
                }
                T await_resume() noexcept {
                    //std::cout << "Resuming with " << (*m_value).size() << " bytes\n";
                    return std::move(*m_value);
                }
            };
        template<typename T>
            struct awaitable<T, std::enable_if_t<std::is_same_v<T, void>>> : base_awaitable {
                std::function<void()> m_func;

                template<typename F>
                    awaitable(socket& sock, F&& func) : base_awaitable{ sock }, m_func{ std::forward<F>(func) } {}

                bool await_suspend(std::coroutine_handle<> coro) noexcept {
                    m_sock.setCoroHandle(coro);
                    m_func(); 
                    return true;
                }
                void await_resume() noexcept {
                    std::cout << "Resuming\n";
                }
            };

        socket(asio::io_context& ctx)
            : m_context{ ctx },
            m_socket{ m_context }
        {}

        ~socket() {
            m_socket.close();
        }

        awaitable<bool> connect(asio::ip::tcp::endpoint ep) {
            return awaitable<bool>{ *this, [this,&ep](std::optional<bool>& bRet) {
                m_socket.async_connect(ep, [&bRet,this](std::error_code ec) {
                        bRet = not ec;
                        m_coro.resume();
                    });
            } };
        }

        awaitable<std::vector<char>> read() {
            return awaitable<std::vector<char>>{ *this, [this](std::optional<std::vector<char>>& val) mutable {
                val.emplace();
                _read(*val); 
            } };
        }

        awaitable<> write(std::string_view str) {
            return awaitable<>{ *this, [this, str]() mutable {
                _write(str);
            } };
        }

        void setCoroHandle(std::coroutine_handle<> coro) {
            m_coro = coro;
        }

        asio::ip::tcp::socket& underlying() { return m_socket; }

        private:
            void _write(std::string_view& str) {
                m_socket.async_write_some(asio::buffer(str.data(), str.size()),
                    [this,&str](std::error_code ec, std::size_t nBytesSent) {
                        std::cout << nBytesSent << " bytes sent\n";
                        if (nBytesSent == str.size()) {
                            m_coro.resume();
                            return;
                        }
                        str.remove_prefix(nBytesSent);
                        _write(str);
                    }
                );
            }
            void _read(std::vector<char>& vWholeBuf) {
                std::cout << "_read() called\n";
                std::vector<char> vBuf(1024*64);
                m_socket.async_read_some(asio::buffer(vBuf.data(), vBuf.size()),
                        [this,&vWholeBuf,vBuf=std::move(vBuf)](std::error_code ec, std::size_t nSize) mutable {
                            if (ec) {
                                m_coro.resume();
                                return;
                            }
                            std::cout << "Read " << nSize << " bytes\n";
                            vWholeBuf.reserve(vWholeBuf.size() + nSize);
                            std::move(vBuf.begin(), std::next(vBuf.begin(), nSize), std::back_inserter(vWholeBuf));
                            //vWholeBuf.insert(vWholeBuf.end(), std::make_move_iterator(vBuf.begin()), std::make_move_iterator(std::next(vBuf.begin(), nSize)));
                            std::cout << "vWholeBuf is now " << vWholeBuf.size() << " bytes in size...\n";
                            _read(vWholeBuf);
                        }
                );
            }


    };
*/
}

/*
class task {

public:

	struct promise_type;

	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type {
		task get_return_object() noexcept {
			return task{ handle_type::from_promise( *this ) };
		}

		std::suspend_never initial_suspend() const noexcept { return {}; }
		std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() const noexcept {
            std::cout << "co_return\n";
        }
		void unhandled_exception() {}
	};


	task(handle_type handle) 
        : handle_{ handle }
     //   m_idleWork{ m_context },
        // idleWork
       // m_thread{ [this]() { m_context.run(); } }
    {}

	task(task&& rhs) noexcept : handle_{ std::exchange(rhs.handle_, nullptr) } {}
	task& operator=(task&& rhs) noexcept { 
		std::swap(rhs.handle_, handle_);
		return *this;
	}
	task(const task&) = delete;
	task& operator=(const task&) = delete;
	~task() {
		if (handle_)
			handle_.destroy();
	}

	void resume() {
		handle_.resume();
	}

private:
	handle_type handle_;
    asio::io_context m_context;
    asio::io_context::work m_idleWork;
    std::thread m_thread;
};

task client(ice::socket& sock, asio::ip::tcp::endpoint ep) {
    bool bSucc = co_await sock.connect(ep);
    if (not bSucc)
        throw std::runtime_error("Could not connect");

    std::string sRequest = 
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n\r\n";

    co_await sock.write(sRequest);
    
    std::vector<char> buf = co_await sock.read();

    std::cout << "Buf is " << buf.size() << " bytes\n";
    for (auto ch: buf)
        std::cout << ch;
}
*/

namespace ice {
  namespace net {
    template<typename T>
      class client {
        asio::io_context m_ctx{};
        asio::ip::tcp::endpoint m_ep; 
        std::shared_ptr<ice::net::connection<T>> m_conn;
        std::thread m_thread{};

        public:
        client(asio::ip::tcp::endpoint ep) : m_ep{ ep }, m_conn{ std::make_shared<ice::net::connection<T>>(m_ctx) } {}
        client(std::string_view strUrl, std::uint16_t nPort)
          : m_ep{ std::invoke([this,&strUrl, nPort]() {
                asio::ip::tcp::resolver resolver{ m_ctx };
                auto results = resolver.resolve(strUrl, std::to_string(nPort));
                return results->endpoint();
              }) },
          m_conn{ std::make_shared<ice::net::connection<T>>(m_ctx) }
        {
        }
        client(client const&) = delete;
        client& operator=(client const&) = delete;
        client(client&& rhs) noexcept {
          swap(rhs);
        }
        client& operator=(client&& rhs) noexcept {
          swap(rhs);
          return *this;
        }
        ~client() {
          disconnect();
        }
        void swap(client& rhs) noexcept {
          std::swap(m_ctx, rhs.m_ctx);
          std::swap(m_ep, rhs.m_ep);
          std::swap(m_conn, rhs.m_conn);
          std::swap(m_thread, rhs.m_thread);
        }
        friend void swap(client& lhs, client& rhs) noexcept {
          lhs.swap(rhs);
        }

        asio::awaitable<bool> connect() {
          const auto bConnected = co_await m_conn->connect(m_ep);
          if (not bConnected)
            co_return false;

          std::cout << "[CLIENT] Connected. Sending hello\n";

          message<system_message> helloMsg{ system_message::CLIENT_HELLO, payload_definition<system_message, system_message::CLIENT_HELLO>::size_bytes };
          helloMsg.payload << "Hello";

          m_conn->send(helloMsg);

          std::clog << "[CLIENT] Back from send\n";

          asio::error_code ec{};

          /*
          auto msgCoro = m_conn->message(co_await asio::this_coro::executor);
          auto handshakeMsgOpt = co_await msgCoro.async_resume(asio::use_awaitable);
          if (not handshakeMsgOpt.has_value())
            co_return false;
          auto handshakeMsg = handshakeMsgOpt.value();
          */
          auto handshakeMsg = co_await m_conn->message(asio::use_awaitable);

          std::vector<char> vHandshake;
          handshakeMsg.payload >> vHandshake;
          std::string_view strHandshake(vHandshake.data(), vHandshake.size());

          std::cout << "[CLIENT] Payload is '" << strHandshake << "'\n";


          handshakeMsg.header.messageID = (T)(int)(system_message::CLIENT_HANDSHAKE);
          handshakeMsg.header.nSize = payload_definition<system_message, system_message::CLIENT_HANDSHAKE>::size_bytes;
          handshakeMsg.payload.clear();
          handshakeMsg.payload << std::hash<std::string_view>{}(strHandshake);

          std::cout << "[CLIENT] Hash I calculated is " << std::hash<std::string_view>{}(strHandshake) << "\n";

          m_conn->send(handshakeMsg);

          co_return true;

        }

        std::shared_ptr<ice::net::connection<T>> connection() noexcept { return m_conn; }

        void run() {
          asio::co_spawn(m_ctx, run_impl(), [](std::exception_ptr ptr) {
              std::clog << "[CLIENT] Exception caught in run().\n";
              std::rethrow_exception(ptr);
              });
          m_thread = std::thread([this]() { m_ctx.run(); });
        }

        void send(ice::net::message<T> msg) {
          connection()->send(std::move(msg));
        }

        void disconnect() {
          connection()->disconnect();
          m_ctx.stop();
          m_thread.join();
        }

        virtual void onConnected() {}
        virtual void onMessageReceived(const ice::net::message<T>& msg) = 0;

        private:
        asio::awaitable<void> run_impl() {
          using namespace ice::net;

          const auto bConn = co_await connect();
          if (bConn) {
            std::cout << "[CLIENT] Connected to server\n";
            onConnected();
          } else {
            std::cout << "[CLIENT] Connection failed\n";
            throw std::runtime_error("Connection to server failed.");
            co_return;
          }

          //auto msgCoro = connection()->message(connection()->socket().get_executor());
          for (;;) {
            /*
            std::optional<ice::net::message<T>> msgOpt;
            try {
              msgOpt = co_await msgCoro.async_resume(asio::use_awaitable);
              if (not msgOpt) {
                std::cout << "[CLIENT] Empty optional received. Breaking from loop.\n";
                co_return;
              }
            }
            catch (...) {
              std::clog << "[CLIENT] Exception thrown from async_resume.\n";
            }
            auto& msg = msgOpt.value();
            */
            auto msg = co_await connection()->message(asio::use_awaitable);
            if (not connection()->connected()) {
              std::cout << "[CLIENT] Disconnected, breaking from loop.\n";
              break;
            }
            std::cout << "[CLIENT] Received message " << (int)msg.header.messageID << " of size " << msg.header.nSize << "\n";
            onMessageReceived(msg);
          }

          co_return;
        }

      };
  }
}

class mensch_client : public ice::net::client<my_message> {
public:
  using ice::net::client<my_message>::client;

  void roll_dice() {
    auto msg = ice::net::message{ my_message::ROLL_DICE, 0 };
    std::cout << "[CLIENT] Sending message with ID " << msg.header.rawID() << "\n";
    send(std::move(msg));
  }

  void onMessageReceived(const ice::net::message<my_message>& msg) override {
    switch (msg.header.messageID) {
      case my_message::ROLL_DICE_RESULT:
        {
          auto res = msg.payload.template read<my_message, my_message::ROLL_DICE_RESULT>();
          std::cout << "[CLIENT] Die roll result: " << std::get<0>(res) << "\n";
        }
        break;
      default:
        break;
    }
  }
};

int main() {

    mensch_client client{ "127.0.0.1", 60000 };

    client.run();

    int i{-1};
    while (i != 0) {
      std::cin >> i;
      if (i == 1)
        client.roll_dice();
    }

}
