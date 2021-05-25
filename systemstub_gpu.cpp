/*
 * REminiscence / REinforced
 * Copyright (C) 2020-2021 Alex Chermenin (alex@chermenin.ru)
 */

#include <SDL_gpu.h>
#include "resource.h"
#include "scaler.h"
#include "screenshot.h"
#include "systemstub.h"
#include "util.h"

static const int kAudioHz = 22050;

static const char *kIconBmp = "icon.bmp";

static const int kJoystickIndex = 0;
static const int kJoystickCommitValue = 3200;

static const uint32_t kPixelFormat = SDL_PIXELFORMAT_RGB888;

struct SystemStub_GPU : SystemStub {
	SDL_Window *_window;
	GPU_Target *_screen;
	GPU_Image *_image;
	int _texW, _texH;
	SDL_GameController *_controller;
	SDL_PixelFormat *_fmt;
	const char *_caption;
	unsigned char *_screenBuffer;
	bool _fullscreen;
	uint8_t _overscanColor;
	unsigned char _rgbPalette[256];
	unsigned char _shadowPalette[256];
	unsigned char _darkPalette[256];
	int _screenW, _screenH;
	SDL_Joystick *_joystick;
	bool _fadeOnUpdateScreen;
	void (*_audioCbProc)(void *, int16_t *, int);
	void *_audioCbData;
	int _screenshot;
	ScalerType _scalerType;
	int _scaleFactor;
	const Scaler *_scaler;
	void *_scalerSo;
	int _widescreenMode;
	GPU_Image *_widescreenImage;
	int _wideMargin;
	bool _enableWidescreen;

	virtual ~SystemStub_GPU() {}
	virtual void init(const char *title, int w, int h, bool fullscreen, int widescreenMode, const ScalerParameters *scalerParameters);
	virtual void destroy();
	virtual bool hasWidescreen() const;
	virtual void setScreenSize(int w, int h);
	virtual void setPalette(const uint8_t *pal, int n);
	virtual void getPalette(uint8_t *pal, int n);
	virtual void setPaletteEntry(int i, const Color *c);
	virtual void getPaletteEntry(int i, Color *c);
	virtual void setOverscanColor(int i);
	virtual void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch);
	virtual void copyRectRgb24(int x, int y, int w, int h, const uint8_t *rgb);
	virtual void copyWidescreenLeft(int w, int h, const uint8_t *buf, bool dark = true);
	virtual void copyWidescreenRight(int w, int h, const uint8_t *buf, bool dark = true);
	virtual void copyWidescreenMirror(int w, int h, const uint8_t *buf);
	virtual void copyWidescreenBlur(int w, int h, const uint8_t *buf);
	virtual void clearWidescreen();
	virtual void enableWidescreen(bool enable);
	virtual void fadeScreen();
	virtual void updateScreen(int shakeOffset);
	virtual void processEvents();
	virtual void sleep(int duration);
	virtual uint32_t getTimeStamp();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32_t getOutputSampleRate();
	virtual void lockAudio();
	virtual void unlockAudio();

	void setPaletteColor(int color, int r, int g, int b);
	void processEvent(const SDL_Event &ev, bool &paused);
	void prepareGraphics();
	void cleanupGraphics();
	void changeGraphics(bool fullscreen, int scaleFactor);
	void setScaler(const ScalerParameters *parameters);
	void changeScaler(int scalerNum);
	void drawRect(int x, int y, int w, int h, uint8_t color);
};

SystemStub *SystemStub_GPU_create() {
	return new SystemStub_GPU();
}

