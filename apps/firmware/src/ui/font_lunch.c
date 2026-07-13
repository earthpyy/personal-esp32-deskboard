/*******************************************************************************
 * Size: 12 px
 * Bpp: 4
 * Opts: --font fontawesome-webfont.ttf --range 0xF0F5 --size 12 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o src/ui/font_lunch.c
 *
 * One-glyph icon font: FontAwesome 4.7 fa-cutlery (U+F0F5), the fork+knife shown
 * on the schedule's "lunch break" divider. Regenerate with the command above
 * (fontawesome-webfont.ttf from the Font-Awesome v4.7.0 release; not vendored).
 * Referenced in code as the UTF-8 bytes "\xEF\x83\xB5".
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_LUNCH
#define FONT_LUNCH 1
#endif

#if FONT_LUNCH

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F0F5 "" */
    0x0, 0x0, 0x0, 0x0, 0x10, 0xc5, 0x6c, 0x2,
    0xdf, 0xf7, 0xd5, 0x6c, 0x1a, 0xff, 0xf7, 0xd5,
    0x6c, 0x1c, 0xff, 0xf7, 0xfb, 0xbe, 0x1c, 0xff,
    0xf7, 0xff, 0xff, 0xc, 0xff, 0xf7, 0x8f, 0xf9,
    0xc, 0xff, 0xf7, 0x2f, 0xf3, 0xc, 0xff, 0xf7,
    0x2f, 0xf3, 0x8, 0xaf, 0xf7, 0x2f, 0xf3, 0x0,
    0x1f, 0xf7, 0x2f, 0xf3, 0x0, 0x1f, 0xf7, 0x2f,
    0xf3, 0x0, 0x1f, 0xf7, 0xd, 0xd1, 0x0, 0xc,
    0xe4
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 151, .box_w = 10, .box_h = 13, .ofs_x = 0, .ofs_y = -2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 61685, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_lunch = {
#else
lv_font_t font_lunch = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_LUNCH*/

