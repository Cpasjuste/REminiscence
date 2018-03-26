
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "resource.h"
#include "systemstub.h"
#include "unpack.h"
#include "util.h"
#include "video.h"

Video::Video(Resource *res, SystemStub *stub)
	: _res(res), _stub(stub) {
	_w = GAMESCREEN_W;
	_h = GAMESCREEN_H;
	_layerSize = _w * _h;
	_frontLayer = (uint8_t *)calloc(1, _layerSize);
	_backLayer = (uint8_t *)calloc(1,_layerSize);
	_tempLayer = (uint8_t *)calloc(1, _layerSize);
	_tempLayer2 = (uint8_t *)calloc(1, _layerSize);
	_screenBlocks = (uint8_t *)calloc(1, (_w / SCREENBLOCK_W) * (_h / SCREENBLOCK_H));
	_fullRefresh = true;
	_shakeOffset = 0;
	_charFrontColor = 0;
	_charTransparentColor = 0;
	_charShadowColor = 0;
	_drawChar = 0;
	switch (_res->_type) {
	case kResourceTypeAmiga:
		_drawChar = &Video::AMIGA_drawStringChar;
		break;
	case kResourceTypeDOS:
		_drawChar = &Video::PC_drawStringChar;
		break;
	}
}

Video::~Video() {
	free(_frontLayer);
	free(_backLayer);
	free(_tempLayer);
	free(_tempLayer2);
	free(_screenBlocks);
}

void Video::markBlockAsDirty(int16_t x, int16_t y, uint16_t w, uint16_t h) {
	debug(DBG_VIDEO, "Video::markBlockAsDirty(%d, %d, %d, %d)", x, y, w, h);
	assert(x >= 0 && x + w <= _w && y >= 0 && y + h <= _h);
	int bx1 = x / SCREENBLOCK_W;
	int by1 = y / SCREENBLOCK_H;
	int bx2 = (x + w - 1) / SCREENBLOCK_W;
	int by2 = (y + h - 1) / SCREENBLOCK_H;
	assert(bx2 < _w / SCREENBLOCK_W && by2 < _h / SCREENBLOCK_H);
	for (; by1 <= by2; ++by1) {
		for (int i = bx1; i <= bx2; ++i) {
			_screenBlocks[by1 * (_w / SCREENBLOCK_W) + i] = 2;
		}
	}
}

void Video::updateScreen() {
	debug(DBG_VIDEO, "Video::updateScreen()");
//	_fullRefresh = true;
	if (_fullRefresh) {
		_stub->copyRect(0, 0, _w, _h, _frontLayer, 256);
		_stub->updateScreen(_shakeOffset);
		_fullRefresh = false;
	} else {
		int i, j;
		int count = 0;
		uint8_t *p = _screenBlocks;
		for (j = 0; j < _h / SCREENBLOCK_H; ++j) {
			uint16_t nh = 0;
			for (i = 0; i < _w / SCREENBLOCK_W; ++i) {
				if (p[i] != 0) {
					--p[i];
					++nh;
				} else if (nh != 0) {
					int16_t x = (i - nh) * SCREENBLOCK_W;
					_stub->copyRect(x, j * SCREENBLOCK_H, nh * SCREENBLOCK_W, SCREENBLOCK_H, _frontLayer, 256);
					nh = 0;
					++count;
				}
			}
			if (nh != 0) {
				int16_t x = (i - nh) * SCREENBLOCK_W;
				_stub->copyRect(x, j * SCREENBLOCK_H, nh * SCREENBLOCK_W, SCREENBLOCK_H, _frontLayer, 256);
				++count;
			}
			p += _w / SCREENBLOCK_W;
		}
		if (count != 0) {
			_stub->updateScreen(_shakeOffset);
		}
	}
	if (_shakeOffset != 0) {
		_shakeOffset = 0;
		_fullRefresh = true;
	}
}

void Video::fullRefresh() {
	debug(DBG_VIDEO, "Video::fullRefresh()");
	_fullRefresh = true;
	memset(_screenBlocks, 0, (_w / SCREENBLOCK_W) * (_h / SCREENBLOCK_H));
}

void Video::fadeOut() {
	debug(DBG_VIDEO, "Video::fadeOut()");
	if (g_options.fade_out_palette) {
		fadeOutPalette();
	} else {
		_stub->fadeScreen();
	}
}