void SystemStub_GPU::init(const char *title, int w, int h, bool fullscreen, int widescreenMode, const ScalerParameters *scalerParameters) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);
	SDL_ShowCursor(SDL_DISABLE);
	_caption = title;
	memset(&_pi, 0, sizeof(_pi));
	_window = 0;
	_screen = 0;
	_image = 0;
	_fmt = SDL_AllocFormat(kPixelFormat);
	_fadeOnUpdateScreen = false;
	_fullscreen = fullscreen;
	_scalerType = kScalerTypeInternal;
	_scaleFactor = 1;
	_scaler = 0;
	_scalerSo = 0;
	if (scalerParameters->name[0]) {
		setScaler(scalerParameters);
	}
	memset(_rgbPalette, 0, sizeof(_rgbPalette));
	memset(_shadowPalette, 0, sizeof(_shadowPalette));
	memset(_darkPalette, 0, sizeof(_darkPalette));
	_screenW = _screenH = 0;
	_widescreenMode = widescreenMode;
	_widescreenImage = 0;
	_wideMargin = 0;
	_enableWidescreen = false;
	setScreenSize(w, h);
	_joystick = 0;
	_controller = 0;
	if (SDL_NumJoysticks() > 0) {
		SDL_GameControllerAddMapping(Resource::_controllerMapping);
		_joystick = SDL_JoystickOpen(kJoystickIndex);
		if (SDL_IsGameController(kJoystickIndex)) {
			_controller = SDL_GameControllerOpen(kJoystickIndex);
		}
	}
	_screenshot = 1;
}

void SystemStub_GPU::destroy() {
	cleanupGraphics();
	if (_fmt) {
		SDL_FreeFormat(_fmt);
		_fmt = 0;
	}
	if (_scalerSo) {
		SDL_UnloadObject(_scalerSo);
		_scalerSo = 0;
	}
	if (_controller) {
		SDL_GameControllerClose(_controller);
		_controller = 0;
	}
	if (_joystick) {
		SDL_JoystickClose(_joystick);
		_joystick = 0;
	}
	SDL_Quit();
}

bool SystemStub_GPU::hasWidescreen() const {
	return _widescreenMode != kWidescreenNone;
}

void SystemStub_GPU::setScreenSize(int w, int h) {
	if (_screenW == w && _screenH == h) {
		return;
	}
	cleanupGraphics();
	_screenBuffer = new unsigned char[w * h];
	_screenW = w;
	_screenH = h;
	prepareGraphics();
}

void SystemStub_GPU::setPaletteColor(int color, int r, int g, int b) {
	_rgbPalette[color] = (unsigned char)SDL_MapRGB(_fmt, r, g, b);
	_shadowPalette[color] = (unsigned char)SDL_MapRGB(_fmt, r / 3, g / 3, b / 3);
	_darkPalette[color] = (unsigned char)SDL_MapRGB(_fmt, r / 4, g / 4, b / 4);
}

void SystemStub_GPU::setPalette(const uint8_t *pal, int n) {
	assert(n <= 256);
	for (int i = 0; i < n; ++i) {
		setPaletteColor(i, pal[0], pal[1], pal[2]);
		pal += 3;
	}
}

void SystemStub_GPU::getPalette(uint8_t *pal, int n) {
	assert(n <= 256);
	for (int i = 0; i < n; ++i) {
		SDL_GetRGB(_rgbPalette[i], _fmt, &pal[0], &pal[1], &pal[2]);
		pal += 3;
	}
}

void SystemStub_GPU::setPaletteEntry(int i, const Color *c) {
	setPaletteColor(i, c->r, c->g, c->b);
}

void SystemStub_GPU::getPaletteEntry(int i, Color *c) {
	SDL_GetRGB(_rgbPalette[i], _fmt, &c->r, &c->g, &c->b);
}

void SystemStub_GPU::setOverscanColor(int i) {
	_overscanColor = i;
}

void SystemStub_GPU::copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch) {
	if (x < 0) {
		x = 0;
	} else if (x >= _screenW) {
		return;
	}
	if (y < 0) {
		y = 0;
	} else if (y >= _screenH) {
		return;
	}
	if (x + w > _screenW) {
		w = _screenW - x;
	}
	if (y + h > _screenH) {
		h = _screenH - y;
	}

	unsigned char *p = _screenBuffer + y * _screenW + x;
	buf += y * pitch + x;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			p[i] = _rgbPalette[buf[i]];
		}
		p += _screenW;
		buf += pitch;
	}

	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
		drawRect(x, y, w, h, 0xE7);
	}
}

void SystemStub_GPU::copyRectRgb24(int x, int y, int w, int h, const uint8_t *rgb) {
	assert(x >= 0 && x + w <= _screenW && y >= 0 && y + h <= _screenH);
	unsigned char *p = _screenBuffer + y * _screenW + x;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			p[i] = SDL_MapRGB(_fmt, rgb[0], rgb[1], rgb[2]); rgb += 3;
		}
		p += _screenW;
	}

	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
		drawRect(x, y, w, h, 0xE7);
	}
}

