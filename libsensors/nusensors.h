/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_SENSORS_H
#define ANDROID_SENSORS_H

#include <stdint.h>
#include <errno.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include <linux/input.h>

#include <hardware/hardware.h>
#include <hardware/sensors.h>

// gingerbread/ics compat
#ifndef SENSOR_TYPE_AMBIENT_TEMPERATURE
#define SENSOR_TYPE_AMBIENT_TEMPERATURE SENSOR_TYPE_TEMPERATURE
#endif

__BEGIN_DECLS

/*****************************************************************************/

int init_nusensors(hw_module_t const* module, hw_device_t** device);

/*****************************************************************************/

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/*****************************************************************************/

/*
 * The Defy has two accelerometers in hardware (KXTF9 and AK8973). We use
 * the KXTF9 one for higher precision.
 */
#define USE_KXTF9_ACCELEROMETER

/*****************************************************************************/

/*
 * The SENSORS Module
 */

/* the CM3602 is a binary proximity sensor that triggers around 9 cm on
 * this hardware */
#define PROXIMITY_THRESHOLD_CM  9.0f

/*****************************************************************************/

#define CM_DEVICE_NAME      "/dev/sfh7743"
#define LS_DEVICE_NAME      "/dev"

#define EVENT_TYPE_YAW              ABS_RX
#define EVENT_TYPE_PITCH            ABS_RY
#define EVENT_TYPE_ROLL             ABS_RZ
#define EVENT_TYPE_ORIENT_STATUS    ABS_RUDDER

#define EVENT_TYPE_MAGV_X           ABS_HAT0X
#define EVENT_TYPE_MAGV_Y           ABS_HAT0Y
#define EVENT_TYPE_MAGV_Z           ABS_BRAKE

#define EVENT_TYPE_TEMPERATURE      ABS_THROTTLE
#define EVENT_TYPE_STEP_COUNT       ABS_GAS
#define EVENT_TYPE_PROXIMITY        ABS_DISTANCE
#define EVENT_TYPE_LIGHT            LED_MISC // led sensor 1
#define EVENT_TYPE_LIGHT2           MSC_RAW  // led sensor 2

#define KXTF9_DEVICE_NAME                  "/dev/kxtf9"
#define KXTF9_DEFAULT_DELAY                (50 * 1000000)

#define KXTF9_LSG                          (1000.0f)
#define KXTF9_CONVERT_A                    (GRAVITY_EARTH / KXTF9_LSG)
#define KXTF9_CONVERT_A_X                  (KXTF9_CONVERT_A)
#define KXTF9_CONVERT_A_Y                  (KXTF9_CONVERT_A)
#define KXTF9_CONVERT_A_Z                  (KXTF9_CONVERT_A)

#define AK8973_DEVICE_NAME                 "/dev/akm8973_aot"
#define AK8973_DEFAULT_DELAY               (50 * 1000000)

#define AK8973_LSG                         (1000.0f)
#define AK8973_CONVERT_A                   (GRAVITY_EARTH / AK8973_LSG)
#define AK8973_CONVERT_A_X                 (AK8973_CONVERT_A)
#define AK8973_CONVERT_A_Y                 (AK8973_CONVERT_A)
#define AK8973_CONVERT_A_Z                 (AK8973_CONVERT_A)

#define AK8973_CONVERT_M                   (1.0f/16.0f)
#define AK8973_CONVERT_M_X                 (AK8973_CONVERT_M)
#define AK8973_CONVERT_M_Y                 (-AK8973_CONVERT_M)
#define AK8973_CONVERT_M_Z                 (-AK8973_CONVERT_M)

#define AK8973_CONVERT_O                   (1.0f/64.0f)
#define AK8973_CONVERT_O_A                 (AK8973_CONVERT_O)
#define AK8973_CONVERT_O_P                 (AK8973_CONVERT_O)
#define AK8973_CONVERT_O_R                 (-AK8973_CONVERT_O)

#define AK8973_SENSOR_STATE_MASK           (0x3)

/*****************************************************************************/

__END_DECLS

#endif  // ANDROID_SENSORS_H
