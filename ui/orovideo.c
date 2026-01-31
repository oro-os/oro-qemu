/*
 * Oro Operating System Video Stream Interface
 *
 * Copyright (c) 2026 the Oro Operating System Project.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/orovideo.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qapi/error.h"

typedef struct OroVideoState {
    DisplayChangeListener dcl;
    CharFrontend chr;
    QemuConsole *con;
    int width;
    int height;
} OroVideoState;

static void orovideo_update(DisplayChangeListener *dcl,
                            int x, int y, int w, int h)
{
    OroVideoState *ovs = container_of(dcl, OroVideoState, dcl);
    DisplaySurface *surface = qemu_console_surface(ovs->con);
    
    if (!surface) {
        return;
    }

    int width = surface_width(surface);
    int height = surface_height(surface);
    
    /* Update dimensions if they changed */
    ovs->width = width;
    ovs->height = height;
    
    /* Send full frame on every update */
    /* Prepare header: width (u64) + height (u64) */
    uint64_t header[2];
    header[0] = (uint64_t)width;
    header[1] = (uint64_t)height;
    
    /* Send header */
    qemu_chr_fe_write_all(&ovs->chr, (uint8_t *)header, sizeof(header));
    
    /* Convert and send pixel data in RGB8 format */
    size_t pixel_count = width * height;
    uint8_t *rgb_data = g_malloc(pixel_count * 3);
    
    uint8_t *src = surface_data(surface);
    int stride = surface_stride(surface);
    pixman_format_code_t format = surface_format(surface);
    
    /* Convert pixels to RGB8 */
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint32_t pixel;
            uint8_t r, g, b;
            
            /* Read pixel based on format */
            if (format == PIXMAN_x8r8g8b8 || format == PIXMAN_a8r8g8b8) {
                pixel = *(uint32_t *)(src + row * stride + col * 4);
                b = (pixel >> 0) & 0xFF;
                g = (pixel >> 8) & 0xFF;
                r = (pixel >> 16) & 0xFF;
            } else if (format == PIXMAN_r8g8b8) {
                r = src[row * stride + col * 3 + 0];
                g = src[row * stride + col * 3 + 1];
                b = src[row * stride + col * 3 + 2];
            } else {
                /* Fallback for other formats - try to extract RGB */
                pixel = *(uint32_t *)(src + row * stride + col * 4);
                r = (pixel >> 16) & 0xFF;
                g = (pixel >> 8) & 0xFF;
                b = (pixel >> 0) & 0xFF;
            }
            
            size_t offset = (row * width + col) * 3;
            rgb_data[offset + 0] = r;
            rgb_data[offset + 1] = g;
            rgb_data[offset + 2] = b;
        }
    }
    
    /* Send pixel data */
    qemu_chr_fe_write_all(&ovs->chr, rgb_data, pixel_count * 3);
    
    g_free(rgb_data);
}

static void orovideo_switch(DisplayChangeListener *dcl,
                            DisplaySurface *new_surface)
{
    /* Surface changed, force full frame on next update */
    if (new_surface) {
        /* Trigger an immediate update with the new surface */
        orovideo_update(dcl, 0, 0, 
                       surface_width(new_surface), 
                       surface_height(new_surface));
    }
}

static void orovideo_refresh(DisplayChangeListener *dcl)
{
    /* Periodically called - trigger a frame capture */
    OroVideoState *ovs = container_of(dcl, OroVideoState, dcl);
    DisplaySurface *surface = qemu_console_surface(ovs->con);
    
    if (surface) {
        orovideo_update(dcl, 0, 0, surface_width(surface), surface_height(surface));
    }
    
    graphic_hw_update(ovs->con);
}

static const DisplayChangeListenerOps orovideo_dcl_ops = {
    .dpy_name          = "orovideo",
    .dpy_gfx_update    = orovideo_update,
    .dpy_gfx_switch    = orovideo_switch,
    .dpy_refresh       = orovideo_refresh,
};

void orovideo_display_init(Chardev *chr)
{
    OroVideoState *ovs;
    
    if (!chr) {
        return;
    }
    
    ovs = g_new0(OroVideoState, 1);
    
    if (!qemu_chr_fe_init(&ovs->chr, chr, &error_abort)) {
        g_free(ovs);
        return;
    }
    
    ovs->con = qemu_console_lookup_by_index(0);
    if (!ovs->con) {
        ovs->con = qemu_console_lookup_by_index(0);
    }
    
    ovs->dcl.ops = &orovideo_dcl_ops;
    ovs->width = 0;
    ovs->height = 0;
    
    register_displaychangelistener(&ovs->dcl);
}