static void clearImage(GPU_Image *image, int h, SDL_PixelFormat *fmt) {
	// void *dst = 0;
	// int pitch = 0;
	// if (SDL_LockTexture(texture, 0, &dst, &pitch) == 0) {
	// 	assert((pitch & 3) == 0);
	// 	const uint32_t color = SDL_MapRGB(fmt, 0, 0, 0);
	// 	for (uint32_t i = 0; i < h * pitch / sizeof(uint32_t); ++i) {
	// 		((uint32_t *)dst)[i] = color;
	// 	}
	// 	SDL_UnlockTexture(texture);
	// }
}

static void blur_h(int radius, const unsigned char *src, int srcPitch, int w, int h, const SDL_PixelFormat *fmt, unsigned char *dst, int dstPitch) {

	const int count = 2 * radius + 1;

	for (int y = 0; y < h; ++y) {

		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;

		unsigned char color;

		for (int x = -radius; x <= radius; ++x) {
			color = src[MAX(x, 0)];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;
		}
		dst[0] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);

		for (int x = 1; x < w; ++x) {
			color = src[MIN(x + radius, w - 1)];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;

			color = src[MAX(x - radius - 1, 0)];
			r -= (color & fmt->Rmask) >> fmt->Rshift;
			g -= (color & fmt->Gmask) >> fmt->Gshift;
			b -= (color & fmt->Bmask) >> fmt->Bshift;

			dst[x] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);
		}

		src += srcPitch;
		dst += dstPitch;
	}
}

static void blur_v(int radius, const unsigned char *src, int srcPitch, int w, int h, const SDL_PixelFormat *fmt, unsigned char *dst, int dstPitch) {

	const int count = 2 * radius + 1;

	for (int x = 0; x < w; ++x) {

		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;

		unsigned char color;

		for (int y = -radius; y <= radius; ++y) {
			color = src[MAX(y, 0) * srcPitch];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;
		}
		dst[0] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);

		for (int y = 1; y < h; ++y) {
			color = src[MIN(y + radius, h - 1) * srcPitch];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;

			color = src[MAX(y - radius - 1, 0) * srcPitch];
			r -= (color & fmt->Rmask) >> fmt->Rshift;
			g -= (color & fmt->Gmask) >> fmt->Gshift;
			b -= (color & fmt->Bmask) >> fmt->Bshift;

			dst[y * dstPitch] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);
		}

		++src;
		++dst;
	}
}

void SystemStub_GPU::copyWidescreenLeft(int w, int h, const uint8_t *buf, bool dark) {
	assert(w >= _wideMargin);
	unsigned char *rgb = new unsigned char[w * h];
	if (buf) {
		for (int i = 0; i < w * h; ++i) {
			if (dark) {
				rgb[i] = _darkPalette[buf[i]];
			} else {
				rgb[i] = _shadowPalette[buf[i]];
			}
		}
	} else {
		unsigned char color = (unsigned char)SDL_MapRGB(_fmt, 0, 0, 0);
		for (int i = 0; i < w * h; ++i) {
			rgb[i] = color;
		}
	}

	if (!dark) {
		unsigned char *tmp = new unsigned char[w * h];

		if (rgb && tmp) {
			static const int radius = 2;
			blur_h(radius, rgb, w, w, h, _fmt, tmp, w);
			blur_v(radius, tmp, w, w, h, _fmt, rgb, w);
		}

		free(tmp);
	}

	const int xOffset = w - _wideMargin;
	GPU_Rect r = GPU_MakeRect(0, 0, _wideMargin, h);
	GPU_UpdateImageBytes(_widescreenImage, &r, rgb + xOffset, w * sizeof(uint32_t));
}

