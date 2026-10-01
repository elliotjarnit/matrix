// Scripted, headless driver for the calculator shell running on PortCE.
//
// The shell is linked unmodified with -Wl,--wrap=kb_Scan,--wrap=gfx_SwapDraw
// (and --wrap=fontlib_GetFontByIndex, see below):
// every kb_Scan() (one per frame) advances a key script instead of reading the
// SDL keyboard, and every gfx_SwapDraw() can dump the frame that was just
// presented as a PPM. This makes UI changes testable without a calculator.
//
// Environment:
//   SHELL_SIM_SCRIPT  path of the key script (required)
//   SHELL_SIM_OUT     directory for snapshots (default ".")
//
// Script, one command per line ('#' starts a comment):
//   press <key> [n]   press and release <key>, n times (default 1)
//   hold <key> <n>    keep <key> down for n frames, then release it
//   type <digits>     press each digit key in turn ('-' is the (-) key)
//   wait <n>          n frames with nothing pressed
//   snap <name>       write <name>.ppm from the next presented frame
//   quit              exit immediately
#include <PortCE.h>
#include <fontlibc.h>
#include <keypadc.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C++" void copyFrame(uint32_t* data); // PortCE_Render.cpp
extern "C" void __real_gfx_SwapDraw(void);
extern "C" void __real_kb_Scan(void);
extern "C" fontlib_font_t* __real_fontlib_GetFontByIndex(const char* font_pack_name, uint8_t index);

namespace {

struct KeyName {
		const char* name;
		kb_lkey_t key;
};

const KeyName kKeys[] = {
        {"enter", kb_KeyEnter},
        {"clear", kb_KeyClear},
        {"2nd", kb_Key2nd},
        {"del", kb_KeyDel},
        {"up", kb_KeyUp},
        {"down", kb_KeyDown},
        {"left", kb_KeyLeft},
        {"right", kb_KeyRight},
        {"chs", kb_KeyChs},
        {"0", kb_Key0},
        {"1", kb_Key1},
        {"2", kb_Key2},
        {"3", kb_Key3},
        {"4", kb_Key4},
        {"5", kb_Key5},
        {"6", kb_Key6},
        {"7", kb_Key7},
        {"8", kb_Key8},
        {"9", kb_Key9},
        {"div", kb_KeyDiv},
        {"dot", kb_KeyDecPnt},
        {"mode", kb_KeyMode},
        {"alpha", kb_KeyAlpha},
        {"sub", kb_KeySub},
        {"add", kb_KeyAdd},
};

// one entry per frame: the key held during that frame (0 = none), and an
// optional snapshot to take when that frame is presented
struct Frame {
		kb_lkey_t key = 0;
		std::string snap;
		bool quit = false;
};

std::vector<Frame> g_frames;
std::size_t g_frame = 0;
bool g_loaded = false;
std::string g_out = ".";
std::string g_pending_snap;

[[noreturn]] void die(const char* msg, const std::string& detail = {}) {
		std::fprintf(stderr, "shell_sim: %s%s%s\n", msg, detail.empty() ? "" : ": ", detail.c_str());
		std::exit(2);
}

kb_lkey_t key_by_name(const std::string& name) {
		for (const KeyName& k : kKeys) {
				if (name == k.name)
						return k.key;
		}
		die("unknown key", name);
}

void push_press(kb_lkey_t key) {
		g_frames.push_back({key, {}, false});
		g_frames.push_back({0, {}, false});
}

void load_script() {
		g_loaded = true;
		const char* path = std::getenv("SHELL_SIM_SCRIPT");
		if (!path)
				die("SHELL_SIM_SCRIPT is not set");
		if (const char* out = std::getenv("SHELL_SIM_OUT"))
				g_out = out;

		FILE* f = std::fopen(path, "r");
		if (!f)
				die("cannot open script", path);

		// a few idle frames so App::init and the first render settle
		for (int i = 0; i < 2; i++)
				g_frames.push_back({});

		char line[256];
		while (std::fgets(line, sizeof(line), f)) {
				if (char* hash = std::strchr(line, '#'))
						*hash = '\0';
				char cmd[32] = {0};
				char arg[128] = {0};
				int n = 1;
				const int got = std::sscanf(line, "%31s %127s %d", cmd, arg, &n);
				if (got <= 0)
						continue;
				const std::string c = cmd;
				if (c == "press") {
						const kb_lkey_t key = key_by_name(arg);
						for (int i = 0; i < n; i++)
								push_press(key);
				} else if (c == "hold") {
						const kb_lkey_t key = key_by_name(arg);
						for (int i = 0; i < n; i++)
								g_frames.push_back({key, {}, false});
						g_frames.push_back({});
				} else if (c == "type") {
						for (const char* p = arg; *p; p++)
								push_press(*p == '-' ? kb_KeyChs : key_by_name(std::string(1, *p)));
				} else if (c == "wait") {
						const int frames = std::atoi(arg);
						for (int i = 0; i < frames; i++)
								g_frames.push_back({});
				} else if (c == "snap") {
						// the snapshot is taken from the frame presented after this point
						g_frames.push_back({0, arg, false});
				} else if (c == "quit") {
						g_frames.push_back({0, {}, true});
				} else {
						die("unknown command", c);
				}
		}
		std::fclose(f);
		g_frames.push_back({0, {}, true});
}

void write_snapshot(const std::string& name) {
		static uint32_t pixels[320 * 240];
		copyFrame(pixels);
		const std::string path = g_out + "/" + name + ".ppm";
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f)
				die("cannot write snapshot", path);
		std::fprintf(f, "P6\n320 240\n255\n");
		for (uint32_t px : pixels) {
				// ABGR8888: red in the low byte
				const unsigned char rgb[3] = {static_cast<unsigned char>(px & 0xFF),
				        static_cast<unsigned char>((px >> 8) & 0xFF),
				        static_cast<unsigned char>((px >> 16) & 0xFF)};
				std::fwrite(rgb, 1, 3, f);
		}
		std::fclose(f);
}

} // namespace

