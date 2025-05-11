#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string.h>
// #include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mman.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

u_int32_t hexrgb(const u_int32_t rgb)
{
    return (rgb | 0xff000000);
}

u_int32_t rgb(const u_int32_t r, const u_int32_t g, const u_int32_t b)
{
    u_int32_t a = 255;
    return ((a << 24) | (r << 16) | (g << 8) | b);
}

float ratio(float a, float b)
{
    return (MAX(a, b) / MIN(a, b));
}

/*
u_int32_t argb(const u_int32_t a, const u_int32_t r, const u_int32_t g, const u_int32_t b)
{
    return ((a << 24) | (r << 16) | (g << 8) | b);
}
*/
void drawrect(u_int32_t *data, int stripe, int x1, int y1, int x2, int y2, u_int32_t color)
{
    for (int y = y1; y < y2; y++)
    {
        for (int x = x1; x < x2; x++)
        {
            int pix_pos = y * (stripe / 4) + x;
            data[pix_pos] = color;
        }
    }
}

void drawrectimg(u_int32_t *data, int stripe, int x1, int y1, int x2, int y2,
                 unsigned char *data_image, int image_x)
{
    int stride = stripe / 4; // framebuffer stride in pixels
    for (int y = y1; y < y2; y++)
    {
        for (int x = x1; x < x2; x++)
        {
            int fb_index = y * stride + x;
            int img_x = x - x1;
            int img_y = y - y1;
            int img_index = (img_y * image_x + img_x) * 4;

            unsigned char r = data_image[img_index + 0];
            unsigned char g = data_image[img_index + 1];
            unsigned char b = data_image[img_index + 2];

            data[fb_index] = rgb(r, g, b);
        }
    }
}

drmModeConnectorPtr getFistConnectedConnector(int fdcard, drmModeResPtr res)
{
    drmModeConnectorPtr connector = 0;
    for (int i = 0; i < res->count_connectors; i++)
    {
        connector = drmModeGetConnectorCurrent(fdcard, res->connectors[i]);
        if (!connector)
            continue;

        if (connector->connection == DRM_MODE_CONNECTED)
        {
            return connector;
        }
    }
    return NULL;
}

/* Get the preferred resolution */
drmModeModeInfoPtr getPreferredMode(drmModeConnectorPtr connector)
{
    for (int i = 0; i < connector->count_modes; i++)
    {
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED)
            return &connector->modes[i];
    }
    return NULL;
}

