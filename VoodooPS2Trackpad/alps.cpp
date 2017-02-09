/*
 * Copyright (c) 2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.2 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/hidsystem/IOHIDParameter.h>
#include "VoodooPS2Controller.h"
#include "alps.h"

enum {
    kTapEnabled = 0x01
};

#define ARRAY_SIZE(x)    (sizeof(x)/sizeof(x[0]))
#define MAX(X,Y)         ((X) > (Y) ? (X) : (Y))
#define abs(x) ((x) < 0 ? -(x) : (x))
#define BIT(x) (1 << (x))


/*
 * Definitions for ALPS version 3 and 4 command mode protocol
 */
#define ALPS_CMD_NIBBLE_10  0x01f2

#define ALPS_REG_BASE_RUSHMORE  0xc2c0
#define ALPS_REG_BASE_V7	0xc2c0
#define ALPS_REG_BASE_PINNACLE  0x0000

static const struct alps_nibble_commands alps_v3_nibble_commands[] = {
    { kDP_MouseSetPoll,                 0x00 }, /* 0 no send/recv */
    { kDP_SetDefaults,                  0x00 }, /* 1 no send/recv */
    { kDP_SetMouseScaling2To1,          0x00 }, /* 2 no send/recv */
    { kDP_SetMouseSampleRate | 0x1000,  0x0a }, /* 3 send=1 recv=0 */
    { kDP_SetMouseSampleRate | 0x1000,  0x14 }, /* 4 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x28 }, /* 5 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x3c }, /* 6 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x50 }, /* 7 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x64 }, /* 8 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0xc8 }, /* 9 ..*/
    { kDP_CommandNibble10    | 0x0100,  0x00 }, /* a send=0 recv=1 */
    { kDP_SetMouseResolution | 0x1000,  0x00 }, /* b send=1 recv=0 */
    { kDP_SetMouseResolution | 0x1000,  0x01 }, /* c ..*/
    { kDP_SetMouseResolution | 0x1000,  0x02 }, /* d ..*/
    { kDP_SetMouseResolution | 0x1000,  0x03 }, /* e ..*/
    { kDP_SetMouseScaling1To1,          0x00 }, /* f no send/recv */
};

static const struct alps_nibble_commands alps_v4_nibble_commands[] = {
    { kDP_Enable,                       0x00 }, /* 0 no send/recv */
    { kDP_SetDefaults,                  0x00 }, /* 1 no send/recv */
    { kDP_SetMouseScaling2To1,          0x00 }, /* 2 no send/recv */
    { kDP_SetMouseSampleRate | 0x1000,  0x0a }, /* 3 send=1 recv=0 */
    { kDP_SetMouseSampleRate | 0x1000,  0x14 }, /* 4 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x28 }, /* 5 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x3c }, /* 6 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x50 }, /* 7 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0x64 }, /* 8 ..*/
    { kDP_SetMouseSampleRate | 0x1000,  0xc8 }, /* 9 ..*/
    { kDP_CommandNibble10    | 0x0100,  0x00 }, /* a send=0 recv=1 */
    { kDP_SetMouseResolution | 0x1000,  0x00 }, /* b send=1 recv=0 */
    { kDP_SetMouseResolution | 0x1000,  0x01 }, /* c ..*/
    { kDP_SetMouseResolution | 0x1000,  0x02 }, /* d ..*/
    { kDP_SetMouseResolution | 0x1000,  0x03 }, /* e ..*/
    { kDP_SetMouseScaling1To1,          0x00 }, /* f no send/recv */
};

static const struct alps_nibble_commands alps_v6_nibble_commands[] = {
    { kDP_Enable,		            0x00 }, /* 0 */
    { kDP_SetMouseSampleRate,		0x0a }, /* 1 */
    { kDP_SetMouseSampleRate,		0x14 }, /* 2 */
    { kDP_SetMouseSampleRate,		0x28 }, /* 3 */
    { kDP_SetMouseSampleRate,		0x3c }, /* 4 */
    { kDP_SetMouseSampleRate,		0x50 }, /* 5 */
    { kDP_SetMouseSampleRate,		0x64 }, /* 6 */
    { kDP_SetMouseSampleRate,		0xc8 }, /* 7 */
    { kDP_GetId,		            0x00 }, /* 8 */
    { kDP_GetMouseInformation,		0x00 }, /* 9 */
    { kDP_SetMouseResolution,		0x00 }, /* a */
    { kDP_SetMouseResolution,		0x01 }, /* b */
    { kDP_SetMouseResolution,		0x02 }, /* c */
    { kDP_SetMouseResolution,		0x03 }, /* d */
    { kDP_SetMouseScaling2To1,	    0x00 }, /* e */
    { kDP_SetMouseScaling1To1,	    0x00 }, /* f */
};


#define ALPS_DUALPOINT          0x02    /* touchpad has trackstick */
#define ALPS_PASS               0x04    /* device has a pass-through port */

#define ALPS_WHEEL              0x08    /* hardware wheel present */
#define ALPS_FW_BK_1            0x10    /* front & back buttons present */
#define ALPS_FW_BK_2            0x20    /* front & back buttons present */
#define ALPS_FOUR_BUTTONS       0x40    /* 4 direction button present */
#define ALPS_PS2_INTERLEAVED    0x80    /* 3-byte PS/2 packet interleaved with
6-byte ALPS packet */
#define ALPS_STICK_BITS		    0x100	/* separate stick button bits */
#define ALPS_BUTTONPAD		    0x200	/* device is a clickpad */
#define ALPS_DUALPOINT_WITH_PRESSURE	0x400	/* device can report trackpoint pressure */


