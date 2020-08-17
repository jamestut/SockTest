#pragma once

#include "objsock.h"
#include <hvsocket.h>
#include <string_view>
#include <Windows.h>

namespace wsockwrapper {

	class HyperVSocket : public Socket {
	public:
		HyperVSocket() : Socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW) {}

		auto bindSocket(SockArg const& args) -> int override {
			if (!isValid()) {
				throw std::runtime_error("Invalid socket.");
			}

			SOCKADDR_HV bindAddr;
			ZeroMemory(&bindAddr, sizeof(bindAddr));
			parseArgs(args, bindAddr);
			
			return bind(sock_, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
		}

		auto connectSocket(SockArg const& args) -> int {
			if (!isValid()) {
				throw std::runtime_error("Invalid socket.");
			}

			SOCKADDR_HV connAddr;
			ZeroMemory(&connAddr, sizeof(connAddr));
			connAddr.VmId = HV_GUID_LOOPBACK;
			parseArgs(args, connAddr);
			
			return connect(sock_, reinterpret_cast<sockaddr*>(&connAddr), sizeof(connAddr));
		}

		auto getParamDesc() -> SockArg const& override {
			return paramDesc_;
		}

	private:
		static SockArg paramDesc_;

		auto parseArgs(SockArg const& args, SOCKADDR_HV& target) -> void {
			target.Family = AF_HYPERV;
			auto const argTarget = args.find("addr");
			if (argTarget != args.end()) {
				if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char*>(argTarget->second.c_str())), &target.VmId) != RPC_S_OK) {
					throw std::runtime_error("Invalid bind address GUID.");
				}
			}
			if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char*>(args.at("appid").c_str())), &target.ServiceId) != RPC_S_OK) {
				throw std::runtime_error("Invalid service GUID.");
			}
		}
	};

	HyperVSocket::SockArg HyperVSocket::paramDesc_ = {
		{"appid","Service GUID. See https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/make-integration-service"},
		{"addr","Target VM GUID to bind or connect. Defaults to HV_GUID_ZERO for server, or HV_GUID_LOOPBACK for client. See link above, or use 'hcsdiag list'."}
	};

} //namespace wsockwrapper