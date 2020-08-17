#include "hypervsock.h"
#include "tcpsock.h"
#include "rng.h"

#include <iostream>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>
#include <fmt/format.h>

enum class SockType {
	UNDEF,
	HYPERV,
	TCP
};

static auto const sockStr_ = std::unordered_map<std::string, SockType>{
	{"hyperv", SockType::HYPERV},
	{"tcp", SockType::TCP}
};

auto get_socket_object(SockType type) -> std::pair<std::unique_ptr<wsockwrapper::Socket>, std::string> {
	switch (type) {
	case SockType::HYPERV:
		return { std::make_unique<wsockwrapper::HyperVSocket>(), "Hyper-V socket" };
	case SockType::TCP:
		return { std::make_unique<wsockwrapper::TCPSocket>(), "TCP socket" };
	default:
		return { nullptr, "(unknown socket)" };
	}
}

auto print_help() -> void {
	puts("Usage:");
	puts(" - SockTest <socket_type> server (<socket_option_key>=<socket_option_value>)...");
	puts(" - SockTest <socket_type> options");
	puts(" - SockTest <socket_type> client <repeat> <buff_size> (<socket_option_key>=<socket_option_value>)...");
	puts("");
	puts("Where:");
	puts(" - <buff_size>");
	puts("   Size of buffer to use, in bytes. Max 2^31-1 bytes.");
	puts(" - <socket_type> is one of the following:");
	for (auto const& kvp : sockStr_) {
		fmt::print("   - {}\n", kvp.first);
	}
}

auto friendly_size(size_t sz) -> std::string {
	if (sz < 10240) {
		return fmt::format("{} bytes", sz);
	}
	else if (sz < (1024ULL * 10000ULL)) {
		return fmt::format("{} KiB", sz / 1024);
	}
	else if (sz < (1024ULL * 1024ULL * 10000ULL)) {
		return fmt::format("{} MiB", sz / (1024ULL * 1024ULL));
	}
	else {
		return fmt::format("{} GiB", sz / (1024ULL * 1024ULL * 1024ULL));
	}
}

auto parse_socket_options(int argc, const char** argv) -> wsockwrapper::Socket::SockArg {
	wsockwrapper::Socket::SockArg ret;
	for (int i = 0; i < argc; ++i) {
		auto const arg = std::string_view(argv[i]);
		auto const delimpos = arg.find('=');
		if (delimpos == std::string::npos) {
			throw std::runtime_error("Invalid socket option format.");
		}
		
		auto const key = std::string_view(arg.data(), delimpos);
		auto const value = std::string_view(reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(arg.data()) + delimpos + 1));
		ret[std::string(key)] = value;
	}
	return ret;
}

auto command_options(SockType sockType, int argc, const char** argv) -> void {
	auto const [sockObj, sockName] = get_socket_object(sockType);
	if (!sockObj) {
		puts("Unknown socket");
		return;
	}
	fmt::print("Options available for the {}:\n", sockName);
	auto const& opts = sockObj->getParamDesc();
	for (auto const& opt : opts) {
		fmt::print(" - {}\n   {}\n", opt.first, opt.second);
	}
}

auto command_server(SockType sockType, int argc, const char** argv) -> void {
	auto const [sockObj, sockName] = get_socket_object(sockType);
	if (!sockObj) {
		puts("Unknown socket");
		return;
	}

	auto const sockOpt = parse_socket_options(argc, argv);
	if (sockObj->bindSocket(sockOpt)) {
		throw std::runtime_error("Error binding socket.");
	}
	if (listen(sockObj->getSock(), 1)) {
		throw std::runtime_error("Error listening socket.");
	}

	for (;;) {
		puts("Waiting for connection ...");
		auto const commSock = wsockwrapper::GenericSocket(accept(sockObj->getSock(), NULL, NULL));
		if (!commSock.isValid()) {
			puts("Connection error");
			continue;
		}

		// get the requested buffer size
		int buffsz;
		if (recv(commSock.getSock(), reinterpret_cast<char*>(&buffsz), sizeof(buffsz), 0) != sizeof(buffsz)) {
			puts("Invalid buffer length.");
			continue;
		}
		if (buffsz <= 0) {
			puts("Invalid buffer size.");
			continue;
		}

		fmt::print("Client requested buffer size of {}. Allocating ...\n", buffsz);
		auto recvBuff = std::make_unique<uint8_t[]>(buffsz);
		// "touch" all pages
		ZeroMemory(recvBuff.get(), buffsz);

		// as long as the client is willing to send, we'll handle it!
		bool stop = false;
		while(!stop) {
			puts("Processing data from client ...");
			uint8_t status;
			if (recv(commSock.getSock(), reinterpret_cast<char*>(&status), sizeof(status), 0) != sizeof(status)) {
				puts("Command error");
				stop = true;
				continue;
			}
			switch (status) {
			case 1:
				break;
			case 0:
				puts("Client asked to stop");
				stop = true;
				continue;
			default:
				puts("Unknown command");
				stop = true;
				continue;
			}
			// receive
			for (int i = 0; i < buffsz;) {
				auto rd = recv(commSock.getSock(), reinterpret_cast<char*>(recvBuff.get() + i), buffsz - i, MSG_WAITALL);
				if (rd == SOCKET_ERROR) {
					puts("Error receiving data from client.");
					stop = true;
					break;
				}
				i += rd;
			}
			if (stop) {
				continue;
			}
			// send indicator
			status = 1;
			if (send(commSock.getSock(), reinterpret_cast<char*>(&status), 1, 0) != 1) {
				puts("Error sending command");
				stop = true;
				continue;
			}
			// send back (echo)
			for (int i = 0; i < buffsz;) {
				auto rd = send(commSock.getSock(), reinterpret_cast<char*>(recvBuff.get() + i), buffsz - i, 0);
				if (rd == SOCKET_ERROR) {
					puts("Error sending data to client.");
					stop = true;
					break;
				}
				i += rd;
			}
		}
	}
}

