/* 
 * Gmu Music Player
 *
 * Copyright (c) 2006-2012 Johannes Heimansberg (wejp.k.vu)
 *
 * File: gmuc.c  Created: 120920
 *
 * Description: Gmu command line tool
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of
 * the License. See the file COPYING in the Gmu's main directory
 * for details.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED
#endif
#include <curses.h>
#include <signal.h>
#include "../wejpconfig.h"
#include "../ringbuffer.h"
#include "../debug.h"
#include "../frontends/web/websocket.h"
#include "../frontends/web/json.h"
#include "../dir.h"
#include "window.h"
#include "listwidget.h"
#include "ui.h"

#define BUF 1024
#define PORT 4680

typedef enum { STATE_WEBSOCKET_HANDSHAKE, STATE_CONNECTION_ESTABLISHED, STATE_WEBSOCKET_HANDSHAKE_FAILED } State;

static char cur_artist[128], cur_title[128], cur_status[32];
static int  cur_time, cur_playmode;

static int resized = 0;

static void sig_handler_sigwinch(int sig)
{
	resized = 1;
}

typedef enum Command {
	PLAY,
	PAUSE,
	NEXT,
	PREVIOUS,
	STOP,
	FILES,
	RAW, /* Sends raw data as command to thr server (which has to be supplied as parameter) */
	NO_COMMAND
} Command;

static char *cmd_arr[] = {
	"play",
	"pause",
	"next",
	"previous",
	"stop",
	"files",
	"raw",
	NULL
};

/* Parses the input string and sotres the detected command in cmd and 
 * its parameters in params, when successful and returns 1. Memory for
 * 'params' will be allocated as needed. If there are no parameters
 * 'params' will be set to NULL. No memory will be allocated for 'cmd'.
 * When unable to parse the input, the function returns 0. */
static int parse_input_alloc(char *input, Command *cmd, char **params)
{
	int res = 0;
	if (input) {
		int   len = strlen(input);
		char *input_copy;
		if (len > 0) {
			input_copy = malloc(len+1);
			if (input_copy) {
				int   i;
				char *parameters = NULL, *command = NULL;
				memcpy(input_copy, input, len+1);
				for (i = 0; input_copy[i] != ' ' && input_copy[i] != '\0'; i++);
				if (input_copy[i] == ' ') {
					input_copy[i] = '\0';
					parameters = input_copy+i+1;
				}
				command = input_copy;
				/* Check command */
				for (i = 0; cmd_arr[i]; i++) {
					if (strncmp(command, cmd_arr[i], strlen(command)) == 0) {
						int len_params = parameters ? strlen(parameters) : 0;

						*cmd = i;
						if (len_params > 0) {
							*params = malloc(len_params+1);
							if (*params) {
								memcpy(*params, parameters, len_params+1);
								//endwin(); printf("params=[%s]\n", params); exit(0);
							}
						} else {
							params = NULL;
						}
						res = 1;
						break;
					}
				}
				free(input_copy);
			}
		}
	}
	return res;
}

static char *wchars_to_utf8_str_realloc(char *input, wchar_t *wchars)
{
	int len = wcstombs(NULL, wchars, 0);
	if (len >= 0) {
		input = realloc(input, len + 1);
		if (input) {
			if (wcstombs(input, wchars, len) != len) {
				free(input);
				input = NULL;
			} else {
				input[len] = '\0';
			}
		}
	}
	return input;
}

static State do_websocket_handshake(RingBuffer *rb, State state)
{
	int  i, data_available = 1;
	char buf[256], ch;
	ringbuffer_set_unread_pos(rb);
	while (data_available && state == STATE_WEBSOCKET_HANDSHAKE) {
		for (i = 0, ch = 0; i < 255 && ch != '\n'; ) {
			if (ringbuffer_read(rb, &ch, 1)) {
				buf[i] = ch;
				if (buf[i] == '\r') buf[i] = '\0';
				i++;
			} else {
				ringbuffer_unread(rb);
				data_available = 0;
				//printf("Not enough data available...\n");
				break;
			}
		}
		if (buf[0] == '\n' || (buf[0] == '\0' && buf[1] == '\n')) { /* End of header */
			/* Check for valid websocket server response */
			wdprintf(V_DEBUG, "gmuc", "End of header detected!\n");
			if (1) // temporary
				state = STATE_CONNECTION_ESTABLISHED;
			else
				state = STATE_WEBSOCKET_HANDSHAKE_FAILED;
		}
		//if (buf[i] == '\r' || buf[i] == '\n') buf[i] = '\0';
		buf[i] = '\0';
		if (i > 0) wdprintf(V_DEBUG, "gmuc", "i=%d LINE=[%s]\n", i, buf);
	}
	return state;
}