void SystemStub_GPU::copyWidescreenRight(int w, int h, const uint8_t *buf, bool dark) {
	assert(w >= _wideMargin);
	unsigned char *rgb = new unsigned char[w * h];
	if (buf) {
		for (int i = 0; i < w * h; ++i) {
			if (dark) {
				rgb[i] = _darkPalette[buf[i]];
			} else {
				rgb[i] = _shadowPalette[buf[i]];
			}
		}
	} else {
		const unsigned char color = (unsigned char)SDL_MapRGB(_fmt, 0, 0, 0);
		for (int i = 0; i < w * h; ++i) {
			rgb[i] = color;
		}
	}

	if (!dark) {
		unsigned char *tmp = new unsigned char[w * h];

		if (rgb && tmp) {
			static const int radius = 2;
			blur_h(radius, rgb, w, w, h, _fmt, tmp, w);
			blur_v(radius, tmp, w, w, h, _fmt, rgb, w);
		}
	}

	const int xOffset = 0;
	GPU_Rect r = GPU_MakeRect(_wideMargin + _screenW, 0, _wideMargin, h);
	GPU_UpdateImageBytes(_widescreenImage, &r, rgb + xOffset, w * sizeof(uint32_t));
}

void SystemStub_GPU::copyWidescreenMirror(int w, int h, const uint8_t *buf) {
	// assert(w >= _wideMargin);
	// uint32_t *rgb = (uint32_t *)malloc(w * h * sizeof(uint32_t));
	// if (rgb) {
	// 	for (int i = 0; i < w * h; ++i) {
	// 		rgb[i] = _darkPalette[buf[i]];
	// 	}
	// 	void *dst = 0;
	// 	int pitch = 0;
	// 	if (SDL_LockTexture(_widescreenImage, 0, &dst, &pitch) == 0) {
	// 		assert((pitch & 3) == 0);
	// 		uint32_t *p = (uint32_t *)dst;
	// 		for (int y = 0; y < h; ++y) {
	// 			for (int x = 0; x < _wideMargin; ++x) {
	// 				// left side
	// 				const int xLeft = _wideMargin - 1 - x;
	// 				p[x] = rgb[y * w + xLeft];
	// 				// right side
	// 				const int xRight = w - 1 - x;
	// 				p[_wideMargin + _screenW + x] = rgb[y * w + xRight];
	// 			}
	// 			p += pitch / sizeof(uint32_t);
	// 		}
	// 		SDL_UnlockTexture(_widescreenImage);
	// 	}
	// 	free(rgb);
	// }
}

void SystemStub_GPU::copyWidescreenBlur(int w, int h, const uint8_t *buf) {
	// assert(w == _screenW && h == _screenH);
	// void *ptr = 0;
	// int pitch = 0;
	// if (SDL_LockTexture(_widescreenImage, 0, &ptr, &pitch) == 0) {
	// 	assert((pitch & 3) == 0);

	// 	uint32_t *src = (uint32_t *)malloc(w * h * sizeof(uint32_t));
	// 	uint32_t *tmp = (uint32_t *)malloc(w * h * sizeof(uint32_t));
	// 	uint32_t *dst = (uint32_t *)ptr;

	// 	if (src && tmp) {
	// 		for (int i = 0; i < w * h; ++i) {
	// 			src[i] = _rgbPalette[buf[i]];
	// 		}
	// 		static const int radius = 8;
	// 		blur_h(radius, src, w, w, h, _fmt, tmp, w);
	// 		blur_v(radius, tmp, w, w, h, _fmt, dst, pitch / sizeof(uint32_t));
	// 	}

	// 	free(src);
	// 	free(tmp);

	// 	SDL_UnlockTexture(_widescreenImage);
	// }
}

void SystemStub_GPU::clearWidescreen() {
	clearImage(_widescreenImage, _screenH, _fmt);
}

void SystemStub_GPU::enableWidescreen(bool enable) {
	_enableWidescreen = enable;
}

void SystemStub_GPU::fadeScreen() {
	_fadeOnUpdateScreen = true;
}

