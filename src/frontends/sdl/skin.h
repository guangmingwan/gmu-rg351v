/* 
 * Gmu Music Player
 *
 * Copyright (c) 2006-2015 Johannes Heimansberg (wejp.k.vu)
 *
 * File: skin.h  Created: 061107
 *
 * Description: Gmu theme engine
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of
 * the License. See the file COPYING in the Gmu's main directory
 * for details.
 */
#include <SDL2/SDL.h>
#include "textrenderer.h"
#include "gmuwidget.h"

#ifndef _SKIN_H
#define _SKIN_H

typedef struct _Skin
{
	int  version;
	char name[128];

	int  title_scroller_offset_x1;
	int  title_scroller_offset_x2;
	int  title_scroller_offset_y;

	int  symbols_width;
	int  symbols_height;

	int  symbol_play_offset_x;
	int  symbol_play_offset_y;

	int  symbol_pause_offset_x;
	int  symbol_pause_offset_y;

	int  symbol_stereo_offset_x;
	int  symbol_stereo_offset_y;

	int  volume_offset_x;
	int  volume_offset_y;

	int  bitrate_offset_x;
	int  bitrate_offset_y;

	int  frequency_offset_x;
	int  frequency_offset_y;

	int  time_offset_x;
	int  time_offset_y;

	char font_display_name[128];
	int  font_display_char_width;
	int  font_display_char_height;

	char font1_name[128];
	int  font1_char_width;
	int  font1_char_height;

	char font2_name[128];
	int  font2_char_width;
	int  font2_char_height;

	/* data */
	TextRenderer font1, font2, font_display;
	GmuWidget    header, display, lv, footer;
	SDL_Surface *display_symbols, *arrow_up, *arrow_down;

	/* temporary storage */
	SDL_Surface  *buffer;
	SDL_Renderer *renderer;
	SDL_Texture  *tex;
	/* display_mutex is used to make sure the "display" SDL_Surface supplied to the
	   various skin_ functions is only accessed by one function at a time. This 
	   is necessary, since "display" is accessed from the SDL' frontends thread as
	   well as Gmu's main thread (due to SDL2 requiring the actual rendering taking
	   place inside the main thread only on certain platforms). */
	SDL_mutex    *display_mutex;
} Skin;

typedef enum _SkinDisplaySymbol
{
	SYMBOL_NONE, SYMBOL_PLAY, SYMBOL_PAUSE, SYMBOL_STEREO
} SkinDisplaySymbol;

int  skin_init(Skin *skin, const char *skin_file);
int  skin_lock_renderer(Skin *skin);
int  skin_unlock_renderer(Skin *skin);
void skin_set_renderer(Skin *skin, SDL_Renderer *renderer);
void skin_sdl_render(Skin *skin, SDL_Surface *display);
void skin_free(Skin *skin);
int  skin_create_background(Skin *skin);
void skin_update_display(Skin *skin, SDL_Surface *display, SDL_Surface *buffer);
void skin_draw_display_bg(Skin *skin, SDL_Surface *buffer);
void skin_update_header(Skin *skin, SDL_Surface *display, SDL_Surface *buffer);
void skin_draw_header_bg(Skin *skin, SDL_Surface *buffer);
void skin_update_textarea(Skin *skin, SDL_Surface *display, SDL_Surface *buffer);
void skin_draw_textarea_bg(Skin *skin, SDL_Surface *buffer);
void skin_update_footer(Skin *skin, SDL_Surface *display, SDL_Surface *buffer);
void skin_draw_footer_bg(Skin *skin, SDL_Surface *buffer);
void skin_update_bg(const Skin *skin, SDL_Surface *display, SDL_Surface *buffer);

int  skin_textarea_get_number_of_lines(const Skin *skin);
int  skin_textarea_get_characters_per_line(const Skin *skin);
void skin_draw_header_text(const Skin *skin, const char *text, SDL_Surface *target);
void skin_draw_footer_text(const Skin *skin, const char *text, SDL_Surface *target);

void skin_draw_scroll_arrow_up(const Skin *skin, SDL_Surface *target);
void skin_draw_scroll_arrow_down(const Skin *skin, SDL_Surface *target);
void skin_draw_display_symbol(const Skin *skin, SDL_Surface *target, SkinDisplaySymbol symbol);
#endif
