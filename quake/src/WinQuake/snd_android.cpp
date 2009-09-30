/*
 *  snd_android.c
 *  Android-specific sound interface
 *
 */

#include "quakedef.h"

#include <pthread.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <utils/Log.h>
#include <media/AudioTrack.h>

using namespace android;

static AudioTrack gAudioTrack;

// Written by the callback function running in an audio thread.
// index in bytes of where we last read.

static volatile size_t gDMAByteIndex;


// Written by main thread
static size_t gAvailableBytes;
static bool gSoundMixingStarted;

// The condition is "new data is now available"

static pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  condition_cond  = PTHREAD_COND_INITIALIZER;

/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/


const size_t SAMPLE_RATE = 11025;


const size_t BYTES_PER_SAMPLE = 2;
const size_t CHANNEL_COUNT = 2;
const size_t BITS_PER_SAMPLE = 8 * BYTES_PER_SAMPLE;

const size_t TOTAL_BUFFER_SIZE = 16 * 1024;

static size_t min(size_t a, size_t b) {
  return a < b ? a : b;
}

static size_t mod(size_t value, size_t mod) {
  return value % mod;
}

static size_t next(size_t value, size_t mod) {
  value = value + 1;
  if ( value >= mod ) {
    value = 0;
  }
  return value;
}

static size_t prev(size_t value, size_t mod) {
  if ( value <= 0 ) {
    value = mod;
  }
  return value - 1;
}


static bool enableSound() {

    if (COM_CheckParm("-nosound"))
        return false;

  return true;
}

// Choose one:

// #define GENERATE_SINE_WAVE
#define NORMAL_SOUND

#ifdef GENERATE_SINE_WAVE

static const float p = 2 * M_PI * 440.0f / SAMPLE_RATE;
static float left = 0.0f;
static float right = 0.0f;

static float sinef(float x)
{
    const float A =   1.0f / (2.0f*M_PI);
    const float B = -16.0f;
    const float C =   8.0f;

    // scale angle for easy argument reduction
    x *= A;

    if (fabsf(x) >= 0.5f) {
        // Argument reduction
        x = x - ceilf(x + 0.5f) + 1.0f;
    }

    const float y = B*x*fabsf(x) + C*x;
    return 0.2215f * (y*fabsf(y) - y) + y;
}

static
void AndroidQuakeSoundCallback(int event, void* user, void *info) {

    if (event != AudioTrack::EVENT_MORE_DATA) return;

    const AudioTrack::Buffer *buffer = static_cast<const AudioTrack::Buffer *>(info);
    size_t bytesToCopy = buffer->size;
    size_t framesToCopy = buffer->size / (BYTES_PER_SAMPLE * CHANNEL_COUNT);
    short* pData = buffer->i16;

    for(size_t frame = 0; frame < framesToCopy; frame++) {
        short leftSample = (short) (32767.0f * sinef(left));
        left += p;
        if (left > 2*M_PI) {
            left -= 2*M_PI;
        }
        pData[frame * CHANNEL_COUNT] = leftSample;

        short rightSample = (short) (32767.0f * sinef(right));
        right += 2 * p;
        if (right > 2*M_PI) {
            right -= 2*M_PI;
        }
        pData[1 + frame * CHANNEL_COUNT] = rightSample;
    }

    gDMAByteIndex = mod(gDMAByteIndex + bytesToCopy, TOTAL_BUFFER_SIZE);
    asm volatile ("":::"memory");
}

#endif

#ifdef NORMAL_SOUND

static bool gWaitingForMixerToRestart;

// Assumes the mutex is acquired.
// Waits until audio is available or a time period has elapsed.
static bool shouldMixSilence() {
  if (!gSoundMixingStarted) {
    return true;
  }
    while (gAvailableBytes == 0) {
      if (gWaitingForMixerToRestart) {
        return true;
      }
        timeval tp;
        if (gettimeofday(&tp, NULL)) {
          return true;
        }
     const long WAIT_NS = 40 * 1000 * 1000;
     const long NS_PER_SECOND = 1000 * 1000 * 1000;
     timespec ts;
     ts.tv_sec  = tp.tv_sec;
     ts.tv_nsec = tp.tv_usec * 1000 + WAIT_NS;
     if (ts.tv_nsec >= NS_PER_SECOND) {
       ts.tv_nsec -= NS_PER_SECOND;
       ts.tv_sec += 1;
     }
     if (ETIMEDOUT == pthread_cond_timedwait( &condition_cond,  &condition_mutex, &ts)) {
       gWaitingForMixerToRestart = true;
       return true;
     }
    }
    gWaitingForMixerToRestart = false;
    return false;
}

