#ifndef INPUT_DISPATCH_KEY_MODES_INTERNAL_H
#define INPUT_DISPATCH_KEY_MODES_INTERNAL_H

#include <glib.h>

#include "app.h"
#include "input.h"

gboolean input_dispatch_key_modes_handle_book_jump_input(PixelTermApp *app, const InputEvent *event);

void input_dispatch_key_modes_toggle_video_playback(PixelTermApp *app);
void input_dispatch_key_modes_toggle_video_fps(PixelTermApp *app);

void input_dispatch_handle_key_press_single(PixelTermApp *app,
                                            InputHandler *input_handler,
                                            const InputEvent *event);
void input_dispatch_handle_key_press_preview(PixelTermApp *app,
                                             InputHandler *input_handler,
                                             const InputEvent *event);
void input_dispatch_handle_key_press_book_preview(PixelTermApp *app,
                                                  InputHandler *input_handler,
                                                  const InputEvent *event);
void input_dispatch_handle_key_press_book(PixelTermApp *app,
                                          InputHandler *input_handler,
                                          const InputEvent *event);
void input_dispatch_handle_key_press_file_manager(PixelTermApp *app,
                                                  InputHandler *input_handler,
                                                  const InputEvent *event);

#endif
