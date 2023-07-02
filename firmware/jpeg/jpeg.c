#include "jpeg/jpeg.h"
#include "ppu.h"

#include "jpeg_prepare.pio.h"
#include "jpeg_encoding.pio.h"

#include "osd.h"

#include "hardware/dma.h"

#include <stdio.h>

//The JPEG data uses a Huffman table that is designed such that every pixel can be
//encoded in 5 bit (see https://github.com/Staacks/gbinterceptor/issues/17).

//jpeg encoding is prepared by two parallel PIO state machines (CPU feeds them in an alternating pattern)
//Then two chained DMA channels per SM distributes the results to four state machines that encode in parallel
//The output is then put back together by a DMA channel that chains with a controling DMA channel iterating over the four outputs of the encoding SMs

/*
                    encode SM AA
                   /
      prepare SM A
     /             \
    /               encode SM AB
CPU
    \               encode SM BA
     \             /
      prepare SM B
                   \
                    encode SM BB
*/

//For this to work we have to take into account multiples of the five bit encoded data and 32bit input data with 8bit pixel.

/*
                                                        8 pixels / 32bit => encode SM AA => 8 pixels / 40bit
                                                       /                                                    \
16 pixels / 128bit => prepare SM A => 16 pixels / 64bit                                                      16 pixels / 80 bit
                                                       \                                                    /
                                                        8 pixels / 32bit => encode SM AB => 8 pixels / 40bit

So:
1. The CPU has to write two 32bit blocks representing 16 pixels to alternating prepare state machines.
2. The DMA channels between the SMs need to alternate each 32bit output from a prepare SM to the encode SM, writing 8 pixels with a single 32bit write to each encode SM
3. The output DMA channel needs to pick up five 8bit blocks from each encode SM before moving on to the next.

Therefore, pixels are distributed as follows:

8 pixels => 2 32bit writes to prepare SM A => 1 32bit transfer to encode SM AA => 5 8bit reads and consecutive writes to target memory
8 pixels => 2 32bit writes to prepare SM A => 1 32bit transfer to encode SM AB => 5 8bit reads and consecutive writes to target memory
8 pixels => 2 32bit writes to prepare SM B => 1 32bit transfer to encode SM BA => 5 8bit reads and consecutive writes to target memory
8 pixels => 2 32bit writes to prepare SM B => 1 32bit transfer to encode SM BB => 5 8bit reads and consecutive writes to target memory

So, 32 pixels (8 32bit blocks) should always be started in one go.

*/

#define PREPARE_PIO pio0
#define ENCODE_PIO pio1
#define PREPARE_SM_A 1 //Do not use SM0 on PIO0, that one is used to capture the memory bus
#define PREPARE_SM_B 2
#define ENCODE_SM_AA 0
#define ENCODE_SM_AB 1
#define ENCODE_SM_BA 2
#define ENCODE_SM_BB 3

#define ENCODE_OUT_STALL_MASK 0x0000000f //output from encode SMs stalled in fdebug register
#define ENCODE_IN_EMPTY_MASK 0x0f000000 //Input to encode SMs empty in fstat register 


//DMA channel connecting the jpegPIO out FIFO to the readyBuffer
int dmaChannelToEncodeAA, dmaChannelToEncodeAB, dmaChannelToEncodeBA, dmaChannelToEncodeBB, dmaChannelFromEncode, dmaChannelFromEncodeSource;
dma_channel_config dmaConfigToEncodeAA, dmaConfigToEncodeAB, dmaConfigToEncodeBA, dmaConfigToEncodeBB, dmaConfigFromEncode, dmaConfigFromEncodeSource;
uint32_t volatile const * dmaFromEncodeSourceAddresses[4];

int jpegPreviousDC;

uint32_t * backIterator = NULL;
uint32_t * lastIterator = NULL;
uint encodeIndex; //Position in backbuffer copy process in multiples of four bytes (we copy 32bit at once)
uint osdIndex; //index at which the backbuffer transfer should copy the osdBuffer instead

