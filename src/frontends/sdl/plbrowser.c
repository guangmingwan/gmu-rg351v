/* 
 * Gmu Music Player
 *
 * Copyright (c) 2006-2013 Johannes Heimansberg (wejp.k.vu)
 *
 * File: plbrowser.c  Created: 061025
 *
 * Description: The Gmu playlist browser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of
 * the License. See the file COPYING in the Gmu's main directory
 * for details.
 */
#include <unistd.h>
#include <string.h>
#include "textrenderer.h"
#include "core.h"
#include "plbrowser.h"
#include "skin.h"
#include "debug.h"

void pl_browser_init(PlaylistBrowser *pb, const Skin *skin, Charset filenames_charset)
{
	pb->skin = skin;
	pb->offset = 0;
	pb->horiz_offset = 0;
	pb->selection = 0;
	pb->first_visible_item = 0;
	pb->filenames_charset = filenames_charset;
	pb->longest_line_so_far = 0;
}

void pl_browser_playlist_clear(PlaylistBrowser *pb)
{
	pb->selection = 0;
	pb->offset = 0;
	pb->horiz_offset = 0;
	gmu_core_playlist_clear();
	pb->first_visible_item = -1;
}

int pl_browser_are_selection_and_current_entry_equal(PlaylistBrowser *pb)
{
	int    i = 0, result = 0;
	Entry *entry;

	if (gmu_core_playlist_get_length() > 0) {
		gmu_core_playlist_acquire_lock();
		entry = gmu_core_playlist_get_first();
		while (entry != NULL && i != pl_browser_get_selection(pb)) {
			entry = gmu_core_playlist_get_next(entry);
			i++;
		}
		if (entry == gmu_core_playlist_get_current())
			result = 1;
		gmu_core_playlist_release_lock();
	}
	return result;
}

Entry *pl_browser_get_selected_entry(PlaylistBrowser *pb)
{
	Entry *sel = gmu_core_playlist_get_first();
	int    i = 0;
	while (sel != NULL && i != pl_browser_get_selection(pb)) {
		sel = gmu_core_playlist_get_next(sel);
		i++;
	}
	return sel;
}

int pl_browser_playlist_remove_selection(PlaylistBrowser *pb)
{
	int result = 0;
	int pl_length = gmu_core_playlist_get_length();

	if (pl_length > 0) {
		int item = pl_browser_get_selection(pb);
		gmu_core_playlist_item_delete(item);
		if (pb->first_visible_item == item)
			pb->first_visible_item = item+1 < pl_length ? item+1 : (item-1 > 0 ? item-1 : 0);
		if (pb->selection > pl_length - 1)
			pb->selection = (pl_length - 1 >= 0 ? pl_length - 1 : 0);
	}
	return result;
}

void pl_browser_draw(PlaylistBrowser *pb, SDL_Surface *sdl_target)
{
	int    i = 0;
	char   buf[64];
	Entry *pl_entry;
	int    number_of_visible_lines = skin_textarea_get_number_of_lines(pb->skin);
	int    len = (skin_textarea_get_characters_per_line(pb->skin) > 63 ? 
	              63 : skin_textarea_get_characters_per_line(pb->skin));
	int    selected_entry_drawn = 0;
	char  *mode;
	int    pl_length;

	switch (gmu_core_playlist_get_play_mode()) {
		default:
		case PM_CONTINUE:
			mode = "顺序播放";
			break;
		case PM_REPEAT_ALL:
			mode = "循环所有";
			break;
		case PM_REPEAT_1:
			mode = "单曲循环";
			break;
		case PM_RANDOM:
			mode = "随机";
			break;
		case PM_RANDOM_REPEAT:
			mode = "随机+循环";
			break;
	}

	pl_length = gmu_core_playlist_get_length();
	snprintf(buf, 63, "播放清单 (%d %s, 模式: %s)", pl_length,
	         pl_length != 1 ? "条目" : "条目", mode);
	skin_draw_header_text(pb->skin, buf, sdl_target);

	if (pb->first_visible_item == -1 || pb->offset == 0) {
		if (pl_length > 0)
			pb->first_visible_item = 0;
		else
			pb->first_visible_item = -1;
	}

	pb->longest_line_so_far = 0;
	gmu_core_playlist_acquire_lock();
	pl_entry = gmu_core_playlist_get_entry(pb->first_visible_item);
	for (i = pb->offset; 
	     i < pb->offset + number_of_visible_lines && 
	     i < pl_length && pl_entry != NULL;
	     i++) {
		char          c = (gmu_core_playlist_get_played(pl_entry) ? 'o' : ' ');
		char         *entry_name = gmu_core_playlist_get_entry_name(pl_entry);
		char         *format = "%c%3d";
		int           line_length = strlen(entry_name);
		const TextRenderer *font, *font_inverted;

		if (line_length > pb->longest_line_so_far)
			pb->longest_line_so_far = line_length;

		if (pl_length > 999 && pl_length <= 9999) format = "%c%4d";
		if (pl_length > 9999) format = "%c%5d";

		if (gmu_core_playlist_entry_get_queue_pos(pl_entry) == 0)
			snprintf(buf, len, format, (pl_entry == gmu_core_playlist_get_current() ? '*' : c), i + 1);
		else
			snprintf(buf, len, "%cQ:%d", (pl_entry == gmu_core_playlist_get_current() ? '*' : c),
			         gmu_core_playlist_entry_get_queue_pos(pl_entry));

		if (i == pb->offset + number_of_visible_lines - 1 && !selected_entry_drawn)
			pb->selection = i;

		font =          (i == pb->selection ? &pb->skin->font2 : &pb->skin->font1);
		font_inverted = (i == pb->selection ? &pb->skin->font1 : &pb->skin->font2);

		if (i == pb->selection) selected_entry_drawn = 1;
		textrenderer_draw_string(font, buf, sdl_target, gmu_widget_get_pos_x(&pb->skin->lv, 1),
		                         gmu_widget_get_pos_y(&pb->skin->lv, 1) + 1
		                         + (i-pb->offset) * (pb->skin->font2_char_height + 1));
		textrenderer_draw_string_with_highlight(font, font_inverted, entry_name, pb->horiz_offset, sdl_target, 
		                                        gmu_widget_get_pos_x(&pb->skin->lv, 1)
		                                        + pb->skin->font1_char_width * 7,
		                                        gmu_widget_get_pos_y(&pb->skin->lv, 1) + 1
		                                        + (i-pb->offset)*(pb->skin->font2_char_height+1),
		skin_textarea_get_characters_per_line(pb->skin)-6, RENDER_ARROW);
		pl_entry = gmu_core_playlist_get_next(pl_entry);
	}
	gmu_core_playlist_release_lock();
}

