#include "DVRCMS.h"

#include <cstdio>
#include <ctime>

#include <openssl/md5.h>

#include <crosslib.h>

#include "json.hpp"

using namespace crosslib;
using namespace nativelib;
using namespace std;

using json = nlohmann::json;

#pragma pack(1)
struct THeader
{
	uint32_t firstInt = 0;
	uint32_t sessionId = 0;
	uint32_t thirdInt = 0;
	uint32_t fourthInt = 0;
	uint32_t payloadLength = 0;
};
#pragma pack()

const int PingIntervalMS = 5000;
const int ReadTimeoutMS = 10000;

static const char* now();
static std::string hashPassword(const char* password);

static void sendCommand(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId);
static void sendPacket(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId, json& jsonData);
static void readJson(TcpSocket& socket, json& json);
static void readHeader(TcpSocket& socket, THeader& header);
static void readPacketPayload(TcpSocket& socket, THeader& header, void* packetData);
static void sendPacketWaitForResponse(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId, json& reqJson, json& respJson);

DVRCMS::DVRCMS() : timer(PingIntervalMS)
{
}
DVRCMS::~DVRCMS()
{
	controlSocket.close();
	videoSocket.close();
}

void DVRCMS::addToEpoll(crosslib::EPoll& epoll)
{
	epoll.addRead(videoSocket.getFd());
	epoll.addRead(timer.getFd());
}
void DVRCMS::removeFromEpoll(crosslib::EPoll& epoll)
{
	epoll.remove(videoSocket.getFd());
	epoll.remove(timer.getFd());
}

void DVRCMS::connect(const std::string& ip, uint16_t port, const std::string& username, const std::string& password, int channel)
{
	TcpSocket::Result res;
	json respJson;

	res = controlSocket.connect(ip, port);
	if (res != TcpSocket::Result::ResultOK)
		throw std::runtime_error("Error during connecting to control endpoint");

	json loginJson = {
			{ "EncryptType", "MD5" },
			{ "LoginType",   "DVRIPWeb" },
			{ "PassWord",    hashPassword(password.c_str()) },
			{ "UserName",    username },
			{ "EncryptType", "MD5" },
	};

	sendPacketWaitForResponse(controlSocket, sessionId, 0x03e80000, loginJson, respJson);

	int result = respJson["Ret"];

	if (result == 203)
		throw DVRCMSInvalidCredentialsException();
	if (result != 100)
		throw std::runtime_error("Unknown error during login");

	sessionIdHex = respJson["SessionID"];

	std::stringstream ss;
	ss << std::hex << sessionIdHex;
	ss >> sessionId;

	json sysJson = {
			{ "Name",      "SystemInfo" },
			{ "SessionID", sessionIdHex },
	};
	sendPacketWaitForResponse(controlSocket, sessionId, 0x03fc0000, sysJson, respJson);

	json keepJson = {
			{ "Name",      "KeepAlive" },
			{ "SessionID", sessionIdHex },
	};
	sendPacketWaitForResponse(controlSocket, sessionId, 0x03ee0000, keepJson, respJson);

	json timeJson = {
			{ "Name",               "OPTimeSettingNoRTC" },
			{ "OPTimeSettingNoRTC", now() },
			{ "SessionID",          sessionIdHex },
	};
	sendPacketWaitForResponse(controlSocket, sessionId, 0x06360000, timeJson, respJson);

	json tmpJson = {
			{ "Name",      "" },
			{ "SessionID", sessionIdHex },
	};
	sendPacketWaitForResponse(controlSocket, sessionId, 0x05dc0000, tmpJson, respJson);

	sendCommand(controlSocket, sessionId, 0x061a0000);

	readJson(controlSocket, respJson);

	res = videoSocket.connect(ip, port);
	if (res != TcpSocket::Result::ResultOK)
		throw std::runtime_error("Error during connecting to video endpoint");

	json opJson = {
			{ "Name",      "OPMonitor" },
			{ "SessionID", sessionIdHex },
			{ "OPMonitor", {
					               { "Action", "Claim" },
					               { "Parameter", {
							                              { "Channel", channel },
							                              { "CombinMode", "NONE" },
							                              { "StreamType", "Main" },
							                              // { "StreamType", "Extra1" },
							                              { "TransMode", "TCP" },
					                              }
					               },
			               }
			},
	};

	sendPacketWaitForResponse(videoSocket, sessionId, 0x05850000, opJson, respJson);

	timer.start();

	OS::sleep(300);

	json opStartJson = {
			{ "Name",      "OPMonitor" },
			{ "SessionID", sessionIdHex },
			{ "OPMonitor", {
					               { "Action", "Start" },
					               { "Parameter", {
							                              { "Channel", channel },
							                              { "CombinMode", "NONE" },
							                              { "StreamType", "Main" },
							                              // { "StreamType", "Extra1" },
							                              { "TransMode", "TCP" },
					                              }
					               },
			               }
			},
	};

	sendPacketWaitForResponse(controlSocket, sessionId, 0x05820000, opStartJson, respJson);
}