static void cmd_playback_time_change(UI *ui, JSON_Object *json)
{
	int t = (int)json_get_number_value_for_key(json, "time");
	cur_time = t;
	ui_draw_header(ui, cur_artist, cur_title, cur_status, cur_time, cur_playmode);
}

static void cmd_trackinfo(UI *ui, JSON_Object *json)
{
	char *jv = json_get_string_value_for_key(json, "artist");
	strncpy(cur_artist, jv, 127);
	jv = json_get_string_value_for_key(json, "title");
	strncpy(cur_title, jv, 127);
	strncpy(cur_status, "playing", 31);
	ui_draw_header(ui, cur_artist, cur_title, cur_status, cur_time, cur_playmode);
}

static void cmd_playlist_change(UI *ui, JSON_Object *json, int sock)
{
	int length     = (int)json_get_number_value_for_key(json, "length");
	int changed_at = (int)json_get_number_value_for_key(json, "changed_at_position");
	wprintw(ui->win_cmd->win, "Playlist has been changed!\n");
	wprintw(ui->win_cmd->win, "Length=%d Pos=%d\n", length, changed_at);
	if (length < 0) length = 0;
	listwidget_set_length(ui->lw_pl, length);
	if (length > 0 && changed_at >= 0) {
		char str[64];
		snprintf(str, 63, "playlist_get_item:%d", changed_at);
		wprintw(ui->win_cmd->win, "Sending request: playlist_get_item:%d\n", changed_at);
		if (!websocket_send_str(sock, str, 1))
			wprintw(ui->win_cmd->win, "FAILED sending message over socket!\n");
		else
			wprintw(ui->win_cmd->win, "Message sent SUCCESSFULLY.\n");
		ui_refresh_active_window(ui);
	}
}

static void cmd_playlist_item(UI *ui, JSON_Object *json, int sock)
{
	char  str[128];
	char *title = json_get_string_value_for_key(json, "title");
	int   pos = (int)json_get_number_value_for_key(json, "position");
	if (pos >= 0 && title) {
		snprintf(str, 127, "%5d", pos+1);
		listwidget_set_cell_data(ui->lw_pl, pos, 0, str);
		listwidget_set_cell_data(ui->lw_pl, pos, 1, title);
		if (pos >= ui->lw_pl->first_visible_row &&
		    pos < ui->lw_pl->first_visible_row + ui->lw_pl->win->height-2)
			ui_refresh_active_window(ui);
		if (pos+1 < listwidget_get_rows(ui->lw_pl)) {
			snprintf(str, 127, "playlist_get_item:%d", pos+1);
			websocket_send_str(sock, str, 1);
		}
	}
}

static void cmd_dir_read(UI *ui, JSON_Object *json)
{
	int   i;
	JSON_Key *jk = json_get_key_object_for_key(json, "data");
	JSON_Object *jo = jk ? jk->key_value_object : NULL;
	char *tmp = NULL;
	
	if (jo) tmp = json_get_first_key_string(jo);
	int start = -1;
	if (tmp) start = atoi(tmp);
	if (!jk) wprintw(ui->win_cmd->win, "ERROR: No key for 'data'.\n");

	if (start >= 0) {
		for (i = start; ; i++) {
			JSON_Object *j_file;
			char tmp[16];
			snprintf(tmp, 15, "%d", i);
			wprintw(ui->win_cmd->win, "[%s]", tmp);
			if (!jo) wprintw(ui->win_cmd->win, "ERROR: No object.\n");
			JSON_Key *jk = json_get_key_object_for_key(jo, tmp);
			j_file = jk ? jk->key_value_object : NULL;
			if (j_file) {
				char *jv     = json_get_string_value_for_key(j_file, "name");
				int   is_dir = json_get_number_value_for_key(j_file, "is_dir");
				int   fsize  = json_get_number_value_for_key(j_file, "size");
				if (!jv) break;
				wprintw(ui->win_cmd->win, "FILE:%s\n", jv);
				listwidget_add_row(ui->lw_fb);
				listwidget_set_cell_data(ui->lw_fb, i, 1, jv);
				if (!is_dir)
					snprintf(tmp, 15, "%d", fsize / 1024);
				else
					snprintf(tmp, 15, "[DIR]");
				listwidget_set_cell_data(ui->lw_fb, i, 0, tmp);
			} else {
				break;
			}
		}
		ui_refresh_active_window(ui);
	}
}