void SystemStub_GPU::updateScreen(int shakeOffset) {
	// if (_texW != _screenW || _texH != _screenH) {
	// 	void *dst = 0;
	// 	int pitch = 0;
	// 	if (SDL_LockTexture(_image, 0, &dst, &pitch) == 0) {
	// 		assert((pitch & 3) == 0);
	// 		_scaler->scale(_scaleFactor, (uint32_t *)dst, pitch / sizeof(uint32_t), _screenBuffer, _screenW, _screenW, _screenH);
	// 		SDL_UnlockTexture(_image);
	// 	}
	// } else {
		GPU_UpdateImageBytes(_image, 0, _screenBuffer, _screenW * sizeof(uint32_t));
	// }
	GPU_Clear(_screen);
	if (_widescreenMode != kWidescreenNone) {
		if (_enableWidescreen) {
			// borders / background screen
			GPU_Blit(_widescreenImage, NULL, _screen, 0, 0);
		}
		// game screen
		GPU_Rect r;
		r.y = shakeOffset * _scaleFactor;
		r.h = _screen->h;
		r.x = (_screen->w - _texW) / 2;
		r.w = _texW;
		GPU_Blit(_image, &r, _screen, 0, 0);
	} else {
		if (_fadeOnUpdateScreen) {
			GPU_SetShapeBlendMode(GPU_BLEND_PREMULTIPLIED_ALPHA);
			for (int i = 1; i <= 16; ++i) {
				GPU_Blit(_image, NULL, _screen, 0, 0);
				GPU_RectangleFilled(_screen, 0, 0, _screen->w, _screen->h, GPU_MakeColor(0, 0, 0, 256 - i * 16));
				GPU_Flip(_screen);
				SDL_Delay(30);
			}
			_fadeOnUpdateScreen = false;
			GPU_SetShapeBlendMode(GPU_BLEND_NORMAL);
			return;
		}
		GPU_Rect r = GPU_MakeRect(0, shakeOffset * _scaleFactor, _screen->w, _screen->h);
		GPU_Blit(_image, &r, _screen, 0, 0);
	}
	GPU_Flip(_screen);
}

void SystemStub_GPU::processEvents() {
	bool paused = false;
	while (true) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			processEvent(ev, paused);
			if (_pi.quit) {
				return;
			}
		}
		if (!paused) {
			break;
		}
		SDL_Delay(100);
	}
}

// only used for the protection codes and level passwords
static void setAsciiChar(PlayerInput &pi, const SDL_Keysym *key) {
	if (key->sym >= SDLK_0 && key->sym <= SDLK_9) {
		pi.lastChar = '0' + key->sym - SDLK_0;
	} else if (key->sym >= SDLK_a && key->sym <= SDLK_z) {
		pi.lastChar = 'A' + key->sym - SDLK_a;
	} else if (key->scancode == SDL_SCANCODE_0) {
		pi.lastChar = '0';
	} else if (key->scancode >= SDL_SCANCODE_1 && key->scancode <= SDL_SCANCODE_9) {
		pi.lastChar = '1' + key->scancode - SDL_SCANCODE_1;
	} else if (key->sym == SDLK_SPACE || key->sym == SDLK_KP_SPACE) {
		pi.lastChar = ' ';
	} else {
		pi.lastChar = 0;
	}
}

