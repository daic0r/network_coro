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
        template<typename T>
            struct awaitable {
                socket& m_sock;
                std::function<void(std::optional<T>&)> m_func;
                std::optional<T> m_value;

                template<typename F>
                    awaitable(socket& sock, F&& func) : m_sock{ sock }, m_func{ std::forward<F>(func) } {}

                bool await_ready() const noexcept { return false; }
                bool await_suspend(std::coroutine_handle<> coro) noexcept {
                    m_sock.setCoroHandle(coro);
                    m_func(m_value); 
                    return true;
                }
                T await_resume() noexcept {
                    std::cout << "Resuming with " << (*m_value).size() << " bytes\n";
                    return std::move(*m_value);
                }
            };

        socket(asio::io_context& ctx)
            : m_context{ ctx },
            m_socket{ m_context }
        {}

        ~socket() {
            m_socket.close();
        }

        void connect(asio::ip::tcp::endpoint ep) {
            std::error_code ec;
            m_socket.connect(ep, ec);
        }

        awaitable<std::vector<char>> read() {
            return awaitable<std::vector<char>>{ *this, [this](std::optional<std::vector<char>>& val) mutable {
                val.emplace();
                _read(*val); 
            } };
        }

        void setCoroHandle(std::coroutine_handle<> coro) {
            m_coro = coro;
        }

        asio::ip::tcp::socket& underlying() { return m_socket; }

        private:
            void _read(std::vector<char>& vWholeBuf) {
                std::cout << "_read() called\n";
                std::vector<char> vBuf(1024*64);
                m_socket.async_read_some(asio::buffer(vBuf.data(), vBuf.size()),
                        [this,&vWholeBuf,vBuf=std::move(vBuf)](std::error_code ec, std::size_t nSize) {
                            if (ec) {
                                std::cout << "Resuming now with buffer of size " << vWholeBuf.size() << "...\n";
                                m_coro.resume();
                                return;
                            }
                            std::cout << "Read " << nSize << " bytes\n";
                            std::copy(vBuf.begin(), vBuf.end(), std::back_inserter(vWholeBuf));
                            std::cout << "vBuf is now " << vBuf.size() << " bytes in size...\n";
                            std::cout << "vWholeBuf is now " << vWholeBuf.size() << " bytes in size...\n";
                            //vWholeBuf.insert(vWholeBuf.end(), vBuf.begin(), vBuf.end());
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

task client(ice::socket& sock) {
    std::vector<char> buf = co_await sock.read();
    std::cout << "Buf is " << buf.size() << " bytes\n";
    for (auto ch: buf)
        std::cout << ch;
}

/*
task my_coro(std::thread& t) {

	std::cout << "Coroutine started on thread " << std::this_thread::get_id() << "\n";
    int answr;
	co_await calculate_it(t, answr);
	std::cout << "Calculated " << answr << ", not on thread " << std::this_thread::get_id() << "\n";
	co_return;
}
*/

std::vector<char> vBuf(1024 * 64);

void grabData(asio::ip::tcp::socket& sock) {
    sock.async_read_some(asio::buffer(vBuf.data(), vBuf.size()),
            [&](std::error_code ec, std::size_t nSize) {
                if (ec)
                    return;
                std::cout << "Reading " << nSize << " bytes\n";
                for (auto ch : vBuf)
                    std::cout << ch;
                grabData(sock);
            }
    );
}


int main() {
    asio::io_context ctx;

    asio::io_context::work idleWork{ ctx };

    std::thread thr{ [&ctx]() { ctx.run(); } };

	asio::error_code ec{};

	asio::ip::tcp::endpoint ep{ asio::ip::make_address("93.184.216.34", ec), 80 };

	//asio::ip::tcp::socket sock{ ctx };

    ice::socket sock{ ctx };

	sock.connect(ep);

	if (!ec) {
		std::cout << "Connected\n";	
	} else {
		std::cerr << "Error connecting: " << ec.message() << "\n";
        return -1;
	}

    if (sock.underlying().is_open()) {
        std::string sRequest = 
            "GET /index.html HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Connection: close\r\n\r\n";
            sock.underlying().write_some(asio::buffer(sRequest.data(), sRequest.size()), ec);

            /*
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(2s);
            const auto nAvail = sock.available();
            std::cout << "Bytes available: " << nAvail << std::endl;
            
            if (nAvail > 0) {
                std::vector<char> vBuf(nAvail);
                sock.read_some(asio::buffer(vBuf.data(), nAvail), ec);

                for (auto ch : vBuf)
                    std::cout << ch;
            }
            */
        //grabData(sock);
        //
        task t = client(sock);

        int i;
        std::cin >> i;

        ctx.stop();
        if (thr.joinable())
            thr.join();


        //sock.close();
    }

    /*
    std::thread thr;
	task t = my_coro(thr);

	int i;
	std::cin >> i;
    */

//    thr.join();

}
