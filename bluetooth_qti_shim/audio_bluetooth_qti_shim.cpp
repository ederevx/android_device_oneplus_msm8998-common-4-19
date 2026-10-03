/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * ABI shim for the Android 10 prebuilt Bluetooth QTI audio HAL.
 *
 * The prebuilt audio.bluetooth_qti.default.so returns a legacy
 * struct audio_stream_out that ends at 0x78 (arm32). Android 11 appended
 * set_event_callback at 0x78, and the A16 HIDL StreamOut wrapper reads that
 * field and calls it when it is not NULL. The vendor keeps its
 * BluetoothAudioPortOut C++ member at stream+0x78 and every vendor stream op
 * derives its this pointer from stream+0x78, so the slot cannot be zeroed on
 * the vendor object itself.
 *
 * This module is installed in place of the prebuilt and forwards the legacy
 * device. It hands the framework a proxy whose 30 legacy function-pointer
 * slots are thin trampolines that call the original object. Every vendor op
 * still receives the original A10 object as its stream argument, so the
 * vendor's BluetoothAudioPortOut member at 0x78 is never touched. The proxy
 * owns the A16 set_event_callback slot and stores the framework callback so
 * AudioFlinger's registration succeeds; update_source_metadata_v7 is left
 * NULL because the vendor predates it.
 */

#define LOG_TAG "bluetooth_qti_shim"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>

#include <string>

extern "C" {
#include <hardware/audio.h>
#include <hardware/hardware.h>
}

extern "C" struct audio_module HAL_MODULE_INFO_SYM;

