#ifndef _USB_DESCRIPTORS_H_
#define _USB_DESCRIPTORS_H_

void setUniqueSerial();

/* Time stamp base clock. It is a deprecated parameter. */
#define UVC_CLOCK_FREQUENCY 27000000
/* video capture path */
#define UVC_ENTITY_CAP_INPUT_TERMINAL 0x01
#define UVC_ENTITY_CAP_OUTPUT_TERMINAL 0x02

#define FRAME_WIDTH 160
#define FRAME_HEIGHT 144
#if CFG_TUD_VIDEO_STREAMING_BULK == 1
    #define FRAME_RATE 30 //Note, that 30fps cannot be achieved this way at the moment.
#else
    #define FRAME_RATE 29
#endif
#define FRAME_RATE_STEP 100 //Number of steps into which the interval of FRAME_RATE should be divided to offer fine grain selection (note that this refers to the interval, hence the fps options are not evenly divided by this)
#define FRAME_RATE_STEP_INTERVAL (10000000/FRAME_RATE/FRAME_RATE_STEP)
//We are close to the limit for isochronous transfer for the rp2040
//Max endpoint buffer size is 1023 B
//Full Game Boy resolution at 30 fps and 12bit NV12 (Windows support) requires
//160*144*30*12/8 = 1036800 and therefore 1037 B per bInterval. Limit is 1023 B. So close... But we also need a bit of headroom.

enum {
    ITF_NUM_VIDEO_CONTROL,
    ITF_NUM_VIDEO_STREAMING,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define TUD_VIDEO_CAPTURE_DESC_LEN (                                                                                                                                                                                                                           \
    TUD_VIDEO_DESC_IAD_LEN                                                                                                                                                                                                      /* control */                  \
    + TUD_VIDEO_DESC_STD_VC_LEN + (TUD_VIDEO_DESC_CS_VC_LEN + 1 /*bInCollection*/) + TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN                                                                            /* Interface 1, Alternate 0 */ \
    + TUD_VIDEO_DESC_STD_VS_LEN + (TUD_VIDEO_DESC_CS_VS_IN_LEN + 1 /*bNumFormats x bControlSize*/) + TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN + TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT_LEN + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN /* Interface 1, Alternate 1 */ \
    + TUD_VIDEO_DESC_STD_VS_LEN + 7                                                                                                                                                                                             /* Endpoint */                 \
)

#define TUD_VIDEO_CAPTURE_DESC_BULK_LEN (                                                                                                                                                                                                                           \
    TUD_VIDEO_DESC_IAD_LEN                                                                                                                                                                                                      /* control */                  \
    + TUD_VIDEO_DESC_STD_VC_LEN + (TUD_VIDEO_DESC_CS_VC_LEN + 1 /*bInCollection*/) + TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN                                                                            /* Interface 1, Alternate 0 */ \
    + TUD_VIDEO_DESC_STD_VS_LEN + (TUD_VIDEO_DESC_CS_VS_IN_LEN + 1 /*bNumFormats x bControlSize*/) + TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN + TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT_LEN + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN /* Interface 1, Alternate 1 */ \
    + 7                                                                                                                                                                                             /* Endpoint */                 \
)

/* Windows support YUY2 and NV12
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/stream/usb-video-class-driver-overview */

#define TUD_VIDEO_DESC_CS_VS_FMT_YUY2(_fmtidx, _numfmtdesc, _frmidx, _asrx, _asry, _interlace, _cp) \
    TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(_fmtidx, _numfmtdesc, TUD_VIDEO_GUID_YUY2, 16, _frmidx, _asrx, _asry, _interlace, _cp)
#define TUD_VIDEO_DESC_CS_VS_FMT_NV12(_fmtidx, _numfmtdesc, _frmidx, _asrx, _asry, _interlace, _cp) \
    TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(_fmtidx, _numfmtdesc, TUD_VIDEO_GUID_NV12, 12, _frmidx, _asrx, _asry, _interlace, _cp)