void Video::fadeOutPalette() {
	for (int step = 16; step >= 0; --step) {
		for (int c = 0; c < 256; ++c) {
			Color col;
			_stub->getPaletteEntry(c, &col);
			col.r = col.r * step >> 4;
			col.g = col.g * step >> 4;
			col.b = col.b * step >> 4;
			_stub->setPaletteEntry(c, &col);
		}
		fullRefresh();
		updateScreen();
		_stub->sleep(50);
	}
}

void Video::setPaletteColorBE(int num, int offset) {
	const int color = READ_BE_UINT16(_res->_pal + offset * 2);
	Color c = AMIGA_convertColor(color, true);
	_stub->setPaletteEntry(num, &c);
}

void Video::setPaletteSlotBE(int palSlot, int palNum) {
	debug(DBG_VIDEO, "Video::setPaletteSlotBE()");
	const uint8_t *p = _res->_pal + palNum * 0x20;
	for (int i = 0; i < 16; ++i) {
		const int color = READ_BE_UINT16(p); p += 2;
		Color c = AMIGA_convertColor(color, true);
		_stub->setPaletteEntry(palSlot * 0x10 + i, &c);
	}
}

void Video::setPaletteSlotLE(int palSlot, const uint8_t *palData) {
	debug(DBG_VIDEO, "Video::setPaletteSlotLE()");
	for (int i = 0; i < 16; ++i) {
		uint16_t color = READ_LE_UINT16(palData); palData += 2;
		Color c = AMIGA_convertColor(color);
		_stub->setPaletteEntry(palSlot * 0x10 + i, &c);
	}
}

void Video::setTextPalette() {
	debug(DBG_VIDEO, "Video::setTextPalette()");
	setPaletteSlotLE(0xE, _textPal);
	if (_res->isAmiga()) {
		Color c;
		c.r = c.g = 0xEE;
		c.b = 0;
		_stub->setPaletteEntry(0xE7, &c);
	}
}

void Video::setPalette0xF() {
	debug(DBG_VIDEO, "Video::setPalette0xF()");
	const uint8_t *p = _palSlot0xF;
	for (int i = 0; i < 16; ++i) {
		Color c;
		c.r = *p++;
		c.g = *p++;
		c.b = *p++;
		_stub->setPaletteEntry(0xF0 + i, &c);
	}
}

void Video::PC_decodeLev(int level, int room) {
	uint8_t *tmp = _res->_mbk;
	_res->_mbk = _res->_bnq;
	_res->clearBankData();
	AMIGA_decodeLev(level, room);
	_res->_mbk = tmp;
	_res->clearBankData();
}

static void PC_decodeMapHelper(int sz, const uint8_t *src, uint8_t *dst) {
	const uint8_t *end = src + sz;
	while (src < end) {
		int16_t code = (int8_t)*src++;
		if (code < 0) {
			const int len = 1 - code;
			memset(dst, *src++, len);
			dst += len;
		} else {
			++code;
			memcpy(dst, src, code);
			src += code;
			dst += code;
		}
	}
}

void Video::PC_decodeMap(int level, int room) {
	debug(DBG_VIDEO, "Video::PC_decodeMap(%d)", room);
	assert(room < 0x40);
	int32_t off = READ_LE_UINT32(_res->_map + room * 6);
	if (off == 0) {
		error("Invalid room %d", room);
	}
	bool packed = true;
	if (off < 0) {
		off = -off;
		packed = false;
	}
	const uint8_t *p = _res->_map + off;
	_mapPalSlot1 = *p++;
	_mapPalSlot2 = *p++;
	_mapPalSlot3 = *p++;
	_mapPalSlot4 = *p++;
	if (level == 4 && room == 60) {
		// workaround for wrong palette colors (fire)
		_mapPalSlot4 = 5;
	}
	if (packed) {
		uint8_t *vid = _frontLayer;
		for (int i = 0; i < 4; ++i) {
			const int sz = READ_LE_UINT16(p); p += 2;
			PC_decodeMapHelper(sz, p, _res->_memBuf); p += sz;
			memcpy(vid, _res->_memBuf, 256 * 56);
			vid += 256 * 56;
		}
	} else {
		for (int i = 0; i < 4; ++i) {
			for (int y = 0; y < 224; ++y) {
				for (int x = 0; x < 64; ++x) {
					_frontLayer[i + x * 4 + 256 * y] = p[256 * 56 * i + x + 64 * y];
				}
			}
		}
	}
	memcpy(_backLayer, _frontLayer, _layerSize);
}