int main()
{
/* Open the dri device /dev/dri/cardX */
#define MY_DRI_CARD "/dev/dri/card1"
#define MY_ERR_RET -1

    u_int32_t *data_db = NULL;
    drmModeResPtr res = NULL;
    drmModeConnectorPtr connector = NULL;
    drmModeModeInfoPtr resolution = NULL;
    drmModeEncoderPtr encoder = NULL;
    drmModeCrtcPtr crtc = NULL;
    int fdcard = 0;
    int ret_value = 0;
    unsigned char *data_image = NULL;
    unsigned char *data_image_scale = NULL;

    u_int32_t bg = rgb(255, 255, 255);

    fdcard = open(MY_DRI_CARD, O_RDWR);
    if (fdcard < 0)
    {
        fprintf(stderr, "Could not open dri device card: %s\n", MY_DRI_CARD);
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* Get the resources of the DRM device (connectors, encoders, etc.)*/
    res = drmModeGetResources(fdcard);
    if (!res)
    {
        printf("Could not get drm resources\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }
    /* get first connected */
    connector = getFistConnectedConnector(fdcard, res);
    if (connector == NULL)
    {
        fprintf(stderr, "fail to connect connector\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* Get the preferred resolution */
    resolution = getPreferredMode(connector);
    if (resolution == NULL)
    {
        fprintf(stderr, "fail to Get the preferred resolution\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    u_int32_t handle_db, pitch_db;
    u_int64_t size_db;
    if (drmModeCreateDumbBuffer(fdcard, resolution->hdisplay, resolution->vdisplay, 32, 0, &handle_db, &pitch_db, &size_db))
    {
        fprintf(stderr, "fail to create framebuffer\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    u_int64_t offset_db;
    if (drmModeMapDumbBuffer(fdcard, handle_db, &offset_db))
    {
        fprintf(stderr, "fail to prepare map dumbbuffer\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    int image_x, image_y, image_chan;
    /* force 4 channel */
    data_image = stbi_load("resources/landscape-ai-art_1952x1120.jpg", &image_x, &image_y, &image_chan, 4);
    image_chan = 4;
    if (data_image == NULL)
    {
        fprintf(stderr, "fail to load image\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }
    float image_ratio = ratio((float)image_x, (float)image_y);
    printf("image_x: %d image_y: %d ratio: %f\n", image_x, image_y, image_ratio);

    int scale_factor = 2;
    int image_x_scale = image_x / scale_factor;
    int image_y_scale = image_y / scale_factor;
    data_image_scale = malloc(image_x_scale * image_y_scale * 4);
    if (data_image_scale == NULL)
    {
        fprintf(stderr, "fail to scale image\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }
    float image_scale_ratio = ratio((float)image_x_scale, (float)image_y_scale);
    printf("image_x_scale: %d image_y_scale: %d ratio_scale: %f\n", image_x_scale, image_y_scale, image_scale_ratio);

    stbir_resize_uint8_srgb(data_image, image_x, image_y, 0,
                            data_image_scale, image_x_scale, image_y_scale, 0,
                            4);

    int border_offset_x = (resolution->hdisplay - image_x_scale) / 2;
    int border_offset_y = (resolution->vdisplay - image_y_scale) / 2;

    uint32_t fb_id;
    if (drmModeAddFB(fdcard, resolution->hdisplay, resolution->vdisplay, 24, 32, pitch_db, handle_db, &fb_id))
    {
        fprintf(stderr, "fail to add framebuffer\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    encoder = drmModeGetEncoder(fdcard, connector->encoder_id);
    if (encoder == NULL)
    {
        fprintf(stderr, "fail to Get the encoder\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    crtc = drmModeGetCrtc(fdcard, encoder->crtc_id);
    if (crtc == NULL)
    {
        fprintf(stderr, "fail to Get the crtc\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    float my_dispay_ratio = ratio((float)resolution->hdisplay, (float)resolution->vdisplay);
    printf("hdisplay: %d vdisplay: %d ratio: %f\n", resolution->hdisplay, resolution->vdisplay, my_dispay_ratio);

    data_db = mmap(0, size_db, PROT_READ | PROT_WRITE, MAP_SHARED, fdcard, offset_db);
    if (data_db == MAP_FAILED)
    {
        fprintf(stderr, "fail to map data\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* bg */
    for (uint32_t i = 0; i < size_db / sizeof(*data_db); i++)
    {
        data_db[i] = bg;
    }

    // drawrect(data_db, pitch_db, 0, 0, 100, 100, RGB(0, 0, 0));

    drawrectimg(data_db, pitch_db, border_offset_x, border_offset_y, image_x_scale + border_offset_x, image_y_scale + border_offset_y, data_image_scale, image_x_scale);

    drmModeSetCrtc(fdcard, crtc->crtc_id, fb_id, 0, 0, &connector->connector_id, 1, resolution);

    sleep(5);

    /* restore */
    drmModeSetCrtc(fdcard, crtc->crtc_id,
                   crtc->buffer_id,
                   crtc->x, crtc->y,
                   &connector->connector_id, 1,
                   &crtc->mode);

go_exit:
    if (data_db != NULL)
    {
        munmap(data_db, size_db);
    }
    if (encoder != NULL)
    {
        drmModeFreeEncoder(encoder);
    }
    if (crtc != NULL)
    {
        drmModeFreeCrtc(crtc);
    }
    if (res != NULL)
    {
        drmModeFreeResources(res);
    }
    if (connector != NULL)
    {
        drmModeFreeConnector(connector);
    }
    if (data_image != NULL)
    {
        stbi_image_free(data_image);
    }
    if (data_image_scale != NULL)
    {
        free(data_image_scale);
    }
    if (fdcard)
    {
        close(fdcard);
    }
    return ret_value;
}