static void cmd_playmode_info(UI *ui, JSON_Object *json)
{
	cur_playmode = (int)json_get_number_value_for_key(json, "mode");
}

int main(int argc, char **argv)
{
	int                sock, res = EXIT_FAILURE;
	char              *buffer = malloc(BUF);
	ssize_t            size;
	struct sockaddr_in address;
	const int          y = 1;
	ConfigFile         config;
	char              *password, *host;
	char               config_file_path[256], *homedir;

	cfg_init_config_file_struct(&config);
	cfg_add_key(&config, "Host", "127.0.0.1");
	cfg_add_key(&config, "Password", "stupidpassword");

	homedir = getenv("HOME");
	if (homedir) {
		snprintf(config_file_path, 255, "%s/.config/gmu/gmu-cli.conf", homedir);
		if (cfg_read_config_file(&config, config_file_path) != 0) {
			char tmp[256];
			wdprintf(V_INFO, "gmuc", "No config file found. Creating a config file at %s. Please edit that file and try again.\n", config_file_path);
			snprintf(tmp, 255, "%s/.config", homedir);
			mkdir(tmp, 0);
			snprintf(tmp, 255, "%s/.config/gmu", homedir);
			mkdir(tmp, 0);
			if (cfg_write_config_file(&config, config_file_path))
				wdprintf(V_ERROR, "gmuc", "ERROR: Unable to create config file.\n");
			cfg_free_config_file_struct(&config);
			exit(2);
		}
		host = cfg_get_key_value(config, "Host");
		password = cfg_get_key_value(config, "Password");
		if (!host || !password) {
			wdprintf(V_ERROR, "gmuc", "ERROR: Invalid configuration.\n");
			exit(4);
		}
	} else {
		wdprintf(V_ERROR, "gmuc", "ERROR: Cannot find user's home directory.\n");
		exit(3);
	}

	wdprintf_set_verbosity(V_SILENT);

	if (argc >= 1) {
		int   quit = 0, network_error = 0;
		UI    ui;
		char *cur_dir = NULL;

		/* Setup SIGWINCH */
		struct sigaction act;
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_RESTART;
		act.sa_handler = sig_handler_sigwinch;
		if (sigaction(SIGWINCH, &act, NULL) < 0)
			wdprintf(V_ERROR, "gmuc", "ERROR: Cannot setup SIGWINCH handler\n");

		ui_init(&ui);
		ui_draw(&ui);
		ui_cursor_text_input(&ui, NULL);

		while (!quit) {
			if (buffer && (sock = socket(AF_INET, SOCK_STREAM, 0)) > 0) {
				struct timeval tv;
				tv.tv_sec = 2;
				tv.tv_usec = 0;
				if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv)) {
					//wdprintf(V_INFO, "reader", "setsockopt: %s\n", strerror(errno));
				}
				setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int));
				address.sin_family = AF_INET;
				inet_aton(host, &address.sin_addr);
				address.sin_port = htons(PORT);

				wdprintf(V_INFO, "gmuc", "Connecting to server (%s).\n", inet_ntoa(address.sin_addr));
				if (connect(sock, (struct sockaddr *)&address, sizeof(address)) == 0) {
					struct timeval tv;
					fd_set         readfds, errorfds;
					char          *input = NULL;
					int            cpos = 0;
					RingBuffer     rb;
					int            r = 1, connected = 1;
					State          state = STATE_WEBSOCKET_HANDSHAKE;
					char           str2[1024], *key;
					char          *str = "GET /gmu HTTP/1.1\r\n"
						"Host: %s\r\n"
						"Upgrade: websocket\r\n"
						"Connection: Upgrade\r\n"
						"Sec-WebSocket-Key: %s\r\n"
						"Sec-WebSocket-Version: 13\r\n\r\n";

					network_error = 0;

					key = websocket_client_generate_sec_websocket_key_alloc();
					if (key) {
						snprintf(str2, 1023, str, inet_ntoa(address.sin_addr), key);
						wdprintf(V_DEBUG, "gmuc", "request:%s\n", str2);
						send(sock, str2, strlen(str2), 0);
						free(key);
					} else {
						wdprintf(V_ERROR, "gmuc", "ERROR: Out of memory!\n");
					}
					wdprintf(V_DEBUG, "gmuc", "Connected to server (%s).\n", inet_ntoa(address.sin_addr));

					ringbuffer_init(&rb, 65536);
					while (!quit && connected && !network_error) {
						FD_ZERO(&readfds);
						FD_ZERO(&errorfds);
						FD_SET(fileno(stdin), &readfds);
						FD_SET(sock, &readfds);
						tv.tv_sec = 1;
						tv.tv_usec = 500000;
						if (select(sock+1, &readfds, NULL, &errorfds, &tv) < 0) {
							/*printf("ERROR in select(): %s\n", strerror(errno));*/
							FD_ZERO(&readfds);
						}

						if (resized) {
							resized = 0;
							ui_resize(&ui);
						}

						if (FD_ISSET(fileno(stdin), &readfds)) {
							char       buf[1024];
							wchar_t    wchars[256];
							wint_t     ch;
							int        res;

							memset(buf, 0, 1024);
							wdprintf(V_DEBUG, "gmuc", "Text was entered!\n");
							res = wget_wch(stdscr, &ch);
							if (res == OK) {
								ui_refresh_active_window(&ui);

								switch (ch) {
									case '\n': {
										int len = wcstombs(NULL, wchars, 0);
										if (len > 0) {
											input = wchars_to_utf8_str_realloc(input, wchars);
											if (input) {
												Command cmd = NO_COMMAND;
												char   *params = NULL;

												if (len > 0) {
													wprintw(ui.win_cmd->win, "%s\n", input);
													cpos = 0;
													parse_input_alloc(input, &cmd, &params);
													if (state == STATE_CONNECTION_ESTABLISHED) {
														char *str = NULL;
														switch (cmd) {
															case PLAY:
																str = "play";
																break;
															case PAUSE:
																str = "pause";
																break;
															case NEXT:
																str = "next";
																break;
															case PREVIOUS:
																str = "prev";
																break;
															case STOP:
																str = "stop";
																break;
															case FILES:
																str = "dir_read:/";
																break;
															case RAW:
																str = params;
															default:
																break;
														}
														if (str) websocket_send_str(sock, str, 1);
													} else {
														wdprintf(V_INFO, "gmuc", "Connection not established. Cannot send command.\n");
													}
												}
												wprintw(ui.win_cmd->win, "Command translates to %d", cmd);
												if (params) {
													wprintw(ui.win_cmd->win, " with parameters '%s'", params);
													free(params);
												}
												wprintw(ui.win_cmd->win, ".\n", cmd);
												free(input);
												input = NULL;
												ui_cursor_text_input(&ui, NULL);
											} else {
												wprintw(ui.win_cmd->win, "Error when processing input text. :(\n");
											}
											wchars[0] = L'\0';
										} else { /* No string has been entered -> Execute command depending on active window */
											char str[128];
											switch (ui.active_win) {
												case WIN_PL:
													snprintf(str, 127, "play:%d", listwidget_get_selection(ui.lw_pl));
													websocket_send_str(sock, str, 1);
													break;
												case WIN_FB: {
													char tmp[256], *prev_cur_dir = cur_dir;
													int  sel_row = listwidget_get_selection(ui.lw_fb);
													char *ftype = listwidget_get_row_data(ui.lw_fb, sel_row, 0);
													if (ftype && strcmp(ftype, "[DIR]") == 0) {
														cur_dir = dir_get_new_dir_alloc(prev_cur_dir ? prev_cur_dir : "/", 
																	 listwidget_get_row_data(ui.lw_fb, sel_row, 1));
														free(prev_cur_dir);
														wprintw(ui.win_cmd->win, "Selected dir: %s/%d\n", listwidget_get_row_data(ui.lw_fb, sel_row, 1), sel_row);
														wprintw(ui.win_cmd->win, "Full path: %s\n", cur_dir);
														listwidget_clear_all_rows(ui.lw_fb);
														ui_refresh_active_window(&ui);
														if (cur_dir) {
															snprintf(tmp, 255, "dir_read:%s", cur_dir);
															websocket_send_str(sock, tmp, 1);
														}
													} else { /* Add file */
														/* TODO */
													}
													break;
												}
												default:
													break;
											}
										}
										ui_refresh_active_window(&ui);
										ui_cursor_text_input(&ui, NULL);
										window_refresh(ui.win_footer);
										break;
									}
									/*case 'q':
										quit = 1;
										break;*/
									case KEY_BACKSPACE:
									case '\b':
									case KEY_DC:
									case 127: {
										if (cpos > 0) {
											wchars[cpos-1] = L'\0';
											cpos--;
										}
										input = wchars_to_utf8_str_realloc(input, wchars);
										if (input) {
											ui_refresh_active_window(&ui);
											ui_cursor_text_input(&ui, input);
										} else {
											wprintw(ui.win_cmd->win, "Error when processing input text. :(\n");
										}
										window_refresh(ui.win_footer);
										break;
									}
									case KEY_RESIZE:
										break;
									default: {
										wchars[cpos] = ch;
										wchars[cpos+1] = L'\0';
										cpos++;
										input = wchars_to_utf8_str_realloc(input, wchars);
										ui_cursor_text_input(&ui, input);
										window_refresh(ui.win_footer);
										break;
									}
								}
							} else if (res == KEY_CODE_YES) { /* Handle function and cursor keys here */
								int i;
								Function func = FUNC_NONE;
								for (i = 0; ui.fb_visible && ui.fb_visible[i].button_name; i++) {
									if (ui.fb_visible[i].key == ch) {
										func = ui.fb_visible[i].func;
										break;
									}
								}
								switch (func) {
									case FUNC_NEXT_WINDOW:
										ui_active_win_next(&ui);
										break;
									case FUNC_FB_ADD: {
										char  cmd[256];
										int   sel_row = listwidget_get_selection(ui.lw_fb);
										char *ftype = listwidget_get_row_data(ui.lw_fb, sel_row, 0);
										char *file = listwidget_get_row_data(ui.lw_fb, sel_row, 1);
										char *type = strcmp(ftype, "[DIR]") == 0 ? "dir" : "file";
										char *cur_dir_esc = json_string_escape_alloc(cur_dir);
										char *file_esc = json_string_escape_alloc(file);
										if (cur_dir_esc && file_esc) {
											snprintf(cmd, 255, 
													 "{\"cmd\":\"playlist_add\",\"path\":\"%s/%s\",\"type\":\"%s\"}",
													 cur_dir_esc, file_esc, type);
											websocket_send_str(sock, cmd, 1);
										}
										free(cur_dir_esc);
										free(file_esc);
										break;
									}
									case FUNC_PREVIOUS:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"prev\"}", 1);
										}
										break;
									case FUNC_NEXT:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"next\"}", 1);
										}
										break;
									case FUNC_STOP:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"stop\"}", 1);
										}
										break;
									case FUNC_PAUSE:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"pause\"}", 1);
										}
										break;
									case FUNC_PLAY:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"play\"}", 1);
										}
										break;
									case FUNC_PLAYMODE:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"playlist_playmode_cycle\"}", 1);
										}
										break;
									case FUNC_PL_CLEAR:
										if (state == STATE_CONNECTION_ESTABLISHED) {
											websocket_send_str(sock, "{\"cmd\":\"playlist_clear\"}", 1);
										}
										break;
									default:
										break;
								}

								switch (ch) {
									case KEY_DOWN:
										switch(ui.active_win) {
											case WIN_PL:
												listwidget_move_cursor(ui.lw_pl, 1);
												break;
											case WIN_FB:
												listwidget_move_cursor(ui.lw_fb, 1);
												break;
											default:
												break;
										}
										break;
									case KEY_UP:
										switch(ui.active_win) {
											case WIN_PL:
												listwidget_move_cursor(ui.lw_pl, -1);
												break;
											case WIN_FB:
												listwidget_move_cursor(ui.lw_fb, -1);
												break;
											default:
												break;
										}
										break;
									case KEY_NPAGE:
										switch(ui.active_win) {
											case WIN_PL:
												listwidget_move_cursor(ui.lw_pl, ui.lw_pl->win->height-2);
												break;
											case WIN_FB:
												listwidget_move_cursor(ui.lw_fb, ui.lw_pl->win->height-2);
												break;
											default:
												break;
										}
										break;
									case KEY_PPAGE:
										switch(ui.active_win) {
											case WIN_PL:
												listwidget_move_cursor(ui.lw_pl, -(ui.lw_pl->win->height-2));
												break;
											case WIN_FB:
												listwidget_move_cursor(ui.lw_fb, -(ui.lw_pl->win->height-2));
												break;
											default:
												break;
										}
										break;
								}
								ui_refresh_active_window(&ui);
								ui_cursor_text_input(&ui, input);
								window_refresh(ui.win_footer);
							} else {
								wprintw(ui.win_cmd->win, "ERROR\n");
							}
						}
						if (FD_ISSET(sock, &readfds) && !FD_ISSET(sock, &errorfds)) {
							//wprintw(ui.win_cmd->win, "Data received on socket: ");
							if (r) {
								//printf("Trying to read from server...\n");
								memset(buffer, 0, BUF); // we don't need that later
								size = recv(sock, buffer, BUF-1, 0);
								if (size > 0) {
									//wprintw(ui.win_cmd->win, "%d bytes:[%s]\n", size, buffer);
									//wdprintf(V_DEBUG, "gmuc", "Server reply (size:%d)=[%s]\n", size, buffer);
								} else {
									wdprintf(V_DEBUG, "gmuc", "Server reply with size %d :|\n", size);
									wprintw(ui.win_cmd->win, "Network Error: Data size = %d\n", size);
									if (size == -1) wprintw(ui.win_cmd->win, "Error: %s\n", strerror(errno));
									network_error = 1;
								}
								if (size > 0) r = ringbuffer_write(&rb, buffer, size);
								if (!r) wprintw(ui.win_cmd->win, "Write failed, ringbuffer full!\n");
							} else {
								wprintw(ui.win_cmd->win, "Can't read more from socket, ringbuffer full!\n");
								wdprintf(V_DEBUG, "gmuc", "Can't read more from socket, ringbuffer full!\n");
							}
						}
						//printf("Ringbuffer write %sokay.\n", r ? "" : "not ");
						switch (state) {
							case STATE_WEBSOCKET_HANDSHAKE: {
								state = do_websocket_handshake(&rb, state);
								break;
							}
							case STATE_CONNECTION_ESTABLISHED: {
								char tmp_buf[16];
								int  size, loop = 1;
								do {
									size = 0;
									// 0. set unread pos on ringbuffer
									ringbuffer_set_unread_pos(&rb);
									// 1. read a few bytes from the ringbuffer (Websocket flags+packet size)
									if (ringbuffer_read(&rb, tmp_buf, 10)) {
										// 2. Check wether the required packet size is available in the ringbuffer
										size = websocket_calculate_packet_size(tmp_buf);
										ringbuffer_unread(&rb);
									}
									// 3. If so, read the whole packet from the ringbuffer and process it, then startover at step 0.
									// 4. If not, unread the already read bytes from the ringbuffer and leave inner loop so the ringbuffer can fill more 
									if (ringbuffer_get_fill(&rb) >= size && size > 0) {
										char *wspacket = malloc(size+1); /* packet size+1 byte null terminator */
										char *payload;
										if (wspacket) {
											JSON_Object *json;
											ringbuffer_read(&rb, wspacket, size);
											wspacket[size] = '\0';
											payload = websocket_get_payload(wspacket);
											//wprintw(ui.win_cmd->win, "Payload data=[%s]\n", payload);
											json = json_parse_alloc(payload);
											if (!json_object_has_parse_error(json)) {
												char *cmd = json_get_string_value_for_key(json, "cmd");
												wdprintf(V_INFO, "gmuc", "Valid JSON object found.\n");
												if (cmd) {
													int screen_update = 0;
													wdprintf(V_INFO, "gmuc", "Got command: %s\n", cmd);
													wprintw(ui.win_cmd->win, "Got command: %s\n", cmd);
													//ui_refresh_active_window(&ui);
													if (strcmp(cmd, "trackinfo") == 0) {
														cmd_trackinfo(&ui, json);
													} else if (strcmp(cmd, "playback_time") == 0) {
														cmd_playback_time_change(&ui, json);
													} else if (strcmp(cmd, "hello") == 0) {
														websocket_send_str(sock, "{\"cmd\":\"trackinfo\"}", 1);
														websocket_send_str(sock, "{\"cmd\":\"playlist_playmode_get_info\"}", 1);
														listwidget_clear_all_rows(ui.lw_fb);
														if (!cur_dir) {
															websocket_send_str(sock, "{\"cmd\":\"dir_read\", \"dir\": \"/\"}", 1);
														} else {
															char tmp[256];
															snprintf(tmp, 255, "dir_read:%s", cur_dir);
															websocket_send_str(sock, tmp, 1);
														}
													} else if (strcmp(cmd, "playlist_info") == 0) {
														wprintw(ui.win_cmd->win, "Playlist info received!\n");
													} else if (strcmp(cmd, "playlist_change") == 0) {
														cmd_playlist_change(&ui, json, sock);
														screen_update = 1;
													} else if (strcmp(cmd, "playlist_item") == 0) {
														cmd_playlist_item(&ui, json, sock);
													} else if (strcmp(cmd, "dir_read") == 0) {
														cmd_dir_read(&ui, json);
													} else if (strcmp(cmd, "playmode_info") == 0) {
														cmd_playmode_info(&ui, json);
													}
													if (screen_update) ui_refresh_active_window(&ui);
													ui_cursor_text_input(&ui, input);
												}
											} else {
												wprintw(ui.win_cmd->win, "ERROR: Invalid JSON data received.\n");
												endwin();
												wdprintf_set_verbosity(V_DEBUG);
												json = json_parse_alloc(payload);
												wdprintf(V_ERROR, "gmuc", "ERROR: Invalid JSON data received.\n");exit(1);
												network_error = 1;
											}
											json_object_free(json);
											free(wspacket);
										} else {
											loop = 0;
										}
									} else if (size > 0) {
										wprintw(ui.win_cmd->win,
										        "Not enough data available. Need %d bytes, but only %d avail.\n",
										        size, ringbuffer_get_fill(&rb));
										ringbuffer_unread(&rb);
										loop = 0;
									} else {
										loop = 0;
										if (size == -1) network_error = 1;
									}
								} while (loop && !network_error);
								break;
							}
							case STATE_WEBSOCKET_HANDSHAKE_FAILED:
								wdprintf(V_ERROR, "gmuc", "Websocket handshake failed!\n");
								network_error = 1;
								connected = 0;
								break;
						}
					}
					if (input) free(input);
					ringbuffer_free(&rb);
				} else {
					wdprintf(V_ERROR, "gmuc", "Failed to connect: %s\n", strerror(errno));
					network_error = 1;
				}
				close(sock);
				res = EXIT_SUCCESS;
			}
			usleep(500000);
		}
		ui_free(&ui);
		free(cur_dir);
	} else {
		printf("Usage: %s <command> [parameters]\n", argv[0]);
		printf("Use \"%s h\" for a list of available commands.\n", argv[0]);
	}
	cfg_free_config_file_struct(&config);
	if (buffer) free(buffer);
	return res;
}