void Video::PC_setLevelPalettes() {
	debug(DBG_VIDEO, "Video::PC_setLevelPalettes()");
	if (_unkPalSlot2 == 0) {
		_unkPalSlot2 = _mapPalSlot3;
	}
	if (_unkPalSlot1 == 0) {
		_unkPalSlot1 = _mapPalSlot3;
	}
	setPaletteSlotBE(0x0, _mapPalSlot1);
	setPaletteSlotBE(0x1, _mapPalSlot2);
	setPaletteSlotBE(0x2, _mapPalSlot3);
	setPaletteSlotBE(0x3, _mapPalSlot4);
	if (_unkPalSlot1 == _mapPalSlot3) {
		setPaletteSlotLE(4, _conradPal1);
	} else {
		setPaletteSlotLE(4, _conradPal2);
	}
	// slot 5 is monster palette
	setPaletteSlotBE(0x8, _mapPalSlot1);
	setPaletteSlotBE(0x9, _mapPalSlot2);
	setPaletteSlotBE(0xA, _unkPalSlot2);
	setPaletteSlotBE(0xB, _mapPalSlot4);
	// slots 0xC and 0xD are cutscene palettes
	setTextPalette();
}

void Video::PC_decodeIcn(const uint8_t *src, int num, uint8_t *dst) {
	const int offset = READ_LE_UINT16(src + num * 2);
	const uint8_t *p = src + offset + 2;
	for (int i = 0; i < 16 * 16 / 2; ++i) {
		*dst++ = p[i] >> 4;
		*dst++ = p[i] & 15;
	}
}

void Video::PC_decodeSpc(const uint8_t *src, int w, int h, uint8_t *dst) {
	const int size = w * h / 2;
	for (int i = 0; i < size; ++i) {
		*dst++ = src[i] >> 4;
		*dst++ = src[i] & 15;
	}
}

static void AMIGA_planar16(uint8_t *dst, int w, int h, int depth, const uint8_t *src) {
	const int pitch = w * 16;
	const int planarSize = w * 2 * h;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			for (int i = 0; i < 16; ++i) {
				int color = 0;
				const int mask = 1 << (15 - i);
				for (int bit = 0; bit < depth; ++bit) {
					if (READ_BE_UINT16(src + bit * planarSize) & mask) {
						color |= 1 << bit;
					}
				}
				dst[x * 16 + i] = color;
			}
			src += 2;
		}
		dst += pitch;
	}
}

static void AMIGA_planar8(uint8_t *dst, int w, int h, const uint8_t *src) {
	assert(w == 8);
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < 8; ++i) {
			int color = 0;
			const int mask = 1 << (7 - i);
			for (int bit = 0; bit < 4; ++bit) {
				if (src[bit] & mask) {
					color |= 1 << bit;
				}
			}
			dst[i] = color;
		}
		src += 4;
		dst += w;
	}
}

static void AMIGA_planar24(uint8_t *dst, int w, int h, const uint8_t *src) {
	assert(w == 24);
	for (int y = 0; y < h; ++y) {
		for (int i = 0; i < 16; ++i) {
			int color = 0;
			const int mask = 1 << (15 - i);
			for (int bit = 0; bit < 4; ++bit) {
				if (READ_BE_UINT16(src + bit * 2) & mask) {
					color |= 1 << bit;
				}
			}
			dst[i] = color;
		}
		src += 8;
		for (int i = 0; i < 8; ++i) {
			int color = 0;
			const int mask = 1 << (7 - i);
			for (int bit = 0; bit < 4; ++bit) {
				if (src[bit] & mask) {
					color |= 1 << bit;
				}
			}
			dst[16 + i] = color;
		}
		src += 4;
		dst += w;
	}
}