static const struct alps_model_info alps_model_data[] = {
    { { 0x32, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
    /* Toshiba Salellite Pro M10 */
    { { 0x33, 0x02, 0x0a }, 0x00, ALPS_PROTO_V1, 0x88, 0xf8, 0 },               /* UMAX-530T */
    { { 0x53, 0x02, 0x0a }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x53, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x60, 0x03, 0xc8 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },               /* HP ze1115 */
    { { 0x63, 0x02, 0x0a }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x63, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x63, 0x02, 0x28 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 },    /* Fujitsu Siemens S6010 */
    { { 0x63, 0x02, 0x3c }, 0x00, ALPS_PROTO_V2, 0x8f, 0x8f, ALPS_WHEEL },      /* Toshiba Satellite S2400-103 */
    { { 0x63, 0x02, 0x50 }, 0x00, ALPS_PROTO_V2, 0xef, 0xef, ALPS_FW_BK_1 },    /* NEC Versa L320 */
    { { 0x63, 0x02, 0x64 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x63, 0x03, 0xc8 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
    /* Dell Latitude D800 */
    { { 0x73, 0x00, 0x0a }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_DUALPOINT },  /* ThinkPad R61 8918-5QG */
    { { 0x73, 0x02, 0x0a }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, 0 },
    { { 0x73, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 },    /* Ahtec Laptop */
    { { 0x20, 0x02, 0x0e }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
    /* XXX */
    { { 0x22, 0x02, 0x0a }, 0x00, ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
    { { 0x22, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xff, 0xff, ALPS_PASS | ALPS_DUALPOINT },
    /* Dell Latitude D600 */
    /* Dell Latitude E5500, E6400, E6500, Precision M4400 */
    { { 0x62, 0x02, 0x14 }, 0x00, ALPS_PROTO_V2, 0xcf, 0xcf,
        ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED },
    { { 0x73, 0x02, 0x50 }, 0x00, ALPS_PROTO_V2, 0xcf, 0xcf, ALPS_FOUR_BUTTONS },
    /* Dell Vostro 1400 */
    { { 0x52, 0x01, 0x14 }, 0x00, ALPS_PROTO_V2, 0xff, 0xff,
        ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED },
    /* Toshiba Tecra A11-11L */
    { { 0x73, 0x02, 0x64 }, 0x8a, ALPS_PROTO_V4, 0x8f, 0x8f, 0 },
};

// =============================================================================
// AppleUSBMultitouchDriver Class Implementation
//

OSDefineMetaClassAndStructors(AppleUSBMultitouchDriver, VoodooPS2TouchPadBase);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

AppleUSBMultitouchDriver *AppleUSBMultitouchDriver::probe(IOService *provider, SInt32 *score) {
    DEBUG_LOG("AppleUSBMultitouchDriver::probe entered...\n");
    bool success;
    
    //
    // The driver has been instructed to verify the presence of the actual
    // hardware we represent. We are guaranteed by the controller that the
    // mouse clock is enabled and the mouse itself is disabled (thus it
    // won't send any asynchronous mouse data that may mess up the
    // responses expected by the commands we send it).
    //
    
    _device = (ApplePS2MouseDevice *) provider;
    
    _device->lock();
    resetMouse();
    
    if (identify() != 0) {
        success = false;
    } else {
        success = true;
    }
    _device->unlock();
    
    _device = 0;
    
    DEBUG_LOG("AppleUSBMultitouchDriver::probe leaving.\n");
    
    return success ? this : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool AppleUSBMultitouchDriver::deviceSpecificInit() {
    
    resetMouse();
    
    if (identify() != 0) {
        goto init_fail;
    }
    
    // Setup expected packet size
    priv.pktsize = priv.proto_version == ALPS_PROTO_V4 ? 8 : 6;
    
    IOLog("ALPS: TouchPad driver started...\n");
    
    if (!(this->*hw_init)()) {
        goto init_fail;
    }
    
    return true;
    
init_fail:
    IOLog("ALPS: Device initialization failed. Touchpad probably won't work\n");
    resetMouse();
    return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Link with Base Driver */
bool AppleUSBMultitouchDriver::init(OSDictionary *dict) {
    if (!super::init(dict)) {
        return false;
    }
    return true;
}

void AppleUSBMultitouchDriver::stop(IOService *provider) {
    resetMouse();
    
    super::stop(provider);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool AppleUSBMultitouchDriver::resetMouse() {
    TPS2Request<3> request;
    
    // Reset mouse
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_Reset;
    request.commands[1].command = kPS2C_ReadDataPort;
    request.commands[1].inOrOut = 0;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commandsCount = 3;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    // Verify the result
    if (request.commands[1].inOrOut != kSC_Reset && request.commands[2].inOrOut != kSC_ID) {
        DEBUG_LOG("Failed to reset mouse, return values did not match. [0x%02x, 0x%02x]\n", request.commands[1].inOrOut, request.commands[2].inOrOut);
        return false;
    }
    return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void AppleUSBMultitouchDriver::processPacketV1V2(UInt8 *packet) {
    int x, y, z, ges, fin, left, right, middle, buttons = 0, fingers = 0;
    int back = 0, forward = 0;
    uint64_t now_abs;
    
    clock_get_uptime(&now_abs);
    
    if (priv.proto_version == ALPS_PROTO_V1) {
        left = packet[2] & 0x10;
        right = packet[2] & 0x08;
        middle = 0;
        x = packet[1] | ((packet[0] & 0x07) << 7);
        y = packet[4] | ((packet[3] & 0x07) << 7);
        z = packet[5];
    } else {
        left = packet[3] & 1;
        right = packet[3] & 2;
        middle = packet[3] & 4;
        x = packet[1] | ((packet[2] & 0x78) << (7 - 3));
        y = packet[4] | ((packet[3] & 0x70) << (7 - 4));
        z = packet[5];
    }
    
    if (priv.flags & ALPS_FW_BK_1) {
        back = packet[0] & 0x10;
        forward = packet[2] & 4;
    }
    
    if (priv.flags & ALPS_FW_BK_2) {
        back = packet[3] & 4;
        forward = packet[2] & 4;
        if ((middle = forward && back)) {
            forward = back = 0;
        }
    }
    
    ges = packet[2] & 1;
    fin = packet[2] & 2;
    
    /* To make button reporting compatible with rest of driver */
    buttons |= left ? 0x01 : 0;
    buttons |= right ? 0x02 : 0;
    buttons |= middle ? 0x04 : 0;
    
    
    if ((priv.flags & ALPS_DUALPOINT) && z == 127) {
        int dx, dy;
        dx = x > 383 ? (x - 768) : x;
        dy = -(y > 255 ? (y - 512) : y);
        
        dispatchRelativePointerEventX(dx, dy, buttons, now_abs);
        return;
    }
    
    /* Some models have separate stick button bits */
    if (priv.flags & ALPS_STICK_BITS) {
        left |= packet[0] & 1;
        right |= packet[0] & 2;
        middle |= packet[0] & 4;
    }
    
    /* Convert hardware tap to a reasonable Z value */
    if (ges && !fin) {
        z = 40;
    }
    
    /*
     * A "tap and drag" operation is reported by the hardware as a transition
     * from (!fin && ges) to (fin && ges). This should be translated to the
     * sequence Z>0, Z==0, Z>0, so the Z==0 event has to be generated manually.
     */
    if (ges && fin && !priv.prev_fin) {
        touchmode = MODE_DRAG;
    }
    priv.prev_fin = fin;
    
    if (z > 30) {
        fingers = 1;
    }
    
    if (z < 25) {
        fingers = 0;
    }
    
    dispatchEventsWithInfo(x, y, 0, 0, z, fingers, buttons);
    
    if (priv.flags & ALPS_WHEEL) {
        int scrollAmount = ((packet[2] << 1) & 0x08) - ((packet[0] >> 4) & 0x07);
        if (scrollAmount) {
            dispatchScrollWheelEventX(scrollAmount, 0, 0, now_abs);
        }
    }
    
    /* OS X incompatible */
    
    /*if (priv->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
     input_report_key(dev, BTN_FORWARD, forward);
     input_report_key(dev, BTN_BACK, back);
     }
     
     if (priv->flags & ALPS_FOUR_BUTTONS) {
     input_report_key(dev, BTN_0, packet[2] & 4);
     input_report_key(dev, BTN_1, packet[0] & 0x10);
     input_report_key(dev, BTN_2, packet[3] & 4);
     input_report_key(dev, BTN_3, packet[0] & 0x20);
     }*/
}

static void alps_get_bitmap_points(unsigned int map,
                                   struct alps_bitmap_point *low,
                                   struct alps_bitmap_point *high,
                                   int *fingers)
{
    struct alps_bitmap_point *point;
    int i, bit, prev_bit = 0;
    
    point = low;
    for (i = 0; map != 0; i++, map >>= 1) {
        bit = map & 1;
        if (bit) {
            if (!prev_bit) {
                point->start_bit = i;
                point->num_bits = 0;
                (*fingers)++;
            }
            point->num_bits++;
        } else {
            if (prev_bit)
                point = high;
        }
        prev_bit = bit;
    }
}

/*
 * Process bitmap data from semi-mt protocols. Returns the number of
 * fingers detected. A return value of 0 means at least one of the
 * bitmaps was empty.
 *
 * The bitmaps don't have enough data to track fingers, so this function
 * only generates points representing a bounding box of all contacts.
 * These points are returned in fields->mt when the return value
 * is greater than 0.
 */
int AppleUSBMultitouchDriver::processBitmap(struct alps_data *priv,
                                            struct alps_fields *fields)
{
    
    int i, fingers_x = 0, fingers_y = 0, fingers, closest;
    struct alps_bitmap_point x_low = {0,}, x_high = {0,};
    struct alps_bitmap_point y_low = {0,}, y_high = {0,};
    struct input_mt_pos corner[4];
    
    
    if (!fields->x_map || !fields->y_map) {
        return 0;
    }
    
    alps_get_bitmap_points(fields->x_map, &x_low, &x_high, &fingers_x);
    alps_get_bitmap_points(fields->y_map, &y_low, &y_high, &fingers_y);
    
    /*
     * Fingers can overlap, so we use the maximum count of fingers
     * on either axis as the finger count.
     */
    fingers = max(fingers_x, fingers_y);
    
    /*
     * If an axis reports only a single contact, we have overlapping or
     * adjacent fingers. Divide the single contact between the two points.
     */
    if (fingers_x == 1) {
        i = x_low.num_bits / 2;
        x_low.num_bits = x_low.num_bits - i;
        x_high.start_bit = x_low.start_bit + i;
        x_high.num_bits = max(i, 1);
    }
    
    if (fingers_y == 1) {
        i = y_low.num_bits / 2;
        y_low.num_bits = y_low.num_bits - i;
        y_high.start_bit = y_low.start_bit + i;
        y_high.num_bits = max(i, 1);
    }
    
    /* top-left corner */
    corner[0].x = (priv->x_max * (2 * x_low.start_bit + x_low.num_bits - 1)) /
    (2 * (priv->x_bits - 1));
    corner[0].y = (priv->y_max * (2 * y_low.start_bit + y_low.num_bits - 1)) /
    (2 * (priv->y_bits - 1));
    
    /* top-right corner */
    corner[1].x = (priv->x_max * (2 * x_high.start_bit + x_high.num_bits - 1)) /
    (2 * (priv->x_bits - 1));
    corner[1].y = (priv->y_max * (2 * y_low.start_bit + y_low.num_bits - 1)) /
    (2 * (priv->y_bits - 1));
    
    /* bottom-right corner */
    corner[2].x = (priv->x_max * (2 * x_high.start_bit + x_high.num_bits - 1)) /
    (2 * (priv->x_bits - 1));
    corner[2].y = (priv->y_max * (2 * y_high.start_bit + y_high.num_bits - 1)) /
    (2 * (priv->y_bits - 1));
    
    /* bottom-left corner */
    corner[3].x = (priv->x_max * (2 * x_low.start_bit + x_low.num_bits - 1)) /
    (2 * (priv->x_bits - 1));
    corner[3].y = (priv->y_max * (2 * y_high.start_bit + y_high.num_bits - 1)) /
    (2 * (priv->y_bits - 1));
    
    /* x-bitmap order is reversed on v5 touchpads  */
    if (priv->proto_version == ALPS_PROTO_V5) {
        for (i = 0; i < 4; i++)
            corner[i].x = priv->x_max - corner[i].x;
    }
    
    /* y-bitmap order is reversed on v3 and v4 touchpads  */
    if (priv->proto_version == ALPS_PROTO_V3 || priv->proto_version == ALPS_PROTO_V4) {
        for (i = 0; i < 4; i++)
            corner[i].y = priv->y_max - corner[i].y;
    }
    
    /*
     * We only select a corner for the second touch once per 2 finger
     * touch sequence to avoid the chosen corner (and thus the coordinates)
     * jumping around when the first touch is in the middle.
     */
    if (priv->second_touch == -1) {
        /* Find corner closest to our st coordinates */
        closest = 0x7fffffff;
        for (i = 0; i < 4; i++) {
            int dx = fields->st.x - corner[i].x;
            int dy = fields->st.y - corner[i].y;
            int distance = dx * dx + dy * dy;
            
            if (distance < closest) {
                priv->second_touch = i;
                closest = distance;
            }
        }
        /* And select the opposite corner to use for the 2nd touch */
        priv->second_touch = (priv->second_touch + 2) % 4;
    }
    
    fields->mt[0] = fields->st;
    fields->mt[1] = corner[priv->second_touch];
    
    //IOLog("ALPS: Process Bitmap, Corner=%d, Fingers=%d, x1=%d, x2=%d, y1=%d, y2=%d\n", priv->second_touch, fingers, fields->mt[0].x, fields->mt[1].x, fields->mt[0].y, fields->mt[1].y);
    return fingers;
}

void AppleUSBMultitouchDriver::processTrackstickPacketV3(UInt8 *packet) {
    int x, y, z, left, right, middle;
    uint64_t now_abs;
    UInt32 buttons = 0, raw_buttons = 0;
    
    /* It should be a DualPoint when received trackstick packet */
    if (!(priv.flags & ALPS_DUALPOINT)) {
        return;
    }
    
    /* Sanity check packet */
    if (!(packet[0] & 0x40)) {
        DEBUG_LOG("ps2: bad trackstick packet, disregarding...\n");
        return;
    }
    
    /* There is a special packet that seems to indicate the end
     * of a stream of trackstick data. Filter these out
     */
    if (packet[1] == 0x7f && packet[2] == 0x7f && packet[3] == 0x7f) {
        return;
    }
    
    x = (SInt8) (((packet[0] & 0x20) << 2) | (packet[1] & 0x7f));
    y = (SInt8) (((packet[0] & 0x10) << 3) | (packet[2] & 0x7f));
    z = (packet[4] & 0x7c) >> 2;
    
    /* Prevent pointer jump on finger lift */
    if ((abs(x) >= 0x7f) && (abs(y) >= 0x7f)) {
        x = y = 0;
    }
    
    /*
     * The x and y values tend to be quite large, and when used
     * alone the trackstick is difficult to use. Scale them down
     * to compensate.
     */
    x /= 3;
    y /= 3;
    
    /* To get proper movement direction */
    y = -y;
    
    clock_get_uptime(&now_abs);
    
    /*
     * Most ALPS models report the trackstick buttons in the touchpad
     * packets, but a few report them here. No reliable way has been
     * found to differentiate between the models upfront, so we enable
     * the quirk in response to seeing a button press in the trackstick
     * packet.
     */
    left = packet[3] & 0x01;
    right = packet[3] & 0x02;
    middle = packet[3] & 0x04;
    
    if (!(priv.quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) &&
        (left || middle || right)) {
        priv.quirks |= ALPS_QUIRK_TRACKSTICK_BUTTONS;
    }
    
    if (priv.quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) {
        raw_buttons |= left ? 0x01 : 0;
        raw_buttons |= right ? 0x02 : 0;
        raw_buttons |= middle ? 0x04 : 0;
    }
    
    /* Button status can appear in normal packet */
    if (0 == raw_buttons) {
        buttons = lastbuttons;
    } else {
        buttons = raw_buttons;
        lastbuttons = buttons;
    }
    
    /* If middle button is pressed, switch to scroll mode. Else, move pointer normally */
    if (0 == (buttons & 0x04)) {
        dispatchRelativePointerEventX(x, y, buttons, now_abs);
    } else {
        dispatchScrollWheelEventX(-y, -x, 0, now_abs);
    }
}

bool AppleUSBMultitouchDriver::decodeButtonsV3(struct alps_fields *f, unsigned char *p) {
    f->left = !!(p[3] & 0x01);
    f->right = !!(p[3] & 0x02);
    f->middle = !!(p[3] & 0x04);
    
    f->ts_left = !!(p[3] & 0x10);
    f->ts_right = !!(p[3] & 0x20);
    f->ts_middle = !!(p[3] & 0x40);
    return true;
}

bool AppleUSBMultitouchDriver::decodePinnacle(struct alps_fields *f, UInt8 *p) {
    f->first_mp = !!(p[4] & 0x40);
    f->is_mp = !!(p[0] & 0x40);
    
    if (f->is_mp) {
        f->fingers = (p[5] & 0x3) + 1;
        f->x_map = ((p[4] & 0x7e) << 8) |
        ((p[1] & 0x7f) << 2) |
        ((p[0] & 0x30) >> 4);
        f->y_map = ((p[3] & 0x70) << 4) |
        ((p[2] & 0x7f) << 1) |
        (p[4] & 0x01);
    } else {
        f->st.x = ((p[1] & 0x7f) << 4) | ((p[4] & 0x30) >> 2) |
        ((p[0] & 0x30) >> 4);
        f->st.y = ((p[2] & 0x7f) << 4) | (p[4] & 0x0f);
        f->pressure = p[5] & 0x7f;
        
        decodeButtonsV3(f, p);
    }
    return true;
}

bool AppleUSBMultitouchDriver::decodeRushmore(struct alps_fields *f, UInt8 *p) {
    f->first_mp = !!(p[4] & 0x40);
    f->is_mp = !!(p[5] & 0x40);
    
    if (f->is_mp) {
        f->fingers = max((p[5] & 0x3), ((p[5] >> 2) & 0x3)) + 1;
        f->x_map = ((p[5] & 0x10) << 11) |
        ((p[4] & 0x7e) << 8) |
        ((p[1] & 0x7f) << 2) |
        ((p[0] & 0x30) >> 4);
        f->y_map = ((p[5] & 0x20) << 6) |
        ((p[3] & 0x70) << 4) |
        ((p[2] & 0x7f) << 1) |
        (p[4] & 0x01);
    } else {
        f->st.x = ((p[1] & 0x7f) << 4) | ((p[4] & 0x30) >> 2) |
        ((p[0] & 0x30) >> 4);
        f->st.y = ((p[2] & 0x7f) << 4) | (p[4] & 0x0f);
        f->pressure = p[5] & 0x7f;
        
        decodeButtonsV3(f, p);
    }
    return true;
}

bool AppleUSBMultitouchDriver::decodeDolphin(struct alps_fields *f, UInt8 *p) {
    uint64_t palm_data = 0;
    
    f->first_mp = !!(p[0] & 0x02);
    f->is_mp = !!(p[0] & 0x20);
    
    if (!f->is_mp) {
        f->st.x = ((p[1] & 0x7f) | ((p[4] & 0x0f) << 7));
        f->st.y = ((p[2] & 0x7f) | ((p[4] & 0xf0) << 3));
        f->pressure = (p[0] & 4) ? 0 : p[5] & 0x7f;
        decodeButtonsV3(f, p);
    } else {
        f->fingers = ((p[0] & 0x6) >> 1 |
                      (p[0] & 0x10) >> 2);
        
        palm_data = (p[1] & 0x7f) |
        ((p[2] & 0x7f) << 7) |
        ((p[4] & 0x7f) << 14) |
        ((p[5] & 0x7f) << 21) |
        ((p[3] & 0x07) << 28) |
        (((uint64_t)p[3] & 0x70) << 27) |
        (((uint64_t)p[0] & 0x01) << 34);
        
        /* Y-profile is stored in P(0) to p(n-1), n = y_bits; */
        f->y_map = palm_data & (BIT(priv.y_bits) - 1);
        
        /* X-profile is stored in p(n) to p(n+m-1), m = x_bits; */
        f->x_map = (palm_data >> priv.y_bits) &
        (BIT(priv.x_bits) - 1);
    }
    return true;
}

void AppleUSBMultitouchDriver::alps_process_touchpad_packet_v3_v5(UInt8 *packet) {
    //ffff
    int fingers, buttons = 0;
    struct alps_fields f;
    
    memset(&f, 0, sizeof(f));
    
    (this->*decode_fields)(&f, packet);
    /*
     * There's no single feature of touchpad position and bitmap packets
     * that can be used to distinguish between them. We rely on the fact
     * that a bitmap packet should always follow a position packet with
     * bit 6 of packet[4] set.
     */
    if (priv.multi_packet) {
        /*
         * Sometimes a position packet will indicate a multi-packet
         * sequence, but then what follows is another position
         * packet. Check for this, and when it happens process the
         * position packet as usual.
         */
        if (f.is_mp) {
            fingers = f.fingers;
            /*
             * Bitmap processing uses position packet's coordinate
             * data, so we need to do decode it first.
             */
            (this->*decode_fields)(&f, priv.multi_data);
            if (processBitmap(&priv, &f) == 0) {
                fingers = 0; /* Use st data */
            }
        } else {
            priv.multi_packet = 0;
        }
    }
    
    /*
     * Bit 6 of byte 0 is not usually set in position packets. The only
     * times it seems to be set is in situations where the data is
     * suspect anyway, e.g. a palm resting flat on the touchpad. Given
     * this combined with the fact that this bit is useful for filtering
     * out misidentified bitmap packets, we reject anything with this
     * bit set.
     */
    if (f.is_mp) {
        return;
    }
    
    if (!priv.multi_packet && (f.first_mp)) {
        priv.multi_packet = 1;
        memcpy(priv.multi_data, packet, sizeof(priv.multi_data));
        return;
    }
    
    priv.multi_packet = 0;
    
    /*
     * Sometimes the hardware sends a single packet with z = 0
     * in the middle of a stream. Real releases generate packets
     * with x, y, and z all zero, so these seem to be flukes.
     * Ignore them.
     */
    if (f.st.x && f.st.y && !f.pressure) {
        //return;
    }
    
    /* Use st data when we don't have mt data */
    if (fingers < 2) {
        f.mt[0].x = f.st.x;
        f.mt[0].y = f.st.y;
        fingers = f.pressure > 0 ? 1 : 0;
        priv.second_touch = -1;
    }
    
    buttons |= f.left ? 0x01 : 0;
    buttons |= f.right ? 0x02 : 0;
    buttons |= f.middle ? 0x04 : 0;
    
    if ((priv.flags & ALPS_DUALPOINT) &&
        !(priv.quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS)) {
        buttons |= f.ts_left ? 0x01 : 0;
        buttons |= f.ts_right ? 0x02 : 0;
        buttons |= f.ts_middle ? 0x04 : 0;
    }
    
    /* Reverse y co-ordinates to have 0 at bottom for gestures to work */
    f.mt[0].y = priv.y_max - f.mt[0].y;
    f.mt[1].y = priv.y_max - f.mt[1].y;
    
    /* HACK: Improve multifinger accuacy */
    if (last_fingers == 2 && fingers == 1) {
        fingers = last_fingers;
    }
    dispatchEventsWithInfo(f.mt[0].x, f.mt[0].y, f.mt[1].x, f.mt[1].y, f.pressure, fingers, buttons);
}

void AppleUSBMultitouchDriver::processPacketV3(UInt8 *packet) {
    /*
     * v3 protocol packets come in three types, two representing
     * touchpad data and one representing trackstick data.
     * Trackstick packets seem to be distinguished by always
     * having 0x3f in the last byte. This value has never been
     * observed in the last byte of either of the other types
     * of packets.
     */
    if (packet[5] == 0x3f) {
        processTrackstickPacketV3(packet);
        return;
    }
    
    alps_process_touchpad_packet_v3_v5(packet);
}

void AppleUSBMultitouchDriver::alps_process_packet_v6(UInt8 *packet)
{
    int x, y, z, left, right, middle, buttons = 0,fingers = 0;
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    
    /*
     * We can use Byte5 to distinguish if the packet is from Touchpad
     * or Trackpoint.
     * Touchpad:	0 - 0x7E
     * Trackpoint:	0x7F
     */
    if (packet[5] == 0x7F) {
        /* It should be a DualPoint when received Trackpoint packet */
        if (!(priv.flags & ALPS_DUALPOINT)) {
            return;
        }
        
        /* Trackpoint packet */
        x = packet[1] | ((packet[3] & 0x20) << 2);
        y = packet[2] | ((packet[3] & 0x40) << 1);
        z = packet[4];
        left = packet[3] & 0x01;
        right = packet[3] & 0x02;
        middle = packet[3] & 0x04;
        
        buttons |= left ? 0x01 : 0;
        buttons |= right ? 0x02 : 0;
        buttons |= middle ? 0x04 : 0;
        
        /* To prevent the cursor jump when finger lifted */
        if (x == 0x7F && y == 0x7F && z == 0x7F)
            x = y = z = 0;
        
        /* Divide 4 since trackpoint's speed is too fast */
        dispatchRelativePointerEventX(x / 4, y / 4, buttons, now_abs);
        return;
    }
    
    /* Touchpad packet */
    x = packet[1] | ((packet[3] & 0x78) << 4);
    y = packet[2] | ((packet[4] & 0x78) << 4);
    z = packet[5];
    left = packet[3] & 0x01;
    right = packet[3] & 0x02;
    
    fingers = z > 0 ? 1 : 0;
    
    buttons |= left ? 0x01 : 0;
    buttons |= right ? 0x02 : 0;
    
    dispatchEventsWithInfo(x, y, 0, 0, z, fingers, buttons);
}

void AppleUSBMultitouchDriver::processPacketV4(UInt8 *packet) {
    SInt32 offset;
    SInt32 fingers = 0;
    UInt32 buttons = 0;
    struct alps_fields f;
    
    /*
     * v4 has a 6-byte encoding for bitmap data, but this data is
     * broken up between 3 normal packets. Use priv.multi_packet to
     * track our position in the bitmap packet.
     */
    if (packet[6] & 0x40) {
        /* sync, reset position */
        priv.multi_packet = 0;
    }
    
    if (priv.multi_packet > 2) {
        return;
    }
    
    offset = 2 * priv.multi_packet;
    priv.multi_data[offset] = packet[6];
    priv.multi_data[offset + 1] = packet[7];
    
    f.left = packet[4] & 0x01;
    f.right = packet[4] & 0x02;
    
    f.st.x = ((packet[1] & 0x7f) << 4) | ((packet[3] & 0x30) >> 2) |
    ((packet[0] & 0x30) >> 4);
    f.st.y = ((packet[2] & 0x7f) << 4) | (packet[3] & 0x0f);
    f.pressure = packet[5] & 0x7f;
    
    if (++priv.multi_packet > 2) {
        priv.multi_packet = 0;
        
        f.x_map = ((priv.multi_data[2] & 0x1f) << 10) |
        ((priv.multi_data[3] & 0x60) << 3) |
        ((priv.multi_data[0] & 0x3f) << 2) |
        ((priv.multi_data[1] & 0x60) >> 5);
        f.y_map = ((priv.multi_data[5] & 0x01) << 10) |
        ((priv.multi_data[3] & 0x1f) << 5) |
        (priv.multi_data[1] & 0x1f);
        
        fingers = processBitmap(&priv, &f);
        
    }
    
    buttons |= f.left ? 0x01 : 0;
    buttons |= f.right ? 0x02 : 0;
    
    dispatchEventsWithInfo(f.st.x, f.st.y, 0, 0, f.pressure, fingers, buttons);
}

unsigned char AppleUSBMultitouchDriver::alps_get_packet_id_v7(UInt8 *byte)
{
    unsigned char packet_id;
    
    if (byte[4] & 0x40)
        packet_id = V7_PACKET_ID_TWO;
    else if (byte[4] & 0x01)
        packet_id = V7_PACKET_ID_MULTI;
    else if ((byte[0] & 0x10) && !(byte[4] & 0x43))
        packet_id = V7_PACKET_ID_NEW;
    else if (byte[1] == 0x00 && byte[4] == 0x00)
        packet_id = V7_PACKET_ID_IDLE;
    else
        packet_id = V7_PACKET_ID_UNKNOWN;
    
    return packet_id;
}

void AppleUSBMultitouchDriver::alps_get_finger_coordinate_v7(struct input_mt_pos *mt,
                                                             UInt8 *pkt,
                                                             UInt8 pkt_id)
{
    mt[0].x = ((pkt[2] & 0x80) << 4);
    mt[0].x |= ((pkt[2] & 0x3F) << 5);
    mt[0].x |= ((pkt[3] & 0x30) >> 1);
    mt[0].x |= (pkt[3] & 0x07);
    mt[0].y = (pkt[1] << 3) | (pkt[0] & 0x07);
    
    mt[1].x = ((pkt[3] & 0x80) << 4);
    mt[1].x |= ((pkt[4] & 0x80) << 3);
    mt[1].x |= ((pkt[4] & 0x3F) << 4);
    mt[1].y = ((pkt[5] & 0x80) << 3);
    mt[1].y |= ((pkt[5] & 0x3F) << 4);
    
    switch (pkt_id) {
        case V7_PACKET_ID_TWO:
            mt[1].x &= ~0x000F;
            mt[1].y |= 0x000F;
            /* Detect false-postive touches where x & y report max value */
            if (mt[1].y == 0x7ff && mt[1].x == 0xff0)
                mt[1].x = 0;
            /* y gets set to 0 at the end of this function */
            break;
            
        case V7_PACKET_ID_MULTI:
            mt[1].x &= ~0x003F;
            mt[1].y &= ~0x0020;
            mt[1].y |= ((pkt[4] & 0x02) << 4);
            mt[1].y |= 0x001F;
            break;
            
        case V7_PACKET_ID_NEW:
            mt[1].x &= ~0x003F;
            mt[1].x |= (pkt[0] & 0x20);
            mt[1].y |= 0x000F;
            break;
    }
    
    mt[0].y = 0x7FF - mt[0].y;
    mt[1].y = 0x7FF - mt[1].y;
}

int AppleUSBMultitouchDriver::alps_get_mt_count(struct input_mt_pos *mt)
{
    int i, fingers = 0;
    
    for (i = 0; i < MAX_TOUCHES; i++) {
        if (mt[i].x != 0 || mt[i].y != 0)
            fingers++;
    }
    
    return fingers;
}

bool AppleUSBMultitouchDriver::decodeV7(struct alps_fields *f, UInt8 *p){
    //IOLog("Decode V7 touchpad Packet... 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", p[0], p[1], p[2], p[3], p[4], p[5]);
    
    unsigned char pkt_id;
    
    pkt_id = alps_get_packet_id_v7(p);
    if (pkt_id == V7_PACKET_ID_IDLE)
        return true;
    if (pkt_id == V7_PACKET_ID_UNKNOWN)
        return false;
    
    /*
     * NEW packets are send to indicate a discontinuity in the finger
     * coordinate reporting. Specifically a finger may have moved from
     * slot 0 to 1 or vice versa. INPUT_MT_TRACK takes care of this for
     * us.
     *
     * NEW packets have 3 problems:
     * 1) They do not contain middle / right button info (on non clickpads)
     *    this can be worked around by preserving the old button state
     * 2) They do not contain an accurate fingercount, and they are
     *    typically send when the number of fingers changes. We cannot use
     *    the old finger count as that may mismatch with the amount of
     *    touch coordinates we've available in the NEW packet
     * 3) Their x data for the second touch is inaccurate leading to
     *    a possible jump of the x coordinate by 16 units when the first
     *    non NEW packet comes in
     * Since problems 2 & 3 cannot be worked around, just ignore them.
     */
    if (pkt_id == V7_PACKET_ID_NEW)
        return true;
    
    alps_get_finger_coordinate_v7(f->mt, p, pkt_id);
    
    if (pkt_id == V7_PACKET_ID_TWO)
        f->fingers = alps_get_mt_count(f->mt);
    else /* pkt_id == V7_PACKET_ID_MULTI */
        f->fingers = 3 + (p[5] & 0x03);
    
    f->left = (p[0] & 0x80) >> 7;
    if (priv.flags & ALPS_BUTTONPAD) {
        if (p[0] & 0x20)
            f->fingers++;
        if (p[0] & 0x10)
            f->fingers++;
    } else {
        f->right = (p[0] & 0x20) >> 5;
        f->middle = (p[0] & 0x10) >> 4;
    }
    
    /* Sometimes a single touch is reported in mt[1] rather then mt[0] */
    if (f->fingers == 1 && f->mt[0].x == 0 && f->mt[0].y == 0) {
        f->mt[0].x = f->mt[1].x;
        f->mt[0].y = f->mt[1].y;
        f->mt[1].x = 0;
        f->mt[1].y = 0;
    }
    return true;
}

void AppleUSBMultitouchDriver::processTrackstickPacketV7(UInt8 *packet)
{
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    int x, y, z, left, right, middle, buttons = 0;
    
    /* It should be a DualPoint when received trackstick packet */
    if (!(priv.flags & ALPS_DUALPOINT)) {
        IOLog("Rejected trackstick packet from non DualPoint device");
        return;
    }
    
    x = ((packet[2] & 0xbf)) | ((packet[3] & 0x10) << 2);
    y = (packet[3] & 0x07) | (packet[4] & 0xb8) |
    ((packet[3] & 0x20) << 1);
    z = (packet[5] & 0x3f) | ((packet[3] & 0x80) >> 1);
    
    if ((abs(x) >= 0x7f) && (abs(y) >= 0x7f)) {
        x = y = 0;
    }
    
    left = (packet[1] & 0x01);
    right = (packet[1] & 0x02) >> 1;
    middle = (packet[1] & 0x04) >> 2;
    
    buttons |= left ? 0x01 : 0;
    buttons |= right ? 0x02 : 0;
    buttons |= middle ? 0x04 : 0;
    
    dispatchRelativePointerEventX(x, y, buttons, now_abs);
}

void AppleUSBMultitouchDriver::processTouchpadPacketV7(UInt8 *packet){
    int fingers = 0;
    UInt32 buttons = 0;
    struct alps_fields f;
    
    memset(&f, 0, sizeof(alps_fields));
    
    if (!(this->*decode_fields)(&f, packet))
        return;
    
    buttons |= f.left ? 0x01 : 0;
    buttons |= f.right ? 0x02 : 0;
    buttons |= f.middle ? 0x04 : 0;
    
    if ((priv.flags & ALPS_DUALPOINT) &&
        !(priv.quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS)) {
        buttons |= f.ts_left ? 0x01 : 0;
        buttons |= f.ts_right ? 0x02 : 0;
        buttons |= f.ts_middle ? 0x04 : 0;
    }
    
    fingers = f.fingers;
    
    //Hack because V7 doesn't report pressure
    if (fingers != 0 && (f.mt[0].x != 0 && f.mt[0].y != 0))
        f.pressure = 40;
    else
        f.pressure = 0;
    
    dispatchEventsWithInfo(f.mt[0].x, f.mt[0].y, f.mt[1].x, f.mt[1].y, f.pressure, fingers, buttons);
}

void AppleUSBMultitouchDriver::processPacketV7(UInt8 *packet){
    if (packet[0] == 0x48 && (packet[4] & 0x47) == 0x06)
        processTrackstickPacketV7(packet);
    else
        processTouchpadPacketV7(packet);
}

unsigned char AppleUSBMultitouchDriver::alps_get_pkt_id_ss4_v2(UInt8 *byte)
{
    unsigned char pkt_id = SS4_PACKET_ID_IDLE;
    
    switch (byte[3] & 0x30) {
        case 0x00:
            if (byte[0] == 0x18 && byte[1] == 0x10 && byte[2] == 0x00 &&
                (byte[3] & 0x88) == 0x08 && byte[4] == 0x10 &&
                byte[5] == 0x00) {
                pkt_id = SS4_PACKET_ID_IDLE;
            } else {
                pkt_id = SS4_PACKET_ID_ONE;
            }
            break;
        case 0x10:
            /* two-finger finger positions */
            pkt_id = SS4_PACKET_ID_TWO;
            break;
        case 0x20:
            /* stick pointer */
            pkt_id = SS4_PACKET_ID_STICK;
            break;
        case 0x30:
            /* third and fourth finger positions */
            pkt_id = SS4_PACKET_ID_MULTI;
            break;
    }
    
    return pkt_id;
}

bool AppleUSBMultitouchDriver::alps_decode_ss4_v2(struct alps_fields *f, UInt8 *p){
    
    //struct alps_data *priv;
    unsigned char pkt_id;
    unsigned int no_data_x, no_data_y;
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    
    pkt_id = alps_get_pkt_id_ss4_v2(p);
    
    /* Current packet is 1Finger coordinate packet */
    switch (pkt_id) {
        case SS4_PACKET_ID_ONE:
            f->mt[0].x = SS4_1F_X_V2(p);
            f->mt[0].y = SS4_1F_Y_V2(p);
            f->pressure = ((SS4_1F_Z_V2(p)) * 2) & 0x7f;
            /*
             * When a button is held the device will give us events
             * with x, y, and pressure of 0. This causes annoying jumps
             * if a touch is released while the button is held.
             * Handle this by claiming zero contacts.
             */
            f->fingers = f->pressure > 0 ? 1 : 0;
            f->first_mp = 0;
            f->is_mp = 0;
            break;
            
        case SS4_PACKET_ID_TWO:
            if (priv.flags & ALPS_BUTTONPAD) {
                f->mt[0].x = SS4_BTL_MF_X_V2(p, 0);
                f->mt[0].y = SS4_BTL_MF_Y_V2(p, 0);
                f->mt[1].x = SS4_BTL_MF_X_V2(p, 1);
                f->mt[1].y = SS4_BTL_MF_Y_V2(p, 1);
            } else {
                f->mt[0].x = SS4_STD_MF_X_V2(p, 0);
                f->mt[0].y = SS4_STD_MF_Y_V2(p, 0);
                f->mt[1].x = SS4_STD_MF_X_V2(p, 1);
                f->mt[1].y = SS4_STD_MF_Y_V2(p, 1);
            }
            f->pressure = SS4_MF_Z_V2(p, 0) ? 0x30 : 0;
            
            if (SS4_IS_MF_CONTINUE(p)) {
                f->first_mp = 1;
            } else {
                f->fingers = 2;
                f->first_mp = 0;
            }
            f->is_mp = 0;
            
            break;
            
        case SS4_PACKET_ID_MULTI:
            if (priv.flags & ALPS_BUTTONPAD) {
                f->mt[2].x = SS4_BTL_MF_X_V2(p, 0);
                f->mt[2].y = SS4_BTL_MF_Y_V2(p, 0);
                f->mt[3].x = SS4_BTL_MF_X_V2(p, 1);
                f->mt[3].y = SS4_BTL_MF_Y_V2(p, 1);
                no_data_x = SS4_MFPACKET_NO_AX_BL;
                no_data_y = SS4_MFPACKET_NO_AY_BL;
            } else {
                f->mt[2].x = SS4_STD_MF_X_V2(p, 0);
                f->mt[2].y = SS4_STD_MF_Y_V2(p, 0);
                f->mt[3].x = SS4_STD_MF_X_V2(p, 1);
                f->mt[3].y = SS4_STD_MF_Y_V2(p, 1);
                no_data_x = SS4_MFPACKET_NO_AX;
                no_data_y = SS4_MFPACKET_NO_AY;
            }
            
            f->first_mp = 0;
            f->is_mp = 1;
            
            if (SS4_IS_5F_DETECTED(p)) {
                f->fingers = 5;
            } else if (f->mt[3].x == no_data_x &&
                       f->mt[3].y == no_data_y) {
                f->mt[3].x = 0;
                f->mt[3].y = 0;
                f->fingers = 3;
            } else {
                f->fingers = 4;
            }
            break;
            
        case SS4_PACKET_ID_STICK:
            if (!(priv.flags & ALPS_DUALPOINT)) {
                
            } else {
                int x = (((p[0] & 1) << 7) | (p[1] & 0x7f));
                int y = (((p[3] & 1) << 7) | (p[2] & 0x7f));
                int pressure = (p[4] & 0x7f);
                
                if ((abs(x) >= 0x7f) && (abs(y) >= 0x7f)) {
                    x = y = 0;
                }
                dispatchRelativePointerEventX(x, y, 0, now_abs);
            }
            break;
            
        case SS4_PACKET_ID_IDLE:
        default:
            memset(f, 0, sizeof(struct alps_fields));
            break;
    }
    
    /* handle buttons */
    if (pkt_id == SS4_PACKET_ID_STICK) {
        f->ts_left = !!(SS4_BTN_V2(p) & 0x01);
        if (!(priv.flags & ALPS_BUTTONPAD)) {
            f->ts_right = !!(SS4_BTN_V2(p) & 0x02);
            f->ts_middle = !!(SS4_BTN_V2(p) & 0x04);
        }
    } else {
        f->left = !!(SS4_BTN_V2(p) & 0x01);
        if (!(priv.flags & ALPS_BUTTONPAD)) {
            f->right = !!(SS4_BTN_V2(p) & 0x02);
            f->middle = !!(SS4_BTN_V2(p) & 0x04);
        }
    }
    return true;
}

void AppleUSBMultitouchDriver::alps_process_packet_ss4_v2(UInt8 *packet) {
    int buttons = 0;
    struct alps_fields f;
    
    memset(&f, 0, sizeof(struct alps_fields));
    (this->*decode_fields)(&f, packet);
    if (priv.multi_packet) {
        /*
         * Sometimes the first packet will indicate a multi-packet
         * sequence, but sometimes the next multi-packet would not
         * come. Check for this, and when it happens process the
         * position packet as usual.
         */
        if (f.is_mp) {
            /* Now process the 1st packet */
            (this->*decode_fields)(&f, priv.multi_data);
        } else {
            priv.multi_packet = 0;
        }
    }
    
    /*
     * "f.is_mp" would always be '0' after merging the 1st and 2nd packet.
     * When it is set, it means 2nd packet comes without 1st packet come.
     */
    if (f.is_mp) {
        return;
    }
    
    /* Save the first packet */
    if (!priv.multi_packet && f.first_mp) {
        priv.multi_packet = 1;
        memcpy(priv.multi_data, packet, sizeof(priv.multi_data));
        return;
    }
    
    priv.multi_packet = 0;
    
    buttons |= f.left ? 0x01 : 0;
    buttons |= f.right ? 0x02 : 0;
    buttons |= f.middle ? 0x04 : 0;
    
    if (priv.flags & ALPS_DUALPOINT) {
        buttons |= f.ts_left ? 0x01 : 0;
        buttons |= f.ts_right ? 0x02 : 0;
        buttons |= f.ts_middle ? 0x04 : 0;
    }
    IOLog("ALPS: Process V8: Fingers=%d, x1=%d, y1=%d, z=%d, buttons=%d\n", f.fingers, f.mt[0].x, f.mt[0].y, f.pressure, buttons);
    dispatchEventsWithInfo(f.mt[0].x, f.mt[0].y, f.mt[1].x, f.mt[1].y, f.pressure, f.fingers, buttons);
}

void AppleUSBMultitouchDriver::dispatchEventsWithInfo(int xraw1, int yraw1, int xraw2, int yraw2, int z, int fingers, UInt32 buttonsraw) {
    DEBUG_LOG("%s::dispatchEventsWithInfo: x=%d, y=%d, z=%d, fingers=%d, buttons=%d\n",
              getName(), xraw, yraw, z, fingers, buttonsraw);
    
    _fingerCount = fingers;
    
    xraw1 /= 5;
    xraw2 /= 5;
    yraw1 /= 5;
    yraw2 /= 5;
    
    if (xraw1 == 0)
        xraw1 = -1;
    if (xraw2 == 0)
        xraw2 = -1;
    if (yraw1 == 0)
        yraw1 = -1;
    if (yraw2 == 0)
        yraw2 = -1;
    
    if (fingers < 2){
        xraw2 = -1;
        yraw2 = -1;
    }
    if (fingers < 1){
        xraw1 = -1;
        xraw2 = -1;
    }
    
    _xraw1 = xraw1;
    _yraw1 = yraw1;
    
    _xraw2 = xraw2;
    _yraw2 = yraw2;
    
    _buttonDown = (buttonsraw != 0);
    
    
    fingers = z > z_finger ? fingers : 0;
    
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void AppleUSBMultitouchDriver::
dispatchRelativePointerEventWithPacket(UInt8 *packet,
                                       UInt32 packetSize) {
    //
    // Process the three byte relative format packet that was retrieved from the
    // trackpad. The format of the bytes is as follows:
    //
    //  7  6  5  4  3  2  1  0
    // -----------------------
    // YO XO YS XS  1  M  R  L
    // X7 X6 X5 X4 X3 X3 X1 X0  (X delta)
    // Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0  (Y delta)
    //
    
    UInt32 buttons = 0;
    SInt32 dx, dy;
    
    if ((packet[0] & 0x1)) buttons |= 0x1;  // left button   (bit 0 in packet)
    if ((packet[0] & 0x2)) buttons |= 0x2;  // right button  (bit 1 in packet)
    if ((packet[0] & 0x4)) buttons |= 0x4;  // middle button (bit 2 in packet)
    
    dx = packet[1];
    if (dx) {
        dx = packet[1] - ((packet[0] << 4) & 0x100);
    }
    
    dy = packet[2];
    if (dy) {
        dy = ((packet[0] << 3) & 0x100) - packet[2];
    }
    
    uint64_t now_abs;
    clock_get_uptime(&now_abs);
    DEBUG_LOG("Dispatch relative PS2 packet: dx=%d, dy=%d, buttons=%d\n", dx, dy, buttons);
    dispatchRelativePointerEventX(dx, dy, buttons, now_abs);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void AppleUSBMultitouchDriver::setTouchPadEnable(bool enable) {
    DEBUG_LOG("setTouchpadEnable enter\n");
    //
    // Instructs the trackpad to start or stop the reporting of data packets.
    // It is safe to issue this request from the interrupt/completion context.
    //
    
    if (enable) {
        initTouchPad();
    } else {
        // to disable just reset the mouse
        resetMouse();
    }
}

bool AppleUSBMultitouchDriver::getStatus(ALPSStatus_t *status) {
    return repeatCmd(NULL, NULL, kDP_SetDefaultsAndDisable, status);
}

/*
 * Turn touchpad tapping on or off. The sequences are:
 * 0xE9 0xF5 0xF5 0xF3 0x0A to enable,
 * 0xE9 0xF5 0xF5 0xE8 0x00 to disable.
 * My guess that 0xE9 (GetInfo) is here as a sync point.
 * For models that also have stickpointer (DualPoints) its tapping
 * is controlled separately (0xE6 0xE6 0xE6 0xF3 0x14|0x0A) but
 * we don't fiddle with it.
 */
bool AppleUSBMultitouchDriver::tapMode(bool enable) {
    int cmd = enable ? kDP_SetMouseSampleRate : kDP_SetMouseResolution;
    UInt8 tapArg = enable ? 0x0A : 0x00;
    TPS2Request<8> request;
    ALPSStatus_t result;
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_GetMouseInformation;
    request.commands[1].command = kPS2C_ReadDataPort;
    request.commands[1].inOrOut = 0;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut = kDP_SetDefaultsAndDisable;
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetDefaultsAndDisable;
    request.commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut = cmd;
    request.commands[7].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[7].inOrOut = tapArg;
    request.commandsCount = 8;
    _device->submitRequestAndBlock(&request);
    
    if (request.commandsCount != 8) {
        DEBUG_LOG("Enabling tap mode failed before getStatus call, command count=%d\n",
                  request.commandsCount);
        return false;
    }
    
    return getStatus(&result);
}

PS2InterruptResult AppleUSBMultitouchDriver::interruptOccurred(UInt8 data) {
    //
    // This will be invoked automatically from our device when asynchronous
    // events need to be delivered. Process the trackpad data. Do NOT issue
    // any BLOCKING commands to our device in this context.
    //
    
    UInt8* packet = _ringBuffer.head();
    packet[_packetByteCount++] = data;
    
    /*
     * Check if we are dealing with a bare PS/2 packet, presumably from
     * a device connected to the external PS/2 port. Because bare PS/2
     * protocol does not have enough constant bits to self-synchronize
     * properly we only do this if the device is fully synchronized.
     * Can not distinguish V8's first byte from PS/2 packet's
     */
    if (priv.proto_version != ALPS_PROTO_V8 &&
        (packet[0] & 0xc8) == 0x08) {
        
        if (_packetByteCount == 3) {
            _ringBuffer.advanceHead(priv.pktsize);
            _packetByteCount = 0;
            return kPS2IR_packetReady;
        }
        return kPS2IR_packetBuffering;
    }
    
    /* Check for PS/2 packet stuffed in the middle of ALPS packet. */
    
    if ((priv.flags & ALPS_PS2_INTERLEAVED) &&
        _packetByteCount >= 4 && (packet[3] & 0x0f) == 0x0f) {
        return kPS2IR_packetBuffering;
    }
    
    /* alps_is_valid_first_byte */
    if ((packet[0] & priv.mask0) != priv.byte0) {
        return kPS2IR_packetBuffering;
    }
    
    /* Bytes 2 - pktsize should have 0 in the highest bit */
    if (priv.proto_version < ALPS_PROTO_V5 &&
        _packetByteCount >= 2 && _packetByteCount <= priv.pktsize &&
        (packet[_packetByteCount - 1] & 0x80)) {
        
        if (priv.proto_version == ALPS_PROTO_V3_RUSHMORE &&
            _packetByteCount == priv.pktsize) {
            /*
             * Some Dell boxes, such as Latitude E6440 or E7440
             * with closed lid, quite often smash last byte of
             * otherwise valid packet with 0xff. Given that the
             * next packet is very likely to be valid let's
             * report PSMOUSE_FULL_PACKET but not process data,
             * rather than reporting PSMOUSE_BAD_DATA and
             * filling the logs.
             */
            return kPS2IR_packetReady;
        }
        return kPS2IR_packetBuffering;
    }
    
    /* alps_is_valid_package_v7 */
    if (priv.proto_version == ALPS_PROTO_V7 &&
        (((_packetByteCount == 3) && ((packet[2] & 0x40) != 0x40)) ||
         ((_packetByteCount == 4) && ((packet[3] & 0x48) != 0x48)) ||
         ((_packetByteCount == 6) && ((packet[5] & 0x40) != 0x0)))) {
            return kPS2IR_packetBuffering;
        }
    
    /* alps_is_valid_package_ss4_v2 */
    if (priv.proto_version == ALPS_PROTO_V8 &&
        ((_packetByteCount == 4 && ((packet[3] & 0x08) != 0x08)) ||
         (_packetByteCount == 6 && ((packet[5] & 0x10) != 0x0)))) {
            return kPS2IR_packetBuffering;
        }
    
    if (_packetByteCount == priv.pktsize)
    {
        _ringBuffer.advanceHead(priv.pktsize);
        _packetByteCount = 0;
        return kPS2IR_packetReady;
    }
    return kPS2IR_packetBuffering;
}

void AppleUSBMultitouchDriver::packetReady() {
    // empty the ring buffer, dispatching each packet...
    while (_ringBuffer.count() >= priv.pktsize) {
        UInt8 *packet = _ringBuffer.tail();
        (this->*process_packet)(packet);
        _ringBuffer.advanceTail(priv.pktsize);
    }
}

bool AppleUSBMultitouchDriver::commandModeSendNibble(int nibble) {
    SInt32 command;
    // The largest amount of requests we will have is 2 right now
    // 1 for the initial command, and 1 for sending data OR 1 for receiving data
    // If the nibble commands at the top change then this will need to change as
    // well. For now we will just validate that the request will not overload
    // this object.
    TPS2Request<2> request;
    int cmdCount = 0, send = 0, receive = 0, i;
    
    if (nibble > 0xf) {
        IOLog("%s::commandModeSendNibble ERROR: nibble value is greater than 0xf, command may fail\n", getName());
    }
    
    request.commands[cmdCount].command = kPS2C_SendMouseCommandAndCompareAck;
    command = priv.nibble_commands[nibble].command;
    request.commands[cmdCount++].inOrOut = command & 0xff;
    
    send = (command >> 12 & 0xf);
    receive = (command >> 8 & 0xf);
    
    // Validate that the number of requests will not exceed our buffer as
    // defined above
    // Also, send can never be > 1 since all we have available is the data
    // from the alps_nibble_commands which is 1 byte
    if ((send > 1) || ((send + receive + 1) > 2)) {
        IOLog("%s::commandModeSendNibble: ERROR: Nibble commands have changed. Cannot process nibble that sends or receives more than 1 byte of data.\n", getName());
        return false;
    }
    
    //DEBUG_LOG("%s: send nibble: nibble=%x command info=%x command=0x%02x send=%d, receive=%d, data=0x%02x\n",
    //          getName(), nibble, command, request.commands[0].inOrOut, send, receive, priv.nibble_commands[nibble].data);
    
    if (send > 0) {
        request.commands[cmdCount].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmdCount++].inOrOut = priv.nibble_commands[nibble].data;
    }
    
    // Receive the amount of data for the given command
    // Even though we don't read the data, we should drain the data port to follow protocol
    for (i = 0; i < receive; i++) {
        request.commands[cmdCount].command = kPS2C_ReadDataPort;
        request.commands[cmdCount++].inOrOut = 0;
    }
    
    request.commandsCount = cmdCount;
    assert(request.commandsCount <= countof(request.commands));
    
    _device->submitRequestAndBlock(&request);
    
    //DEBUG_LOG("%s: num nibble commands=%d, expected=%d\n", getName(), request.commandsCount, cmdCount);
    
    return request.commandsCount == cmdCount;
}

bool AppleUSBMultitouchDriver::commandModeSetAddr(int addr) {
    
    TPS2Request<1> request;
    int i, nibble;
    
    //    DEBUG_LOG("command mode set addr with addr command: 0x%02x\n", priv.addr_command);
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = priv.addr_command;
    request.commandsCount = 1;
    _device->submitRequestAndBlock(&request);
    
    if (request.commandsCount != 1) {
        return false;
    }
    
    for (i = 12; i >= 0; i -= 4) {
        nibble = (addr >> i) & 0xf;
        if (!commandModeSendNibble(nibble)) {
            return false;
        }
    }
    
    return true;
}

int AppleUSBMultitouchDriver::commandModeReadReg(int addr) {
    TPS2Request<4> request;
    ALPSStatus_t status;
    
    if (!commandModeSetAddr(addr)) {
        DEBUG_LOG("Failed to set addr to read register\n");
        return -1;
    }
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_GetMouseInformation; //sync..
    request.commands[1].command = kPS2C_ReadDataPort;
    request.commands[1].inOrOut = 0;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commandsCount = 4;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    if (request.commandsCount != 4) {
        return -1;
    }
    
    status.bytes[0] = request.commands[1].inOrOut;
    status.bytes[1] = request.commands[2].inOrOut;
    status.bytes[2] = request.commands[3].inOrOut;
    
    DEBUG_LOG("AppleUSBMultitouchDriver read reg result: { 0x%02x, 0x%02x, 0x%02x }\n", status.bytes[0], status.bytes[1], status.bytes[2]);
    
    /* The address being read is returned in the first 2 bytes
     * of the result. Check that the address matches the expected
     * address.
     */
    if (addr != ((status.bytes[0] << 8) | status.bytes[1])) {
        DEBUG_LOG("AppleUSBMultitouchDriver ERROR: read wrong registry value, expected: %x\n", addr);
        return -1;
    }
    
    return status.bytes[2];
}

bool AppleUSBMultitouchDriver::commandModeWriteReg(int addr, UInt8 value) {
    
    if (!commandModeSetAddr(addr)) {
        return false;
    }
    
    return commandModeWriteReg(value);
}

bool AppleUSBMultitouchDriver::commandModeWriteReg(UInt8 value) {
    if (!commandModeSendNibble((value >> 4) & 0xf)) {
        return false;
    }
    if (!commandModeSendNibble(value & 0xf)) {
        return false;
    }
    
    return true;
}

bool AppleUSBMultitouchDriver::repeatCmd(SInt32 init_command, SInt32 init_arg, SInt32 repeated_command, ALPSStatus_t *report) {
    TPS2Request<9> request;
    int byte0, cmd;
    cmd = byte0 = 0;
    
    if (init_command) {
        request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmd++].inOrOut = kDP_SetMouseResolution;
        request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmd++].inOrOut = init_arg;
    }
    
    
    // 3X run command
    request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmd++].inOrOut = repeated_command;
    request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmd++].inOrOut = repeated_command;
    request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmd++].inOrOut = repeated_command;
    
    // Get info/result
    request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmd++].inOrOut = kDP_GetMouseInformation;
    byte0 = cmd;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commandsCount = cmd;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    report->bytes[0] = request.commands[byte0].inOrOut;
    report->bytes[1] = request.commands[byte0+1].inOrOut;
    report->bytes[2] = request.commands[byte0+2].inOrOut;
    
    DEBUG_LOG("%02x report: [0x%02x 0x%02x 0x%02x]\n",
              repeated_command,
              report->bytes[0],
              report->bytes[1],
              report->bytes[2]);
    
    return request.commandsCount == cmd;
}

bool AppleUSBMultitouchDriver::enterCommandMode() {
    DEBUG_LOG("enter command mode\n");
    TPS2Request<4> request;
    ALPSStatus_t status;
    
    if (!repeatCmd(NULL, NULL, kDP_MouseResetWrap, &status)) {
        IOLog("ALPS: Failed to enter command mode!\n");
        return false;
    }
    return true;
}

bool AppleUSBMultitouchDriver::exitCommandMode() {
    DEBUG_LOG("exit command mode\n");
    TPS2Request<1> request;
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetMouseStreamMode;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return true;
}

bool AppleUSBMultitouchDriver::passthroughModeV2(bool enable) {
    int cmd = enable ? kDP_SetMouseScaling2To1 : kDP_SetMouseScaling1To1;
    TPS2Request<4> request;
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = cmd;
    request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut = cmd;
    request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut = cmd;
    request.commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut = kDP_SetDefaultsAndDisable;
    request.commandsCount = 4;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount == 4;
}

bool AppleUSBMultitouchDriver::absoluteModeV1V2() {
    
    ps2_command_short(kDP_SetDefaultsAndDisable);
    ps2_command_short(kDP_SetDefaultsAndDisable);
    ps2_command_short(kDP_SetDefaultsAndDisable);
    ps2_command_short(kDP_SetDefaultsAndDisable);
    ps2_command_short(kDP_Enable);
    
    /*
     * Switch mouse to poll (remote) mode so motion data will not
     * get in our way
     */
    ps2_command_short(kDP_MouseSetPoll);
    
    return true;
}

bool AppleUSBMultitouchDriver::hwInitV1V2() {
    TPS2Request<1> request;
    
    if (priv.flags & ALPS_PASS) {
        if (!passthroughModeV2(true)) {
            return false;
        }
    }
    
    if (!tapMode(true)) {
        return false;
    }
    
    if (!absoluteModeV1V2()) {
        IOLog("ERROR: Failed to enable absolute mode\n");
        return false;
    }
    
    if (priv.flags & ALPS_PASS) {
        if (!passthroughModeV2(false)) {
            return false;
        }
    }
    
    /* ALPS needs stream mode, otherwise it won't report any data */
    ps2_command_short(kDP_SetMouseStreamMode);
    
    return true;
}

bool AppleUSBMultitouchDriver::alps_hw_init_v6()
{
    //unsigned char param[2] = {0xC8, 0x14};
    
    /* Enter passthrough mode to let trackpoint enter 6byte raw mode */
    /*if (alps_passthrough_mode_v2(psmouse, true))
     return -1;*/
    
    ps2_command_short(kDP_SetMouseScaling1To1);
    ps2_command_short(kDP_SetMouseScaling1To1);
    ps2_command_short(kDP_SetMouseScaling1To1);
    ps2_command(0xC8, kDP_SetMouseSampleRate);
    ps2_command(0x14, kDP_SetMouseSampleRate);
    
    /*if (alps_passthrough_mode_v2(psmouse, false))
     return -1;
     
     if (alps_absolute_mode_v6(psmouse)) {
     psmouse_err(psmouse, "Failed to enable absolute mode\n");
     return -1;
     }*/
    
    return true;
}

bool AppleUSBMultitouchDriver::passthroughModeV3(int regBase, bool enable) {
    int regVal;
    bool ret = false;
    
    DEBUG_LOG("passthrough mode enable=%d\n", enable);
    
    if (!enterCommandMode()) {
        IOLog("ERROR: Failed to enter command mode while enabling passthrough mode\n");
        return false;
    }
    
    regVal = commandModeReadReg(regBase + 0x0008);
    if (regVal == -1) {
        IOLog("Failed to read register while setting up passthrough mode\n");
        goto error;
    }
    
    if (enable) {
        regVal |= 0x01;
    } else {
        regVal &= ~0x01;
    }
    
    ret = commandModeWriteReg(regVal);
    
error:
    if (!exitCommandMode()) {
        IOLog("ERROR: failed to exit command mode while enabling passthrough mode v3\n");
        return false;
    }
    
    return ret;
}

bool AppleUSBMultitouchDriver::absoluteModeV3() {
    
    int regVal;
    
    regVal = commandModeReadReg(0x0004);
    if (regVal == -1) {
        return false;
    }
    
    regVal |= 0x06;
    if (!commandModeWriteReg(regVal)) {
        return false;
    }
    
    return true;
}

IOReturn AppleUSBMultitouchDriver::alps_probe_trackstick_v3_v7(int regBase) {
    int ret = kIOReturnIOError, regVal;
    
    if (!enterCommandMode()) {
        goto error;
    }
    
    regVal = commandModeReadReg(regBase + 0x08);
    
    if (regVal == -1) {
        goto error;
    }
    
    /* bit 7: trackstick is present */
    ret = regVal & 0x80 ? 0 : kIOReturnNoDevice;
    
error:
    exitCommandMode();
    return ret;
}

IOReturn AppleUSBMultitouchDriver::setupTrackstickV3(int regBase) {
    IOReturn ret = 0;
    ALPSStatus_t report;
    TPS2Request<3> request;
    
    if (!passthroughModeV3(regBase, true)) {
        return kIOReturnIOError;
    }
    
    /*
     * E7 report for the trackstick
     *
     * There have been reports of failures to seem to trace back
     * to the above trackstick check failing. When these occur
     * this E7 report fails, so when that happens we continue
     * with the assumption that there isn't a trackstick after
     * all.
     */
    if (!repeatCmd(NULL, NULL, kDP_SetMouseScaling2To1, &report)) {
        IOLog("WARN: trackstick E7 report failed\n");
        ret = kIOReturnNoDevice;
    } else {
        /*
         * Not sure what this does, but it is absolutely
         * essential. Without it, the touchpad does not
         * work at all and the trackstick just emits normal
         * PS/2 packets.
         */
        request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[0].inOrOut = kDP_SetMouseScaling1To1;
        request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[1].inOrOut = kDP_SetMouseScaling1To1;
        request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[2].inOrOut = kDP_SetMouseScaling1To1;
        request.commandsCount = 3;
        assert(request.commandsCount <= countof(request.commands));
        _device->submitRequestAndBlock(&request);
        if (request.commandsCount != 3) {
            IOLog("ERROR: error sending magic E6 scaling sequence\n");
            ret = kIOReturnIOError;
            goto error;
        }
        if (!(commandModeSendNibble(0x9) && commandModeSendNibble(0x4))) {
            IOLog("ERROR: error sending magic E6 nibble sequence\n");
            ret = kIOReturnIOError;
            goto error;
        }
        DEBUG_LOG("Sent magic E6 sequence\n");
        
        /*
         * This ensures the trackstick packets are in the format
         * supported by this driver. If bit 1 isn't set the packet
         * format is different.
         */
        if (!(enterCommandMode() &&
              commandModeWriteReg(regBase + 0x0008, 0x82) &&
              exitCommandMode())) {
            ret = -kIOReturnIOError;
            //goto error;
        }
    }
error:
    if (!passthroughModeV3(regBase, false)) {
        ret = kIOReturnIOError;
    }
    
    return ret;
}

bool AppleUSBMultitouchDriver::hwInitV3() {
    int regVal;
    
    if ((priv.flags & ALPS_DUALPOINT) &&
        setupTrackstickV3(ALPS_REG_BASE_PINNACLE) == kIOReturnIOError)
        goto error;
    
    if (!(enterCommandMode() &&
          absoluteModeV3())) {
        IOLog("ALPS: Failed to enter absolute mode\n");
        goto error;
    }
    
    regVal = commandModeReadReg(0x0006);
    if (regVal == -1)
        goto error;
    if (!commandModeWriteReg(regVal | 0x01))
        goto error;
    
    regVal = commandModeReadReg(0x0007);
    if (regVal == -1)
        goto error;
    if (!commandModeWriteReg(regVal | 0x01))
        goto error;
    
    if (commandModeReadReg(0x0144) == -1)
        goto error;
    if (!commandModeWriteReg(0x04))
        goto error;
    
    if (commandModeReadReg(0x0159) == -1)
        goto error;
    if (!commandModeWriteReg(0x03))
        goto error;
    
    if (commandModeReadReg(0x0163) == -1)
        goto error;
    if (!commandModeWriteReg(0x0163, 0x03))
        goto error;
    
    if (commandModeReadReg(0x0162) == -1)
        goto error;
    if (!commandModeWriteReg(0x0162, 0x04))
        goto error;
    
    exitCommandMode();
    
    /* Set rate and enable data reporting */
    ps2_command(0x28, kDP_SetMouseSampleRate);
    ps2_command_short(kDP_Enable);
    
    return true;
    
error:
    exitCommandMode();
    return false;
}

bool AppleUSBMultitouchDriver::alps_get_v3_v7_resolution(int reg_pitch)
{
    int reg, x_pitch, y_pitch, x_electrode, y_electrode, x_phys, y_phys;
    
    reg = commandModeReadReg(reg_pitch);
    if (reg < 0)
        return reg;
    
    x_pitch = (char)(reg << 4) >> 4; /* sign extend lower 4 bits */
    x_pitch = 50 + 2 * x_pitch; /* In 0.1 mm units */
    
    y_pitch = (char)reg >> 4; /* sign extend upper 4 bits */
    y_pitch = 36 + 2 * y_pitch; /* In 0.1 mm units */
    
    reg = commandModeReadReg(reg_pitch + 1);
    if (reg < 0)
        return reg;
    
    x_electrode = (char)(reg << 4) >> 4; /* sign extend lower 4 bits */
    x_electrode = 17 + x_electrode;
    
    y_electrode = (char)reg >> 4; /* sign extend upper 4 bits */
    y_electrode = 13 + y_electrode;
    
    x_phys = x_pitch * (x_electrode - 1); /* In 0.1 mm units */
    y_phys = y_pitch * (y_electrode - 1); /* In 0.1 mm units */
    
    priv.x_res = priv.x_max * 10 / x_phys; /* units / mm */
    priv.y_res = priv.y_max * 10 / y_phys; /* units / mm */
    
    /*IOLog("pitch %dx%d num-electrodes %dx%d physical size %dx%d mm res %dx%d\n",
     x_pitch, y_pitch, x_electrode, y_electrode,
     x_phys / 10, y_phys / 10, priv.x_res, priv.y_res);*/
    
    return true;
}

bool AppleUSBMultitouchDriver::hwInitRushmoreV3() {
    
    
    int regVal;
    TPS2Request<1> request;
    
    if (priv.flags & ALPS_DUALPOINT) {
        regVal = setupTrackstickV3(ALPS_REG_BASE_RUSHMORE);
        if (regVal == kIOReturnIOError) {
            goto error;
        }
        /*if (regVal == kIOReturnNoDevice) {
         priv.flags &= ~ALPS_DUALPOINT;
         }*/
    }
    
    if (!enterCommandMode() ||
        commandModeReadReg(0xc2d9) == -1 ||
        !commandModeWriteReg(0xc2cb, 0x00)) {
        goto error;
    }
    
    regVal = commandModeReadReg(0xc2c6);
    if (regVal == -1)
        goto error;
    if (!commandModeWriteReg(regVal & 0xfd))
        goto error;
    
    if (!commandModeWriteReg(0xc2c9, 0x64))
        goto error;
    
    /* enter absolute mode */
    regVal = commandModeReadReg(0xc2c4);
    if (regVal == -1)
        goto error;
    if (!commandModeWriteReg(regVal | 0x02))
        goto error;
    
    exitCommandMode();
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_Enable;
    request.commandsCount = 1;
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount == 1;
    
error:
    exitCommandMode();
    return false;
}

/*
 * Used during both passthrough mode initialization and touchpad enablement
 */


/* Must be in command mode when calling this function */
bool AppleUSBMultitouchDriver::absoluteModeV4() {
    int regVal;
    
    regVal = commandModeReadReg(0x0004);
    if (regVal == -1) {
        return false;
    }
    
    regVal |= 0x02;
    if (!commandModeWriteReg(regVal)) {
        return false;
    }
    
    return true;
}

bool AppleUSBMultitouchDriver::hwInitV4() {
    TPS2Request<7> request;
    
    if (!enterCommandMode()) {
        goto error;
    }
    
    if (!absoluteModeV4()) {
        IOLog("ALPS: Failed to enter absolute mode\n");
        goto error;
    }
    
    DEBUG_LOG("now setting a bunch of regs\n");
    
    if (!commandModeWriteReg(0x0007, 0x8c)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x0149, 0x03)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x0160, 0x03)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x017f, 0x15)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x0151, 0x01)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x0168, 0x03)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x014a, 0x03)) {
        goto error;
    }
    
    if (!commandModeWriteReg(0x0161, 0x03)) {
        goto error;
    }
    
    exitCommandMode();
    
    /*
     * This sequence changes the output from a 9-byte to an
     * 8-byte format. All the same data seems to be present,
     * just in a more compact format.
     */
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetMouseSampleRate;
    request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut = 0xc8;
    request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut = kDP_SetMouseSampleRate;
    request.commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut = 0x64;
    request.commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut = kDP_SetMouseSampleRate;
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = 0x50;
    request.commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut = kDP_GetId;
    request.commandsCount = 7;
    _device->submitRequestAndBlock(&request);
    
    if (request.commandsCount != 7) {
        return false;
    }
    
    /* Set rate and enable data reporting */
    ps2_command(0x64, kDP_SetMouseSampleRate);
    ps2_command_short(kDP_Enable);
    return true;
    
error:
    exitCommandMode();
    return false;
}

void AppleUSBMultitouchDriver::alps_get_otp_values_ss4_v2(unsigned char index)
{
    int cmd = 0;
    TPS2Request<4> request;
    
    switch (index) {
        case 0:
            ps2_command_short(kDP_SetMouseStreamMode);
            ps2_command_short(kDP_SetMouseStreamMode);
            
            request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
            request.commands[cmd++].inOrOut = kDP_GetMouseInformation;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commandsCount = cmd;
            assert(request.commandsCount <= countof(request.commands));
            _device->submitRequestAndBlock(&request);
            
            break;
            
        case 1:
            ps2_command_short(kDP_MouseSetPoll);
            ps2_command_short(kDP_MouseSetPoll);
            
            request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
            request.commands[cmd++].inOrOut = kDP_GetMouseInformation;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commands[cmd].command = kPS2C_ReadDataPort;
            request.commands[cmd++].inOrOut = 0;
            request.commandsCount = cmd;
            assert(request.commandsCount <= countof(request.commands));
            _device->submitRequestAndBlock(&request);
            
            break;
    }
}

void AppleUSBMultitouchDriver::alps_set_defaults_ss4_v2(struct alps_data *priv)
{
    alps_get_otp_values_ss4_v2(0);
    alps_get_otp_values_ss4_v2(1);
    
}

int AppleUSBMultitouchDriver::alps_dolphin_get_device_area(struct alps_data *priv)
{
    int cmd = 0;
    TPS2Request<4> request;
    enterCommandMode();
    
    ps2_command_short(kDP_MouseResetWrap);
    ps2_command_short(kDP_MouseSetPoll);
    ps2_command_short(kDP_MouseSetPoll);
    ps2_command(0x0a, kDP_SetMouseSampleRate);
    ps2_command(0x0a, kDP_SetMouseSampleRate);
    
    request.commands[cmd].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmd++].inOrOut = kDP_GetMouseInformation;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commands[cmd].command = kPS2C_ReadDataPort;
    request.commands[cmd++].inOrOut = 0;
    request.commandsCount = cmd;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    exitCommandMode();
    
    return 0;
}

