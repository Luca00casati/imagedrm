#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string.h>
#include <sys/ioctl.h>
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
        drmModeFreeConnector(connector);
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

    int fdcard = open(MY_DRI_CARD, O_RDWR);
    if (fdcard < 0)
    {
        fprintf(stderr, "Could not open dri device %s\n", MY_DRI_CARD);
        return -1;
    }

    /* Get the resources of the DRM device (connectors, encoders, etc.)*/
    drmModeResPtr res = drmModeGetResources(fdcard);
    if (!res)
    {
        printf("Could not get drm resources\n");
        
        return -1;
    }
    /* get first connected */
    drmModeConnectorPtr connector = getFistConnectedConnector(fdcard, res);
    if (connector == NULL)
    {
        fprintf(stderr, "fail to connect connector\n");
        return -1;
    }

    /* Get the preferred resolution */
    drmModeModeInfoPtr resolution = getPreferredMode(connector);
    if (resolution == NULL)
    {
        fprintf(stderr, "fail to Get the preferred resolution\n");
        return -1;
    }
    printf("horizontal parameters\ndisplay: %d\nsync_start: %d\nsync_end: %d\ntotal: %d\n", resolution->hdisplay, resolution->hsync_start, resolution->hsync_end, resolution->htotal);
    printf("vertical parameters\ndisplay: %d\nsync_start: %d\nsync_end: %d\ntotal: %d\n", resolution->vdisplay, resolution->vsync_start, resolution->vsync_end, resolution->vtotal);

    u_int32_t handle_db, pitch_db;
    u_int64_t size_db;
    if (drmModeCreateDumbBuffer(fdcard, resolution->hdisplay, resolution->vdisplay, 32, 0, &handle_db, &pitch_db, &size_db))
    {
        fprintf(stderr, "fail to create framebuffer\n");
        return -1;
    }

    uint32_t fb_id;
    if(drmModeAddFB(fdcard, resolution->hdisplay, resolution->vdisplay, 24, 32, pitch_db, handle_db, &fb_id)){
        fprintf(stderr, "fail to add framebuffer\n");
        return -1;
    }

    drmModeEncoderPtr encoder = drmModeGetEncoder(fdcard, connector->encoder_id);
    if (encoder == NULL)
    {
        fprintf(stderr, "fail to Get the encoder\n");
        return -1;
    }

    drmModeCrtcPtr crtc = drmModeGetCrtc(fdcard, encoder->crtc_id);
    if (crtc == NULL)
    {
        fprintf(stderr, "fail to Get the crtc\n");
        return -1;
    }

    u_int64_t offset_db;
    if (drmModeMapDumbBuffer(fdcard, handle_db, &offset_db)){
        fprintf(stderr, "fail to prepare map dumbbuffer\n");
        return -1;
    }

    u_int32_t* data_db = mmap(0, size_db, PROT_READ | PROT_WRITE, MAP_SHARED, fdcard, offset_db);
    if (data_db == MAP_FAILED){
        fprintf(stderr, "fail to map data\n");
        return -1;
    }

    memset(data_db, 0xffffffff, size_db);

    //drmSetMaster(fdcard);
    drmModeSetCrtc(fdcard, crtc->crtc_id, 0, 0, 0, NULL, 0, NULL);
    /* reset */
    drmModeSetCrtc(fdcard, crtc->crtc_id, fb_id, 0, 0, &connector->connector_id, 1, resolution);
    //drmDropMaster(fdcard);


    sleep(5);

    drmModeSetCrtc(fdcard, crtc->crtc_id,
        crtc->buffer_id,
        crtc->x, crtc->y,
        &connector->connector_id, 1,
        &crtc->mode);
    drmModeFreeCrtc(crtc);

    munmap(data_db, size_db);
    close(fdcard);
    return 0;
}