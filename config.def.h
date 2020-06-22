/* define crosshairs used for various operations */
char *xhair[] = {
	[XHAIR_MOVE] = "hand1",
	[XHAIR_SIZE] = "lr_angle",
	[XHAIR_TELE] = "tcross",
	[XHAIR_DFLT] = "left_ptr",
};

/* key that must be pressed to register mouse events */
int modifier = XCB_MOD_MASK_1;

/* window borders and titlebar */
int border = 8;
int inner_border = 2;
uint32_t border_color = 0x666666;
uint32_t border_color_active = 0xdeadca7;

/* move/resize step amound in pixels */
int move_step = 8;
