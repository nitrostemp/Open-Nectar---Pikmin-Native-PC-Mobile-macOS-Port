#include "pc_dsu.h"

#include "settings/pc_settings.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#if !defined(_WIN32) && SDL_VERSION_ATLEAST(2, 24, 0)
#define PC_DSU_SUPPORTED 1
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#define PC_DSU_SUPPORTED 0
#endif

#if PC_DSU_SUPPORTED

namespace {

constexpr uint16_t kProtocolVersion = 1001;
constexpr uint32_t kMsgPadInfo      = 0x100001;
constexpr uint32_t kMsgPadData      = 0x100002;
constexpr uint32_t kMsgRumble       = 0x110002;
constexpr Uint32 kRequestIntervalMs = 1000; // servers drop clients idle for ~5 s
constexpr Uint32 kServerTimeoutMs   = 3000;

int sSock          = -1;
int sPort          = -1;
int sSlot          = -1;
uint32_t sClientId = 0;
sockaddr_in sServer {};

Uint32 sLastRequestMs = 0;
Uint32 sLastReplyMs   = 0;
bool sSlotConnected   = false;

SDL_Joystick* sJoy     = nullptr;
SDL_JoystickID sJoyId  = -1;

uint32_t crc32(const uint8_t* p, size_t n)
{
	uint32_t crc = 0xffffffffu;
	for (size_t i = 0; i < n; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v)
{
	for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t get32(const uint8_t* p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Header (16 bytes) + message type, CRC over the whole packet with the CRC
// field zeroed. `size` is the total packet size.
void sendPacket(uint8_t* p, size_t size, uint32_t type)
{
	if (sSock < 0) return;
	memcpy(p, "DSUC", 4);
	put16(p + 4, kProtocolVersion);
	put16(p + 6, (uint16_t)(size - 16));
	put32(p + 8, 0);
	put32(p + 12, sClientId);
	put32(p + 16, type);
	put32(p + 8, crc32(p, size));
	sendto(sSock, p, size, 0, reinterpret_cast<const sockaddr*>(&sServer), sizeof(sServer));
}

void sendRequests()
{
	// Info for our slot: tells us whether a pad is there even when it is idle
	// (pad data only arrives when the state changes).
	uint8_t info[25] = {};
	put32(info + 20, 1);
	info[24] = (uint8_t)sSlot;
	sendPacket(info, sizeof(info), kMsgPadInfo);

	// Subscribe to pad data by slot (flags bit 0).
	uint8_t sub[28] = {};
	sub[20] = 1;
	sub[21] = (uint8_t)sSlot;
	sendPacket(sub, sizeof(sub), kMsgPadData);
}

int rumble(void* userdata, Uint16 low, Uint16 high)
{
	(void)userdata;
	uint8_t p[30] = {};
	p[20] = 1;
	p[21] = (uint8_t)sSlot;
	p[28] = 0; // motor
	p[29] = (uint8_t)((low > high ? low : high) >> 8);
	sendPacket(p, sizeof(p), kMsgRumble);
	return 0;
}

int virtualDeviceIndex()
{
	for (int i = 0; i < SDL_NumJoysticks(); i++)
		if (SDL_JoystickGetDeviceInstanceID(i) == sJoyId) return i;
	return -1;
}

void attachPad()
{
	if (sJoy) return;
	SDL_VirtualJoystickDesc desc;
	SDL_zero(desc);
	desc.version  = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	desc.type     = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	desc.naxes    = SDL_CONTROLLER_AXIS_MAX;
	desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
	desc.name     = "NSO GameCube Controller (DSU)";
	desc.Rumble   = rumble;
	const int index = SDL_JoystickAttachVirtualEx(&desc);
	if (index < 0) {
		printf("[PC Port] DSU: could not attach virtual pad: %s\n", SDL_GetError());
		return;
	}
	sJoy = SDL_JoystickOpen(index);
	if (!sJoy) {
		SDL_JoystickDetachVirtual(index);
		return;
	}
	sJoyId = SDL_JoystickInstanceID(sJoy);
	// Triggers rest at the bottom, not at the centre like a stick.
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0);
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	printf("[PC Port] DSU: controller in slot %d attached\n", sSlot + 1);
	fflush(stdout);
}

void detachPad()
{
	if (!sJoy) return;
	const int index = virtualDeviceIndex();
	SDL_JoystickClose(sJoy);
	if (index >= 0) SDL_JoystickDetachVirtual(index);
	sJoy   = nullptr;
	sJoyId = -1;
	printf("[PC Port] DSU: controller detached\n");
	fflush(stdout);
}

Sint16 stickAxis(uint8_t v, bool invert)
{
	int a = ((int)v - 128) * 256;
	if (invert) a = -a;
	return (Sint16)(a < -32768 ? -32768 : a > 32767 ? 32767 : a);
}

void applyPadData(const uint8_t* p)
{
	if (!sJoy) return;
	const uint8_t b36 = p[36], b37 = p[37];
	struct { int button; bool down; } buttons[] = {
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, (b36 & 0x80) != 0 },
		{ SDL_CONTROLLER_BUTTON_DPAD_DOWN, (b36 & 0x40) != 0 },
		{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT, (b36 & 0x20) != 0 },
		{ SDL_CONTROLLER_BUTTON_DPAD_UP, (b36 & 0x10) != 0 },
		{ SDL_CONTROLLER_BUTTON_START, (b36 & 0x08) != 0 },
		{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, (b36 & 0x04) != 0 }, // Z
		{ SDL_CONTROLLER_BUTTON_Y, (b37 & 0x80) != 0 },
		{ SDL_CONTROLLER_BUTTON_B, (b37 & 0x40) != 0 },
		{ SDL_CONTROLLER_BUTTON_A, (b37 & 0x20) != 0 },
		{ SDL_CONTROLLER_BUTTON_X, (b37 & 0x10) != 0 },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, (b37 & 0x04) != 0 }, // L click
		{ SDL_CONTROLLER_BUTTON_PADDLE1, (b37 & 0x01) != 0 },      // ZL, bindable
		{ SDL_CONTROLLER_BUTTON_GUIDE, p[38] != 0 },              // Home
		{ SDL_CONTROLLER_BUTTON_MISC1, p[39] != 0 },              // Capture
	};
	for (const auto& b : buttons)
		SDL_JoystickSetVirtualButton(sJoy, b.button, b.down ? SDL_PRESSED : SDL_RELEASED);

	// DSU sticks are 0..255 with up positive; SDL's Y axis points down.
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_LEFTX, stickAxis(p[40], false));
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_LEFTY, stickAxis(p[41], true));
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_RIGHTX, stickAxis(p[42], false));
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_RIGHTY, stickAxis(p[43], true));

	// Analog L/R, pushed to full when the digital click is down so the game's
	// "trigger past 30000 = digital R" rule still fires on a worn trigger.
	const int r = (b37 & 0x08) ? 255 : p[54];
	const int l = (b37 & 0x04) ? 255 : p[55];
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, (Sint16)(r * 32767 / 255));
	SDL_JoystickSetVirtualAxis(sJoy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, (Sint16)(l * 32767 / 255));
}

