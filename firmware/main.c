#include "main.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"

#include "bsp/rp2040/board.h"
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

#include "cpubus.h"
#include "ppu.h"
#include "osd.h"
#include "debug.h"
#include "gamedb/game_detection.h"

#include "jpeg/base_jpeg.h"
#include "jpeg/base_jpeg_no_chroma.h"

#include "screens/default.h"
#include "screens/off.h"
#include "screens/error.h"

bool frameSending = false;

bool modeButtonDebounce = true;

uint fallbackFrameIndex = 0;
enum FallbackScreenType {FST_NONE = 0, FST_DEFAULT, FST_OFF, FST_ERROR} fallbackScreenType = FST_NONE;  

int dmaChannel;
dma_channel_config dmaConfig;

bool dmgColorMode = false;

bool includeChroma = true;
bool is30fpsFrame = true;

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
    #ifndef BASE_VIDEO_MODE
    if (gpio_get(LED_SWITCH_PIN)) {
        if (!modeButtonDebounce) {
            modeButtonDebounce = true;
            //Button pressed, switch mode
            frameBlending = !frameBlending;
            if (frameBlending) {
                dmgColorMode = !dmgColorMode;
                frontBuffer[JPEG_CHROMA_OFFSET] = dmgColorMode ? 0b10001000 : 0x00;
                readyBuffer[JPEG_CHROMA_OFFSET] = dmgColorMode ? 0b10001000 : 0x00;
            }
            renderOSD(frameBlending ? "Blending ON" : "Blending OFF", 0x03, 0x00, MODE_INFO_DURATION);
        }
    } else if (modeButtonDebounce) {
        modeButtonDebounce = false;
    }
    #endif
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
    osdPosition = SCREEN_H;
    dma_channel_configure(dmaChannel, &dmaConfig, backBuffer, screen, SCREEN_SIZE / 4, true);
    while (dma_channel_is_busy(dmaChannel)) {
        tud_task();
    }
    fallbackScreenType = type;
    renderText(VERSION, 0x00, 0x03, (uint8_t *)backBuffer, SCREEN_W-(sizeof(VERSION)-1)*8, 1);
}

bool usbSendFrame() {
    if (tud_video_n_streaming(0, 0)) {
        if (!frameSending) {
            if (swapFrontbuffer()) {
                is30fpsFrame = !is30fpsFrame; //If clock is determined by the Game Boy and if we include Chroma, we only send every second frame (i.e. 30fps)
                if (is30fpsFrame || !includeChroma || !running) {
                    frameSending = true;
                    tud_video_n_frame_xfer(0, 0, (void*)(frontBuffer + (includeChroma || !running ? 0 : JPEG_HEADER_SIZE - JPEG_HEADER_SIZE_NO_CHROMA)), includeChroma || !running ? FRAME_SIZE : FRAME_SIZE_NO_CHROMA);
                }
                return true;
            }
        }
    } else {
        frameSending = false;
    }
    return false;
}

void updateFallbackScreen() { 
    fallbackFrameIndex++;
    if (fallbackFrameIndex >= 160)
        fallbackFrameIndex = 0;
    for (int x = 0; x < SCREEN_W/2; x++) {
        if (x < fallbackFrameIndex && x + 80 > fallbackFrameIndex) {
            backBuffer[63*SCREEN_W + 80 + x] = 0x03;
            backBuffer[63*SCREEN_W + 79 - x] = 0x03;
        } else {
            backBuffer[63*SCREEN_W + 80 + x] = 0x00;
            backBuffer[63*SCREEN_W + 79 - x] = 0x00;
        }
    }
}

void fillBufferWithBaseJpeg(uint8_t * target, bool includeChroma) {
    int chromaOffset = (includeChroma ? 0 : JPEG_HEADER_SIZE - JPEG_HEADER_SIZE_NO_CHROMA);
    int headerSize = (includeChroma ? JPEG_HEADER_SIZE : JPEG_HEADER_SIZE_NO_CHROMA);
    for (int i = 0; i < headerSize; i++)
        target[i + chromaOffset] = (includeChroma ? base_jpeg : base_jpeg_no_chroma)[i];
    int fullSize = (includeChroma ? FRAME_SIZE : FRAME_SIZE_NO_CHROMA);
    for (int i = JPEG_DATA_SIZE + headerSize; i < fullSize; i++)
        target[i + chromaOffset] = (includeChroma ? base_jpeg : base_jpeg_no_chroma)[i];
}

