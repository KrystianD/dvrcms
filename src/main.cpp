#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <SDL/SDL.h>
#include <tclap/CmdLine.h>

#include <crosslib.h>
#include <libkdav.h>

#include "DVRCMS.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;
using namespace crosslib;
using namespace kdav;

void crosslib_on_error(const char* fmt, va_list arg)
{
	fprintf(stderr, "crossLib error: ");
	vfprintf(stderr, fmt, arg);
}

int main(int argc, char** argv)
{
	KDAV::init();

	TCLAP::CmdLine cmd("");
	TCLAP::ValueArg<string> ipArg("H", "host", "DVR hostname or IP address", true, "", "HOST", cmd);
	TCLAP::ValueArg<int> portArg("P", "port", "DVR port", false, 34567, "PORT", cmd);
	TCLAP::ValueArg<int> channelArg("c", "channel", "DVR channel to use", true, 0, "CHANNEL", cmd);
	TCLAP::ValueArg<string> userArg("u", "user", "DVR user", true, "", "USER", cmd);
	TCLAP::ValueArg<string> passArg("p", "password", "DVR password", true, "", "PASSWORD", cmd);
	TCLAP::ValueArg<string> codecArg("", "codec", "Codec to use (default h264)", false, "h264", "CODEC", cmd);
	TCLAP::SwitchArg debugArg("d", "debug", "Debug mode", cmd);

	string ip, user, pass;
	int port, channel;
	bool debug;
	string codecName;

	try {
		cmd.parse(argc, argv);

		ip = ipArg.getValue();
		port = portArg.getValue();
		channel = channelArg.getValue();
		user = userArg.getValue();
		pass = passArg.getValue();
		codecName = codecArg.getValue();
		debug = debugArg.getValue();

	} catch (TCLAP::ArgException& e) {
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}

	if (debug)
		av_log_set_level(AV_LOG_DEBUG);
	else
		av_log_set_level(AV_LOG_QUIET);

	// std::shared_ptr<KDAVCodec> codec = KDAVCodec::createH264Codec();
	std::shared_ptr<KDAVCodec> codec = KDAVCodec::findCodecByName(codecName);
	std::shared_ptr<KDAVCodecContext> codecCtx = KDAVCodecContext::create(codec);
	codecCtx->setPixelFormat(KDPixelFormat::YUV420P);
	codecCtx->open();

	std::shared_ptr<KDAVParser> h264Parser = KDAVParser::createH264Parser();

	EPoll epoll;

	DVRCMS cms;
	cms.connect(ip, port, user, pass, channel);
	cms.addToEpoll(epoll);

	stringstream ss;
	ss << ip << ", ch: " << channel;
	SDL_Init(SDL_INIT_VIDEO);
	string title = ss.str();
	SDL_WM_SetCaption(title.c_str(), nullptr);

	SDL_Overlay* overlay = nullptr;

	KDAVFrame frame;

	while (true) {
		int r = epoll.wait();
		if (r == -1)
			break;

		vector<uint8_t> payload = cms.readFrame(epoll);

		if (debug)
			printf("Received new payload of size: %lu\n", payload.size());

		if (!payload.empty()) {
			int payloadPtr = 0;

			while (payloadPtr < payload.size()) {
				size_t remaining = payload.size() - payloadPtr;

				KDAVParserFrame parsedFrame;
				int consumedBytes;
				h264Parser->parse(codecCtx, payload.data(), payloadPtr, remaining, parsedFrame, consumedBytes);

				if (debug)
					printf("Parsed payload with H264 parser, frame size: %d, consumed bytes: %d\n", parsedFrame.getSize(), consumedBytes);

				if (parsedFrame.getSize() > 0) {
					bool hasFrame;
					codecCtx->decode(parsedFrame, frame, hasFrame);

					if (hasFrame) {
						if (debug)
							printf("Decoded H264 frame of size: %dx%d\n", frame.getWidth(), frame.getHeight());

						auto sizes = frame.getLineSizes();
						if (!overlay) {
							SDL_Surface* screen = SDL_SetVideoMode(frame.getWidth(), frame.getHeight(), 24, SDL_SWSURFACE);
							overlay = SDL_CreateYUVOverlay(sizes[0], frame.getHeight(), SDL_IYUV_OVERLAY, screen);
						}

						SDL_LockYUVOverlay(overlay);
						overlay->pixels = const_cast<uint8_t**>(frame.getData());
						SDL_UnlockYUVOverlay(overlay);

						SDL_Rect rect;
						rect.x = 0;
						rect.y = 0;
						rect.w = frame.getWidth();
						rect.h = frame.getHeight();
						SDL_DisplayYUVOverlay(overlay, &rect);
					}
					else {
						printf("Not full H264 frame, continue\n");
					}
				}

				payloadPtr += consumedBytes;
			}
		}

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				goto end;
		}
	}

end:
	if (overlay)
		SDL_FreeYUVOverlay(overlay);
}
