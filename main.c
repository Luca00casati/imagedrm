#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string.h>
// #include <sys/ioctl.h>
#include <sys/mman.h>

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
            // printf("connector: %d\ncount connector: %d\n", res->connectors[i], res->count_connectors);
            // printf("crtc: %d\ncount crtc: %d\n", res->crtcs[i], res->crtcs[i]);
            // printf("connector type: %d\nconnector type id: %d\n", connector->connector_type, connector->connector_type_id);
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

    fdcard = open(MY_DRI_CARD, O_RDWR);
    if (fdcard < 0)
    {
        fprintf(stderr, "Could not open dri device %s\n", MY_DRI_CARD);
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

    /*
        printf("horizontal parameters\ndisplay: %d\nsync_start: %d\nsync_end: %d\ntotal: %d\n", resolution->hdisplay, resolution->hsync_start, resolution->hsync_end, resolution->htotal);
        printf("vertical parameters\ndisplay: %d\nsync_start: %d\nsync_end: %d\ntotal: %d\n", resolution->vdisplay, resolution->vsync_start, resolution->vsync_end, resolution->vtotal);
    */
    u_int32_t handle_db, pitch_db;
    u_int64_t size_db;
    if (drmModeCreateDumbBuffer(fdcard, resolution->hdisplay, resolution->vdisplay, 32, 0, &handle_db, &pitch_db, &size_db))
    {
        fprintf(stderr, "fail to create framebuffer\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

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

    u_int64_t offset_db;
    if (drmModeMapDumbBuffer(fdcard, handle_db, &offset_db))
    {
        fprintf(stderr, "fail to prepare map dumbbuffer\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    data_db = mmap(0, size_db, PROT_READ | PROT_WRITE, MAP_SHARED, fdcard, offset_db);
    if (data_db == MAP_FAILED)
    {
        fprintf(stderr, "fail to map data\n");
        ret_value = MY_ERR_RET;
        goto go_exit;
    }

    memset(data_db, 0xffffffff, size_db);

    drmModeSetCrtc(fdcard, crtc->crtc_id, fb_id, 0, 0, &connector->connector_id, 1, resolution);

    sleep(5);

    /* reset */
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
    if (fdcard)
    {
        close(fdcard);
    }
    return ret_value;
}