void setupJpegPIO() {
    uint offsetPrepare = pio_add_program(PREPARE_PIO, &jpegPrepare_program);
    uint offsetEncode = pio_add_program(ENCODE_PIO, &jpegEncoding_program);

    jpegPrepare_program_init(PREPARE_PIO, PREPARE_SM_A, offsetPrepare);
    pio_sm_set_enabled(PREPARE_PIO, PREPARE_SM_A, true);
    jpegPrepare_program_init(PREPARE_PIO, PREPARE_SM_B, offsetPrepare);
    pio_sm_set_enabled(PREPARE_PIO, PREPARE_SM_B, true);

    jpegEncoding_program_init(ENCODE_PIO, ENCODE_SM_AA, offsetEncode);
    pio_sm_set_enabled(ENCODE_PIO, ENCODE_SM_AA, true);
    jpegEncoding_program_init(ENCODE_PIO, ENCODE_SM_AB, offsetEncode);
    pio_sm_set_enabled(ENCODE_PIO, ENCODE_SM_AB, true);
    jpegEncoding_program_init(ENCODE_PIO, ENCODE_SM_BA, offsetEncode);
    pio_sm_set_enabled(ENCODE_PIO, ENCODE_SM_BA, true);
    jpegEncoding_program_init(ENCODE_PIO, ENCODE_SM_BB, offsetEncode);
    pio_sm_set_enabled(ENCODE_PIO, ENCODE_SM_BB, true);
}

void setupJpegDMA() {
    encodeIndex = SCREEN_SIZE; //Reset transfer state to "end"

    //dmaChannelToEncodeAA
    dmaChannelToEncodeAA = dma_claim_unused_channel(true);
    dmaConfigToEncodeAA = dma_channel_get_default_config(dmaChannelToEncodeAA);
    channel_config_set_transfer_data_size(&dmaConfigToEncodeAA, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaConfigToEncodeAA, false);
    channel_config_set_write_increment(&dmaConfigToEncodeAA, false);
    channel_config_set_dreq(&dmaConfigToEncodeAA, pio_get_dreq(PREPARE_PIO, PREPARE_SM_A, false));

    //dmaChannelToEncodeAB
    dmaChannelToEncodeAB = dma_claim_unused_channel(true);
    dmaConfigToEncodeAB = dma_channel_get_default_config(dmaChannelToEncodeAB);
    channel_config_set_transfer_data_size(&dmaConfigToEncodeAB, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaConfigToEncodeAB, false);
    channel_config_set_write_increment(&dmaConfigToEncodeAB, false);
    channel_config_set_dreq(&dmaConfigToEncodeAB, pio_get_dreq(PREPARE_PIO, PREPARE_SM_A, false));

    //Chaining AA <-> AB
    channel_config_set_chain_to(&dmaConfigToEncodeAA, dmaChannelToEncodeAB);
    channel_config_set_chain_to(&dmaConfigToEncodeAB, dmaChannelToEncodeAA);

    //dmaChannelToEncodeBA
    dmaChannelToEncodeBA = dma_claim_unused_channel(true);
    dmaConfigToEncodeBA = dma_channel_get_default_config(dmaChannelToEncodeBA);
    channel_config_set_transfer_data_size(&dmaConfigToEncodeBA, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaConfigToEncodeBA, false);
    channel_config_set_write_increment(&dmaConfigToEncodeBA, false);
    channel_config_set_dreq(&dmaConfigToEncodeBA, pio_get_dreq(PREPARE_PIO, PREPARE_SM_B, false));

    //dmaChannelToEncodeBB
    dmaChannelToEncodeBB = dma_claim_unused_channel(true);
    dmaConfigToEncodeBB = dma_channel_get_default_config(dmaChannelToEncodeBB);
    channel_config_set_transfer_data_size(&dmaConfigToEncodeBB, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaConfigToEncodeBB, false);
    channel_config_set_write_increment(&dmaConfigToEncodeBB, false);
    channel_config_set_dreq(&dmaConfigToEncodeBB, pio_get_dreq(PREPARE_PIO, PREPARE_SM_B, false));

    //Chaining BA <-> BB
    channel_config_set_chain_to(&dmaConfigToEncodeBA, dmaChannelToEncodeBB);
    channel_config_set_chain_to(&dmaConfigToEncodeBB, dmaChannelToEncodeBA);

    //dmaChannelFromEncode
    dmaChannelFromEncode = dma_claim_unused_channel(true);
    dmaConfigFromEncode = dma_channel_get_default_config(dmaChannelFromEncode);
    channel_config_set_transfer_data_size(&dmaConfigFromEncode, DMA_SIZE_8);
    channel_config_set_read_increment(&dmaConfigFromEncode, false);
    channel_config_set_write_increment(&dmaConfigFromEncode, true);

    //dmaChannelFromEncodeSource
    dmaChannelFromEncodeSource = dma_claim_unused_channel(true);
    dmaConfigFromEncodeSource = dma_channel_get_default_config(dmaChannelFromEncodeSource);
    channel_config_set_transfer_data_size(&dmaConfigFromEncodeSource, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaConfigFromEncodeSource, true);
    channel_config_set_write_increment(&dmaConfigFromEncodeSource, false);
    dmaFromEncodeSourceAddresses[0] = &ENCODE_PIO->rxf[ENCODE_SM_AA];
    dmaFromEncodeSourceAddresses[1] = &ENCODE_PIO->rxf[ENCODE_SM_AB];
    dmaFromEncodeSourceAddresses[2] = &ENCODE_PIO->rxf[ENCODE_SM_BA];
    dmaFromEncodeSourceAddresses[3] = &ENCODE_PIO->rxf[ENCODE_SM_BB];
    dmaFromEncodeSourceAddresses[4] = 0; //Do not trigger again

    //Chaining
    channel_config_set_chain_to(&dmaConfigFromEncode, dmaChannelFromEncodeSource);

}