const std::vector<uint8_t>& DVRCMS::readFrame(crosslib::EPoll& epoll)
{
	THeader header;

	if (epoll.hasRead(videoSocket.getFd())) {
		readHeader(videoSocket, header);

		int payloadLen = header.payloadLength;

		buffer.resize(payloadLen);
		readPacketPayload(videoSocket, header, &buffer[0]);
	}
	else {
		buffer.resize(0);
	}

	if (epoll.hasRead(timer.getFd())) {
		timer.get();

		json keepJson = {
				{ "Name",      "KeepAlive" },
				{ "SessionID", sessionIdHex },
		};

		json respJson;
		sendPacketWaitForResponse(controlSocket, sessionId, 0x03ee0000, keepJson, respJson);
	}

	return buffer;
}

static void sendPacket(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId, json& jsonData)
{
	string jsonStr = jsonData.dump();

	THeader header;
	header.firstInt = 0x000000ff;
	header.sessionId = sessionId;
	header.thirdInt = 0x0;
	header.fourthInt = cmdId;
	header.payloadLength = 0;

	if (cmdId == 0x05a00000)
		header.thirdInt = 0x6;

	if (!jsonData.empty())
		header.payloadLength = jsonStr.size();

	int r;
	r = socket.writeAll(&header, 0, sizeof(THeader));
	if (r <= 0)
		throw std::runtime_error("Error during sending packet header");

	if (!jsonData.empty()) {
		r = socket.writeAll(jsonStr.data(), 0, header.payloadLength);
		if (r <= 0)
			throw std::runtime_error("Error during sending packet payload");
	}
}

static void sendCommand(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId)
{
	json empty;
	sendPacket(socket, sessionId, cmdId, empty);
}

static void readJson(TcpSocket& socket, json& json)
{
	THeader header;
	readHeader(socket, header);

	if (header.payloadLength > 0) {
		char buf[header.payloadLength];
		readPacketPayload(socket, header, buf);

		json = json::parse(string(buf, header.payloadLength));
	}
}

static void readHeader(TcpSocket& socket, THeader& header)
{
	int r = socket.readAll(&header, 0, sizeof(THeader), ReadTimeoutMS);
	if (r <= 0)
		throw std::runtime_error("Unable to read header");
}

static void readPacketPayload(TcpSocket& socket, THeader& header, void* packetData)
{
	int r = socket.readAll(packetData, 0, header.payloadLength, ReadTimeoutMS);
	if (r <= 0)
		throw std::runtime_error("Unable to read payload");

	if (header.payloadLength == 0xf91f)
		throw std::runtime_error("Invalid header");

	if (header.firstInt != 0x1ff)
		throw std::runtime_error("Invalid header");
}

static void sendPacketWaitForResponse(TcpSocket& socket, uint32_t sessionId, uint32_t cmdId, json& reqJson, json& respJson)
{
	sendPacket(socket, sessionId, cmdId, reqJson);
	readJson(socket, respJson);
}

static const char* now()
{
	static char str[30];
	struct tm* pTm;

	time_t seconds = time(nullptr);
	pTm = localtime(&seconds);

	sprintf(str, "%02d-%02d-%02d %02d:%02d:%02d", pTm->tm_year + 1900, pTm->tm_mon + 1, pTm->tm_mday, pTm->tm_hour, pTm->tm_min, pTm->tm_sec);

	return str;
}

static std::string hashPassword(const char* password)
{
	MD5_CTX ctx;
	unsigned char digest[16];
	char hash[8];

	MD5_Init(&ctx);
	MD5_Update(&ctx, password, strlen(password));
	MD5_Final(digest, &ctx);

	for (int i = 0; i < 8; i++) {
		int rem = (digest[2 * i] + digest[2 * i + 1]) % 0x3e;

		if (rem < 10)
			hash[i] = (char)(rem + '0');
		else if ((rem - 10) < 0x1a)
			hash[i] = (char)(rem + '7');
		else
			hash[i] = (char)(rem + '=');
	}
	return std::string(hash, hash + 8);
}