void SystemStub_GPU::processEvent(const SDL_Event &ev, bool &paused) {
	switch (ev.type) {
	case SDL_QUIT:
		_pi.quit = true;
		break;
	case SDL_WINDOWEVENT:
		switch (ev.window.event) {
		case SDL_WINDOWEVENT_FOCUS_GAINED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			paused = (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST);
			SDL_PauseAudio(paused);
			break;
		}
		break;
	case SDL_JOYHATMOTION:
		if (_joystick) {
			_pi.dirMask = 0;
			if (ev.jhat.value & SDL_HAT_UP) {
				_pi.dirMask |= PlayerInput::DIR_UP;
			}
			if (ev.jhat.value & SDL_HAT_DOWN) {
				_pi.dirMask |= PlayerInput::DIR_DOWN;
			}
			if (ev.jhat.value & SDL_HAT_LEFT) {
				_pi.dirMask |= PlayerInput::DIR_LEFT;
			}
			if (ev.jhat.value & SDL_HAT_RIGHT) {
				_pi.dirMask |= PlayerInput::DIR_RIGHT;
			}
		}
		break;
	case SDL_JOYAXISMOTION:
		if (_joystick) {
			switch (ev.jaxis.axis) {
			case 0:
				_pi.dirMask &= ~(PlayerInput::DIR_RIGHT | PlayerInput::DIR_LEFT);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				}
				break;
			case 1:
				_pi.dirMask &= ~(PlayerInput::DIR_UP | PlayerInput::DIR_DOWN);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				}
				break;
			}
		}
		break;
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		if (_joystick) {
			const bool pressed = (ev.jbutton.state == SDL_PRESSED);
			switch (ev.jbutton.button) {
			case 0:
				_pi.space = pressed;
				break;
			case 1:
				_pi.shift = pressed;
				break;
			case 2:
				_pi.enter = pressed;
				break;
			case 3:
				_pi.backspace = pressed;
				break;
			case 9:
				_pi.escape = pressed;
			}
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
		if (_controller) {
			switch (ev.caxis.axis) {
			case SDL_CONTROLLER_AXIS_LEFTX:
			case SDL_CONTROLLER_AXIS_RIGHTX:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			case SDL_CONTROLLER_AXIS_LEFTY:
			case SDL_CONTROLLER_AXIS_RIGHTY:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			}
		}
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		if (_controller) {
			const bool pressed = (ev.cbutton.state == SDL_PRESSED);
			switch (ev.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A:
				_pi.enter = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_B:
				_pi.space = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_X:
				_pi.shift = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_Y:
				_pi.backspace = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_BACK:
			case SDL_CONTROLLER_BUTTON_START:
				_pi.escape = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			}
		}
		break;
	case SDL_KEYUP:
		if (ev.key.keysym.mod & KMOD_ALT) {
			switch (ev.key.keysym.sym) {
			case SDLK_RETURN:
				changeGraphics(!_fullscreen, _scaleFactor);
				break;
			case SDLK_KP_PLUS:
			case SDLK_PAGEUP:
				if (_scalerType == kScalerTypeInternal || _scalerType == kScalerTypeExternal) {
					changeGraphics(_fullscreen, _scaleFactor + 1);
				}
				break;
			case SDLK_KP_MINUS:
			case SDLK_PAGEDOWN:
				if (_scalerType == kScalerTypeInternal || _scalerType == kScalerTypeExternal) {
					changeGraphics(_fullscreen, _scaleFactor - 1);
				}
				break;
			case SDLK_s: {
					char name[32];
					snprintf(name, sizeof(name), "screenshot-%03d.tga", _screenshot);
					saveTGA(name, (const uint8_t *)_screenBuffer, _screenW, _screenH);
					++_screenshot;
					debug(DBG_INFO, "Written '%s'", name);
				}
				break;
			case SDLK_x:
				_pi.quit = true;
				break;
			}
			break;
		} else if (ev.key.keysym.mod & KMOD_CTRL) {
			switch (ev.key.keysym.sym) {
			case SDLK_f:
				_pi.dbgMask ^= PlayerInput::DF_FASTMODE;
				break;
			case SDLK_b:
				_pi.dbgMask ^= PlayerInput::DF_DBLOCKS;
				break;
			case SDLK_i:
				_pi.dbgMask ^= PlayerInput::DF_SETLIFE;
				break;
			case SDLK_s:
				_pi.save = true;
				break;
			case SDLK_l:
				_pi.load = true;
				break;
			case SDLK_r:
				_pi.rewind = true;
				break;
			case SDLK_KP_PLUS:
			case SDLK_PAGEUP:
				_pi.stateSlot = 1;
				break;
			case SDLK_KP_MINUS:
			case SDLK_PAGEDOWN:
				_pi.stateSlot = -1;
				break;
			}
			break;
		}
		setAsciiChar(_pi, &ev.key.keysym);
		switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			break;
		case SDLK_SPACE:
			_pi.space = false;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = false;
			break;
		case SDLK_RETURN:
			_pi.enter = false;
			break;
		case SDLK_ESCAPE:
			_pi.escape = false;
			break;
		case SDLK_F1:
		case SDLK_F2:
		case SDLK_F3:
		case SDLK_F4:
		case SDLK_F5:
		case SDLK_F6:
		case SDLK_F7:
		case SDLK_F8:
			changeScaler(ev.key.keysym.sym - SDLK_F1);
			break;
		default:
			break;
		}
		break;
	case SDL_KEYDOWN:
		if (ev.key.keysym.mod & (KMOD_ALT | KMOD_CTRL)) {
			break;
		}
		switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask |= PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask |= PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask |= PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask |= PlayerInput::DIR_DOWN;
			break;
		case SDLK_BACKSPACE:
		case SDLK_TAB:
			_pi.backspace = true;
			break;
		case SDLK_SPACE:
			_pi.space = true;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = true;
			break;
		case SDLK_RETURN:
			_pi.enter = true;
			break;
		case SDLK_ESCAPE:
			_pi.escape = true;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

