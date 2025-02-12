/*
 * ____ DAPHNE COPYRIGHT NOTICE ____
 *
 * Copyright (C) 2001 Matt Ownby
 *
 * This file is part of DAPHNE, a laserdisc arcade game emulator
 *
 * DAPHNE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DAPHNE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

// video.cpp
// Part of the DAPHNE emulator
// This code started by Matt Ownby, May 2000

#include "../game/game.h"
#include "../io/conout.h"
#include "../io/error.h"
#include "../io/mpo_fileio.h"
#include "../io/mpo_mem.h"
#include "../ldp-out/ldp.h"
#include "palette.h"
#include "video.h"
#include <SDL_syswm.h> // rdg2010
#include <SDL_image.h> // screenshot
#include <plog/Log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string> // for some error messages

// MAC: sdl_video_run thread defines block
#define SDL_VIDEO_RUN_UPDATE_YUV_TEXTURE	1
#define SDL_VIDEO_RUN_CREATE_YUV_TEXTURE	2
#define SDL_VIDEO_RUN_DESTROY_TEXTURE		3
#define SDL_VIDEO_RUN_END_THREAD		4

using namespace std;

namespace video
{
int g_vid_width = 640, g_vid_height = 480; // default video dimensions
unsigned int g_draw_width, g_probe_width = g_vid_width;
unsigned int g_draw_height, g_probe_height = g_vid_height;

#ifdef DEBUG
const Uint16 cg_normalwidths[]  = {320, 640, 800, 1024, 1280, 1280, 1600};
const Uint16 cg_normalheights[] = {240, 480, 600, 768, 960, 1024, 1200};
#else
const Uint16 cg_normalwidths[]  = {640, 800, 1024, 1280, 1280, 1600};
const Uint16 cg_normalheights[] = {480, 600, 768, 960, 1024, 1200};
#endif // DEBUG

// the current game overlay dimensions
unsigned int g_overlay_width = 0, g_overlay_height = 0;

FC_Font *g_font                    = NULL;
FC_Font *g_fixfont                 = NULL;
TTF_Font *g_tfont                  = NULL;
SDL_Surface *g_led_bmps[LED_RANGE] = {0};
SDL_Surface *g_other_bmps[B_EMPTY] = {0};
SDL_Window *g_window               = NULL;
SDL_Window *g_sb_window            = NULL;
SDL_Renderer *g_renderer           = NULL;
SDL_Renderer *g_sb_renderer        = NULL;
SDL_Texture *g_sb_texture          = NULL;
SDL_Texture *g_overlay_texture     = NULL; // The OVERLAY texture, excluding LEDs wich are a special case
SDL_Texture *g_yuv_texture         = NULL; // The YUV video texture, registered from ldp-vldp.cpp
SDL_Surface *g_screen_blitter      = NULL; // The main blitter surface
SDL_Surface *g_leds_surface        = NULL;

SDL_Rect g_overlay_size_rect; 
SDL_Rect g_display_size_rect = {0, 0, g_vid_width, g_vid_height};
SDL_Rect g_leds_size_rect = {0, 0, 320, 240}; 

bool g_LDP1450_overlay = false;

bool queue_take_screenshot = false;

bool g_fs_scale_nearest = false;

bool g_singe_blend_sprite = false;

bool g_scanlines = false;

bool g_fakefullscreen = false;

bool g_vid_resized = false;

bool g_bForceAspectRatio = false;

bool g_fullscreen = false; // whether we should initialize video in fullscreen
                           // mode or not
int g_scalefactor = 100;   // by RDG2010 -- scales the image to this percentage
                           // value (for CRT TVs with overscan problems).
int sboverlay_characterset = 2;

int g_aspect_ratio;


// Move subtitle rendering to SDL_RenderPresent(g_renderer);
bool g_bSubtitleShown = false;
char *subchar;
SDL_Surface *subscreen;

char *LDP1450_069;
char *LDP1450_085;
char *LDP1450_101;
char *LDP1450_104;
char *LDP1450_120;
char *LDP1450_128;
char *LDP1450_136;
char *LDP1450_168;
char *LDP1450_184;
char *LDP1450_200;

// the # of degrees to rotate counter-clockwise in opengl mode
float g_fRotateDegrees = 0.0;

// SDL sdl_video_run thread variables
SDL_Thread *sdl_video_run_thread;
bool sdl_video_run_loop = true;
int sdl_video_run_action = 0;
int sdl_video_run_result = 0;

// SDL sdl_video_run thread function prototypes
void sdl_video_run_rendercopy (SDL_Renderer *renderer, SDL_Texture *texture, SDL_Rect *src, SDL_Rect* dst);

// SDL Texture creation, update and destruction parameters
int yuv_texture_width;
int yuv_texture_height;
SDL_Renderer *sdl_renderer;
SDL_Texture *yuv_texture;
//SDL_Texture *sdl_texture;
void *sdl_run_param;

// SDL YUV texture update parameters
uint8_t *yuv_texture_Yplane;
uint8_t *yuv_texture_Uplane;
uint8_t *yuv_texture_Vplane;
int yuv_texture_Ypitch;
int yuv_texture_Upitch;
int yuv_texture_Vpitch;

// SDL video and texture readyness variables
bool g_bIsSDLDisplayReady = false;

typedef struct {
    uint8_t *Yplane;
    uint8_t *Uplane;
    uint8_t *Vplane;
    int width, height;
    int Ysize, Usize, Vsize; // The size of each plane in bytes.
    int Ypitch, Upitch, Vpitch; // The pitch of each plane in bytes.
    SDL_mutex *mutex;
} g_yuv_surface_t;

g_yuv_surface_t *g_yuv_surface;

// Blitting parameters. What textures need updating from their surfaces during a blitting strike?
bool g_scoreboard_needs_update = false;
bool g_overlay_needs_update    = false;
bool g_yuv_video_needs_update  = false;
bool g_yuv_video_needs_blank   = false;
bool g_ldp1450_old_overlay     = false;

////////////////////////////////////////////////////////////////////////////////////////////////////

// initializes the window in which we will draw our BMP's
// returns true if successful, false if failure
bool init_display()
{
    bool result = false; // whether video initialization is successful or not
    Uint32 sdl_flags = 0;
    Uint32 sdl_sb_flags = 0;
    bool fs = false;
    char title[50] = "HYPSEUS Singe: Multiple Arcade Laserdisc Emulator";


    sdl_flags = SDL_WINDOW_SHOWN;
    sdl_sb_flags = SDL_WINDOW_ALWAYS_ON_TOP;

    // if we were able to initialize the video properly
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) >= 0) {

        if (g_fullscreen) { sdl_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP; fs = true; }
        else if (g_fakefullscreen) sdl_flags |= SDL_WINDOW_MAXIMIZED | SDL_WINDOW_BORDERLESS;

        g_overlay_width = g_game->get_video_overlay_width();
        g_overlay_height = g_game->get_video_overlay_height();

        if (g_vid_resized) {
            g_draw_width  = g_vid_width;
            g_draw_height = g_vid_height;
        } else {
            g_draw_width  = g_probe_width;
            g_draw_height = g_probe_height;
        }

        // Enforce 4:3 aspect ratio
        if (g_bForceAspectRatio) {
            double dCurAspectRatio = (double)g_draw_width / g_draw_height;
            const double dTARGET_ASPECT_RATIO = 4.0 / 3.0;

            if (dCurAspectRatio < dTARGET_ASPECT_RATIO) {
                g_draw_height = (g_draw_width * 3) / 4;
            }
            else if (dCurAspectRatio > dTARGET_ASPECT_RATIO) {
                g_draw_width = (g_draw_height * 4) / 3;
            }
        }

        // if we're supposed to scale the image...
        if (g_scalefactor < 100) {
            g_draw_width  = g_draw_width * g_scalefactor / 100;
            g_draw_height = g_draw_height * g_scalefactor / 100;
        }

        if (g_draw_width < 500) {
            char target[14];
            memcpy(target, title, sizeof(target));
            target[13] = 0;
            memcpy(title, target, sizeof(target));
        }

        if (g_window) SDL_HideWindow(g_window);

	g_window =
            SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             g_draw_width, g_draw_height, sdl_flags);

        if (!g_window) {
            LOGE << fmt("Could not initialize window: %s", SDL_GetError());
            deinit_display();
            shutdown_display();
            SDL_Quit();
        } else {
            if (g_game->m_sdl_software_rendering) {
                g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE |
                                                              SDL_RENDERER_TARGETTEXTURE);
            } else {
                g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED |
                                                              SDL_RENDERER_TARGETTEXTURE |
                                                              SDL_RENDERER_PRESENTVSYNC);
            }

            if (!g_renderer) {
                LOGE << fmt("Could not initialize renderer: %s", SDL_GetError());
                deinit_display();
                shutdown_display();
                SDL_Quit();
                exit(1);
            } else {
                // MAC: If we start in fullscreen mode, we have to set the logical
                // render size to get the desired aspect ratio.
                // Also, we set bilinear filtering
                if ((sdl_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 ||
                    (sdl_flags & SDL_WINDOW_MAXIMIZED) != 0) {
                    if(!g_fs_scale_nearest)
                        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
                    SDL_RenderSetLogicalSize(g_renderer, g_draw_width, g_draw_height);
                }

                if (g_game->m_sdl_software_scoreboard && !fs) {
                    g_sb_window = SDL_CreateWindow(NULL, 4, 28, 340, 480, sdl_sb_flags);

                    if (!g_sb_window) {
                        LOGE << fmt("Could not initialize scoreboard window: %s", SDL_GetError());
                        deinit_display();
                        shutdown_display();
                        SDL_Quit();
                    }

                    if (g_game->m_sdl_software_rendering)
                        g_sb_renderer = SDL_CreateRenderer(g_sb_window, -1, SDL_RENDERER_SOFTWARE |
                                                                      SDL_RENDERER_TARGETTEXTURE);
                    else
                        g_sb_renderer = SDL_CreateRenderer(g_sb_window, -1, SDL_RENDERER_ACCELERATED |
                                                                      SDL_RENDERER_TARGETTEXTURE);
                   if (g_sb_renderer)
                        g_sb_texture = SDL_CreateTexture(g_sb_renderer, SDL_GetWindowPixelFormat(g_sb_window),
                                                        SDL_TEXTUREACCESS_TARGET, 320, 240);
                   else {
                        LOGE << fmt("Could not initialize scoreboard renderer: %s", SDL_GetError());
                        deinit_display();
                        shutdown_display();
                        SDL_Quit();
                        exit(1);
                   }
		}

		// Always hide the mouse cursor
                SDL_ShowCursor(SDL_DISABLE);

                if (g_scanlines)
                    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_MOD);

                // Calculate font sizes
                int ffs;
                int fs = get_draw_width() / 36;
                if (g_aspect_ratio == 0xB1) ffs = get_draw_width() / 24;
                else ffs = get_draw_width() / 18;

                char font[32]="fonts/default.ttf";
                char fixfont[32] = "fonts/timewarp.ttf";
                char ttfont[32];

                if (g_game->get_use_old_overlay()) strncpy(ttfont, "fonts/daphne.ttf", sizeof(ttfont));
                else strncpy(ttfont, "fonts/digital.ttf", sizeof(ttfont));

                g_font = FC_CreateFont();
                FC_LoadFont(g_font, g_renderer, font, fs,
                            FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);

                g_fixfont = FC_CreateFont();
                FC_LoadFont(g_fixfont, g_renderer, fixfont, ffs,
                            FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);

                TTF_Init();
                if (g_game->get_use_old_overlay())
                    g_tfont = TTF_OpenFont(ttfont, 12);
                else
                    g_tfont = TTF_OpenFont(ttfont, 14);

                if (g_tfont == NULL) {
                        LOG_ERROR << fmt("Cannot load TTF font: '%s'", (char*)ttfont);
                        shutdown_display();
                        exit(1);
                }

                // Create a 32-bit surface with alpha component. As big as an overlay can possibly be...
		int surfacebpp;
		Uint32 Rmask, Gmask, Bmask, Amask;              
		SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_RGBA8888, &surfacebpp, &Rmask, &Gmask, &Bmask, &Amask);
		g_screen_blitter =
		    SDL_CreateRGBSurface(SDL_SWSURFACE, g_overlay_width, g_overlay_height,
					surfacebpp, Rmask, Gmask, Bmask, Amask);

		g_leds_surface =
		    SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240,
					surfacebpp, Rmask, Gmask, Bmask, Amask);

                // Convert the LEDs surface to the destination surface format for faster blitting,
                // and set it's color key to NOT copy 0x000000ff pixels.
                // We couldn't do it earlier in load_bmps() because we need the g_screen_blitter format.
                g_other_bmps[B_OVERLAY_LEDS] = SDL_ConvertSurface(g_other_bmps[B_OVERLAY_LEDS],
                                    g_screen_blitter->format, 0);
                SDL_SetColorKey (g_other_bmps[B_OVERLAY_LEDS], 1, 0x000000ff);

                if (g_game->get_use_old_overlay()) {
                    g_other_bmps[B_OVERLAY_LDP1450] = SDL_ConvertSurface(g_other_bmps[B_OVERLAY_LDP1450],
                                    g_screen_blitter->format, 0);
                    SDL_SetColorKey (g_other_bmps[B_OVERLAY_LDP1450], 1, 0x000000ff);

                }

                // MAC: If the game uses an overlay, create a texture for it.
                // The g_screen_blitter surface is used from game.cpp anyway, so we always create it, used or not.
		if (g_overlay_width && g_overlay_height) {
		    g_overlay_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
						 SDL_TEXTUREACCESS_TARGET,
						 g_overlay_width, g_overlay_height);

		    SDL_SetTextureBlendMode(g_overlay_texture, SDL_BLENDMODE_BLEND);
		    SDL_SetTextureAlphaMod(g_overlay_texture, 255);
                }

                SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
                SDL_RenderClear(g_renderer);
                SDL_RenderPresent(g_renderer);
                // NOTE: SDL Console was initialized here.
                result = true;
            }
        }
    } else {
        LOGE << fmt("Could not initialize SDL: %s", SDL_GetError());
        deinit_display();
        shutdown_display();
        SDL_Quit();
        exit(1);
    }

    return (result);
}

void vid_free_yuv_overlay () {
    // Here we free both the YUV surface and YUV texture.
    SDL_DestroyMutex (g_yuv_surface->mutex);
   
    free(g_yuv_surface->Yplane);
    free(g_yuv_surface->Uplane);
    free(g_yuv_surface->Vplane);
    free(g_yuv_surface);

    SDL_DestroyTexture(g_yuv_texture);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// deinitializes the window and renderer we have used.
// returns true if successful, false if failure
bool deinit_display()
{
    SDL_FreeSurface(g_screen_blitter);
    SDL_FreeSurface(g_leds_surface);

    SDL_DestroyTexture(g_overlay_texture);
    SDL_DestroyTexture(g_sb_texture);

    SDL_DestroyRenderer(g_sb_renderer);
    SDL_DestroyRenderer(g_renderer);

    return (true);
}

// shuts down video display
void shutdown_display()
{
    LOGD << "Shutting down video display...";

    TTF_Quit();
    FC_FreeFont(g_font);
    FC_FreeFont(g_fixfont);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// Clear the renderer. Good for avoiding texture mess (YUV, LEDs, Overlay...)
void vid_blank()
{
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
}

// redraws the proper display (Scoreboard, etc) on the screen, after first
// clearing the screen
// call this every time you want the display to return to normal
void display_repaint()
{
    vid_blank();
    //vid_flip();
    g_game->force_blit();
}

// loads all the .bmp's
// returns true if they were all successfully loaded, or a false if they weren't
bool load_bmps()
{

    bool result = true; // assume success unless we hear otherwise
    int index   = 0;
    char filename[81];

    for (; index < LED_RANGE; index++) {
        sprintf(filename, "pics/led%d.bmp", index);

        g_led_bmps[index] = load_one_bmp(filename);

        // If the bit map did not successfully load
        if (g_led_bmps[index] == 0) {
            result = false;
        }
    }

    g_other_bmps[B_DL_PLAYER1]     = load_one_bmp("pics/player1.bmp");
    g_other_bmps[B_DL_PLAYER2]     = load_one_bmp("pics/player2.bmp");
    g_other_bmps[B_DL_LIVES]       = load_one_bmp("pics/lives.bmp");
    g_other_bmps[B_DL_CREDITS]     = load_one_bmp("pics/credits.bmp");
    g_other_bmps[B_HYPSEUS_SAVEME] = load_one_bmp("pics/saveme.bmp");
    g_other_bmps[B_GAMENOWOOK]     = load_one_bmp("pics/gamenowook.bmp");

    if (sboverlay_characterset != 2)
	g_other_bmps[B_OVERLAY_LEDS] = load_one_bmp("pics/overlayleds1.bmp");
    else
	g_other_bmps[B_OVERLAY_LEDS] = load_one_bmp("pics/overlayleds2.bmp");
   
    g_other_bmps[B_OVERLAY_LDP1450] = load_one_bmp("pics/ldp1450font.bmp");

    // check to make sure they all loaded
    for (index = 0; index < B_EMPTY; index++) {
        if (g_other_bmps[index] == NULL) {
            result = false;
        }
    }

    return (result);
}

SDL_Surface *load_one_bmp(const char *filename)
{
    SDL_Surface *result  = SDL_LoadBMP(filename);

    if (!result)
        LOGW << fmt("Could not load bitmap: %s", SDL_GetError());
 
    return (result);
}

// Draw's one of our LED's to the screen
// value contains the bitmap to draw (0-9 is valid)
// x and y contain the coordinates on the screen
// This function is called from img-scoreboard.cpp
// 1 is returned on success, 0 on failure
bool draw_led(int value, int x, int y)
{
    SDL_Surface *srf = g_led_bmps[value];
    SDL_Texture *tx;

    SDL_Rect dest;
    dest.x = (short) x;
    dest.y = (short) y;
    dest.w = (unsigned short) srf->w;
    dest.h = (unsigned short) srf->h;

    tx = SDL_CreateTextureFromSurface(g_sb_renderer, srf);
    SDL_RenderCopy(g_sb_renderer, tx, NULL, &dest);

    return true;
}


// Update scoreboard surface
void draw_overlay_leds(unsigned int values[], int num_digits, int start_x,
                       int y, SDL_Surface *overlay)
{
    SDL_Rect src, dest;

    dest.x = start_x;
    dest.y = y;
    dest.w = OVERLAY_LED_WIDTH;
    dest.h = OVERLAY_LED_HEIGHT;

    src.y = 0;
    src.w = OVERLAY_LED_WIDTH;
    src.h = OVERLAY_LED_HEIGHT;
    
    // Draw the digit(s) to the overlay surface
    for (int i = 0; i < num_digits; i++) {
        src.x = values[i] * OVERLAY_LED_WIDTH;

        // MAC : We need to call SDL_FillRect() here if we don't want our LED characters to "overlap", because
        // we set the g_other_bmps[B_OVERLAY_LEDS] color key in such a way black is not being copied
        // so segments are not clean when we go from 0 to 1, for example.
        // Also, note that SDL_BlitSurface() won't blit the 0x000000ff pixels because we set up a color key
        // using SDL_SetColorKey() in init_display(). See notes there on why we don't do it in load_bmps().
        // If scoreboard transparency problems appear, look there.
        SDL_FillRect(g_leds_surface, &dest, 0x00000000); 
        SDL_BlitSurface(g_other_bmps[B_OVERLAY_LEDS], &src, g_leds_surface, &dest);

        dest.x += OVERLAY_LED_WIDTH;
    }

    g_scoreboard_needs_update = true;

    // MAC: Even if we updated the overlay surface here, there's no need to do not-thread-safe stuff
    // like SDL_UpdateTexture(), SDL_RenderCopy(), etc... until we are going to compose a final frame
    // with the YUV texture and the overlay on top (which is issued from vldp for now) in VIDEO_RUN_BLIT.
}

// Draw LDP1450 overlay characters to the screen - rewrite for SDL2 (DBX)
void draw_charline_LDP1450(char *LDP1450_String, int start_x, int y, SDL_Surface *overlay)
{
    int i, j = 0;
    int LDP1450_strlen;

    LDP1450_strlen = strlen(LDP1450_String);

    if (!LDP1450_strlen)
    {
        strcpy(LDP1450_String, "           ");
        LDP1450_strlen = strlen(LDP1450_String);
    } else {
        if (LDP1450_strlen <= 11)
        {
            for (i = LDP1450_strlen; i <= 11; i++) LDP1450_String[i] = 32;
            LDP1450_strlen = strlen(LDP1450_String);
        }
    }

    char* p = (char*)malloc(LDP1450_strlen+1);
    strcpy(p, LDP1450_String);
    for (i = 0; i < LDP1450_strlen; p++, i++)
       if (*p == 32) j++;

    if (j == 12) draw_LDP1450_overlay(LDP1450_String, 0, 0, 0, 1);
    else draw_LDP1450_overlay(LDP1450_String, start_x, y, 1, 0);
}

void draw_singleline_LDP1450(char *LDP1450_String, int start_x, int y, SDL_Surface *overlay)
{
    SDL_Rect src, dest;

    int i = 0;
    int value = 0;
    int LDP1450_strlen;
    g_ldp1450_old_overlay = true;

    if (g_aspect_ratio == 0x96) start_x = (start_x - (start_x/4));

    dest.x = start_x;
    dest.y = y;
    dest.w = OVERLAY_LDP1450_WIDTH;
    dest.h = OVERLAY_LDP1450_HEIGHT;

    src.y = 0;
    src.w = OVERLAY_LDP1450_WIDTH;
    src.h = OVERLAY_LDP1450_WIDTH;

    LDP1450_strlen = strlen(LDP1450_String);

    if (!LDP1450_strlen)
    {
        strcpy(LDP1450_String,"           ");
        LDP1450_strlen = strlen(LDP1450_String);
    }
    else
    {
        if (LDP1450_strlen <= 11)
        {
            for (i=LDP1450_strlen; i<=11; i++)
                 LDP1450_String[i] = 32;

            LDP1450_strlen = strlen(LDP1450_String);
        }
    }

    for (i=0; i<LDP1450_strlen; i++)
    {
         value = LDP1450_String[i];

         if (value >= 0x26 && value <= 0x39) value -= 0x25;
         else if (value >= 0x41 && value <= 0x5a) value -= 0x2a;
         else if (value == 0x13) value = 0x32;
         else value = 0x31;

         src.x = value * OVERLAY_LDP1450_WIDTH;
         SDL_FillRect(g_leds_surface, &dest, 0x00000000);
         SDL_BlitSurface(g_other_bmps[B_OVERLAY_LDP1450], &src, g_leds_surface, &dest);
         dest.x += OVERLAY_LDP1450_CHARACTER_SPACING;
    }
}

//  used to draw non LED stuff like scoreboard text
//  'which' corresponds to enumerated values
bool draw_othergfx(int which, int x, int y, bool bSendToScreenBlitter)
{
    SDL_Surface *srf = g_other_bmps[which];
    SDL_Texture *tx;

    SDL_Rect dest;
    dest.x = (short)x;
    dest.y = (short)y;
    dest.w = (unsigned short) srf->w;
    dest.h = (unsigned short) srf->h;

    tx = SDL_CreateTextureFromSurface(g_sb_renderer, srf);
    SDL_RenderCopy(g_sb_renderer, tx, NULL, &dest);

    return true;
}

// de-allocates all of the .bmps that we have allocated
void free_bmps()
{

    int nuke_index = 0;

    // get rid of all the LED's
    for (; nuke_index < LED_RANGE; nuke_index++) {
        free_one_bmp(g_led_bmps[nuke_index]);
    }
    for (nuke_index = 0; nuke_index < B_EMPTY; nuke_index++) {
        // check to make sure it exists before we try to free
        if (g_other_bmps[nuke_index]) {
            free_one_bmp(g_other_bmps[nuke_index]);
        }
    }
}

void free_one_bmp(SDL_Surface *candidate) { 
	SDL_FreeSurface(candidate); 
}

void clean_control_char(char *src, char *dst, int len)
{
    for (int i = 0; i < len; src++, i++) {
        if (*src == 0x13) *dst = 0x5F;
        else *dst = *src;
        dst++;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////

SDL_Renderer *get_renderer() { return g_renderer; }

SDL_Texture *get_screen() { return g_overlay_texture; }

SDL_Surface *get_screen_blitter() { return g_screen_blitter; }

SDL_Texture *get_yuv_screen() { return g_yuv_texture; }

SDL_Surface *get_screen_leds() { return g_leds_surface; }

bool get_fullscreen() { return g_fullscreen; }

bool get_force_aspect_ratio() { return g_bForceAspectRatio; }

bool get_singe_blend_sprite() { return g_singe_blend_sprite; }

bool get_use_old_osd() { return g_game->get_use_old_overlay(); }

bool get_LDP1450_enabled() { return g_LDP1450_overlay; }

// sets our g_fullscreen bool (determines whether will be in fullscreen mode or
// not)
void set_fullscreen(bool value) { g_fullscreen = value; }

void set_fakefullscreen(bool value) { g_fakefullscreen = value; }

void set_scanlines(bool value) { g_scanlines = value; }

void set_queue_screenshot(bool value) { queue_take_screenshot = value; }

void set_fullscreen_scale_nearest(bool value) { g_fs_scale_nearest = value; }

void set_singe_blend_sprite(bool value) { g_singe_blend_sprite = value; }

void set_LDP1450_enabled(bool value) { g_LDP1450_overlay = value; }

void set_yuv_video_blank(bool value) { g_yuv_video_needs_blank = value; }

int get_scalefactor() { return g_scalefactor; }
void set_scalefactor(int value)
{
    if (value > 100 || value < 50) // Validating in case user inputs crazy
                                   // values.
    {
        printline("Invalid scale value. Ignoring -scalefactor parameter.");
        g_scalefactor = 100;

    } else {
        g_scalefactor = value;
    }
}

void set_rotate_degrees(float fDegrees) { g_fRotateDegrees = fDegrees; }

void set_sboverlay_characterset(int value) { sboverlay_characterset = value; }

// returns video width
Uint16 get_video_width() { return g_vid_width; }

// sets g_vid_width
void set_video_width(Uint16 width)
{
    // Let the user specify whatever width s/he wants (and suffer the
    // consequences)
    // We need to support arbitrary resolution to accomodate stuff like screen
    // rotation
    g_vid_width = width;
    g_vid_resized = true;
}

// returns video height
Uint16 get_video_height() { return g_vid_height; }

// sets g_vid_height
void set_video_height(Uint16 height)
{
    // Let the user specify whatever height s/he wants (and suffer the
    // consequences)
    // We need to support arbitrary resolution to accomodate stuff like screen
    // rotation
    g_vid_height = height;
    g_vid_resized = true;
}

FC_Font *get_font() { return g_font; }
FC_Font *get_fixfont() { return g_fixfont; }
TTF_Font *get_tfont() { return g_tfont; }

void draw_string(const char *t, int col, int row, SDL_Surface *surface)
{
    SDL_Rect dest;
    dest.y = (short)(row);
    dest.w = (unsigned short)(6 * strlen(t));
    dest.h = 14;

    SDL_Surface *text_surface;

    if (g_game->get_use_old_overlay()) dest.x = (short)((col * 6));
    else dest.x = (short)((col * 5));

    SDL_FillRect(surface, &dest, 0x00000000);
    SDL_Color color={225, 225, 225};
    text_surface=TTF_RenderText_Solid(g_tfont, t, color);

    SDL_BlitSurface(text_surface, NULL, surface, &dest);
    SDL_FreeSurface(text_surface);
}

void draw_subtitle(char *s, SDL_Surface *surface, bool insert)
{
    int x = (int)(get_draw_width() - (get_draw_width() * 0.97));
    int y = (int)(get_draw_height() * 0.92);
    SDL_Renderer *renderer = get_renderer();
    static int count;
    int delay = 100;

    if (insert) {
       count = 0;
       set_subtitle_enabled(true);
       set_subtitle_display(s, surface);
    }

    if ((!insert) && (count > delay)) {
        set_subtitle_enabled(false);
    }

    if (count > 2)
       FC_Draw(get_font(), renderer, x, y, s);

    count++;
}

void draw_LDP1450_overlay(char *s, int start_x, int y, bool insert, bool reset)
{
    SDL_Renderer *renderer = get_renderer();
    FC_Font *fixfont = get_fixfont();
    float f = (get_draw_height()*0.004);
    float x = (double)g_game->get_video_overlay_width() /
               g_game->get_video_overlay_height();
    static float x0, x1, x2, x3, x4, x5, x6, x7, x8, x9;
    static bool y0, y1, y2, y3, y4, y5, y6, y7, y8, y9 = false;
    static int rcount, cr;
    static char *rank;
    int i, k = 0;
    char t[13];

    if (reset && !get_LDP1450_enabled()) return;

    if (reset) {
       rcount++;
       if (rcount > 1) {
          y0 = y1 = y2 = y3 = y4 = y5
             = y6 = y7 = y8 = y9
             = false;
          set_LDP1450_enabled(false);
          rcount = 0;
       }
       return;
    }

    if (insert) {

       if (g_aspect_ratio == 0x96) x = (((double)get_draw_width()/320) * start_x);
       else if (g_aspect_ratio == 0xB1) x = (((double)get_draw_width()/225) * start_x);
       else x = (((double)get_draw_width()/256) * start_x);

       switch(y)
       {
          case 69:
             LDP1450_069 = strdup(s);
             y0 = true; x0 = x;
             break;
          case 85:
             LDP1450_085 = strdup(s);
             y1 = true; x1 = x;
             break;
          case 101:
             LDP1450_101 = strdup(s);
             y2 = true; x2 = x;
             break;
          case 103:
          case 104:
             clean_control_char(s, t, sizeof(t));
             LDP1450_104 = strdup(t);
             for (i = 0; i < (int)sizeof(s); s++, i++)
               if (*s != 32) k++;
             if (k==3) {
                if (cr == 1) rank = strdup(LDP1450_104);
                else if (rank) LDP1450_104 = strdup(rank);
                cr++;
                if (cr>2) cr = 0;
             }
             else cr = 0;
             y3 = true; x3 = x;
             break;
          case 119:
          case 120:
             LDP1450_120 = strdup(s);
             y4 = true; x4 = x;
             break;
          case 128:
             LDP1450_128 = strdup(s);
             y5 = true; x5 = x;
             break;
          case 135:
          case 136:
             LDP1450_136 = strdup(s);
             y6 = true; x6 = x;
             break;
          case 168:
             LDP1450_168 = strdup(s);
             y7 = true; x7 = x;
             break;
          case 184:
             LDP1450_184 = strdup(s);
             y8 = true; x8 = x;
             cr = 0;
             break;
          case 200:
             LDP1450_200 = strdup(s);
             y9 = true; x9 = x;
             cr = 0;
             break;
       }
       rcount = 0;
       set_LDP1450_enabled(true);
    }

    if (get_LDP1450_enabled()) {
       if (y0) FC_Draw(fixfont, renderer, x0, 69*f,  LDP1450_069);
       if (y1) FC_Draw(fixfont, renderer, x1, 85*f,  LDP1450_085);
       if (y2) FC_Draw(fixfont, renderer, x2, 101*f, LDP1450_101);
       if (y3) FC_Draw(fixfont, renderer, x3, 104*f, LDP1450_104);
       if (y4) FC_Draw(fixfont, renderer, x4, 120*f, LDP1450_120);
       if (y5) FC_Draw(fixfont, renderer, x5, 128*f, LDP1450_128);
       if (y6) FC_Draw(fixfont, renderer, x6, 136*f, LDP1450_136);
       if (y7) FC_Draw(fixfont, renderer, x7, 168*f, LDP1450_168);
       if (y8) FC_Draw(fixfont, renderer, x8, 184*f, LDP1450_184);
       if (y9) FC_Draw(fixfont, renderer, x9, 200*f, LDP1450_200);
    }

    if (y3 && (k==0x2||k==0x3)) SDL_RenderPresent(renderer);
}

// toggles fullscreen mode
void vid_toggle_fullscreen()
{
    Uint32 flags = (SDL_GetWindowFlags(g_window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (SDL_SetWindowFullscreen(g_window, flags) < 0) {
        LOGW << fmt("Toggle fullscreen failed: %s", SDL_GetError());
        return;
    }
    if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SDL_RenderSetLogicalSize(g_renderer, g_draw_width, g_draw_height);
        return;
    }
    SDL_SetWindowSize(g_window, g_draw_width, g_draw_height);
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
}

void vid_toggle_scanlines()
{
    SDL_BlendMode mode;
    SDL_GetRenderDrawBlendMode(g_renderer, &mode);
    if (mode != SDL_BLENDMODE_MOD)
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_MOD);

    if (g_scanlines) g_scanlines = false;
    else g_scanlines = true;
}

void set_subtitle_enabled(bool bEnabled) { g_bSubtitleShown = bEnabled; }
void set_subtitle_display(char *s, SDL_Surface *surface) { subchar = strdup(s); subscreen = surface; }
void set_force_aspect_ratio(bool bEnabled) { g_bForceAspectRatio = bEnabled; }
void set_aspect_ratio(int fRatio) { g_aspect_ratio = fRatio; }
void set_detected_height(int pHeight) { g_probe_height = pHeight; }
void set_detected_width(int pWidth) { g_probe_width = pWidth; }
unsigned int get_draw_width() { return g_draw_width; }
unsigned int get_draw_height() { return g_draw_height; }

void vid_setup_yuv_overlay (int width, int height) {
    // Prepare the YUV overlay, wich means setting up both the YUV surface and YUV texture.

    // If we have already been here, free things first.
    if (g_yuv_surface) {
        // Free both the YUV surface and YUV texture.
        vid_free_yuv_overlay();
    }

    g_yuv_surface = (g_yuv_surface_t*) malloc (sizeof(g_yuv_surface_t));

    // 12 bits (1 + 0.5 bytes) per pixel, and each plane has different size. Crazy stuff.
    g_yuv_surface->Ysize = width * height;
    g_yuv_surface->Usize = g_yuv_surface->Ysize / 4;
    g_yuv_surface->Vsize = g_yuv_surface->Ysize / 4;
   
    g_yuv_surface->Yplane = (uint8_t*) malloc (g_yuv_surface->Ysize);
    g_yuv_surface->Uplane = (uint8_t*) malloc (g_yuv_surface->Usize);
    g_yuv_surface->Vplane = (uint8_t*) malloc (g_yuv_surface->Vsize);

    g_yuv_surface->width  = width;
    g_yuv_surface->height = height;

    // Setup the threaded access stuff, since this surface is accessed from the vldp thread, too.
    g_yuv_surface->mutex = SDL_CreateMutex();
}

SDL_Texture *vid_create_yuv_texture (int width, int height) {
    g_yuv_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_TARGET, width, height);
    vid_blank_yuv_texture(true);
    return g_yuv_texture;
}

void vid_blank_yuv_texture (bool s) {

    // Black: YUV#108080, YUV(16,0,0)
    memset(g_yuv_surface->Yplane, 0x10, g_yuv_surface->Ysize);
    memset(g_yuv_surface->Uplane, 0x80, g_yuv_surface->Usize);
    memset(g_yuv_surface->Vplane, 0x80, g_yuv_surface->Vsize);

    if (s) SDL_UpdateYUVTexture(g_yuv_texture, NULL,
	    g_yuv_surface->Yplane, g_yuv_surface->width,
            g_yuv_surface->Uplane, g_yuv_surface->width/2,
            g_yuv_surface->Vplane, g_yuv_surface->width/2);
}

// REMEMBER it updates the YUV surface ONLY: the YUV texture is updated on vid_blit().
int vid_update_yuv_overlay ( uint8_t *Yplane, uint8_t *Uplane, uint8_t *Vplane,
	int Ypitch, int Upitch, int Vpitch)
{
    // This function is called from the vldp thread, so access to the
    // yuv surface is protected (mutexed).
    // As a reminder, mutexes are very simple: this fn tries to lock(=get)
    // the mutex, but if it's already taken, LockMutex doesn't return
    // until the mutex is free and we can lock(=get) it here.
    SDL_LockMutex(g_yuv_surface->mutex);

    if (g_yuv_video_needs_blank) {

        vid_blank_yuv_texture(false);
        set_yuv_video_blank(false);

    } else {

        memcpy (g_yuv_surface->Yplane, Yplane, g_yuv_surface->Ysize);
        memcpy (g_yuv_surface->Uplane, Uplane, g_yuv_surface->Usize);
        memcpy (g_yuv_surface->Vplane, Vplane, g_yuv_surface->Vsize);

        g_yuv_surface->Ypitch = Ypitch;
        g_yuv_surface->Upitch = Upitch;
        g_yuv_surface->Vpitch = Vpitch;
    }

    g_yuv_video_needs_update = true;

    SDL_UnlockMutex(g_yuv_surface->mutex);

    return 0;
}

void vid_update_overlay_surface (SDL_Surface *tx, int x, int y) {
    // We have got here from game::blit(), which is also called when scoreboard is updated,
    // so in that case we simply return and don't do any overlay surface update. 
    if (g_scoreboard_needs_update || g_ldp1450_old_overlay) {
        return;
    }
    
    // Remember: tx is m_video_overlay[] passed from game::blit() 
    // Careful not comment this part on testing, because this rect is used in vid_blit!
    g_overlay_size_rect.x = (short)x;
    g_overlay_size_rect.y = (short)y;
    g_overlay_size_rect.w = tx->w;
    g_overlay_size_rect.h = tx->h;

    // MAC: 8bpp to RGBA8888 conversion. Black pixels are considered totally transparent so they become 0x00000000;
    for (int i = 0; i < (tx->w * tx->h); i++) {
	    *((uint32_t*)(g_screen_blitter->pixels)+i) =
	    (0x00000000 | tx->format->palette->colors[*((uint8_t*)(tx->pixels)+i)].r) << 24|
	    (0x00000000 | tx->format->palette->colors[*((uint8_t*)(tx->pixels)+i)].g) << 16|
	    (0x00000000 | tx->format->palette->colors[*((uint8_t*)(tx->pixels)+i)].b) << 8|
	    (0x00000000 | tx->format->palette->colors[*((uint8_t*)(tx->pixels)+i)].a);
    }

    g_overlay_needs_update = true;
    // MAC: We update the overlay texture later, just when we are going to SDL_RenderCopy() it to the renderer.
    // SDL_UpdateTexture(g_overlay_texture, &g_overlay_size_rect, (void *)g_screen_blitter->pixels, g_screen_blitter->pitch);
}

void vid_blit () {
    // *IF* we get to SDL_VIDEO_BLIT from game::blit(), then the access to the
    // overlay and scoreboard textures is done from the "hypseus" thread, that blocks
    // until all blitting operations are completed and only then loops again, so NO
    // need to protect the access to these surfaces or their needs_update booleans.
    // However, since we get here from game::blit(), the yuv "surface" is accessed
    // simultaneously from the vldp thread and from here, the main thread (to update
    // the YUV texture from the YUV surface), so access to that surface and it's
    // boolean DO need to be protected with a mutex.


    // First clear the renderer before the SDL_RenderCopy() calls for this frame.
    // Prevents stroboscopic effects on the background in fullscreen mode,
    // and is recommended by SDL_Rendercopy() documentation.
    SDL_RenderClear(g_renderer);

    // Does YUV texture need update from the YUV "surface"?
    // Don't try if the vldp object didn't call setup_yuv_surface (in noldp mode)
    if (g_yuv_surface) {
	SDL_LockMutex(g_yuv_surface->mutex);
	if (g_yuv_video_needs_update) {
	    // If we don't have a YUV texture yet (we may be here for the first time or the vldp could have
	    // ordered it's destruction in the mpeg_callback function because video dimensions have changed),
	    // create it now. Dimensions were passed to the video object (this) by the vldp object earlier,
	    // using vid_setup_yuv_texture()
	    if (!g_yuv_texture) {
		g_yuv_texture = vid_create_yuv_texture(g_yuv_surface->width, g_yuv_surface->height);
	    }

	    SDL_UpdateYUVTexture(g_yuv_texture, NULL,
		g_yuv_surface->Yplane, g_yuv_surface->Ypitch,
		g_yuv_surface->Uplane, g_yuv_surface->Vpitch,
		g_yuv_surface->Vplane, g_yuv_surface->Vpitch);
	    g_yuv_video_needs_update = false;
	}
	SDL_UnlockMutex(g_yuv_surface->mutex);
    }

    // Does OVERLAY texture need update from the scoreboard surface?
    if(g_scoreboard_needs_update) {
	SDL_UpdateTexture(g_overlay_texture, &g_leds_size_rect,
	    (void *)g_leds_surface->pixels, g_leds_surface->pitch);
	g_scoreboard_needs_update = false;
    }

    // Does OVERLAY texture need update from the overlay surface?
    if(g_overlay_needs_update) {
	SDL_UpdateTexture(g_overlay_texture, &g_overlay_size_rect,
	    (void *)g_screen_blitter->pixels, g_screen_blitter->pitch);
	g_overlay_needs_update = false;
    }

    // Sadly, we have to RenderCopy the YUV texture on every blitting strike, because
    // the image on the renderer gets "dirty" with previous overlay frames on top of the yuv.
    if(g_yuv_texture) {
        SDL_RenderCopy(g_renderer, g_yuv_texture, NULL, NULL); 
    }

    // If there's an overlay texture, it means we are using some kind of overlay,
    // be it LEDs or any other thing, so RenderCopy it to the renderer ON TOP of the YUV video.
    // ONLY a rect of the LEDs surface size is copied for now.
    if(g_overlay_texture) {
	SDL_RenderCopy(g_renderer, g_overlay_texture, &g_leds_size_rect, NULL);
    }

    // If there's a subtitle overlay
    if (g_bSubtitleShown) draw_subtitle(subchar, subscreen, 0);

    // LDP1450 overlays
    if (g_ldp1450_old_overlay) SDL_UpdateTexture(g_overlay_texture, &g_leds_size_rect,
                 (void *)g_leds_surface->pixels, g_leds_surface->pitch);
    else if (get_LDP1450_enabled()) draw_LDP1450_overlay(NULL, 0, 0, 0, 0);

    if (g_scanlines) draw_scanlines();

    SDL_RenderPresent(g_renderer);

    if (g_sb_renderer) SDL_RenderPresent(g_sb_renderer);

    if (queue_take_screenshot) {
        set_queue_screenshot(false);
        take_screenshot();
    }
}

int get_yuv_overlay_width() {
    if (g_yuv_surface) {
        return g_yuv_surface->width;
    }
    else return 0; 
}

int get_yuv_overlay_height() {
    if (g_yuv_surface) {
        return g_yuv_surface->height;
    }
    else return 0; 
}

bool get_yuv_overlay_ready() {
    if (g_yuv_surface && g_yuv_texture) return true;
    else return false;
}

void take_screenshot()
{
    struct       stat info;
    char         filename[64];
    int32_t      screenshot_num = 0;
    const char   dir[12] = "screenshots";

    if (stat(dir, &info ) != 0 )
        { LOGW << fmt("'%s' directory does not exist.", dir); return; }
    else if (!(info.st_mode & S_IFDIR))
        { LOGW << fmt("'%s' is not a directory.", dir); return; }

    int flags = SDL_GetWindowFlags(g_window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP || flags & SDL_WINDOW_MAXIMIZED)
        { LOGW << "Cannot screenshot in fullscreen render."; return; }

    SDL_Rect     screenshot;
    SDL_Renderer *g_renderer   = get_renderer();
    SDL_Surface  *surface      = NULL;

    if (g_renderer) {
        SDL_RenderGetViewport(g_renderer, &screenshot);
        surface = SDL_CreateRGBSurface(0, screenshot.w, screenshot.h, 32, 0, 0, 0, 0);
        if (!surface) { LOGE << "Cannot allocate surface"; return; }
        if (SDL_RenderReadPixels(g_renderer, NULL, surface->format->format,
            surface->pixels, surface->pitch) != 0)
            { LOGE << fmt("Cannot ReadPixels - Something bad happened: %s", SDL_GetError());
                 deinit_display();
                 shutdown_display();
                 SDL_Quit();
                 exit(1); }
    } else {
        LOGE << "Could not allocate renderer";
        return;
    }

    for (;;) {
        screenshot_num++;
        sprintf(filename, "%s%shypseus-%d.png",
          dir, PATH_SEPARATOR, screenshot_num);

        if (!mpo_file_exists(filename))
            break;
    }

    if (IMG_SavePNG(surface, filename) == 0) {
        LOGI << fmt("Wrote screenshot: %s", filename);
    } else {
        LOGE <<  fmt("Could not write screenshot: %s !!", filename);
    }

    SDL_FreeSurface(surface);
}

void draw_scanlines() {
    unsigned char c;
    for (unsigned int i = 0; i < g_draw_height; i+=5) {
         c = 0x40;
         for (int j = 0; j < 4; j++) {
             SDL_SetRenderDrawColor(g_renderer, c, c, c, SDL_ALPHA_OPAQUE);
             SDL_RenderDrawLine(g_renderer, 0, i+j, g_draw_width, i+j);
             switch(j)
             {
                case 0:
                  c = 0x90;
                  break;
                case 1:
                  c = 0xB0;
                  break;
                default:
                  c = 0xD0;
                  break;
            }
        }
    }
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
}

}