static
void AndroidQuakeSoundCallback(int event, void* user, void *info) {

    if (event != AudioTrack::EVENT_MORE_DATA) return;

    const AudioTrack::Buffer *buffer = static_cast<const AudioTrack::Buffer *>(info);
    size_t dmaByteIndex = gDMAByteIndex;
    size_t size = buffer->size;
    unsigned char* pDestBuffer = (unsigned char*) buffer->raw;

    if (size == 0) return;

    if ( ! shm ) {
        memset(pDestBuffer, 0, size);
        return;
    }

    const unsigned char* pSrcBuffer = shm->buffer;

    while(size > 0) {
        pthread_mutex_lock( &condition_mutex );

        if (shouldMixSilence()) {
          memset(pDestBuffer, 0, size);
          pthread_mutex_unlock( &condition_mutex );
          return;
        }

        size_t chunkSize = min(gAvailableBytes, min(TOTAL_BUFFER_SIZE-dmaByteIndex, size));
        gAvailableBytes -= chunkSize;

        pthread_mutex_unlock( &condition_mutex );

    memcpy(pDestBuffer, pSrcBuffer + dmaByteIndex, chunkSize);
    size -= chunkSize;
    pDestBuffer += chunkSize;
    dmaByteIndex += chunkSize;
    if (dmaByteIndex >= TOTAL_BUFFER_SIZE) {
      dmaByteIndex = 0;
    }
  }

  gDMAByteIndex = dmaByteIndex;
  asm volatile ("":::"memory");
}

#endif

qboolean SNDDMA_Init(void)
{
  if ( ! enableSound() ) {
    return false;
  }

  gDMAByteIndex = 0;

  // Initialize the AudioTrack.

  status_t result = gAudioTrack.set(
    AudioSystem::DEFAULT, // stream type
    SAMPLE_RATE,   // sample rate
    BITS_PER_SAMPLE == 16 ? AudioSystem::PCM_16_BIT : AudioSystem::PCM_8_BIT,      // format (8 or 16)
    (CHANNEL_COUNT > 1) ? AudioSystem::CHANNEL_OUT_STEREO : AudioSystem::CHANNEL_OUT_MONO,       // channel mask
    0,       // default buffer size
    0, // flags
    AndroidQuakeSoundCallback, // callback
    0,  // user
    0); // default notification size

  LOGI("AudioTrack status  = %d (%s)\n", result, result == NO_ERROR ? "success" : "error");

  if ( result == NO_ERROR ) {
    LOGI("AudioTrack latency = %u ms\n", gAudioTrack.latency());
    LOGI("AudioTrack format = %u bits\n", gAudioTrack.format() == AudioSystem::PCM_16_BIT ? 16 : 8);
    LOGI("AudioTrack sample rate = %u Hz\n", gAudioTrack.getSampleRate());
    LOGI("AudioTrack frame count = %d\n", int(gAudioTrack.frameCount()));
    LOGI("AudioTrack channel count = %d\n", gAudioTrack.channelCount());

    // Initialize Quake's idea of a DMA buffer.

    shm = &sn;
    memset((void*)&sn, 0, sizeof(sn));

    shm->splitbuffer = false;	// Not used.
    shm->samplebits = gAudioTrack.format() == AudioSystem::PCM_16_BIT ? 16 : 8;
    shm->speed = gAudioTrack.getSampleRate();
    shm->channels = gAudioTrack.channelCount();
    shm->samples = TOTAL_BUFFER_SIZE / BYTES_PER_SAMPLE;
    shm->samplepos = 0; // Not used.
    shm->buffer = (unsigned char*) Hunk_AllocName(TOTAL_BUFFER_SIZE, (char*) "shmbuf");
    shm->submission_chunk = 1; // Not used.

    shm->soundalive = true;

    if ( (shm->samples & 0x1ff) != 0 ) {
      LOGE("SNDDDMA_Init: samples must be power of two.");
      return false;
    }

    if ( shm->buffer == 0 ) {
      LOGE("SNDDDMA_Init: Could not allocate sound buffer.");
      return false;
    }

    gAudioTrack.setVolume(1.0f, 1.0f);
    gAudioTrack.start();
  }

  return result == NO_ERROR;
}

/*
==============
SNDDMA_GetDMAPos

return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos(void)
{
  int dmaPos = gDMAByteIndex / BYTES_PER_SAMPLE;
  asm volatile ("":::"memory");
  return dmaPos;
}

/*
===============
SNDDMA_ReportWrite

Report valid data being written into the DMA buffer by the sound mixing code.
This is an Android specific API.
================
*/
void SNDDMA_ReportWrite(size_t lengthBytes) {
    pthread_mutex_lock( &condition_mutex );
    gSoundMixingStarted = true;
    if (gAvailableBytes == 0) {
        pthread_cond_signal( &condition_cond );
    }
    gAvailableBytes += lengthBytes;
    pthread_mutex_unlock( &condition_mutex );
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit(void)
{
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown(void)
{
  gAudioTrack.stop();
}