void SystemStub_GPU::sleep(int duration) {
	SDL_Delay(duration);
}

uint32_t SystemStub_GPU::getTimeStamp() {
	return SDL_GetTicks();
}

static void mixAudioS16(void *param, uint8_t *buf, int len) {
	SystemStub_GPU *stub = (SystemStub_GPU *)param;
	memset(buf, 0, len);
	stub->_audioCbProc(stub->_audioCbData, (int16_t *)buf, len / 2);
}

void SystemStub_GPU::startAudio(AudioCallback callback, void *param) {
	SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));
	desired.freq = kAudioHz;
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = 2048;
	desired.callback = mixAudioS16;
	desired.userdata = this;
	if (SDL_OpenAudio(&desired, 0) == 0) {
		_audioCbProc = callback;
		_audioCbData = param;
		SDL_PauseAudio(0);
	} else {
		error("SystemStub_GPU::startAudio() Unable to open sound device");
	}
}

void SystemStub_GPU::stopAudio() {
	SDL_CloseAudio();
}

uint32_t SystemStub_GPU::getOutputSampleRate() {
	return kAudioHz;
}

void SystemStub_GPU::lockAudio() {
	SDL_LockAudio();
}

void SystemStub_GPU::unlockAudio() {
	SDL_UnlockAudio();
}

static bool is16_9(const SDL_DisplayMode *mode) {
	return (mode->w / (float)mode->h) >= (16 / 9.f);
}

void SystemStub_GPU::prepareGraphics() {
	_texW = _screenW;
	_texH = _screenH;
	switch (_scalerType) {
	case kScalerTypePoint:
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest pixel sampling
		break;
	case kScalerTypeLinear:
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // linear filtering
		break;
	case kScalerTypeInternal:
	case kScalerTypeExternal:
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
		_texW *= _scaleFactor;
		_texH *= _scaleFactor;
		break;
	}
	int windowW = _screenW * _scaleFactor;
	int windowH = _screenH * _scaleFactor;
	int flags = 0;
	if (_fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	} else {
		flags |= SDL_WINDOW_RESIZABLE;
	}
	if (0 /* && _widescreenMode == kWidescreenDefault */) {
		SDL_DisplayMode dm;
		if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && is16_9(&dm)) {
			_widescreenMode = kWidescreenBlur; // default widescreen mode
		} else {
			_widescreenMode = kWidescreenNone;
		}
	}
	if (_widescreenMode != kWidescreenNone) {
		windowW = windowH * 16 / 9;
	}
	_window = SDL_CreateWindow(_caption, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowW, windowH, flags);
	SDL_Surface *icon = SDL_LoadBMP(kIconBmp);
	if (icon) {
		SDL_SetWindowIcon(_window, icon);
		SDL_FreeSurface(icon);
	}
	GPU_SetInitWindow(SDL_GetWindowID(_window));
	_screen = GPU_Init(windowW, windowH, GPU_DEFAULT_INIT_FLAGS);
	_image = GPU_CreateImage(_texW, _texH, GPU_FORMAT_RGBA);
	if (_widescreenMode != kWidescreenNone) {
		// in blur mode, the background texture has the same dimensions as the game texture
		// SDL stretches the texture to 16:9
		const int w = (_widescreenMode == kWidescreenBlur) ? _screenW : _screenH * 16 / 9;
		const int h = _screenH;
		_widescreenImage = GPU_CreateImage(w, h, GPU_FORMAT_RGBA);
		clearImage(_widescreenImage, _screenH, _fmt);

		// left and right borders
		_wideMargin = (w - _screenW) / 2;
	}
}

void SystemStub_GPU::cleanupGraphics() {
	if (_image) {
		GPU_FreeImage(_image);
		_image = 0;
	}
	if (_widescreenImage) {
		GPU_FreeImage(_widescreenImage);
		_widescreenImage = 0;
	}
	if (_screen) {
		GPU_FreeTarget(_screen);
		_screen = 0;
	}
	if (_window) {
		SDL_DestroyWindow(_window);
		_window = 0;
	}
}

void SystemStub_GPU::changeGraphics(bool fullscreen, int scaleFactor) {
	int factor = CLIP(scaleFactor, _scaler->factorMin, _scaler->factorMax);
	if (fullscreen == _fullscreen && factor == _scaleFactor) {
		// no change
		return;
	}
	_fullscreen = fullscreen;
	_scaleFactor = factor;
	cleanupGraphics();
	prepareGraphics();
}

