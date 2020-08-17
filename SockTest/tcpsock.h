#pragma once

#include "objsock.h"
#include <ws2tcpip.h>

namespace wsockwrapper {
	class TCPSocket : public Socket {
	public:
		TCPSocket() : Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) {}

		auto bindSocket(SockArg const& args) -> int override {
			return connBind(false, args);
		}

		auto connectSocket(SockArg const& args) -> int {
			return connBind(true, args);
		}

		virtual auto getParamDesc()->SockArg const& {
			return paramDesc_;
		}

	private:
		static SockArg paramDesc_;

		auto connBind(bool conn, SockArg const& args) -> int {
			if (!isValid()) {
				throw std::runtime_error("Invalid socket.");
			}

			struct addrinfo* result = NULL;
			struct addrinfo hints;

			ZeroMemory(&hints, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_PASSIVE;

			const char* bindAddr = NULL;
			if (args.contains("host")) {
				bindAddr = args.at("host").c_str();
			}
			else if (conn) {
				throw std::runtime_error("Address must be specified.");
			}
			if (getaddrinfo(bindAddr, args.at("port").c_str(), &hints, &result)) {
				throw std::runtime_error("Error parsing specified address.");
			}

			if (conn) {
				return connect(sock_, result->ai_addr, static_cast<int>(result->ai_addrlen));
			}
			else {
				return bind(sock_, result->ai_addr, static_cast<int>(result->ai_addrlen));
			}
		}
	};

	TCPSocket::SockArg TCPSocket::paramDesc_ = {
		{"host","Hostname to bind or connect to."},
		{"port","TCP port number for bind or connect to."}
	};
} //namespace wsockwrapper