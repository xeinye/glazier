enum EWMH_TYPES {
	IGNORE,
	NORMAL,
	POPUP,
	FULLSCREEN,
};

int ewmh_init();
int ewmh_supported();
int ewmh_supportingwmcheck();
int ewmh_activewindow(xcb_window_t);
int ewmh_clientlist();
int ewmh_type(xcb_window_t);
int ewmh_message(xcb_window_t, xcb_atom_t, xcb_client_message_data_t);
int ewmh_fullscreen(xcb_window_t, int);