int pl_browser_get_selection(PlaylistBrowser *pb)
{
	return pb->selection;
}

int pl_browser_set_selection(PlaylistBrowser *pb, int pos)
{
	int res = 0;
	int pl_length = gmu_core_playlist_get_length();

	if (pos < pl_length && pos >= 0) {
		pb->selection = pos;
		if (pb->selection < pb->offset) {
			pb->offset = pb->selection;
			pb->first_visible_item = pos;
		} else if (pb->selection > pb->offset + skin_textarea_get_number_of_lines(pb->skin) - 1) {
			pb->offset = pb->selection;
			pb->first_visible_item = pos;
		}
		res = 1;
	}
	return res;
}

void pl_brower_move_selection_down(PlaylistBrowser *pb)
{
	int pl_length = gmu_core_playlist_get_length();
	if (pb->selection < pl_length - 1) {
		pb->selection++;
	} else {
		pb->selection = 0;
		pb->offset = 0;
		pb->first_visible_item = 0;
	}
	if (pb->selection > pb->offset + skin_textarea_get_number_of_lines(pb->skin) - 1) {
		pb->offset++;
		if (pb->first_visible_item >= 0 && pb->first_visible_item < pl_length-1)
			pb->first_visible_item++;
	}
}

void pl_brower_move_selection_up(PlaylistBrowser *pb)
{
	if (pb->selection > 0) {
		pb->selection--;
	} else {
		int nol = skin_textarea_get_number_of_lines(pb->skin);
		int pl_length = gmu_core_playlist_get_length();
		pb->selection = (pl_length - 1 < 0 ? 0 : pl_length - 1);
		pb->offset = (pb->selection - nol + 1 > 0 ? 
		              pb->selection - nol + 1 : 0);
		pb->first_visible_item = pl_length - nol;
	}
	if (pb->selection < pb->offset) {
		pb->offset--;
		if (pb->first_visible_item > 0)
			pb->first_visible_item--;
		else
			pb->first_visible_item = 0;
	}
}

void pl_brower_move_selection_n_items_down(PlaylistBrowser *pb, int n)
{
	int i;
	for (i = n; i--; ) pl_brower_move_selection_down(pb);
}

void pl_brower_move_selection_n_items_up(PlaylistBrowser *pb, int n)
{
	int i;
	for (i = n; i--; ) pl_brower_move_selection_up(pb);
}

void pl_browser_scroll_horiz(PlaylistBrowser *pb, int direction)
{
	int cpl = skin_textarea_get_characters_per_line(pb->skin);
	int tmp = (pb->horiz_offset + direction < pb->longest_line_so_far - cpl + 7 ? 
	           pb->horiz_offset + direction : pb->longest_line_so_far - cpl + 7);
	pb->horiz_offset = (tmp > 0 ? tmp : 0);
}

Charset pl_browser_get_filenames_charset(PlaylistBrowser *pb)
{
	return pb->filenames_charset;
}