#define TUD_VIDEO_DESC_CS_VS_FMT_M420(_fmtidx, _numfmtdesc, _frmidx, _asrx, _asry, _interlace, _cp) \
    TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(_fmtidx, _numfmtdesc, TUD_VIDEO_GUID_M420, 12, _frmidx, _asrx, _asry, _interlace, _cp)
#define TUD_VIDEO_DESC_CS_VS_FMT_I420(_fmtidx, _numfmtdesc, _frmidx, _asrx, _asry, _interlace, _cp) \
    TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(_fmtidx, _numfmtdesc, TUD_VIDEO_GUID_I420, 12, _frmidx, _asrx, _asry, _interlace, _cp)

#define TUD_VIDEO_CAPTURE_DESCRIPTOR(_stridx, _epin, _width, _height, _fps, _fps_step, _fps_step_interval, _epsize)                                                                           \
    TUD_VIDEO_DESC_IAD(ITF_NUM_VIDEO_CONTROL, /* 2 Interfaces */ 0x02, _stridx), /* Video control 0 */                                                         \
        TUD_VIDEO_DESC_STD_VC(ITF_NUM_VIDEO_CONTROL, 0, _stridx),                                                                                              \
        TUD_VIDEO_DESC_CS_VC(/* UVC 1.5*/ 0x0150, /* wTotalLength - bLength */                                                                                 \
                             TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN,                                                                  \
                             UVC_CLOCK_FREQUENCY, ITF_NUM_VIDEO_STREAMING),                                                                                    \
        TUD_VIDEO_DESC_CAMERA_TERM(UVC_ENTITY_CAP_INPUT_TERMINAL, 0, 0,                                                                                        \
                                   /*wObjectiveFocalLengthMin*/ 0, /*wObjectiveFocalLengthMax*/ 0,                                                             \
                                   /*wObjectiveFocalLength*/ 0, /*bmControls*/ 0),                                                                             \
        TUD_VIDEO_DESC_OUTPUT_TERM(UVC_ENTITY_CAP_OUTPUT_TERMINAL, VIDEO_TT_STREAMING, 0, 1, 0), /* Video stream alt. 0 */                                     \
        TUD_VIDEO_DESC_STD_VS(ITF_NUM_VIDEO_STREAMING, 0, 0, _stridx),                           /* Video stream header for without still image capture */     \
        TUD_VIDEO_DESC_CS_VS_INPUT(/*bNumFormats*/ 1,                                            /*wTotalLength - bLength */                                   \
                                   TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN + TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT_LEN + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN, \
                                   _epin, /*bmInfo*/ 0, /*bTerminalLink*/ UVC_ENTITY_CAP_OUTPUT_TERMINAL,                                                      \
                                   /*bStillCaptureMethod*/ 0, /*bTriggerSupport*/ 0, /*bTriggerUsage*/ 0,                                                      \
                                   /*bmaControls(1)*/ 0), /* Video stream format */                                                                            \
        TUD_VIDEO_DESC_CS_VS_FMT_NV12(/*bFormatIndex*/ 1, /*bNumFrameDescriptors*/ 1,                                                                          \
                                      /*bDefaultFrameIndex*/ 1, 0, 0, 0, /*bCopyProtect*/ 0), /* Video stream frame format */                                  \
        TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT(/*bFrameIndex */ 1, 0, _width, _height,                                                                          \
                                              _width *_height * 12, _width * _height * 12 * _fps,                                                              \
                                              _width * _height * 12 / 8,                                                                                       \
                                              _fps_step_interval*_fps_step,         /*Frame interval = FRAME_RATE rounded to multiple of interval step*/       \
                                              _fps_step_interval*_fps_step,         /*Min frame interval = FRAME_RATE rounded to multiple of interval step*/   \
                                              _fps_step_interval*_fps_step*_fps,    /*Max frame interval = 1fps rounded to multiple of interval step*/         \
                                              _fps_step_interval),                  /*Frame interval step = 0.1fps*/                                           \
        TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(VIDEO_COLOR_PRIMARIES_BT709, VIDEO_COLOR_XFER_CH_BT709, VIDEO_COLOR_COEF_SMPTE170M), /* VS alt 1 */                \
        TUD_VIDEO_DESC_STD_VS(ITF_NUM_VIDEO_STREAMING, 1, 1, _stridx),                                                           /* EP */                      \
        TUD_VIDEO_DESC_EP_ISO(_epin, _epsize, 1)

