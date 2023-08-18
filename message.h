#ifndef MESSAGE_H
#define MESSAGE_H

#include <cstdint>
#include <type_traits>
#include <concepts>

namespace ice {
    namespace net {
        template<typename T, typename = void>
        struct message;
        template<typename T>
        struct message<T, std::enable_if_t<std::is_enum_v<T>>> {
            T messageID;
            std::uint32_t nSize;
        };
    }
}

#endif
