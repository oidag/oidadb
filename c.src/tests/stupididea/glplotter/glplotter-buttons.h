#ifndef _glplotter_btns_h
#define _glplotter_btns_h

typedef int glp_key_t; // see below
/* The unknown key */
#define GLP_KEY_UNKNOWN            -1
/* Printable keys */
#define GLP_KEY_SPACE              32
#define GLP_KEY_APOSTROPHE         39  /* ' */
#define GLP_KEY_COMMA              44  /* , */
#define GLP_KEY_MINUS              45  /* - */
#define GLP_KEY_PERIOD             46  /* . */
#define GLP_KEY_SLASH              47  /* / */
#define GLP_KEY_0                  48
#define GLP_KEY_1                  49
#define GLP_KEY_2                  50
#define GLP_KEY_3                  51
#define GLP_KEY_4                  52
#define GLP_KEY_5                  53
#define GLP_KEY_6                  54
#define GLP_KEY_7                  55
#define GLP_KEY_8                  56
#define GLP_KEY_9                  57
#define GLP_KEY_SEMICOLON          59  /* ; */
#define GLP_KEY_EQUAL              61  /* = */
#define GLP_KEY_A                  65
#define GLP_KEY_B                  66
#define GLP_KEY_C                  67
#define GLP_KEY_D                  68
#define GLP_KEY_E                  69
#define GLP_KEY_F                  70
#define GLP_KEY_G                  71
#define GLP_KEY_H                  72
#define GLP_KEY_I                  73
#define GLP_KEY_J                  74
#define GLP_KEY_K                  75
#define GLP_KEY_L                  76
#define GLP_KEY_M                  77
#define GLP_KEY_N                  78
#define GLP_KEY_O                  79
#define GLP_KEY_P                  80
#define GLP_KEY_Q                  81
#define GLP_KEY_R                  82
#define GLP_KEY_S                  83
#define GLP_KEY_T                  84
#define GLP_KEY_U                  85
#define GLP_KEY_V                  86
#define GLP_KEY_W                  87
#define GLP_KEY_X                  88
#define GLP_KEY_Y                  89
#define GLP_KEY_Z                  90
#define GLP_KEY_LEFT_BRACKET       91  /* [ */
#define GLP_KEY_BACKSLASH          92  /* \ */
#define GLP_KEY_RIGHT_BRACKET      93  /* ] */
#define GLP_KEY_GRAVE_ACCENT       96  /* ` */
#define GLP_KEY_WORLD_1            161 /* non-US #1 */
#define GLP_KEY_WORLD_2            162 /* non-US #2 */

/* Function keys */
#define GLP_KEY_ESCAPE             256
#define GLP_KEY_ENTER              257
#define GLP_KEY_TAB                258
#define GLP_KEY_BACKSPACE          259
#define GLP_KEY_INSERT             260
#define GLP_KEY_DELETE             261
#define GLP_KEY_RIGHT              262
#define GLP_KEY_LEFT               263
#define GLP_KEY_DOWN               264
#define GLP_KEY_UP                 265
#define GLP_KEY_PAGE_UP            266
#define GLP_KEY_PAGE_DOWN          267
#define GLP_KEY_HOME               268
#define GLP_KEY_END                269
#define GLP_KEY_CAPS_LOCK          280
#define GLP_KEY_SCROLL_LOCK        281
#define GLP_KEY_NUM_LOCK           282
#define GLP_KEY_PRINT_SCREEN       283
#define GLP_KEY_PAUSE              284
#define GLP_KEY_F1                 290
#define GLP_KEY_F2                 291
#define GLP_KEY_F3                 292
#define GLP_KEY_F4                 293
#define GLP_KEY_F5                 294
#define GLP_KEY_F6                 295
#define GLP_KEY_F7                 296
#define GLP_KEY_F8                 297
#define GLP_KEY_F9                 298
#define GLP_KEY_F10                299
#define GLP_KEY_F11                300
#define GLP_KEY_F12                301
#define GLP_KEY_F13                302
#define GLP_KEY_F14                303
#define GLP_KEY_F15                304
#define GLP_KEY_F16                305
#define GLP_KEY_F17                306
#define GLP_KEY_F18                307
#define GLP_KEY_F19                308
#define GLP_KEY_F20                309
#define GLP_KEY_F21                310
#define GLP_KEY_F22                311
#define GLP_KEY_F23                312
#define GLP_KEY_F24                313
#define GLP_KEY_F25                314
#define GLP_KEY_KP_0               320
#define GLP_KEY_KP_1               321
#define GLP_KEY_KP_2               322
#define GLP_KEY_KP_3               323
#define GLP_KEY_KP_4               324
#define GLP_KEY_KP_5               325
#define GLP_KEY_KP_6               326
#define GLP_KEY_KP_7               327
#define GLP_KEY_KP_8               328
#define GLP_KEY_KP_9               329
#define GLP_KEY_KP_DECIMAL         330
#define GLP_KEY_KP_DIVIDE          331
#define GLP_KEY_KP_MULTIPLY        332
#define GLP_KEY_KP_SUBTRACT        333
#define GLP_KEY_KP_ADD             334
#define GLP_KEY_KP_ENTER           335
#define GLP_KEY_KP_EQUAL           336
#define GLP_KEY_LEFT_SHIFT         340
#define GLP_KEY_LEFT_CONTROL       341
#define GLP_KEY_LEFT_ALT           342
#define GLP_KEY_LEFT_SUPER         343
#define GLP_KEY_RIGHT_SHIFT        344
#define GLP_KEY_RIGHT_CONTROL      345
#define GLP_KEY_RIGHT_ALT          346
#define GLP_KEY_RIGHT_SUPER        347
#define GLP_KEY_MENU               348

typedef enum {
	GLP_MOUSE_1,  // left click
	GLP_MOUSE_2,  // right click
	GLP_MOUSE_3,  // middle click
	GLP_MOUSE_4,  // 'back'
	GLP_MOUSE_5,  // 'forward'
	GLP_MOUSE_6,
	GLP_MOUSE_7,
	GLP_MOUSE_8,
} mousebuttons_t;

#endif