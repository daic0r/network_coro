#include <asio.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <iostream>
#include <system_error>
#include <thread>
#include <coroutine>
#include <chrono>
#include <optional>
#include <future>

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
}

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
    /*
    asio::io_context m_context;
    asio::io_context::work m_idleWork;
    std::thread m_thread;
    */
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

int main() {
    asio::io_context ctx;

    asio::io_context::work idleWork{ ctx };

    std::thread thr{ [&ctx]() { ctx.run(); } };

	asio::error_code ec{};

	asio::ip::tcp::endpoint ep{ asio::ip::make_address("127.0.0.1", ec), 60000 };

	//asio::ip::tcp::socket sock{ ctx };

    ice::socket sock{ ctx };

    task t = client(sock, ep);

    int i;
    std::cin >> i;

    ctx.stop();
    if (thr.joinable())
        thr.join();



}
