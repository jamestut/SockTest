#pragma once

#include <winsock2.h>
#include <stdint.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace wsockwrapper {

	class Socket {
	public:
		using SockArg = std::unordered_map<std::string, std::string>;

		Socket() = default;

		virtual ~Socket() {
			if (sock_ != INVALID_SOCKET) {
				closesocket(sock_);
				sock_ = INVALID_SOCKET;
				if (!--wsadataRefcount_) {
					WSACleanup();
					wsadataValid_ = false;
				}
			}
		}

		Socket(Socket&) = delete;

		auto operator=(Socket&)->Socket & = delete;

		Socket(Socket&& other) noexcept {
			*this = std::move(other);
		}

		auto operator=(Socket&& other) noexcept -> Socket& {
			sock_ = other.sock_;
			other.sock_ = INVALID_SOCKET;
			return *this;
		}

		auto isValid() const -> bool {
			return sock_ != INVALID_SOCKET;
		}

		auto getSock() const -> SOCKET {
			if (sock_ == INVALID_SOCKET) {
				throw std::runtime_error("Invalid socket");
			}
			return sock_;
		}

		virtual auto bindSocket(SockArg const&) -> int = 0;
		
		virtual auto connectSocket(SockArg const&) -> int = 0;

		virtual auto getParamDesc()->SockArg const& = 0;

	private:
		// global
		static WSADATA wsadata_;
		static bool wsadataValid_;
		static size_t wsadataRefcount_;

		auto wsaInit() -> void {
			if (!wsadataValid_) {
				wsadataValid_ = WSAStartup(MAKEWORD(2, 2), &wsadata_) == NOERROR;
				if (!wsadataValid_) {
					throw std::runtime_error("Unable to startup winsock");
				}
			}
		}

	protected:
		// per instance
		SOCKET sock_ = INVALID_SOCKET;

		Socket(int family, int type, int protocol) {
			wsaInit();
			sock_ = socket(family, type, protocol);
			if (sock_ != INVALID_SOCKET) {
				++wsadataRefcount_;
			}
		}

		Socket(SOCKET s) {
			wsaInit();
			sock_ = s;
			if (sock_ != INVALID_SOCKET) {
				++wsadataRefcount_;
			}
		}
	};

	class GenericSocket : public Socket {
	public:
		GenericSocket(SOCKET s) : Socket{s} {}

		auto bindSocket(SockArg const&) -> int override {
			throw std::runtime_error("Operation not supported");
		}

		auto connectSocket(SockArg const&) -> int override {
			throw std::runtime_error("Operation not supported");
		}

		auto getParamDesc() -> SockArg const& override {
			throw std::runtime_error("Operation not supported");
		}
	};

	// declaration for refcount
	WSADATA Socket::wsadata_{ 0 };
	bool Socket::wsadataValid_ = false;
	size_t Socket::wsadataRefcount_ = 0;

} //namespace wsockwrapper