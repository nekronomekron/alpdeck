#include "core/lua/DisplayApi.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/Org_01.h>

#include "core/lua/LuaApi.h"
#include "peripherals/Display.h"

namespace DisplayApi {
namespace {

// Ink for every drawing call until the next colour change or begin().
uint16_t ink = Display::kBlack;

struct NamedFont {
    const char* name;
    const GFXfont* font;  // nullptr = the built-in 6x8 face
};

const NamedFont kFonts[] = {
    {"default", nullptr},
    {"sans", &FreeSans9pt7b},
    {"bold", &FreeSansBold9pt7b},
    {"pixel", &Org_01},
};

// Drawing calls open a frame implicitly, so a script may just start drawing.
Adafruit_GFX& canvas() {
    if (!Display::frameOpen()) {
        Display::beginFrame();
    }
    return Display::canvas();
}

// Colour argument: "white" or "black". Anything else -- including a missing
// argument -- means black, which is the only sane default on white paper.
uint16_t colorArg(lua_State* L, int index) {
    const char* name = lua_tostring(L, index);
    if (name != nullptr && strcmp(name, "white") == 0) {
        return Display::kWhite;
    }
    return Display::kBlack;
}

uint8_t sizeArg(lua_State* L, int index) {
    // Clamped: size 0 renders nothing and a negative value would wrap huge.
    const lua_Integer raw = luaL_optinteger(L, index, 1);
    return raw < 1 ? 1 : (raw > 8 ? 8 : static_cast<uint8_t>(raw));
}

// Measures a string and reports how far below the cursor its top edge sits.
//
// Adafruit_GFX positions a custom font by its baseline and the built-in font
// by its top-left. Correcting for that here is what lets an app change fonts
// without every y coordinate in its layout moving.
void textMetrics(Adafruit_GFX& gfx, const char* text, uint8_t size,
                 int16_t& width, int16_t& height, int16_t& topOffset) {
    gfx.setTextSize(size);

    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsW = 0;
    uint16_t boundsH = 0;
    gfx.getTextBounds(text, 0, 0, &boundsX, &boundsY, &boundsW, &boundsH);

    width = static_cast<int16_t>(boundsW);
    height = static_cast<int16_t>(boundsH);
    topOffset = boundsY;  // negative for baseline-positioned fonts
}

// begin([mode] [, x, y, w, h])
//
// The refresh mode is fixed when the frame opens (setFullWindow vs
// setPartialWindow), so it cannot be chosen at show() time. A region is always
// a partial refresh: a full refresh drives the whole panel by nature.
int begin(lua_State* L) {
    const char* mode = lua_tostring(L, 1);
    const bool full = mode != nullptr && strcmp(mode, "full") == 0;

    if (!Display::frameOpen()) {
        if (lua_gettop(L) >= 5) {
            Display::beginFrame(luaL_checkinteger(L, 2), luaL_checkinteger(L, 3),
                                luaL_checkinteger(L, 4),
                                luaL_checkinteger(L, 5));
        } else {
            Display::beginFrame(full ? Display::RefreshMode::Full
                                     : Display::RefreshMode::Partial);
        }
    }

    // A new frame starts from a known state, so an app that changed the ink or
    // the font last frame does not inherit it.
    ink = Display::kBlack;
    Display::canvas().setFont(nullptr);
    return 0;
}

// show() -- pushes the frame to the panel. Takes no mode: that was decided by
// begin(). Opening a second frame here would only render a blank one, because
// GxEPD2's firstPage() whitens the buffer.
int show(lua_State* L) {
    (void)L;
    Display::endFrame();  // no-op when nothing was drawn
    return 0;
}

// power_down() -- hibernate the panel now rather than letting the main loop do
// it a couple of seconds after the drawing stops.
//
// Two uses, and neither is "after every frame". Measuring what waking the panel
// costs, by putting it down before a frame and timing that frame against one
// drawn on a panel that was still awake. And an app that has drawn its last
// screen and would rather hand back with the panel already down.
//
// Calling it between ordinary frames simply puts back the 102ms power-down and
// the reset that deferring it exists to avoid.
int powerDown(lua_State* L) {
    (void)L;
    Display::powerDown();
    return 0;
}

int size(lua_State* L) {
    lua_pushinteger(L, Display::width());
    lua_pushinteger(L, Display::height());
    return 2;
}

// timing() -> refreshMs, powerDownMs
//
// Two numbers rather than one because they have different cures: a slow
// refresh is answered with a smaller region or fewer frames, a slow power-down
// is the panel being hibernated after every frame and nothing an app can do
// about it from up here.
int timing(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(Display::lastRefreshMs()));
    lua_pushinteger(L, static_cast<lua_Integer>(Display::lastPowerDownMs()));
    return 2;
}

int color(lua_State* L) {
    ink = colorArg(L, 1);
    return 0;
}

int font(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "default");
    for (const NamedFont& candidate : kFonts) {
        if (strcmp(candidate.name, name) == 0) {
            canvas().setFont(candidate.font);
            return 0;
        }
    }
    return luaL_error(L, "unknown font '%s'", name);
}

int clear(lua_State* L) {
    (void)L;
    canvas().fillScreen(ink);
    return 0;
}