static void AMIGA_planar_mask(uint8_t *dst, int x0, int y0, int w, int h, uint8_t *src, uint8_t *mask, int size) {
	dst += y0 * 256 + x0;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w * 2; ++x) {
			for (int i = 0; i < 8; ++i) {
				const int c_mask = 1 << (7 - i);
				int color = 0;
				for (int j = 0; j < 4; ++j) {
					if (mask[j * size] & c_mask) {
						color |= 1 << j;
					}
				}
				if (*src & c_mask) {
					const int px = x0 + 8 * x + i;
					const int py = y0 + y;
					if (px >= 0 && px < 256 && py >= 0 && py < 224) {
						dst[8 * x + i] = color;
					}
				}
			}
			++src;
			++mask;
		}
		dst += 256;
	}
}

static void AMIGA_decodeRle(uint8_t *dst, const uint8_t *src) {
	const int size = READ_BE_UINT16(src) & 0x7FFF; src += 2;
	for (int i = 0; i < size; ) {
		int code = src[i++];
		if ((code & 0x80) == 0) {
			++code;
			if (i + code > size) {
				code = size - i;
			}
			memcpy(dst, &src[i], code);
			i += code;
		} else {
			code = 1 - ((int8_t)code);
			memset(dst, src[i], code);
			++i;
		}
		dst += code;
	}
}

static void PC_drawTileMask(uint8_t *dst, int x0, int y0, int w, int h, uint8_t *m, uint8_t *p, int size) {
	assert(size == (w * 2 * h));
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const int bits = READ_BE_UINT16(m); m += 2;
			for (int bit = 0; bit < 8; ++bit) {
				const int j = y0 + y;
				const int i = x0 + 2 * (x * 8 + bit);
				if (i >= 0 && i < Video::GAMESCREEN_W && j >= 0 && j < Video::GAMESCREEN_H) {
					const uint8_t color = *p;
					if (bits & (1 << (15 - (bit * 2)))) {
						dst[j * Video::GAMESCREEN_W + i] = color >> 4;
					}
					if (bits & (1 << (15 - (bit * 2 + 1)))) {
						dst[j * Video::GAMESCREEN_W + i + 1] = color & 15;
					}
				}
				++p;
			}
		}
	}
}

static void decodeSgd(uint8_t *dst, const uint8_t *src, const uint8_t *data, const bool isAmiga) {
	int num = -1;
	uint8_t buf[256 * 32];
	int count = READ_BE_UINT16(src) - 1; src += 2;
	do {
		int d2 = READ_BE_UINT16(src); src += 2;
		int d0 = READ_BE_UINT16(src); src += 2;
		int d1 = READ_BE_UINT16(src); src += 2;
		if (d2 != 0xFFFF) {
			d2 &= ~(1 << 15);
			const int32_t offset = READ_BE_UINT32(data + d2 * 4);
			if (offset < 0) {
				const uint8_t *ptr = data - offset;
				const int size = READ_BE_UINT16(ptr); ptr += 2;
				if (num != d2) {
					num = d2;
					assert(size <= (int)sizeof(buf));
					memcpy(buf, ptr, size);
				}
                        } else {
				if (num != d2) {
					num = d2;
					const int size = READ_BE_UINT16(data + offset) & 0x7FFF;
					assert(size <= (int)sizeof(buf));
					AMIGA_decodeRle(buf, data + offset);
				}
			}
		}
		const int w = (buf[0] + 1) >> 1;
		const int h = buf[1] + 1;
		const int planarSize = READ_BE_UINT16(buf + 2);
		if (isAmiga) {
			AMIGA_planar_mask(dst, (int16_t)d0, (int16_t)d1, w, h, buf + 4, buf + 4 + planarSize, planarSize);
		} else {
			PC_drawTileMask(dst, (int16_t)d0, (int16_t)d1, w, h, buf + 4, buf + 4 + planarSize, planarSize);
		}
	} while (--count >= 0);
}

static const uint8_t *AMIGA_mirrorTileY(const uint8_t *a2) {
	static uint8_t buf[32];

        a2 += 24;
	for (int j = 0; j < 4; ++j) {
		for (int i = 0; i < 8; ++i) {
			buf[31 - j * 8 - i] = *a2++;
		}
		a2 -= 16;
	}
	return buf;
}

static const uint8_t *AMIGA_mirrorTileX(const uint8_t *a2) {
	static uint8_t buf[32];

	for (int i = 0; i < 32; ++i) {
		uint8_t mask = 0;
		for (int bit = 0; bit < 8; ++bit) {
			if (a2[i] & (1 << bit)) {
				mask |= 1 << (7 - bit);
			}
		}
		buf[i] = mask;
	}
	return buf;
}

