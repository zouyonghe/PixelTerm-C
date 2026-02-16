#ifndef INPUT_DISPATCH_MOUSE_MODES_INTERNAL_H
#define INPUT_DISPATCH_MOUSE_MODES_INTERNAL_H

#include "app.h"
#include "input.h"

void input_dispatch_handle_mouse_press_single(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_press_preview(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_press_file_manager(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_press_book(PixelTermApp *app, const InputEvent *event);

void input_dispatch_handle_mouse_double_click_single(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_double_click_preview(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_double_click_book_preview(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_double_click_file_manager(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_double_click_book(PixelTermApp *app, const InputEvent *event);

void input_dispatch_handle_mouse_scroll_single(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_scroll_preview(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_scroll_book_preview(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_scroll_file_manager(PixelTermApp *app, const InputEvent *event);
void input_dispatch_handle_mouse_scroll_book(PixelTermApp *app, const InputEvent *event);

#endif
