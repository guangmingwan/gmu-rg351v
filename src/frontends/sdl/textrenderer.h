/* 
 * Gmu Music Player
 *
 * Copyright (c) 2006-2012 Johannes Heimansberg (wejp.k.vu)
 *
 * File: textrenderer.h  Created: 060929
 *
 * Description: Bitmap font renderer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of
 * the License. See the file COPYING in the Gmu's main directory
 * for details.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "charset.h"

#ifndef GMU_TEXTRENDERER_H
#define GMU_TEXTRENDERER_H
struct _TextRenderer
{
	SDL_Surface *chars;
	int chwidth, chheight;
    Uint32 color;
};

typedef struct _TextRenderer TextRenderer;

typedef enum { RENDER_DEFAULT, RENDER_CROP, RENDER_ARROW } Render_Mode;

int  textrenderer_init(TextRenderer *tr, char *chars_file,int chwidth, int chheight,int typeOfColor);
void textrenderer_free(TextRenderer *tr);

Uint32 getpixel(SDL_Surface *surface, int x, int y);
void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel);

void DrawOnePoint(SDL_Surface *screen, int x, int y, const TextRenderer *tr);
void textrenderer_draw_cjk_char(const TextRenderer *tr,Uint16 ch, SDL_Surface *screen, int ShowX, int ShowY);

void textrenderer_draw_asc_char(const TextRenderer *tr, UCodePoint ch, SDL_Surface *target, 
                            int target_x, int target_y);
void textrenderer_draw_string_codepoints(const TextRenderer *tr, const UCodePoint *str, 
                                         int str_len, SDL_Surface *target, 
                                         int target_x, int target_y);
void textrenderer_draw_string(const TextRenderer *tr, const char *str, SDL_Surface *target, 
                              int target_x, int target_y);
void textrenderer_draw_string_with_highlight(const TextRenderer *tr1, const TextRenderer *tr2,
                                             const char *str, int str_offset,
                                             SDL_Surface *target, int target_x, int target_y,
                                             int max_length, Render_Mode rm);
int  textrenderer_get_string_length(const char *str);
#endif