bool AppleUSBMultitouchDriver::alps_hw_init_dolphin_v1() {
    
    ps2_command_short(kDP_SetMouseStreamMode);
    ps2_command(0x64, kDP_SetMouseSampleRate);
    ps2_command(0x28, kDP_SetMouseSampleRate);
    ps2_command_short(kDP_Enable);
    
    return true;
}

bool AppleUSBMultitouchDriver::hwInitV7(){
    TPS2Request<16> request;
    int reg_val;
    
    if (!enterCommandMode())
        goto error;
    
    if (commandModeReadReg(0xc2d9) == -1)
        goto error;
    
    if (!alps_get_v3_v7_resolution(0xc397))
        goto error;
    
    if (!commandModeWriteReg(0xc2c9, 0x64))
        goto error;
    
    reg_val = commandModeReadReg(0xc2c4);
    if (reg_val == -1)
        goto error;
    
    if (!commandModeWriteReg(reg_val | 0x02))
        goto error;
    
    exitCommandMode();
    
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_Enable;
    request.commandsCount = 1;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount == 1;
error:
    exitCommandMode();
    return false;
}

bool AppleUSBMultitouchDriver::alps_hw_init_ss4_v2()
{
    /* enter absolute mode */
    ps2_command_short(kDP_SetMouseStreamMode);
    ps2_command_short(kDP_SetMouseStreamMode);
    ps2_command(0x64, kDP_SetMouseSampleRate);
    ps2_command(0x28, kDP_SetMouseSampleRate);
    
    /* T.B.D. Decread noise packet number, delete in the future */
    exitCommandMode();
    enterCommandMode();
    commandModeWriteReg(0x001D, 0x20);
    exitCommandMode();
    
    /* final init */
    ps2_command_short(kDP_Enable);
    
    return true;
    
}