void updateIncludeChroma() {
    fillBufferWithBaseJpeg((uint8_t *)frontBuffer, includeChroma || !running);
    fillBufferWithBaseJpeg((uint8_t *)readyBuffer, includeChroma || !running);
    fallbackScreenType = FST_NONE;
}

int main(void) {
    set_sys_clock_khz(250000, true);

    board_init();
    setupDMA();
    setUniqueSerial();
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();
    setupGPIO();
    prepareJpegEncoding();

    multicore_launch_core1(handleMemoryBus);
    
    while (1) {

        printf("Waiting for game.\n");
        updateIncludeChroma();
        uint lastFrame = timer_hw->timerawl;
        while (!running) {
            if (isGameBoyOn()) {
                if (fallbackScreenType == FST_NONE || fallbackScreenType == FST_OFF) {
                    loadFallbackScreen(default_raw, FST_DEFAULT);
                    renderText("Waiting for game...", 0x03, 0x00, (uint8_t *)backBuffer, 5, 79);
                    if (!includeChroma)
                        renderText("   60 fps mode.\nSwitch to 30fps if\nthere are problems.", 0x03, 0x00, (uint8_t *)backBuffer, 5, 100);
                    readyBufferIsNew = false;
                    startBackbufferToJPEG(false);
                }
            } else {
                if (fallbackScreenType == FST_NONE || fallbackScreenType == FST_DEFAULT || fallbackScreenType == FST_ERROR) {
                    loadFallbackScreen(off_raw, FST_OFF);
                    renderText("The Game Boy\nis turned off", 0x03, 0x00, (uint8_t *)backBuffer, 40, 79);
                    if (!includeChroma)
                        renderText("   60 fps mode.\nSwitch to 30fps if\nthere are problems.", 0x03, 0x00, (uint8_t *)backBuffer, 5, 100);
                    readyBufferIsNew = false;
                    startBackbufferToJPEG(false);
                }
            }
            if (readyBufferIsNew && (!includeChroma || ((uint)(timer_hw->timerawl - lastFrame) > 33333))) {
                if (usbSendFrame()) {
                    lastFrame = timer_hw->timerawl;
                    updateFallbackScreen();
                    startBackbufferToJPEG(false);
                }
            } else
                continueBackbufferToJPEG();
            tud_task();
        }

        ledOn();
        printf("Game started. Cycle ratio: %d\n", cycleRatio);
        updateIncludeChroma();
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
                    if (!gameDetected) {
                        if (detectGame()) {
                            renderOSD(gameInfo.title, 0x00, 0x03, GAME_DETECTED_INFO_DURATION);
                        }
                    }
                } else if (vblank) {
                    if (y < SCREEN_H) {
                        vblank = false;
                        checkModeSwitch();
                        ledOn();
                    }
                }

                tud_task();
                if (renderState == done) {
                    usbSendFrame();
                }
            #endif
        }

        ledOff();
        if (error != NULL) {
            mutex_enter_blocking(&cpubusMutex); //Grab mutex immediately.
            loadFallbackScreen(error_raw, FST_ERROR);
            renderText("Sorry,\nsomething\nwent wrong...", 0x03, 0x00, (uint8_t *)backBuffer, 44, 72);
            renderText((const char *)error, 0x02, 0x00, (uint8_t *)backBuffer, 4, 110);
            readyBufferIsNew = false;
            startBackbufferToJPEG(false);
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
    #ifdef BASE_VIDEO_MODE
    includeChroma = true;
    #else
    includeChroma = parameters->dwFrameInterval > 200000 /*slower than 50fps*/;
    #endif
    updateIncludeChroma();
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

