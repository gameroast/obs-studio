/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "graphics/vec4.h"
#include "obs.h"
#include "obs-internal.h"

bool obs_display_init(struct obs_display *display,
		struct gs_init_data *graphics_data)
{
	pthread_mutex_init_value(&display->draw_callbacks_mutex);

	if (graphics_data) {
		display->swap = gs_create_swapchain(graphics_data);
		if (!display->swap) {
			blog(LOG_ERROR, "obs_display_init: Failed to "
			                "create swap chain");
			return false;
		}

		display->cx = graphics_data->cx;
		display->cy = graphics_data->cy;
	}

	if (pthread_mutex_init(&display->draw_callbacks_mutex, NULL) != 0) {
		blog(LOG_ERROR, "obs_display_init: Failed to create mutex");
		return false;
	}

	return true;
}

obs_display_t obs_display_create(struct gs_init_data *graphics_data)
{
	struct obs_display *display = bzalloc(sizeof(struct obs_display));

	if (!obs_display_init(display, graphics_data)) {
		obs_display_destroy(display);
		display = NULL;
	}

	return display;
}

void obs_display_free(obs_display_t display)
{
	pthread_mutex_destroy(&display->draw_callbacks_mutex);
	da_free(display->draw_callbacks);

	if (display->swap) {
		swapchain_destroy(display->swap);
		display->swap = NULL;
	}
}

void obs_display_destroy(obs_display_t display)
{
	if (display) {
		pthread_mutex_lock(&obs->data.displays_mutex);
		da_erase_item(obs->data.displays, &display);
		pthread_mutex_unlock(&obs->data.displays_mutex);

		obs_display_free(display);
		bfree(display);
	}
}

void obs_display_resize(obs_display_t display, uint32_t cx, uint32_t cy)
{
	if (!display) return;

	pthread_mutex_lock(&display->draw_callbacks_mutex);

	display->cx = cx;
	display->cy = cy;
	display->size_changed = true;

	pthread_mutex_unlock(&display->draw_callbacks_mutex);
}

void obs_display_add_draw_callback(obs_display_t display,
		void (*draw)(void *param, uint32_t cx, uint32_t cy),
		void *param)
{
	if (!display) return;

	struct draw_callback data = {draw, param};

	pthread_mutex_lock(&display->draw_callbacks_mutex);
	da_push_back(display->draw_callbacks, &data);
	pthread_mutex_unlock(&display->draw_callbacks_mutex);
}

void obs_display_remove_draw_callback(obs_display_t display,
		void (*draw)(void *param, uint32_t cx, uint32_t cy),
		void *param)
{
	if (!display) return;

	struct draw_callback data = {draw, param};

	pthread_mutex_lock(&display->draw_callbacks_mutex);
	da_erase_item(display->draw_callbacks, &data);
	pthread_mutex_unlock(&display->draw_callbacks_mutex);
}

static inline void render_display_begin(struct obs_display *display)
{
	struct vec4 clear_color;

	gs_load_swapchain(display ? display->swap : NULL);

	if (display->size_changed) {
		gs_resize(display->cx, display->cy);
		display->size_changed = false;
	}

	gs_beginscene();
	vec4_set(&clear_color, 0.3f, 0.0f, 0.0f, 1.0f);
	gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL,
			&clear_color, 1.0f, 0);

	gs_enable_depthtest(false);
	/* gs_enable_blending(false); */
	gs_setcullmode(GS_NEITHER);

	gs_ortho(0.0f, (float)obs->video.base_width,
			0.0f, (float)obs->video.base_height, -100.0f, 100.0f);
	gs_setviewport(0, 0, display->cx, display->cy);
}

static inline void render_display_end()
{
	gs_endscene();
	gs_present();
}

void render_display(struct obs_display *display)
{
	if (!display) return;

	render_display_begin(display);

	pthread_mutex_lock(&display->draw_callbacks_mutex);

	for (size_t i = 0; i < display->draw_callbacks.num; i++) {
		struct draw_callback *callback;
		callback = display->draw_callbacks.array+i;

		callback->draw(callback->param, display->cx, display->cy);
	}

	pthread_mutex_unlock(&display->draw_callbacks_mutex);

	render_display_end();
}