bool AppleUSBMultitouchDriver::ps2_command(unsigned char value, UInt8 command)
{
    TPS2Request<2> request;
    int cmdCount = 0;
    
    request.commands[cmdCount].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdCount++].inOrOut = command;
    request.commands[cmdCount].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdCount++].inOrOut = value;
    request.commandsCount = cmdCount;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount = cmdCount;
    
}

bool AppleUSBMultitouchDriver::ps2_command_short(UInt8 command)
{
    TPS2Request<1> request;
    int cmdCount = 0;
    
    request.commands[cmdCount].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdCount++].inOrOut = command;
    request.commandsCount = cmdCount;
    assert(request.commandsCount <= countof(request.commands));
    _device->submitRequestAndBlock(&request);
    
    return request.commandsCount = cmdCount;
    
}

void AppleUSBMultitouchDriver::set_protocol() {
    priv.byte0 = 0x8f;
    priv.mask0 = 0x8f;
    priv.flags = ALPS_DUALPOINT;
    
    priv.x_max = 2000;
    priv.y_max = 1400;
    priv.x_bits = 15;
    priv.y_bits = 11;
    
    switch (priv.proto_version) {
        case ALPS_PROTO_V1:
        case ALPS_PROTO_V2:
            hw_init = &AppleUSBMultitouchDriver::hwInitV1V2;
            process_packet = &AppleUSBMultitouchDriver::processPacketV1V2;
            priv.x_max = 1023;
            priv.y_max = 767;
            //            set_abs_params = alps_set_abs_params_st;
            break;
            
        case ALPS_PROTO_V3:
            hw_init = &AppleUSBMultitouchDriver::hwInitV3;
            process_packet = &AppleUSBMultitouchDriver::processPacketV3;
            //            set_abs_params = alps_set_abs_params_mt;
            decode_fields = &AppleUSBMultitouchDriver::decodePinnacle;
            priv.nibble_commands = alps_v3_nibble_commands;
            priv.addr_command = kDP_MouseResetWrap;
            
            if (alps_probe_trackstick_v3_v7(ALPS_REG_BASE_PINNACLE)) {
                priv.flags &= ~ALPS_DUALPOINT;
            }
            
            break;
            
        case ALPS_PROTO_V3_RUSHMORE:
            hw_init = &AppleUSBMultitouchDriver::hwInitRushmoreV3;
            process_packet = &AppleUSBMultitouchDriver::processPacketV3;
            //            set_abs_params = alps_set_abs_params_mt;
            decode_fields = &AppleUSBMultitouchDriver::decodeRushmore;
            priv.nibble_commands = alps_v3_nibble_commands;
            priv.addr_command = kDP_MouseResetWrap;
            priv.x_bits = 16;
            priv.y_bits = 12;
            
            if (alps_probe_trackstick_v3_v7(ALPS_REG_BASE_RUSHMORE)) {
                priv.flags &= ~ALPS_DUALPOINT;
            }
            break;
            
        case ALPS_PROTO_V4:
            hw_init = &AppleUSBMultitouchDriver::hwInitV4;
            process_packet = &AppleUSBMultitouchDriver::processPacketV4;
            //            set_abs_params = alps_set_abs_params_mt;
            priv.nibble_commands = alps_v4_nibble_commands;
            priv.addr_command = kDP_SetDefaultsAndDisable;
            break;
            
        case ALPS_PROTO_V5:
            hw_init = &AppleUSBMultitouchDriver::alps_hw_init_dolphin_v1;
            process_packet = &AppleUSBMultitouchDriver::alps_process_touchpad_packet_v3_v5;
            decode_fields = &AppleUSBMultitouchDriver::decodeDolphin;
            //            set_abs_params = alps_set_abs_params_mt;
            priv.nibble_commands = alps_v3_nibble_commands;
            priv.addr_command = kDP_MouseResetWrap;
            priv.byte0 = 0xc8;
            priv.mask0 = 0xc8;
            priv.flags = 0;
            priv.x_max = 1360;
            priv.y_max = 660;
            priv.x_bits = 23;
            priv.y_bits = 12;
            
            alps_dolphin_get_device_area(&priv);
            
            break;
            
        case ALPS_PROTO_V7:
            hw_init = &AppleUSBMultitouchDriver::hwInitV7;
            process_packet = &AppleUSBMultitouchDriver::processPacketV7;
            decode_fields = &AppleUSBMultitouchDriver::decodeV7;
            priv.nibble_commands = alps_v3_nibble_commands;
            priv.addr_command = kDP_MouseResetWrap;
            priv.byte0 = 0x48;
            priv.mask0 = 0x48;
            
            priv.x_max = 0xfff;
            priv.y_max = 0x7ff;
            
            if (priv.fw_ver[1] != 0xba){
                priv.flags |= ALPS_BUTTONPAD;
                IOLog("ALPS: ButtonPad Detected!\n");
            }
            
            if (alps_probe_trackstick_v3_v7(ALPS_REG_BASE_V7)){
                priv.flags &= ~ALPS_DUALPOINT;
            }
            
            break;
            
        case ALPS_PROTO_V8:
            hw_init = &AppleUSBMultitouchDriver::alps_hw_init_ss4_v2;
            process_packet = &AppleUSBMultitouchDriver::alps_process_packet_ss4_v2;
            decode_fields = &AppleUSBMultitouchDriver::alps_decode_ss4_v2;
            priv.nibble_commands = alps_v3_nibble_commands;
            priv.addr_command = kDP_MouseResetWrap;
            priv.byte0 = 0x18;
            priv.mask0 = 0x18;
            priv.flags = 0;
            
            alps_set_defaults_ss4_v2(&priv);
            
            priv.x_max = 8192;
            priv.y_max = 4096;
            priv.flags |= ALPS_BUTTONPAD;
            
            if (priv.fw_ver[1] == 0x1)
                priv.flags |= ALPS_DUALPOINT |
                ALPS_DUALPOINT_WITH_PRESSURE;
            
            break;
    }
}