void handlePacket(const uint8_t* p, size_t n, Uint32 now)
{
	if (n < 20 || memcmp(p, "DSUS", 4) != 0 || (size_t)get16(p + 6) + 16 != n) return;
	uint8_t copy[128];
	if (n > sizeof(copy)) return;
	memcpy(copy, p, n);
	put32(copy + 8, 0);
	if (crc32(copy, n) != get32(p + 8)) return;

	const uint32_t type = get32(p + 16);
	if ((type != kMsgPadInfo && type != kMsgPadData) || n < 32 || p[20] != sSlot) return;
	sLastReplyMs   = now;
	sSlotConnected = p[21] == 2;
	if (!sSlotConnected) {
		detachPad();
		return;
	}
	attachPad();
	if (type == kMsgPadData && n >= 80) applyPadData(p);
}

void closeSocket()
{
	detachPad();
	if (sSock >= 0) close(sSock);
	sSock          = -1;
	sPort          = -1;
	sSlot          = -1;
	sSlotConnected = false;
}

bool openSocket(int port, int slot)
{
	sSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sSock < 0) return false;
	fcntl(sSock, F_SETFL, fcntl(sSock, F_GETFL, 0) | O_NONBLOCK);
	memset(&sServer, 0, sizeof(sServer));
	sServer.sin_family      = AF_INET;
	sServer.sin_port        = htons((uint16_t)port);
	sServer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sPort          = port;
	sSlot          = slot;
	sClientId      = (uint32_t)SDL_GetPerformanceCounter() ^ 0x5049'4b49u;
	sLastRequestMs = 0;
	sLastReplyMs   = 0;
	printf("[PC Port] DSU: listening for slot %d on 127.0.0.1:%d\n", slot + 1, port);
	fflush(stdout);
	return true;
}

} // namespace

void pc_dsu_update(void)
{
	const bool enabled = pc_settings_get_dsu_enabled() != 0;
	const int port     = pc_settings_get_dsu_port();
	const int slot     = pc_settings_get_dsu_slot();
	if (sSock >= 0 && (!enabled || port != sPort || slot != sSlot)) closeSocket();
	if (!enabled) return;
	if (sSock < 0 && !openSocket(port, slot)) return;

	const Uint32 now = SDL_GetTicks();
	if (sLastRequestMs == 0 || now - sLastRequestMs >= kRequestIntervalMs) {
		sLastRequestMs = now;
		sendRequests();
	}

	uint8_t buf[256];
	for (;;) {
		const ssize_t n = recv(sSock, buf, sizeof(buf), 0);
		if (n <= 0) break;
		handlePacket(buf, (size_t)n, now);
	}

	// Server closed or stopped answering: drop the pad instead of leaving the
	// last state held down.
	if (sJoy && now - sLastReplyMs > kServerTimeoutMs) {
		sSlotConnected = false;
		detachPad();
	}
}

bool pc_dsu_connected(void) { return sJoy != nullptr; }

void pc_dsu_shutdown(void) { closeSocket(); }

#else // !PC_DSU_SUPPORTED

void pc_dsu_update(void) {}
bool pc_dsu_connected(void) { return false; }
void pc_dsu_shutdown(void) {}

#endif