auto command_client(SockType sockType, int argc, const char** argv) -> void {
	// timing
	LARGE_INTEGER qpcFreq;
	QueryPerformanceFrequency(&qpcFreq);
	const auto getTimespan = [&qpcFreq](LARGE_INTEGER begin, LARGE_INTEGER end) -> std::string {
		double seconds = static_cast<double>(end.QuadPart - begin.QuadPart) / static_cast<double>(qpcFreq.QuadPart);
		int64_t microsecs = static_cast<int64_t>(seconds * (1000LL * 1000LL));
		if (microsecs < 3000) {
			return fmt::format("{} us", microsecs);
		}
		else if (microsecs < 10000000) {
			return fmt::format("{} ms", microsecs / 1000LL);
		}
		else {
			return fmt::format("{} sec", microsecs / 1000000LL);
		}
	};
	const auto getBitrate = [&qpcFreq](LARGE_INTEGER begin, LARGE_INTEGER end, size_t sz) -> std::string {
		double seconds = static_cast<double>(end.QuadPart - begin.QuadPart) / static_cast<double>(qpcFreq.QuadPart);
		int64_t rate = static_cast<int64_t>(static_cast<double>(sz) / seconds);
		if (rate < 0) {
			return "(error)";
		}
		return fmt::format("{} / sec", friendly_size(rate));
	};

	auto const [sockObj, sockName] = get_socket_object(sockType);
	if (!sockObj) {
		puts("Unknown socket");
		return;
	}

	auto const sockOpt = parse_socket_options(argc - 2, argv + 2);
	if (sockObj->connectSocket(sockOpt)) {
		puts("Error connecting to server.");
		return;
	}

	int repetition, buffsz;
	if (argc < 2) {
		print_help();
		return;
	}

	repetition = std::stoi(argv[0]);
	if (repetition <= 0) {
		puts("Nothing to do.");
		return;
	}
	buffsz = std::stoi(argv[1]);
	if (buffsz <= 0) {
		puts("Invalid buffer size.");
		return;
	}

	fmt::print("Allocating 2x {} bytes buffer ...\n", buffsz);
	auto reffBuf = std::make_unique<uint8_t[]>(buffsz);
	auto recvBuf = std::make_unique<uint8_t[]>(buffsz);

	puts("Generating reference data ...");
	fillRandom(reffBuf.get(), buffsz);

	// tell server the buffer size
	if (send(sockObj->getSock(), reinterpret_cast<char*>(&buffsz), sizeof(buffsz), 0) == SOCKET_ERROR) {
		puts("Error telling server about buffer size");
		return;
	}

	for (int i = 0; i < repetition; ++i) {
		ZeroMemory(recvBuf.get(), buffsz);
		uint8_t status = 1;
		if (send(sockObj->getSock(), reinterpret_cast<char*>(&status), 1, 0) != 1) {
			puts("Error sending command");
			return;
		}
		
		LARGE_INTEGER beginSend, endSend, beginRecv, endRecv;
		puts("Sending ...");
		QueryPerformanceCounter(&beginSend);
		for (int i = 0; i < buffsz;) {
			int rd = send(sockObj->getSock(), reinterpret_cast<char*>(reffBuf.get() + i), buffsz - i, 0);
			if (rd == SOCKET_ERROR) {
				puts("Error sending data to server");
				return;
			}
			i += rd;
		}
		if (recv(sockObj->getSock(), reinterpret_cast<char*>(&status), 1, 0) != 1) {
			puts("Error receiving server ack");
			return;
		}
		QueryPerformanceCounter(&endSend);
		
		puts("Receiving ...");
		QueryPerformanceCounter(&beginRecv);
		for (int i = 0; i < buffsz;) {
			int rd = recv(sockObj->getSock(), reinterpret_cast<char*>(recvBuf.get() + i), buffsz - i, MSG_WAITALL);
			if (rd == SOCKET_ERROR) {
				puts("Error receiving data from server");
				return;
			}
			i += rd;
		}
		QueryPerformanceCounter(&endRecv);
		
		puts("Comparing data ...");
		if (memcmp(recvBuf.get(), reffBuf.get(), buffsz)) {
			puts("Data mismatch!");
			return;
		}

		fmt::print("Time send : {}\n", getTimespan(beginSend, endSend));
		fmt::print("Time recv : {}\n", getTimespan(beginRecv, endRecv));
		fmt::print("Rate send : {}\n", getBitrate(beginSend, endSend, buffsz));
		fmt::print("Rate recv : {}\n", getBitrate(beginRecv, endRecv, buffsz));

		puts("");
		Sleep(500);
	}
}

int main(int argc, const char** argv) {
	static auto const commands = std::unordered_map<std::string, std::function<void(SockType, int, const char**)>> {
		{"options", command_options},
		{"server", command_server},
		{"client", command_client},
	};

	if (argc < 3 || !sockStr_.contains(argv[1]) || !commands.contains(argv[2])) {
		print_help();
		return 0;
	}

	try {
		commands.at(argv[2])(sockStr_.at(argv[1]), argc - 3, argv + 3);
	}
	catch (std::exception ex) {
		fmt::print("Application error: {}\n", ex.what());
	}
	return 0;
}