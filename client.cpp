#include <asio.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <iostream>
#include <thread>
#include <coroutine>
#include <chrono>
#include <optional>
#include <future>

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

class task {
public:

	struct promise_type;

	using handle_type = std::coroutine_handle<promise_type>;

	struct promise_type {
		task get_return_object() noexcept {
			return task{ std::coroutine_handle<promise_type>::from_promise( *this ) };
		}

		std::suspend_never initial_suspend() const noexcept { return {}; }
		std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() const noexcept {
            std::cout << "co_return\n";
        }
		void unhandled_exception() {}
	};


	task(handle_type handle) : handle_{ handle } {}
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

};

task my_coro(std::thread& t) {

	std::cout << "Coroutine started on thread " << std::this_thread::get_id() << "\n";
    int answr;
	co_await calculate_it(t, answr);
	std::cout << "Calculated " << answr << ", not on thread " << std::this_thread::get_id() << "\n";
	co_return;
}



int main() {
    asio::io_context ctx;

	asio::error_code ec{};

	asio::ip::tcp::endpoint ep{ asio::ip::make_address("93.184.216.34", ec), 80 };

	asio::ip::tcp::socket sock{ ctx };

	sock.connect(ep, ec);

	if (!ec) {
		std::cout << "Connected\n";	
	} else {
		std::cerr << "Error connecting: " << ec.message() << "\n";
        return -1;
	}

    if (sock.is_open()) {
        std::string sRequest = 
            "GET /index.html HTTP/1.1\r\n"
            "Host: example.com\r\n"
            "Connection: close\r\n\r\n";
            sock.write_some(asio::buffer(sRequest.data(), sRequest.size()), ec);

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

        sock.close();
    }

    /*
    std::thread thr;
	task t = my_coro(thr);

	int i;
	std::cin >> i;
    */

//    thr.join();

}
