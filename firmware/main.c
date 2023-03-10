#include "main.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

#include "cpubus.h"
#include "ppu.h"
#include "debug.h"
#include "gamedb/game_detection.h"

#include "screens/default_yuv.h"
#include "screens/game_end_yuv.h"
#include "screens/stalled_yuv.h"
#include "screens/error_yuv.h"

bool frameSending = false;

bool modeButtonDebounce = true;

uint fallbackFrameIndex = 0;
enum FallbackScreenType {FST_NONE = 0, FST_DEFAULT, FST_OFF, FST_ERROR} fallbackScreenType = FST_NONE;  

int dmaChannel;
dma_channel_config dmaConfig;

struct OnScreenDisplayText versionInfo = {.text = VERSION, .width = 0, .x = SCREEN_W-(sizeof(VERSION)-1)*8, .y = 0};

void setupGPIO() {
    gpio_init(GBSENSE_PIN);
    gpio_init(LED_SWITCH_PIN);
    gpio_set_dir(GBSENSE_PIN, GPIO_IN);
    
     //IMPORTANT: Since the mode button directly connects this GPIO pin to ground, we only want to drive it to the ground state to enable the LED. Disabling the LED is done by disabling the output, never by driving actively to HIGH as this might cause a short circuit if the button is pressed at the same time.
    gpio_put(LED_SWITCH_PIN, 0);
    gpio_set_dir(LED_SWITCH_PIN, GPIO_IN);
    gpio_pull_up(LED_SWITCH_PIN);
}

void ledOn() {
    gpio_set_dir(LED_SWITCH_PIN, GPIO_OUT);
}

void ledOff() {
    gpio_set_dir(LED_SWITCH_PIN, GPIO_IN);
}

void checkModeSwitch() {
    if (gpio_get(LED_SWITCH_PIN)) {
        if (!modeButtonDebounce) {
            modeButtonDebounce = true;
            //Button pressed, switch mode
            switchRenderMode();
            setBufferUVColors();
        }
    } else if (modeButtonDebounce) {
        modeButtonDebounce = false;
    }
}

bool static inline isGameBoyOn() {
    return gpio_get(GBSENSE_PIN);
}

void setupDMA() {
    dmaChannel = dma_claim_unused_channel(true);
    dmaConfig = dma_channel_get_default_config(dmaChannel);
    channel_config_set_read_increment(&dmaConfig, true);
    channel_config_set_write_increment(&dmaConfig, true);
}

void loadFallbackScreen(uint8_t * screen, enum FallbackScreenType type) {
    dma_channel_configure(dmaChannel, &dmaConfig, frontBuffer, screen, FRAME_SIZE / 4, true);
    fallbackScreenType = type;
}

bool usbSendFrame(bool swap) {
    if (tud_video_n_streaming(0, 0)) {
        if (!frameSending) {
            frameSending = true;

            if (swap)
                swapFrontbuffer();
            tud_video_n_frame_xfer(0, 0, (void*)frontBuffer, FRAME_SIZE);
            return true;
        }
    } else {
        frameSending = false;
    }
    return false;
}

void animateFallbackScreen() {
    //Just a quick idle animation by moving around the edges of the UV plane. Not exactly a beautiful rainbow, but enough to show that we are alive.
    uint8_t c, x, y;
    for (int i = 0; i < SCREEN_W; i+=2) {
        c = i + fallbackFrameIndex;
        if (c & 0x80) {
            if (c & 0x40) {
                x = 0x00;
                y = (uint8_t)(c << 2);
            } else {
                x = ~(uint8_t)(c << 2);
                y = 0x00;
            }
        } else {
            if (c & 0x40) {
                x = 0xff;
                y =  ~(uint8_t)(c << 2);
            } else {
                x =  (uint8_t)(c << 2);
                y = 0xff;
            }
        }
        frontBuffer[SCREEN_SIZE+64*SCREEN_W/2+i] = x;
        frontBuffer[SCREEN_SIZE+64*SCREEN_W/2+i+1] = y;
    }
    renderOSD(versionInfo, frontBuffer, 0x00, 0xff);
    fallbackFrameIndex++;
}