// PortCE copies the appvar into a fresh buffer on every lookup and gives up
// after 256 of them, while libtexce looks its fonts up for every step it lays
// out. On the calculator the lookup is a pointer into archive, so cache it.
extern "C" fontlib_font_t* __wrap_fontlib_GetFontByIndex(const char* font_pack_name, uint8_t index) {
		struct Entry {
				std::string name;
				uint8_t index;
				fontlib_font_t* font;
		};
		static std::vector<Entry> cache;
		for (const Entry& e : cache) {
				if (e.name == font_pack_name && e.index == index)
						return e.font;
		}
		fontlib_font_t* font = __real_fontlib_GetFontByIndex(font_pack_name, index);
		if (font)
				cache.push_back({font_pack_name, index, font});
		return font;
}

// The shell's main() is the calculator's and does not know about PortCE, so
// initialise it before main runs. Headless unless SDL_VIDEODRIVER says otherwise.
__attribute__((constructor)) static void sim_init() {
		setenv("SDL_VIDEODRIVER", "offscreen", 0);
		PortCE_initialize("shell_sim");
}

extern "C" void __wrap_kb_Scan(void) {
		if (!g_loaded)
				load_script();
		volatile uint16_t* data = static_cast<volatile uint16_t*>(RAM_ADDRESS(0xF50010));
		for (int g = 0; g < 8; g++)
				data[g] = 0;

		if (g_frame >= g_frames.size())
				std::exit(0);
		const Frame& fr = g_frames[g_frame++];
		if (fr.quit)
				std::exit(0);
		if (!fr.snap.empty())
				g_pending_snap = fr.snap;
		if (fr.key != 0)
				data[fr.key >> 8] = static_cast<uint16_t>(fr.key & 0xFF);
}

extern "C" void __wrap_gfx_SwapDraw(void) {
		__real_gfx_SwapDraw();
		if (!g_pending_snap.empty()) {
				write_snapshot(g_pending_snap);
				g_pending_snap.clear();
		}
}