static void AMIGA_drawTile(uint8_t *dst, int pitch, const uint8_t *src, int pal, const bool xflip, const bool yflip, int colorKey) {
	if (yflip) {
		src = AMIGA_mirrorTileY(src);
	}
	if (xflip) {
		src = AMIGA_mirrorTileX(src);
	}
	for (int y = 0; y < 8; ++y) {
		for (int i = 0; i < 8; ++i) {
			const int mask = 1 << (7 - i);
			int color = 0;
			for (int bit = 0; bit < 4; ++bit) {
				if (src[8 * bit] & mask) {
					color |= 1 << bit;
				}
			}
			if (color != colorKey) {
				dst[i] = pal + color;
			}
		}
		++src;
		dst += pitch;
	}
}

static void PC_drawTile(uint8_t *dst, const uint8_t *src, int mask, const bool xflip, const bool yflip, int colorKey) {
	int pitch = Video::GAMESCREEN_W;
	if (yflip) {
		dst += 7 * pitch;
		pitch = -pitch;
	}
	int inc = 1;
	if (xflip) {
		dst += 7;
		inc = -inc;
	}
	for (int y = 0; y < 8; ++y) {
		for (int i = 0; i < 8; i += 2) {
			int color = *src >> 4;
			if (color != colorKey) {
				dst[inc * i] = mask | color;
			}
			color = *src & 15;
			if (color != colorKey) {
				dst[inc * (i + 1)] = mask | color;
			}
			++src;
		}
		dst += pitch;
	}
}

static void decodeLevHelper(uint8_t *dst, const uint8_t *src, int offset10, int offset12, const uint8_t *a5, bool sgdBuf, bool isPC) {
	if (offset10 != 0) {
		const uint8_t *a0 = src + offset10;
		for (int y = 0; y < 224; y += 8) {
			for (int x = 0; x < 256; x += 8) {
				const int d3 = isPC ? READ_LE_UINT16(a0) : READ_BE_UINT16(a0); a0 += 2;
				const int d0 = d3 & 0x7FF;
				if (d0 != 0) {
					const uint8_t *a2 = a5 + d0 * 32;
					const bool yflip = (d3 & (1 << 12)) != 0;
					const bool xflip = (d3 & (1 << 11)) != 0;
					int mask = 0;
					if ((d3 & 0x8000) != 0) {
						mask = 0x80 + ((d3 >> 6) & 0x10);
					}
					if (isPC) {
						PC_drawTile(dst + y * 256 + x, a2, mask, xflip, yflip, -1);
					} else {
						AMIGA_drawTile(dst + y * 256 + x, 256, a2, mask, xflip, yflip, -1);
					}
				}
			}
		}
	}
	if (offset12 != 0) {
		const uint8_t *a0 = src + offset12;
		for (int y = 0; y < 224; y += 8) {
			for (int x = 0; x < 256; x += 8) {
				const int d3 = isPC ? READ_LE_UINT16(a0) : READ_BE_UINT16(a0); a0 += 2;
				int d0 = d3 & 0x7FF;
				if (d0 != 0 && sgdBuf) {
					d0 -= 896;
				}
				if (d0 != 0) {
					const uint8_t *a2 = a5 + d0 * 32;
					const bool yflip = (d3 & (1 << 12)) != 0;
					const bool xflip = (d3 & (1 << 11)) != 0;
					int mask = 0;
					if ((d3 & 0x6000) != 0 && sgdBuf) {
						mask = 0x10;
					} else if ((d3 & 0x8000) != 0) {
						mask = 0x80 + ((d3 >> 6) & 0x10);
					}
					if (isPC) {
						PC_drawTile(dst + y * 256 + x, a2, mask, xflip, yflip, 0);
					} else {
						AMIGA_drawTile(dst + y * 256 + x, 256, a2, mask, xflip, yflip, 0);
					}
				}
			}
		}
	}
}