// text(x, y, s [, size]) -- x,y is the TOP-LEFT corner, for every font.
int text(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const char* value = luaL_checkstring(L, 3);
    const uint8_t size = sizeArg(L, 4);

    Adafruit_GFX& gfx = canvas();

    int16_t width = 0;
    int16_t height = 0;
    int16_t topOffset = 0;
    textMetrics(gfx, value, size, width, height, topOffset);

    gfx.setTextColor(ink);
    gfx.setCursor(x, y - topOffset);
    gfx.print(value);
    return 0;
}

// measure(s [, size]) -> width, height of the box text() will fill.
int measure(lua_State* L) {
    const char* value = luaL_checkstring(L, 1);
    const uint8_t size = sizeArg(L, 2);

    int16_t width = 0;
    int16_t height = 0;
    int16_t topOffset = 0;
    textMetrics(canvas(), value, size, width, height, topOffset);

    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

int pixel(lua_State* L) {
    canvas().drawPixel(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2), ink);
    return 0;
}

int line(lua_State* L) {
    canvas().drawLine(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                      luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), ink);
    return 0;
}

int rect(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const int16_t w = luaL_checkinteger(L, 3);
    const int16_t h = luaL_checkinteger(L, 4);

    if (lua_toboolean(L, 5)) {
        canvas().fillRect(x, y, w, h, ink);
    } else {
        canvas().drawRect(x, y, w, h, ink);
    }
    return 0;
}

// circle(cx, cy, r [, fill]) -- cx,cy is the centre, not a corner.
int circle(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const int16_t r = luaL_checkinteger(L, 3);

    if (lua_toboolean(L, 4)) {
        canvas().fillCircle(x, y, r, ink);
    } else {
        canvas().drawCircle(x, y, r, ink);
    }
    return 0;
}

int roundrect(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const int16_t w = luaL_checkinteger(L, 3);
    const int16_t h = luaL_checkinteger(L, 4);
    const int16_t r = luaL_checkinteger(L, 5);

    if (lua_toboolean(L, 6)) {
        canvas().fillRoundRect(x, y, w, h, r, ink);
    } else {
        canvas().drawRoundRect(x, y, w, h, r, ink);
    }
    return 0;
}

int triangle(lua_State* L) {
    const int16_t x0 = luaL_checkinteger(L, 1);
    const int16_t y0 = luaL_checkinteger(L, 2);
    const int16_t x1 = luaL_checkinteger(L, 3);
    const int16_t y1 = luaL_checkinteger(L, 4);
    const int16_t x2 = luaL_checkinteger(L, 5);
    const int16_t y2 = luaL_checkinteger(L, 6);

    if (lua_toboolean(L, 7)) {
        canvas().fillTriangle(x0, y0, x1, y1, x2, y2, ink);
    } else {
        canvas().drawTriangle(x0, y0, x1, y1, x2, y2, ink);
    }
    return 0;
}

// bitmap(x, y, w, h, data [, background])
//
// data is 1bpp, rows padded to whole bytes, MSB leftmost -- exactly what
// Adafruit_GFX wants. The length check is not optional: drawBitmap indexes the
// buffer from w and h alone, so a short string would read past its end.
int bitmap(lua_State* L) {
    const int16_t x = luaL_checkinteger(L, 1);
    const int16_t y = luaL_checkinteger(L, 2);
    const int16_t w = luaL_checkinteger(L, 3);
    const int16_t h = luaL_checkinteger(L, 4);

    size_t length = 0;
    const char* data = luaL_checklstring(L, 5, &length);

    if (w <= 0 || h <= 0) {
        return luaL_error(L, "bitmap size must be positive, got %dx%d",
                          static_cast<int>(w), static_cast<int>(h));
    }

    const size_t expected = static_cast<size_t>((w + 7) / 8) * h;
    if (length != expected) {
        return luaL_error(L,
                          "bitmap data is %d bytes, %dx%d needs %d",
                          static_cast<int>(length), static_cast<int>(w),
                          static_cast<int>(h), static_cast<int>(expected));
    }

    const uint8_t* bits = reinterpret_cast<const uint8_t*>(data);
    if (lua_isnoneornil(L, 6)) {
        // Transparent: only set bits are painted, which is what a sprite over
        // existing artwork needs.
        canvas().drawBitmap(x, y, bits, w, h, ink);
    } else {
        canvas().drawBitmap(x, y, bits, w, h, ink, colorArg(L, 6));
    }
    return 0;
}

const luaL_Reg kFunctions[] = {
    {"begin", begin},         {"show", show},
    {"size", size},           {"timing", timing},
    {"power_down", powerDown},
    {"color", color},         {"font", font},
    {"clear", clear},         {"text", text},
    {"measure", measure},
    {"pixel", pixel},         {"line", line},
    {"rect", rect},           {"circle", circle},
    {"roundrect", roundrect}, {"triangle", triangle},
    {"bitmap", bitmap},       {nullptr, nullptr},
};

}  // namespace

void install(lua_State* L) {
    ink = Display::kBlack;
    LuaApi::installTable(L, "display", kFunctions);
}

}  // namespace DisplayApi
