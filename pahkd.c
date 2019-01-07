#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <pulse/pulseaudio.h>

enum {
	KEYCODE_MUTE = 121,
	KEYCODE_VOL_DOWN = 122,
	KEYCODE_VOL_UP = 123
};

static const xcb_keycode_t keys[] = {KEYCODE_MUTE, KEYCODE_VOL_DOWN, KEYCODE_VOL_UP};
static const uint16_t mods[] = {XCB_MOD_MASK_SHIFT, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2};

typedef struct UserData {
	xcb_connection_t* dpy;
	pa_mainloop* mainloop;
	pa_context* pactx;
	int mute;
	pa_cvolume cvol;
	char default_sink_name[256];
} UserData;

void cb_sink_info(pa_context* pactx, const pa_sink_info* i, int eol, void* userdata)
{
	UserData* ud = (UserData*)userdata;

	if (i) {
		ud->mute = i->mute;
		ud->cvol = i->volume;
	}
}

void cb_server_info(pa_context* pactx, const pa_server_info* inf, void* userdata)
{
	UserData* ud = (UserData*)userdata;

	strcpy(ud->default_sink_name, inf->default_sink_name);
	pa_context_get_sink_info_by_name(pactx, ud->default_sink_name, cb_sink_info, ud);
}

void cb_state(pa_context* pactx, void* userdata)
{
	if (pa_context_get_state(pactx) == PA_CONTEXT_READY) {
		pa_context_get_server_info(pactx, cb_server_info, userdata);
		pa_context_subscribe(pactx, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
	}
}

void cb_subscription(pa_context* pactx, pa_subscription_event_type_t t, unsigned int idx, void* userdata)
{
	UserData* ud = (UserData*)userdata;

	if (t & PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
		pa_context_get_server_info(pactx, cb_server_info, userdata);
	} else {
		pa_context_get_sink_info_by_name(pactx, ud->default_sink_name, cb_sink_info, userdata);
	}
}

void handle_xcb(pa_mainloop_api* ea, pa_io_event* e, int fd, pa_io_event_flags_t events, void* userdata)
{
	UserData* ud = (UserData*)userdata;

	xcb_generic_event_t* evt;
	while ((evt = xcb_poll_for_event(ud->dpy))) {
		uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(evt);
		if (event_type == XCB_KEY_PRESS) {
			switch (((xcb_key_press_event_t*)evt)->detail) {
				case KEYCODE_MUTE:
					pa_context_set_sink_mute_by_name(ud->pactx, ud->default_sink_name, ud->mute = !ud->mute, NULL, NULL);
					break;
				case KEYCODE_VOL_DOWN:
					pa_cvolume_dec(&ud->cvol, 0x10000U / 20);
					pa_context_set_sink_volume_by_name(ud->pactx, ud->default_sink_name, &ud->cvol, NULL, NULL);
					break;
				case KEYCODE_VOL_UP:
					pa_cvolume_inc_clamp(&ud->cvol, 0x10000U / 20, (((xcb_key_press_event_t*)evt)->state & XCB_MOD_MASK_SHIFT) ? PA_VOLUME_MAX : PA_VOLUME_NORM);
					pa_context_set_sink_volume_by_name(ud->pactx, ud->default_sink_name, &ud->cvol, NULL, NULL);
					break;
			}
		}
		free(evt);
	}

	if (xcb_connection_has_error(ud->dpy)) {
		pa_mainloop_quit(ud->mainloop, 1);
	}
}

int main()
{
	UserData ud = {0};

	ud.mainloop = pa_mainloop_new();
	pa_mainloop_api* api = pa_mainloop_get_api(ud.mainloop);
	ud.pactx = pa_context_new(api, "pahkd");
	pa_context_set_state_callback(ud.pactx, cb_state, &ud);
	pa_context_set_subscribe_callback(ud.pactx, cb_subscription, &ud);
	pa_context_connect(ud.pactx, NULL, PA_CONTEXT_NOAUTOSPAWN | PA_CONTEXT_NOFAIL, NULL);

	ud.dpy = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(ud.dpy)) {
		fprintf(stderr, "can't open display\n");
		return 1;
	}

	xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(ud.dpy)).data;
	if (!screen) {
		fprintf(stderr, "can't acquire screen\n");
		return 1;
	}

	for (size_t keyi = 0; keyi < sizeof(keys) / sizeof(keys[0]); keyi++) {
		for (size_t modc = 0; modc < (1 << (sizeof(mods) / sizeof(mods[0]))); modc++) {
			uint16_t mod = 0;
			for (size_t modi = 0; modi < sizeof(mods) / sizeof(mods[0]); modi++) {
				if (modc & (1 << modi)) {
					mod |= mods[modi];
				}
			}
			if (xcb_request_check(ud.dpy, xcb_grab_key_checked(ud.dpy, 1, screen->root, mod, keys[keyi], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC))) {
				fprintf(stderr, "can't grab volume keys\n");
				return 1;
			}
		}
	}

	api->io_new(api, xcb_get_file_descriptor(ud.dpy), PA_IO_EVENT_INPUT, handle_xcb, &ud);

	int retval;
	pa_mainloop_run(ud.mainloop, &retval);
	return retval;
}