void Video::AMIGA_decodeLev(int level, int room) {
	uint8_t *tmp = _res->_memBuf;
	const int offset = READ_BE_UINT32(_res->_lev + room * 4);
	if (!delphine_unpack(tmp, _res->_lev, offset)) {
		error("Bad CRC for level %d room %d", level, room);
	}
	uint16_t offset10 = READ_BE_UINT16(tmp + 10);
	const uint16_t offset12 = READ_BE_UINT16(tmp + 12);
	const uint16_t offset14 = READ_BE_UINT16(tmp + 14);
	static const int kTempMbkSize = 1024;
	uint8_t *buf = (uint8_t *)malloc(kTempMbkSize * 32);
	if (!buf) {
		error("Unable to allocate mbk temporary buffer");
	}
	int sz = 0;
	memset(buf, 0, 32);
	sz += 32;
	const uint8_t *a1 = tmp + offset14;
	for (bool loop = true; loop;) {
		int d0 = READ_BE_UINT16(a1); a1 += 2;
		if (d0 & 0x8000) {
			d0 &= ~0x8000;
			loop = false;
		}
		const int d1 = _res->getBankDataSize(d0);
		const uint8_t *a6 = _res->findBankData(d0);
		if (!a6) {
			a6 = _res->loadBankData(d0);
		}
		const int d3 = *a1++;
		if (d3 == 255) {
			assert(sz + d1 <= kTempMbkSize * 32);
			memcpy(buf + sz, a6, d1);
			sz += d1;
		} else {
			for (int i = 0; i < d3 + 1; ++i) {
				const int d4 = *a1++;
				assert(sz + 32 <= kTempMbkSize * 32);
				memcpy(buf + sz, a6 + d4 * 32, 32);
				sz += 32;
			}
		}
	}
	memset(_frontLayer, 0, _layerSize);
	if (tmp[1] != 0) {
		assert(_res->_sgd);
		decodeSgd(_frontLayer, tmp + offset10, _res->_sgd, _res->isAmiga());
		offset10 = 0;
	}
	decodeLevHelper(_frontLayer, tmp, offset10, offset12, buf, tmp[1] != 0, _res->isDOS());
	free(buf);
	memcpy(_backLayer, _frontLayer, _layerSize);
	_mapPalSlot1 = READ_BE_UINT16(tmp + 2);
	_mapPalSlot2 = READ_BE_UINT16(tmp + 4);
	_mapPalSlot3 = READ_BE_UINT16(tmp + 6);
	_mapPalSlot4 = READ_BE_UINT16(tmp + 8);
	if (_res->isDOS()) {
		// done in ::PC_setLevelPalettes
		return;
	}
	// background
	setPaletteSlotBE(0x0, _mapPalSlot1);
	// objects
	setPaletteSlotBE(0x1, (level == 0) ? _mapPalSlot3 : _mapPalSlot2);
	setPaletteSlotBE(0x2, _mapPalSlot3);
	setPaletteSlotBE(0x3, _mapPalSlot3);
	// conrad
	setPaletteSlotBE(0x4, _mapPalSlot3);
	// foreground
	setPaletteSlotBE(0x8, _mapPalSlot1);
	setPaletteSlotBE(0x9, (level == 0) ? _mapPalSlot1 : _mapPalSlot3);
	// inventory
	setPaletteSlotBE(0xA, _mapPalSlot3);
}

void Video::AMIGA_decodeSpm(const uint8_t *src, uint8_t *dst) {
	uint8_t buf[256 * 32];
	const int size = READ_BE_UINT16(src + 3) & 0x7FFF;
	assert(size <= (int)sizeof(buf));
	AMIGA_decodeRle(buf, src + 3);
	const int w = (src[2] >> 7) + 1;
	const int h = src[2] & 0x7F;
	AMIGA_planar16(dst, w, h, 3, buf);
}

void Video::AMIGA_decodeIcn(const uint8_t *src, int num, uint8_t *dst) {
	for (int i = 0; i < num; ++i) {
		const int h = 1 + *src++;
		const int w = 1 + *src++;
		const int size = w * h * 8;
		src += 4 + size;
	}
	const int h = 1 + *src++;
	const int w = 1 + *src++;
	AMIGA_planar16(dst, w, h, 4, src + 4);
}

void Video::AMIGA_decodeSpc(const uint8_t *src, int w, int h, uint8_t *dst) {
	switch (w) {
	case 8:
		AMIGA_planar8(dst, w, h, src);
		break;
	case 16:
	case 32:
		AMIGA_planar16(dst, w / 16, h, 4, src);
		break;
	case 24:
		AMIGA_planar24(dst, w, h, src);
		break;
	default:
		warning("AMIGA_decodeSpc w=%d unimplemented", w);
		break;
	}
}

