#ifndef MESSAGE_H
#define MESSAGE_H

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <concepts>
#include <vector>
#include <tuple>
#include <random>

namespace ice {
  template<typename T>
    concept Enumeration = std::is_enum_v<T> and std::is_same_v<std::underlying_type_t<T>, int>;

  template<typename T>
    concept Trivial = std::is_trivial_v<T>;

  namespace net {
    namespace detail {
      template<typename T>
        struct sizeof_tuple;
      template<typename... Ts>
        struct sizeof_tuple<std::tuple<Ts...>> {
          static constexpr auto value = (sizeof(Ts) + ...);
        };

      template<typename T>
        static constexpr auto sizeof_tuple_v = sizeof_tuple<T>::value;
    }
    enum system_message {
      NONE,
      CLIENT_HELLO = -1,
      SERVER_HANDSHAKE = -2,
      CLIENT_HANDSHAKE = -3,
      HEARTBEAT = -4
    };

    template<Enumeration Enum, Enum T>
      struct payload_definition;
    template<>
      struct payload_definition<system_message, system_message::CLIENT_HELLO> {
        using data = std::tuple<char[6]>;
        static constexpr auto size_bytes = detail::sizeof_tuple_v<data>;
      };
    template<>
      struct payload_definition<system_message, system_message::SERVER_HANDSHAKE> {
        using data = std::tuple<std::vector<char>>;
        static constexpr auto size_bytes = std::numeric_limits<std::size_t>::max();
      };
    template<>
      struct payload_definition<system_message, system_message::CLIENT_HANDSHAKE> {
        using data = std::tuple<std::size_t>;
        static constexpr auto size_bytes = detail::sizeof_tuple_v<data>;
      };

      struct message_payload {
        std::vector<char> vBytes{};
        mutable std::size_t nPos{};

        constexpr message_payload() {
          vBytes.resize(0);
        }

        constexpr auto size() const noexcept { return vBytes.size(); }
        constexpr auto data() noexcept { return vBytes.data(); }
        constexpr auto data() const noexcept { return vBytes.data(); }
        constexpr void clear() noexcept { vBytes.clear(); nPos = 0;}
        constexpr void resize(std::size_t nSize) { vBytes.resize(nSize); }

        template<Trivial T>
          friend constexpr message_payload& operator<<(message_payload& mp, T const& data) {
            const auto nOldSize = mp.vBytes.size();
            mp.vBytes.resize(nOldSize + sizeof(T));
            std::memcpy(std::next(mp.vBytes.data(), nOldSize), &data, sizeof(T));
            mp.nPos += sizeof(T);
            return mp;
          }

        template<typename T>
          friend constexpr message_payload& operator<<(message_payload& mp, std::vector<T> const& vec) {
            const auto nVecBytes = sizeof(T)*vec.size();
            const auto nOldSize = mp.vBytes.size();
            mp.vBytes.resize(nOldSize + nVecBytes);
            std::memcpy(std::next(mp.vBytes.data(), nOldSize), vec.data(), nVecBytes);
            mp.nPos += nVecBytes;
            mp << nVecBytes;
            return mp;
          }

        template<Trivial T>
          friend constexpr message_payload const& operator>>(message_payload const& mp, T& data) {
            mp.nPos -= sizeof(T);
            std::memcpy(&data, std::next(mp.vBytes.data(), mp.nPos), sizeof(T));
            return mp;
          }

        template<typename T>
          friend constexpr message_payload const& operator>>(message_payload const& mp, std::vector<T>& data) {
            std::size_t nSize{};
            mp >> nSize;
            data.clear();
            data.resize(nSize);
            mp.nPos -= nSize;
            std::memcpy(data.data(), std::next(mp.vBytes.data(), mp.nPos), nSize);
            return mp;
          }

        template<Enumeration Enum, Enum E>
          constexpr typename payload_definition<Enum, E>::data read() const noexcept {
            typename payload_definition<Enum, E>::data ret{};
            _read(ret, std::make_index_sequence<std::tuple_size_v<typename payload_definition<Enum, E>::data>>());
            return ret;
          }
        private:
        template<typename... Ts, std::size_t... Is>
          constexpr auto _read(std::tuple<Ts...>& outTup, std::index_sequence<Is...>) const noexcept {
            return std::make_tuple((..., (*this >> std::get<sizeof...(Is)-Is-1>(outTup))));
          }
      };

    template<Enumeration T>
      struct message_header {
        T messageID{};
        std::uint32_t nSize{};

        constexpr auto rawID() noexcept { return static_cast<std::underlying_type_t<T>>(messageID); }

        constexpr message_header() noexcept = default;
        constexpr message_header(T id, std::uint32_t nSize = 0) noexcept : messageID{ id }, nSize{ nSize } {}
        template<typename U>
        constexpr message_header(message_header<U> const& other) noexcept : messageID{ (T)(int)other.messageID }, nSize{ other.nSize } {}
      };

    template<typename T>
      struct message {
        constexpr message() noexcept = default;
        constexpr message(T id, std::uint32_t nSize = 0) noexcept : header{ id, nSize } {}
        template<typename U>
        constexpr message(message<U> const& other) noexcept : header{ other.header }, payload{ other.payload } {}

        message_header<T> header{};
        message_payload payload{};
      };
  }
}
enum class my_message {
  ROLL_DICE = 1,
  ROLL_DICE_RESULT = 2
};

template<>
  struct ice::net::payload_definition<my_message, my_message::ROLL_DICE_RESULT> {
    using data = std::tuple<int>;
    static constexpr auto size_bytes = detail::sizeof_tuple_v<data>;
  };


#endif