#define TUD_VIDEO_CAPTURE_DESCRIPTOR_BULK(_stridx, _epin, _width, _height, _fps, _fps_step, _fps_step_interval, _epsize)                                                                           \
    TUD_VIDEO_DESC_IAD(ITF_NUM_VIDEO_CONTROL, /* 2 Interfaces */ 0x02, _stridx), /* Video control 0 */                                                         \
        TUD_VIDEO_DESC_STD_VC(ITF_NUM_VIDEO_CONTROL, 0, _stridx),                                                                                              \
        TUD_VIDEO_DESC_CS_VC(/* UVC 1.5*/ 0x0150, /* wTotalLength - bLength */                                                                                 \
                             TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN,                                                                  \
                             UVC_CLOCK_FREQUENCY, ITF_NUM_VIDEO_STREAMING),                                                                                    \
        TUD_VIDEO_DESC_CAMERA_TERM(UVC_ENTITY_CAP_INPUT_TERMINAL, 0, 0,                                                                                        \
                                   /*wObjectiveFocalLengthMin*/ 0, /*wObjectiveFocalLengthMax*/ 0,                                                             \
                                   /*wObjectiveFocalLength*/ 0, /*bmControls*/ 0),                                                                             \
        TUD_VIDEO_DESC_OUTPUT_TERM(UVC_ENTITY_CAP_OUTPUT_TERMINAL, VIDEO_TT_STREAMING, 0, 1, 0), /* Video stream alt. 0 */                                     \
        TUD_VIDEO_DESC_STD_VS(ITF_NUM_VIDEO_STREAMING, 0, 1, _stridx),                           /* Video stream header for without still image capture */     \
        TUD_VIDEO_DESC_CS_VS_INPUT(/*bNumFormats*/ 1,                                            /*wTotalLength - bLength */                                   \
                                   TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN + TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT_LEN + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN, \
                                   _epin, /*bmInfo*/ 0, /*bTerminalLink*/ UVC_ENTITY_CAP_OUTPUT_TERMINAL,                                                      \
                                   /*bStillCaptureMethod*/ 0, /*bTriggerSupport*/ 0, /*bTriggerUsage*/ 0,                                                      \
                                   /*bmaControls(1)*/ 0), /* Video stream format */                                                                            \
        TUD_VIDEO_DESC_CS_VS_FMT_NV12(/*bFormatIndex*/ 1, /*bNumFrameDescriptors*/ 1,                                                                          \
                                      /*bDefaultFrameIndex*/ 1, 0, 0, 0, /*bCopyProtect*/ 0), /* Video stream frame format */                                  \
        TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_CONT(/*bFrameIndex */ 1, 0, _width, _height,                                                                          \
                                              _width *_height * 12, _width * _height * 12 * _fps,                                                              \
                                              _width * _height * 12 / 8,                                                                                       \
                                              _fps_step_interval*_fps_step,         /*Frame interval = FRAME_RATE rounded to multiple of interval step*/       \
                                              _fps_step_interval*_fps_step,         /*Min frame interval = FRAME_RATE rounded to multiple of interval step*/   \
                                              _fps_step_interval*_fps_step*_fps,    /*Max frame interval = 1fps rounded to multiple of interval step*/         \
                                              _fps_step_interval),                  /*Frame interval step = 0.1fps*/                                           \
        TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(VIDEO_COLOR_PRIMARIES_BT709, VIDEO_COLOR_XFER_CH_BT709, VIDEO_COLOR_COEF_SMPTE170M), /* VS alt 1 */                \
        TUD_VIDEO_DESC_EP_BULK(_epin, _epsize, 1)

#endif