void Video::AMIGA_decodeCmp(const uint8_t *src, uint8_t *dst) {
	AMIGA_planar16(dst, 20, 224, 5, src);
}

void Video::drawSpriteSub1(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub1(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i] != 0) {
				dst[i] = src[i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub2(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub2(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i] != 0) {
				dst[i] = src[-i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub3(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub3(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub4(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub4(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[-i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub5(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub5(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i * pitch] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[i * pitch] | colMask;
			}
		}
		++src;
		dst += 256;
	}
}

void Video::drawSpriteSub6(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub6(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i * pitch] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[-i * pitch] | colMask;
			}
		}
		++src;
		dst += 256;
	}
}

void Video::PC_drawChar(uint8_t c, int16_t y, int16_t x) {
	debug(DBG_VIDEO, "Video::PC_drawChar(0x%X, %d, %d)", c, y, x);
	y *= 8;
	x *= 8;
	const uint8_t *src = _res->_fnt + (c - 32) * 32;
	uint8_t *dst = _frontLayer + x + 256 * y;
	for (int h = 0; h < 8; ++h) {
		for (int i = 0; i < 4; ++i, ++src) {
			const uint8_t c1 = *src >> 4;
			if (c1 != 0) {
				if (c1 != 2) {
					*dst = _charFrontColor;
				} else {
					*dst = _charShadowColor;
				}
			} else if (_charTransparentColor != 0xFF) {
				*dst = _charTransparentColor;
			}
			++dst;
			const uint8_t c2 = *src & 15;
			if (c2 != 0) {
				if (c2 != 2) {
					*dst = _charFrontColor;
				} else {
					*dst = _charShadowColor;
				}
			} else if (_charTransparentColor != 0xFF) {
				*dst = _charTransparentColor;
			}
			++dst;
		}
		dst += 256 - 8;
	}
}

void Video::AMIGA_drawStringChar(uint8_t *dst, int pitch, const uint8_t *src, uint8_t color, uint8_t chr) {
	assert(chr >= 32);
	AMIGA_decodeIcn(src, chr - 32, _res->_memBuf);
	src = _res->_memBuf;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			if (src[x] != 0) {
				dst[x] = color;
			}
		}
		src += 16;
		dst += pitch;
	}
}

void Video::PC_drawStringChar(uint8_t *dst, int pitch, const uint8_t *src, uint8_t color, uint8_t chr) {
	assert(chr >= 32);
	src += (chr - 32) * 8 * 4;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 4; ++x) {
			const uint8_t c1 = src[x] >> 4;
			if (c1 != 0) {
				*dst = (c1 == 15) ? color : (0xE0 + c1);
			}
			++dst;
			const uint8_t c2 = src[x] & 15;
			if (c2 != 0) {
				*dst = (c2 == 15) ? color : (0xE0 + c2);
			}
			++dst;
		}
		src += 4;
		dst += pitch - CHAR_W;
	}
}

const char *Video::drawString(const char *str, int16_t x, int16_t y, uint8_t col) {
	debug(DBG_VIDEO, "Video::drawString('%s', %d, %d, 0x%X)", str, x, y, col);
	drawCharFunc dcf = _drawChar;
	int len = 0;
	uint8_t *dst = _frontLayer + y * 256 + x;
	while (1) {
		const uint8_t c = *str++;
		if (c == 0 || c == 0xB || c == 0xA) {
			break;
		}
		(this->*dcf)(dst, 256, _res->_fnt, col, c);
		dst += CHAR_W;
		++len;
	}
	markBlockAsDirty(x, y, len * 8, 8);
	return str - 1;
}

Color Video::AMIGA_convertColor(const uint16_t color, bool bgr) { // 4bits to 8bits
	int r = (color & 0xF00) >> 8;
	int g = (color & 0xF0)  >> 4;
	int b =  color & 0xF;
	if (bgr) {
		SWAP(r, b);
	}
	Color c;
	c.r = (r << 4) | r;
	c.g = (g << 4) | g;
	c.b = (b << 4) | b;
	return c;
}