void prepareJpegEncoding() {
    setupJpegPIO();
    setupJpegDMA();
}

void inline startBackbufferToJPEG(bool allowFrameBlend) {
    backIterator = (uint32_t *) (backBuffer);
    if (frameBlending && allowFrameBlend)
        lastIterator = (uint32_t *) (lastBuffer);
    else
        lastIterator = backIterator; //If frame blending is disabled, we simply blend the newest frame with itself.
    animateOSD();
    osdIndex = osdPosition * SCREEN_W;
    encodeIndex = 0;

    //Reset all PIOs and DMA channels to avoid starting in an unknown state if a frame has been aborted

    pio_sm_clear_fifos(PREPARE_PIO, PREPARE_SM_A);
    pio_sm_restart(PREPARE_PIO, PREPARE_SM_A);
    pio_sm_clear_fifos(PREPARE_PIO, PREPARE_SM_B);
    pio_sm_restart(PREPARE_PIO, PREPARE_SM_B);
    pio_sm_clear_fifos(ENCODE_PIO, ENCODE_SM_AA);
    pio_sm_restart(ENCODE_PIO, ENCODE_SM_AA);
    pio_sm_clear_fifos(ENCODE_PIO, ENCODE_SM_AB);
    pio_sm_restart(ENCODE_PIO, ENCODE_SM_AB);
    pio_sm_clear_fifos(ENCODE_PIO, ENCODE_SM_BA);
    pio_sm_restart(ENCODE_PIO, ENCODE_SM_BA);
    pio_sm_clear_fifos(ENCODE_PIO, ENCODE_SM_BB);
    pio_sm_restart(ENCODE_PIO, ENCODE_SM_BB);

    ENCODE_PIO->fdebug = 0xffffffff; //Clear encode debug register (no need to be specific - all SMs are working on encoding)

    dma_channel_configure(dmaChannelToEncodeAB, &dmaConfigToEncodeAB, &ENCODE_PIO->txf[ENCODE_SM_AB], &PREPARE_PIO->rxf[PREPARE_SM_A], 1, false);
    dma_channel_configure(dmaChannelToEncodeAA, &dmaConfigToEncodeAA, &ENCODE_PIO->txf[ENCODE_SM_AA], &PREPARE_PIO->rxf[PREPARE_SM_A], 1, true);

    dma_channel_configure(dmaChannelToEncodeBB, &dmaConfigToEncodeBB, &ENCODE_PIO->txf[ENCODE_SM_BB], &PREPARE_PIO->rxf[PREPARE_SM_B], 1, false);
    dma_channel_configure(dmaChannelToEncodeBA, &dmaConfigToEncodeBA, &ENCODE_PIO->txf[ENCODE_SM_BA], &PREPARE_PIO->rxf[PREPARE_SM_B], 1, true);

    dma_channel_configure(dmaChannelFromEncode, &dmaConfigFromEncode, readyBuffer + JPEG_HEADER_SIZE, NULL, 5, false); //Source will be set later by dmaChannelFromEncodeSource
    dma_channel_configure(dmaChannelFromEncodeSource, &dmaConfigFromEncodeSource, &dma_hw->ch[dmaChannelFromEncode].al3_read_addr_trig, &dmaFromEncodeSourceAddresses[0], 1, false);

    jpegPreviousDC = 3; //We map all colors to -3, -2, -1, 0, +1, +2, +3. Thanks to the differential encoding, we can keep using unsigned integers [0..6] and only need to make sure that the first value is encoded correctly. To achieve this we initialize the "previous" DC value to the new equivalent to zero, which in this case is 3 in the middle of [0..6]

}