bool AppleUSBMultitouchDriver::matchTable(ALPSStatus_t *e7, ALPSStatus_t *ec) {
    const struct alps_model_info *model;
    int i;
    
    for (i = 0; i < ARRAY_SIZE(alps_model_data); i++) {
        model = &alps_model_data[i];
        
        if (!memcmp(e7->bytes, model->signature, sizeof(model->signature)) &&
            (!model->command_mode_resp ||
             model->command_mode_resp == ec->bytes[2])) {
                
                priv.proto_version = model->proto_version;
                set_protocol();
                
                priv.flags = model->flags;
                priv.byte0 = model->byte0;
                priv.mask0 = model->mask0;
                
                return true;
            }
    }
    
    return false;
}

IOReturn AppleUSBMultitouchDriver::identify() {
    ALPSStatus_t e6, e7, ec;
    
    /*
     * First try "E6 report".
     * ALPS should return 0,0,10 or 0,0,100 if no buttons are pressed.
     * The bits 0-2 of the first byte will be 1s if some buttons are
     * pressed.
     */
    
    if (!repeatCmd(kDP_SetMouseResolution, NULL, kDP_SetMouseScaling1To1, &e6)) {
        IOLog("%s::identify: not an ALPS device. Error getting E6 report\n", getName());
        //return kIOReturnIOError;
    }
    
    if ((e6.bytes[0] & 0xf8) != 0 || e6.bytes[1] != 0 || (e6.bytes[2] != 10 && e6.bytes[2] != 100)) {
        IOLog("%s::identify: not an ALPS device. Invalid E6 report\n", getName());
        //return kIOReturnInvalid;
    }
    
    /*
     * Now get the "E7" and "EC" reports.  These will uniquely identify
     * most ALPS touchpads.
     */
    if (!(repeatCmd(kDP_SetMouseResolution, NULL, kDP_SetMouseScaling2To1, &e7) &&
          repeatCmd(kDP_SetMouseResolution, NULL, kDP_MouseResetWrap, &ec) &&
          exitCommandMode())) {
        IOLog("%s::identify: not an ALPS device. Error getting E7/EC report\n", getName());
        return kIOReturnIOError;
    }
    
    if (matchTable(&e7, &ec)) {
        return 0;
        
    } else if (e7.bytes[0] == 0x73 && e7.bytes[1] == 0x03 && e7.bytes[2] == 0x50 &&
               ec.bytes[0] == 0x73 && (ec.bytes[1] == 0x01 || ec.bytes[1] == 0x02)) {
        priv.proto_version = ALPS_PROTO_V5;
        IOLog("ALPS: Found a V5 Dolphin TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
    } else if (ec.bytes[0] == 0x88 &&
               ((ec.bytes[1] & 0xf0) == 0xb0 || (ec.bytes[1] & 0xf0) == 0xc0)) {
        priv.proto_version = ALPS_PROTO_V7;
        IOLog("ALPS: Found a V7 TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
    } else if (ec.bytes[0] == 0x88 && ec.bytes[1] == 0x08) {
        priv.proto_version = ALPS_PROTO_V3_RUSHMORE;
        IOLog("ALPS: Found a V3 Rushmore TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
    } else if (ec.bytes[0] == 0x88 && ec.bytes[1] == 0x07 &&
               ec.bytes[2] >= 0x90 && ec.bytes[2] <= 0x9d) {
        priv.proto_version = ALPS_PROTO_V3;
        IOLog("ALPS: Found a V3 Pinnacle TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
    } else if (e7.bytes[0] == 0x73 && e7.bytes[1] == 0x03 &&
               e7.bytes[2] == 0x14 && ec.bytes[1] == 0x02) {
        priv.proto_version = ALPS_PROTO_V8;
        IOLog("ALPS: Found a V8 TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
        
    } else if (e7.bytes[0] == 0x73 && e7.bytes[1] == 0x03 &&
               e7.bytes[2] == 0x28 && ec.bytes[1] == 0x01) {
        priv.proto_version = ALPS_PROTO_V8;
        IOLog("ALPS: Found a V8 TouchPad with ID: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n", e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        
        
    } else {
        IOLog("ALPS DRIVER: TouchPad didn't match any known IDs: E7=0x%02x 0x%02x 0x%02x, EC=0x%02x 0x%02x 0x%02x\n",
              e7.bytes[0], e7.bytes[1], e7.bytes[2], ec.bytes[0], ec.bytes[1], ec.bytes[2]);
        return kIOReturnInvalid;
    }
    
    /* Save the Firmware version */
    memcpy(priv.fw_ver, ec.bytes, 3);
    set_protocol();
    return 0;
}
