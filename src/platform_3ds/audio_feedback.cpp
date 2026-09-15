#include "office3ds/platform_3ds/audio_feedback.hpp"

#include <array>
#include <cmath>
#include <cstring>

#include <3ds.h>

namespace office3ds::platform_3ds {
namespace {

constexpr unsigned int kChannel = 0;
constexpr unsigned int kSampleRate = 32000;
constexpr unsigned int kSamplesPerBuffer = 2048;
constexpr unsigned int kBufferCount = 2;
constexpr float kTau = 6.283185307F;
constexpr float kMusicVolume = 850.0F;
constexpr float kAccentVolume = 2800.0F;
constexpr std::array<float, 8> kLoopNotes = {196.0F, 246.94F, 293.66F, 246.94F,
                                             220.0F, 293.66F, 246.94F, 220.0F};
std::array<ndspWaveBuf, kBufferCount> wave_buffers{};

} // namespace

AudioFeedback::~AudioFeedback() { shutdown(); }

bool AudioFeedback::initialize() {
  samples_ = static_cast<std::int16_t *>(
    linearAlloc(kBufferCount * kSamplesPerBuffer * 2 * sizeof(std::int16_t)));
  if (samples_ == nullptr || ndspInit() != 0) {
    if (samples_ != nullptr) {
      linearFree(samples_);
      samples_ = nullptr;
    }
    return false;
  }
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspChnSetInterp(kChannel, NDSP_INTERP_LINEAR);
  ndspChnSetRate(kChannel, kSampleRate);
  ndspChnSetFormat(kChannel, NDSP_FORMAT_STEREO_PCM16);
  float mix[12]{};
  mix[0] = 0.35F;
  mix[1] = 0.35F;
  ndspChnSetMix(kChannel, mix);
  std::memset(wave_buffers.data(), 0, sizeof(wave_buffers));
  initialized_ = true;
  queue_buffer(0);
  queue_buffer(1);
  return true;
}

void AudioFeedback::update() noexcept {
  if (initialized_ && wave_buffers[next_buffer_].status == NDSP_WBUF_DONE) {
    queue_buffer(next_buffer_);
    next_buffer_ = (next_buffer_ + 1) % kBufferCount;
  }
}

void AudioFeedback::select() noexcept { set_accent(587.33F, 1800); }

void AudioFeedback::refresh() noexcept { set_accent(440.0F, 3600); }

void AudioFeedback::error() noexcept { set_accent(146.83F, 4800); }

void AudioFeedback::shutdown() noexcept {
  if (initialized_) {
    ndspExit();
    linearFree(samples_);
    samples_ = nullptr;
    initialized_ = false;
  }
}

void AudioFeedback::queue_buffer(unsigned int index) noexcept {
  auto *buffer = samples_ + index * kSamplesPerBuffer * 2;
  for (unsigned int sample = 0; sample < kSamplesPerBuffer; ++sample) {
    const auto loop_index = static_cast<unsigned int>(phase_ * 2.0F) % kLoopNotes.size();
    const auto music = std::sin(phase_ * kTau * kLoopNotes[loop_index]) * kMusicVolume;
    const auto accent =
      accent_frames_ > 0 ? std::sin(phase_ * kTau * accent_frequency_) * kAccentVolume : 0.0F;
    const auto value = static_cast<std::int16_t>(music + accent);
    buffer[sample * 2] = value;
    buffer[sample * 2 + 1] = value;
    phase_ += 1.0F / static_cast<float>(kSampleRate);
    if (phase_ >= 4.0F) {
      phase_ -= 4.0F;
    }
    if (accent_frames_ > 0) {
      --accent_frames_;
    }
  }
  wave_buffers[index].data_pcm16 = buffer;
  wave_buffers[index].nsamples = kSamplesPerBuffer;
  ndspChnWaveBufAdd(kChannel, &wave_buffers[index]);
}

void AudioFeedback::set_accent(float frequency, unsigned int frames) noexcept {
  accent_frequency_ = frequency;
  accent_frames_ = frames;
}

} // namespace office3ds::platform_3ds