namespace {

constexpr uint32_t kProxyMagic = 0x51544953u;  // "Q T I S"

struct QtiStreamProxy {
    struct audio_stream_out out;  // must stay first: the returned pointer
    struct audio_stream_out* legacy;
    uint32_t magic;
    stream_event_callback_t event_callback;
    void* event_cookie;
};

struct hw_module_t* g_legacy_module = nullptr;

decltype(((struct audio_hw_device*)nullptr)->open_output_stream) g_legacy_open = nullptr;
decltype(((struct audio_hw_device*)nullptr)->close_output_stream) g_legacy_close = nullptr;

inline struct audio_stream_out* legacy_of(const void* stream) {
    return reinterpret_cast<const QtiStreamProxy*>(stream)->legacy;
}

// --- struct audio_stream common methods -------------------------------------

uint32_t tr_get_sample_rate(const struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.get_sample_rate(&o->common);
}

int tr_set_sample_rate(struct audio_stream* s, uint32_t rate) {
    auto* o = legacy_of(s);
    return o->common.set_sample_rate(&o->common, rate);
}

size_t tr_get_buffer_size(const struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.get_buffer_size(&o->common);
}

audio_channel_mask_t tr_get_channels(const struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.get_channels(&o->common);
}

audio_format_t tr_get_format(const struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.get_format(&o->common);
}

int tr_set_format(struct audio_stream* s, audio_format_t format) {
    auto* o = legacy_of(s);
    return o->common.set_format(&o->common, format);
}

int tr_standby(struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.standby(&o->common);
}

int tr_dump(const struct audio_stream* s, int fd) {
    auto* o = legacy_of(s);
    return o->common.dump(&o->common, fd);
}

audio_devices_t tr_get_device(const struct audio_stream* s) {
    auto* o = legacy_of(s);
    return o->common.get_device(&o->common);
}

int tr_set_device(struct audio_stream* s, audio_devices_t device) {
    auto* o = legacy_of(s);
    return o->common.set_device(&o->common, device);
}

int tr_set_parameters(struct audio_stream* s, const char* kv_pairs) {
    auto* o = legacy_of(s);
    return o->common.set_parameters(&o->common, kv_pairs);
}

char* tr_get_parameters(const struct audio_stream* s, const char* keys) {
    auto* o = legacy_of(s);
    return o->common.get_parameters(&o->common, keys);
}

int tr_add_audio_effect(const struct audio_stream* s, effect_handle_t effect) {
    auto* o = legacy_of(s);
    return o->common.add_audio_effect(&o->common, effect);
}

int tr_remove_audio_effect(const struct audio_stream* s, effect_handle_t effect) {
    auto* o = legacy_of(s);
    return o->common.remove_audio_effect(&o->common, effect);
}

// --- struct audio_stream_out methods ----------------------------------------

uint32_t tr_get_latency(const struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->get_latency(o);
}

int tr_set_volume(struct audio_stream_out* s, float left, float right) {
    auto* o = legacy_of(s);
    return o->set_volume(o, left, right);
}

ssize_t tr_write(struct audio_stream_out* s, const void* buffer, size_t bytes) {
    auto* o = legacy_of(s);
    return o->write(o, buffer, bytes);
}

int tr_get_render_position(const struct audio_stream_out* s, uint32_t* dsp_frames) {
    auto* o = legacy_of(s);
    return o->get_render_position(o, dsp_frames);
}

int tr_get_next_write_timestamp(const struct audio_stream_out* s, int64_t* timestamp) {
    auto* o = legacy_of(s);
    return o->get_next_write_timestamp(o, timestamp);
}

int tr_set_callback(struct audio_stream_out* s, stream_callback_t callback, void* cookie) {
    auto* o = legacy_of(s);
    return o->set_callback(o, callback, cookie);
}

int tr_pause(struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->pause(o);
}

int tr_resume(struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->resume(o);
}

int tr_drain(struct audio_stream_out* s, audio_drain_type_t type) {
    auto* o = legacy_of(s);
    return o->drain(o, type);
}

int tr_flush(struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->flush(o);
}

int tr_get_presentation_position(const struct audio_stream_out* s, uint64_t* frames,
                                 struct timespec* timestamp) {
    auto* o = legacy_of(s);
    return o->get_presentation_position(o, frames, timestamp);
}

int tr_start(const struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->start(o);
}

int tr_stop(const struct audio_stream_out* s) {
    auto* o = legacy_of(s);
    return o->stop(o);
}

int tr_create_mmap_buffer(const struct audio_stream_out* s, int32_t min_size_frames,
                          struct audio_mmap_buffer_info* info) {
    auto* o = legacy_of(s);
    return o->create_mmap_buffer(o, min_size_frames, info);
}

int tr_get_mmap_position(const struct audio_stream_out* s, struct audio_mmap_position* position) {
    auto* o = legacy_of(s);
    return o->get_mmap_position(o, position);
}

void tr_update_source_metadata(struct audio_stream_out* s,
                               const struct source_metadata* source_metadata) {
    auto* o = legacy_of(s);
    o->update_source_metadata(o, source_metadata);
}

int tr_set_event_callback(struct audio_stream_out* s, stream_event_callback_t callback,
                          void* cookie) {
    auto* p = reinterpret_cast<QtiStreamProxy*>(s);
    p->event_callback = callback;
    p->event_cookie = cookie;
    return 0;
}

void install_trampolines(struct audio_stream_out* p) {
    auto* o = legacy_of(p);
    p->common.get_sample_rate = o->common.get_sample_rate ? tr_get_sample_rate : nullptr;
    p->common.set_sample_rate = o->common.set_sample_rate ? tr_set_sample_rate : nullptr;
    p->common.get_buffer_size = o->common.get_buffer_size ? tr_get_buffer_size : nullptr;
    p->common.get_channels = o->common.get_channels ? tr_get_channels : nullptr;
    p->common.get_format = o->common.get_format ? tr_get_format : nullptr;
    p->common.set_format = o->common.set_format ? tr_set_format : nullptr;
    p->common.standby = o->common.standby ? tr_standby : nullptr;
    p->common.dump = o->common.dump ? tr_dump : nullptr;
    p->common.get_device = o->common.get_device ? tr_get_device : nullptr;
    p->common.set_device = o->common.set_device ? tr_set_device : nullptr;
    p->common.set_parameters = o->common.set_parameters ? tr_set_parameters : nullptr;
    p->common.get_parameters = o->common.get_parameters ? tr_get_parameters : nullptr;
    p->common.add_audio_effect = o->common.add_audio_effect ? tr_add_audio_effect : nullptr;
    p->common.remove_audio_effect =
            o->common.remove_audio_effect ? tr_remove_audio_effect : nullptr;

    p->get_latency = o->get_latency ? tr_get_latency : nullptr;
    p->set_volume = o->set_volume ? tr_set_volume : nullptr;
    p->write = o->write ? tr_write : nullptr;
    p->get_render_position = o->get_render_position ? tr_get_render_position : nullptr;
    p->get_next_write_timestamp =
            o->get_next_write_timestamp ? tr_get_next_write_timestamp : nullptr;
    p->set_callback = o->set_callback ? tr_set_callback : nullptr;
    p->pause = o->pause ? tr_pause : nullptr;
    p->resume = o->resume ? tr_resume : nullptr;
    p->drain = o->drain ? tr_drain : nullptr;
    p->flush = o->flush ? tr_flush : nullptr;
    p->get_presentation_position =
            o->get_presentation_position ? tr_get_presentation_position : nullptr;
    p->start = o->start ? tr_start : nullptr;
    p->stop = o->stop ? tr_stop : nullptr;
    p->create_mmap_buffer = o->create_mmap_buffer ? tr_create_mmap_buffer : nullptr;
    p->get_mmap_position = o->get_mmap_position ? tr_get_mmap_position : nullptr;
    p->update_source_metadata =
            o->update_source_metadata ? tr_update_source_metadata : nullptr;

    // The Android 11 set_event_callback slot stays in the proxy: the vendor
    // keeps its BluetoothAudioPortOut member at the same offset, so the proxy
    // stores the framework callback itself. Accepting it lets AudioFlinger's
    // PlaybackThread::onFirstRef() registration succeed; the pre-v7 vendor has
    // no format-change source, so the callback is held but not fired. The
    // second Android 11 field, update_source_metadata_v7, has no vendor
    // counterpart and stays NULL.
    p->set_event_callback = tr_set_event_callback;
    p->update_source_metadata_v7 = nullptr;
}

bool load_legacy_module() {
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(&HAL_MODULE_INFO_SYM), &info) == 0 ||
        info.dli_fname == nullptr) {
        ALOGE("failed to resolve own module path");
        return false;
    }
    std::string path(info.dli_fname);
    const size_t slash = path.rfind('/');
    const std::string legacy_path =
            (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) +
            "audio.bluetooth_qti.legacy.so";
    void* handle = dlopen(legacy_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        ALOGE("failed to load %s: %s", legacy_path.c_str(), dlerror());
        return false;
    }
    g_legacy_module = reinterpret_cast<struct hw_module_t*>(dlsym(handle, "HMI"));
    if (g_legacy_module == nullptr || g_legacy_module->methods == nullptr ||
        g_legacy_module->methods->open == nullptr) {
        ALOGE("legacy module has no usable HMI methods");
        g_legacy_module = nullptr;
        return false;
    }
    return true;
}