int main(void) {
    set_sys_clock_khz(250000, true);

    board_init();
    setupDMA();
    setUniqueSerial();
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();
    setupGPIO();

    multicore_launch_core1(handleMemoryBus);
    
    while (1) {

        printf("Waiting for game.\n");
        while (!running) {
            if (usbSendFrame(false))
                animateFallbackScreen();
            tud_task();
            if (isGameBoyOn()) {
                if (fallbackScreenType == FST_NONE || fallbackScreenType == FST_OFF)
                    loadFallbackScreen(default_yuv, FST_DEFAULT);
            } else {
                if (fallbackScreenType == FST_NONE || fallbackScreenType == FST_DEFAULT || fallbackScreenType == FST_ERROR)
                    loadFallbackScreen(game_end_yuv, FST_OFF);
            }
            
        }

        ledOn();
        printf("Game started. Cycle ratio: %d\n", cycleRatio);
        ppuInit();

        uint lastCycle = cycleIndex;
        uint8_t vblank = false;
        #ifdef DEBUG_PPU_TIMING
            uint lastPPUTimingRequest = timer_hw->timerawl;
        #endif
        while (running) {
            #if defined(DEBUG_MEMORY_DUMP)
                // Only debug opcodes or memory via serial without trying to render at the same time
                sleep_ms(1000);
                tud_task();
                #ifdef DEBUG_MEMORY_DUMP
                    dumpMemory();
                #endif
            #else
                uint steps = (uint)(cycleIndex - lastCycle);
                lastCycle += steps;
                DEBUG_MARK_VBLANK_ADJUST
                int adjust = vblankOffset; //We work with a copy as another thread may change this at any time
                if (adjust >= 0) {
                    if (adjust > 10) //Limit the amount of catching up per step to avoid to unpleasant results
                        adjust = 10;
                    vblankOffset -= adjust; //Yes, there might be a race condition here, but if another thread has calculated a new value for vblank in the meantime, it used the old state of the PPU, so it still should be adjusted and if it wrote this value between reading and decrementing, then we still only make a mistake of a few cycles which we will eventually fix anyway
                    ppuStep(steps + adjust);
                } else {
                    //else do not perform step to wait for the real Game Boy
                    vblankOffset += steps;
                }

                #ifdef DEBUG_PPU_TIMING
                    if ((uint)(timer_hw->timerawl - lastPPUTimingRequest) > 2e6) { //Wait a moment to avoid influencing the measurement by the serial output
                        lastPPUTimingRequest = timer_hw->timerawl;
                        recordPPUTiming = true;
                    }
                    if (ppuTimingReady) {
                        printPPUTiming();
                    }
                #endif

                if (!vblank && y >= SCREEN_H) {
                    vblank = true;
                    ledOff(); //Switches the LED GPIO to input to allow to use the same GPIO pin to read the mode button state, however, in order to allow the line to settle first, we do the read-out at the end of vblank and then re-enable the LED
                    startBackbufferBlend();
                    if (!gameDetected) {
                        if (detectGame()) {
                            showGameDetectedInfo(gameInfo.title);
                        }
                    }
                } else if (vblank) {
                    if (y < SCREEN_H) {
                        vblank = false;
                        checkModeSwitch();
                        ledOn();
                    } else
                        continueBackbufferBlend();
                }

                tud_task();
                if (renderState == done) {
                    //All pixels for this line have been rendered. Avoid USB tasks while rendering actual pixels. We can do this during HBLANK or VBLANK.
                    usbSendFrame(true);
                }
            #endif
        }

        ledOff();
        if (error != NULL) {
            mutex_enter_blocking(&cpubusMutex); //Grab mutex immediately.
            loadFallbackScreen(errorIsStall ? stalled_yuv : error_yuv, FST_ERROR);
            dumpMemory();
            dumpBus();
            stdio_flush();
            mutex_exit(&cpubusMutex);
        }
    }

    return 0;
}

// Invoked when device is mounted
void tud_mount_cb(void) {
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
}

void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx) {
  (void)ctl_idx; (void)stm_idx;
  frameSending = false;
}

int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx, video_probe_and_commit_control_t const *parameters) {
  (void)ctl_idx; (void)stm_idx;
  return VIDEO_ERROR_NONE;
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)  {
	(void)itf;
	(void)rts;

	if (dtr) {
        fflush(stdout);
		printf("\n\nGB Interceptor\nVersion ");
        printf(VERSION);
        printf("\n\n");
	}
}

