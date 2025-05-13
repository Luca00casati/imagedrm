#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <linux/kd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mman.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

void drawrecttext(u_int32_t *framebuffer, int stride,
                  int xpos, int ypos, const char *text, u_int32_t color, stbtt_fontinfo font, float font_size)
{

    float scale = stbtt_ScaleForPixelHeight(&font, font_size); // font size in pixels
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    int baseline = (int)(ascent * scale);

    int x = xpos;
    for (const char *p = text; *p; p++)
    {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, *p, &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, *p, scale, scale, &x0, &y0, &x1, &y1);

        int w = x1 - x0;
        int h = y1 - y0;
        unsigned char *bitmap = malloc(w * h);

        stbtt_MakeCodepointBitmap(&font, bitmap, w, h, w, scale, scale, *p);

        for (int yb = 0; yb < h; yb++)
        {
            for (int xb = 0; xb < w; xb++)
            {
                int alpha = bitmap[yb * w + xb];
                if (alpha > 0)
                {
                    int px = x + x0 + xb;
                    int py = ypos + baseline + y0 + yb;
                    int pixel_index = py * (stride / 4) + px;
                    framebuffer[pixel_index] = color;
                }
            }
        }

        x += (int)(advance * scale);
        free(bitmap);
    }
}

int is_keyboard_device(const char *devpath)
{
    int fd = open(devpath, O_RDONLY);
    if (fd < 0)
        return 0;

    unsigned long evbit = 0;
    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
    if (!(evbit & (1 << EV_KEY)))
    {
        close(fd);
        return 0;
    }

    char name[256] = "my_keyboard";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    if (strcasestr(name, "keyboard") || strcasestr(name, "kbd"))
    {
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

// Allocates a new string with the full path and returns it via out_path
int find_keyboard_device(char **out_path)
{
    DIR *dir = opendir("/dev/input");
    if (!dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strncmp(entry->d_name, "event", 5) == 0)
        {
            size_t path_len = strlen("/dev/input") + 1 + strlen(entry->d_name) + 1;
            char *devpath = malloc(path_len);
            if (!devpath)
            {
                closedir(dir);
                return -1;
            }

            snprintf(devpath, path_len, "%s/%s", "/dev/input", entry->d_name);

            if (is_keyboard_device(devpath))
            {
                *out_path = devpath;
                closedir(dir);
                return 0;
            }

            free(devpath); // not a match, free and continue
        }
    }

    closedir(dir);
    return -1;
}

int wait_for_key_q(int fdkey)
{
    struct input_event ev;

    while (1)
    {
        ssize_t n = read(fdkey, &ev, sizeof(ev));
        if (n == sizeof(ev))
        {
            if (ev.type == EV_KEY && ev.value == 1) // key press
            {
                if (ev.code == KEY_Q)
                {
                    break;
                }
            }
        }
        usleep(10000); // small delay to avoid busy loop
    }

    return 0;
}

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

int fdtty = 0;

void sig_handler(int sig)
{
    if (fdtty)
    {
        (void)sig; // Suppress unused parameter warning
        ioctl(fdtty, KDSKBMODE, K_XLATE);
    }
}

int main()
{
    /* Open the dri device /dev/dri/cardX */
#define MY_DRI_CARD "/dev/dri/card1"
#define MY_TTY "/dev/tty4"
#define MY_ERR_RET -1
#define IMAGE "resources/image/landscape-ai-art_1952x1120.jpg"
#define FONT "resources/font/COMIC.TTF"
#define TEXT "LANDSCAPE AI"

    u_int32_t *data_db = NULL;
    drmModeResPtr res = NULL;
    drmModeConnectorPtr connector = NULL;
    drmModeModeInfoPtr resolution = NULL;
    drmModeEncoderPtr encoder = NULL;
    drmModeCrtcPtr crtc = NULL;
    int fdcard = 0;
    int fdkeyboard = 0;
    FILE *fdfont = 0;
    unsigned char *ttf_buffer = NULL;
    int ret_value = 0;
    const float font_size = 48.0f;
    unsigned char *data_image = NULL;
    unsigned char *data_image_scale = NULL;
    char *keyboard_dev_str = NULL;

    signal(SIGINT, sig_handler);

    const u_int32_t bg = rgb(255, 255, 255);

    fdfont = fopen(FONT, "rb");
    if (!fdfont)
    {
        fprintf(stderr, "Failed to load font\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    fseek(fdfont, 0, SEEK_END);
    long fdfont_size = ftell(fdfont);
    fseek(fdfont, 0, SEEK_SET);

    ttf_buffer = malloc(fdfont_size);
    if (ttf_buffer == NULL)
    {
        fprintf(stderr, "Failed to malloc font\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    fread(ttf_buffer, 1, fdfont_size, fdfont);

    stbtt_fontinfo fontinfo;
    if (!stbtt_InitFont(&fontinfo, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0)))
    {
        fprintf(stderr, "Failed to initialize font\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    fdtty = open(MY_TTY, O_RDWR);
    if (fdtty < 0)
    {
        fprintf(stderr, "Could not open tty: %s\n", MY_TTY);
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* disable tty keyboard */
    if (ioctl(fdtty, KDSKBMODE, K_OFF) < 0)
    {
        fprintf(stderr, "error KDSKBMODE K_OFF\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* set tty grafics only
    if (ioctl(fdtty, KDSETMODE, KD_GRAPHICS) < 0)
    {
        fprintf(stderr, "error KDSKBMODE KD_GRAPHICS\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }
    */

    if (find_keyboard_device(&keyboard_dev_str) != 0)
    {
        fprintf(stderr, "Could not find a keyboard input device\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    printf("Using device: %s\n", keyboard_dev_str);

    fdcard = open(MY_DRI_CARD, O_RDWR);
    if (fdcard < 0)
    {
        fprintf(stderr, "Could not open dri device card: %s\n", MY_DRI_CARD);
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    fdkeyboard = open(keyboard_dev_str, O_RDWR);
    if (fdkeyboard < 0)
    {
        fprintf(stderr, "Could not open keyboard device: %s\n", keyboard_dev_str);
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
    data_image = stbi_load(IMAGE, &image_x, &image_y, &image_chan, 4);
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

    int image_center_offset_x = (resolution->hdisplay - image_x_scale) / 2;
    int image_center_offset_y = (resolution->vdisplay - image_y_scale) / 2;

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

    drawrectimg(data_db, pitch_db, image_center_offset_x, image_center_offset_y, image_x_scale + image_center_offset_x, image_y_scale + image_center_offset_y, data_image_scale, image_x_scale);

    drawrecttext(data_db, pitch_db, 400, 30, TEXT, rgb(0, 0, 0), fontinfo, font_size);

    drmModeSetCrtc(fdcard, crtc->crtc_id, fb_id, 0, 0, &connector->connector_id, 1, resolution);

    wait_for_key_q(fdkeyboard);

    /* restore tty keyboard */
    if (ioctl(fdtty, KDSKBMODE, K_XLATE) < 0)
    {
        fprintf(stderr, "KDSKBMODE restore keyboard failed\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    /* set tty text
    if (ioctl(fdtty, KDSKBMODE, KD_TEXT) < 0)
    {
        fprintf(stderr, "KDSKBMODE restore text failed\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }
    */

    /* restore tty grafics */
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
    if (keyboard_dev_str != NULL)
    {
        free(keyboard_dev_str);
    }
    if (fdkeyboard)
    {
        close(fdkeyboard);
    }
    if (fdcard)
    {
        close(fdcard);
    }
    if (fdtty)
    {
        close(fdtty);
    }
    if (fdfont)
    {
        fclose(fdfont);
    }
    if (ttf_buffer != NULL)
    {
        free(ttf_buffer);
    }
    return ret_value;
}