int shim_open_output_stream(struct audio_hw_device* dev, audio_io_handle_t handle,
                            audio_devices_t devices, audio_output_flags_t flags,
                            struct audio_config* config, struct audio_stream_out** stream_out,
                            const char* address) {
    if (stream_out == nullptr) {
        return -EINVAL;
    }
    *stream_out = nullptr;
    if (g_legacy_open == nullptr) {
        return -ENOSYS;
    }

    struct audio_stream_out* orig = nullptr;
    int rc = g_legacy_open(dev, handle, devices, flags, config, &orig, address);
    if (rc != 0 || orig == nullptr) {
        *stream_out = orig;
        return rc;
    }

    auto* proxy = static_cast<QtiStreamProxy*>(calloc(1, sizeof(QtiStreamProxy)));
    if (proxy == nullptr) {
        g_legacy_close(dev, orig);
        return -ENOMEM;
    }

    proxy->legacy = orig;
    proxy->magic = kProxyMagic;
    install_trampolines(&proxy->out);
    *stream_out = &proxy->out;
    return 0;
}

void shim_close_output_stream(struct audio_hw_device* dev, struct audio_stream_out* stream_out) {
    if (stream_out == nullptr) {
        if (g_legacy_close != nullptr) {
            g_legacy_close(dev, nullptr);
        }
        return;
    }

    auto* proxy = reinterpret_cast<QtiStreamProxy*>(stream_out);
    if (proxy->magic != kProxyMagic) {
        if (g_legacy_close != nullptr) {
            g_legacy_close(dev, stream_out);
        }
        return;
    }

    struct audio_stream_out* orig = proxy->legacy;
    if (g_legacy_close != nullptr) {
        g_legacy_close(dev, orig);
    }
    free(proxy);
}

int shim_open(const struct hw_module_t* module, const char* id, struct hw_device_t** device) {
    (void)module;
    if (device == nullptr) {
        return -EINVAL;
    }
    *device = nullptr;
    if (g_legacy_module == nullptr && !load_legacy_module()) {
        return -ENOENT;
    }

    int rc = g_legacy_module->methods->open(g_legacy_module, id, device);
    if (rc != 0 || *device == nullptr) {
        return rc;
    }

    auto* dev = reinterpret_cast<struct audio_hw_device*>(*device);
    g_legacy_open = dev->open_output_stream;
    g_legacy_close = dev->close_output_stream;
    dev->open_output_stream = shim_open_output_stream;
    dev->close_output_stream = shim_close_output_stream;
    return 0;
}

struct hw_module_methods_t g_hal_module_methods = {
        shim_open,
};

}  // namespace

extern "C" struct audio_module HAL_MODULE_INFO_SYM = {
        {
                HARDWARE_MODULE_TAG,
                AUDIO_MODULE_API_VERSION_0_1,
                HARDWARE_HAL_API_VERSION,
                AUDIO_HARDWARE_MODULE_ID,
                "Bluetooth QTI audio HW HAL (A16 ABI shim)",
                "The LineageOS Project",
                &g_hal_module_methods,
                nullptr,
                {0},
        },
};