void inline pushPixelsToJpegPIO(int sm) {
    //Take care when looking at the following calculations: We read the sequential data as 32bit integer for performance reasons. But since the rp2040 is little-endian, they are represented in reverse byte order here.

    uint32_t v = *backIterator + *lastIterator; //Map colors indices of -3, -1, +1, +3 and blends can reach -3, -2, -1, 0, +1, +2, +3
    PREPARE_PIO->txf[sm] = (v | 0x08080808) - (v << 8) - jpegPreviousDC;
    jpegPreviousDC = v >> 24;
    backIterator++;
    lastIterator++;
}

void inline continueBackbufferToJPEG() {
    ENCODE_PIO->fdebug = 0xffffffff; //Clear encode debug register (no need to be specific - all SMs are working on encoding)
    if ((ENCODE_PIO->fdebug & ENCODE_OUT_STALL_MASK) == ENCODE_OUT_STALL_MASK) {
        //Data from last pixel push needs to be written to the ready buffer
        dma_hw->ch[dmaChannelFromEncodeSource].al3_read_addr_trig = (uint32_t)&dmaFromEncodeSourceAddresses[0];    //Restart DMA
        if (encodeIndex == SCREEN_SIZE) {
            //This was the last data set.
            readyBufferIsNew = true; //Note: At this point the we have written our pixels to the jpegPIO, but they may still be processed and pushed through DMA. However, this should be extremely fast and even if USB would start transferring the frame before DMA has compelte (which I highly doubt even happens), there is no way the transfer reaches the last bytes before DMA completes, so there is no reason to stall here.
        }
    }
    if (encodeIndex < SCREEN_SIZE && (ENCODE_PIO->fstat & ENCODE_IN_EMPTY_MASK) == ENCODE_IN_EMPTY_MASK) {
        pushPixelsToJpegPIO(PREPARE_SM_A);
        pushPixelsToJpegPIO(PREPARE_SM_A);
        pushPixelsToJpegPIO(PREPARE_SM_A);
        pushPixelsToJpegPIO(PREPARE_SM_A);
        pushPixelsToJpegPIO(PREPARE_SM_B);
        pushPixelsToJpegPIO(PREPARE_SM_B);
        pushPixelsToJpegPIO(PREPARE_SM_B);
        pushPixelsToJpegPIO(PREPARE_SM_B);
        encodeIndex += 8*4; //Bytes
        if (encodeIndex == osdIndex) {
            backIterator = (uint32_t *)osdBuffer;
            lastIterator = (uint32_t *)osdBuffer;
        }
    }
}

