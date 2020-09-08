#pragma once

#include <string>
#include <vector>

#include <crosslib.h>
#include <nativelib.h>

class DVRCMSInvalidCredentialsException : public std::exception
{
	const char* what() const noexcept override { return "Invalid credentials"; }
};

class DVRCMS
{
	nativelib::TcpSocket controlSocket, videoSocket;
	uint32_t sessionId = 0;
	std::string sessionIdHex;
	crosslib::TimerFd timer;
	std::vector<uint8_t> buffer;

public:
	explicit DVRCMS();
	~DVRCMS();

	DVRCMS(DVRCMS&) = delete;
	DVRCMS& operator=(DVRCMS&&) = delete;

	void addToEpoll(crosslib::EPoll& epoll);
	void removeFromEpoll(crosslib::EPoll& epoll);

	void connect(const std::string& ip, uint16_t port, const std::string& username, const std::string& password, int channel);

	const std::vector<uint8_t>& readFrame(crosslib::EPoll& epoll);

private:
};