void SystemStub_GPU::setScaler(const ScalerParameters *parameters) {
	static const struct {
		const char *name;
		int type;
		const Scaler *scaler;
	} scalers[] = {
		{ "point", kScalerTypePoint, 0 },
		{ "linear", kScalerTypeLinear, 0 },
		{ "scale", kScalerTypeInternal, &_internalScaler },
		{ "xbr", kScalerTypeInternal, &scaler_xbr },
#ifdef USE_STATIC_SCALER
		{ "nearest", kScalerTypeInternal, &scaler_nearest },
		{ "tv2x", kScalerTypeInternal, &scaler_tv2x },
#endif
		{ 0, -1 }
	};
	bool found = false;
	for (int i = 0; scalers[i].name; ++i) {
		if (strcmp(scalers[i].name, parameters->name) == 0) {
			_scalerType = (ScalerType)scalers[i].type;
			_scaler = scalers[i].scaler;
			found = true;
			break;
		}
	}
	if (!found) {
#ifdef _WIN32
		static const char *libSuffix = "dll";
#else
		static const char *libSuffix = "so";
#endif
		char libname[64];
		snprintf(libname, sizeof(libname), "scaler_%s.%s", parameters->name, libSuffix);
		_scalerSo = SDL_LoadObject(libname);
		if (!_scalerSo) {
			warning("Scaler '%s' not found, using default", libname);
		} else {
			static const char *kSoSym = "getScaler";
			void *symbol = SDL_LoadFunction(_scalerSo, kSoSym);
			if (!symbol) {
				warning("Symbol '%s' not found in '%s'", kSoSym, libname);
			} else {
				typedef const Scaler *(*GetScalerProc)();
				const Scaler *scaler = ((GetScalerProc)symbol)();
				const int tag = scaler ? scaler->tag : 0;
				if (tag != SCALER_TAG) {
					warning("Unexpected tag %d for scaler '%s'", tag, libname);
				} else {
					_scalerType = kScalerTypeExternal;
					_scaler = scaler;
				}
			}
		}
	}
	_scaleFactor = _scaler ? CLIP(parameters->factor, _scaler->factorMin, _scaler->factorMax) : 1;
}

void SystemStub_GPU::changeScaler(int scalerNum) {
	ScalerType type = kScalerTypeInternal;
	const Scaler *scaler = 0;
	switch (scalerNum) {
	case 0:
		type = kScalerTypePoint;
		break;
	case 1:
		type = kScalerTypeLinear;
		break;
	case 2:
		type = kScalerTypeInternal;
		scaler = &_internalScaler;
		break;
	case 3:
		type = kScalerTypeInternal;
		scaler = &scaler_xbr;
		break;
#ifdef USE_STATIC_SCALER
	case 4:
		type = kScalerTypeInternal;
		scaler = &scaler_nearest;
		break;
	case 5:
		type = kScalerTypeInternal;
		scaler = &scaler_tv2x;
		break;
#endif
	default:
		return;
	}
	if (_scalerType != type || scaler != _scaler) {
		_scalerType = type;
		_scaler = scaler;
		if (_scalerType == kScalerTypeInternal || _scalerType == kScalerTypeExternal) {
			_scaleFactor = CLIP(_scaleFactor, _scaler->factorMin, _scaler->factorMax);
		} else {
			_scaleFactor = 1;
		}
		cleanupGraphics();
		prepareGraphics();
	}
}

void SystemStub_GPU::drawRect(int x, int y, int w, int h, uint8_t color) {
	const int x1 = x;
	const int y1 = y;
	const int x2 = x + w - 1;
	const int y2 = y + h - 1;
	assert(x1 >= 0 && x2 < _screenW && y1 >= 0 && y2 < _screenH);
	for (int i = x1; i <= x2; ++i) {
		*(_screenBuffer + y1 * _screenW + i) = *(_screenBuffer + y2 * _screenW + i) = _rgbPalette[color];
	}
	for (int j = y1; j <= y2; ++j) {
		*(_screenBuffer + j * _screenW + x1) = *(_screenBuffer + j * _screenW + x2) = _rgbPalette[color];
	}
}