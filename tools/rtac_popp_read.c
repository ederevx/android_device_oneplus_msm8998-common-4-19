/*
 * rtac_popp_read - dump the ADM popp registry from /dev/msm_rtac.
 *
 * DEV DIAGNOSTIC, prepared but NOT built by any Android.bp in this tree. It
 * exists so the ACDB loader's "finding active popp id failed" can be checked
 * against the registry the lookup reads: the kernel's RTAC ADM view.
 *
 * Source of truth for every value below, so it can be re-derived:
 *   device node        kernel/oneplus/msm8998/techpack/audio/dsp/rtac.c:1977
 *   ioctl              techpack/audio/include/uapi/linux/msm_audio_calibration.h:23
 *                      AUDIO_GET_RTAC_ADM_INFO = _IOR(CAL_IOCTL_MAGIC, 207, void*)
 *                      CAL_IOCTL_MAGIC = 'a'
 *   structs            techpack/audio/include/dsp/rtac.h:46-72
 *                      (rtac_popp_data, rtac_adm_data, rtac_adm)
 *   sizes              RTAC_MAX_ACTIVE_POPP = 8, devices = 6
 *
 * Usage on device (root): rtac_popp_read [device]
 * Prints one block per active ADM device with its topology, afe topology, afe
 * port, copp, app_type and acdb id, then each popp with its own topology and
 * app type. Read it while a stream is live and again at the moment a cal frame
 * is pushed; that is the falsification test for the copp-registration theory.
 *
 * Build (either is fine, neither is done here):
 *   aarch64-linux-android<api>-clang -static -O2 -o rtac_popp_read rtac_popp_read.c
 * or a cc_binary in a later, deliberate change.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CAL_IOCTL_MAGIC 'a'
#define AUDIO_GET_RTAC_ADM_INFO _IOR(CAL_IOCTL_MAGIC, 207, void *)

#define RTAC_MAX_ACTIVE_POPP 8
#define RTAC_MAX_ACTIVE_DEVICES 6

struct rtac_popp_data {
    uint32_t popp;
    uint32_t popp_topology;
    uint32_t app_type;
};

struct rtac_adm_data {
    uint32_t topology_id;
    uint32_t afe_topology;
    uint32_t afe_port;
    uint32_t copp;
    uint32_t num_of_popp;
    uint32_t app_type;
    uint32_t acdb_dev_id;
    struct rtac_popp_data popp[RTAC_MAX_ACTIVE_POPP];
};

struct rtac_adm {
    uint32_t num_of_dev;
    struct rtac_adm_data device[RTAC_MAX_ACTIVE_DEVICES];
};

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/dev/msm_rtac";
    struct rtac_adm adm;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    memset(&adm, 0, sizeof(adm));
    if (ioctl(fd, AUDIO_GET_RTAC_ADM_INFO, &adm) < 0) {
        perror("AUDIO_GET_RTAC_ADM_INFO");
        close(fd);
        return 1;
    }
    close(fd);

    printf("adm devices: %u\n", adm.num_of_dev);
    for (uint32_t i = 0; i < adm.num_of_dev && i < RTAC_MAX_ACTIVE_DEVICES; i++) {
        const struct rtac_adm_data *d = &adm.device[i];
        printf("device[%u] topology=0x%x afe_topology=0x%x afe_port=0x%x copp=%u "
               "app_type=%u acdb_dev_id=%u popp_count=%u\n",
               i, d->topology_id, d->afe_topology, d->afe_port, d->copp,
               d->app_type, d->acdb_dev_id, d->num_of_popp);
        for (uint32_t j = 0; j < d->num_of_popp && j < RTAC_MAX_ACTIVE_POPP; j++) {
            printf("  popp[%u] id=%u topology=0x%x app_type=%u\n",
                   j, d->popp[j].popp, d->popp[j].popp_topology, d->popp[j].app_type);
        }
    }
    return 0;
}
