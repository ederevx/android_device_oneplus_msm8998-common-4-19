#
# Copyright (C) 2009 The Android Open Source Project
# Copyright (c) 2011, The Linux Foundation. All rights reserved.
# Copyright (C) 2017 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

import common
import os
import re

def FullOTA_Assertions(info):
  AddVendorAssertion(info)

  firmware_files = [
    os.path.basename(item)
    for item in info.input_zip.namelist()
    if item.startswith("RADIO/")
  ]

  if not firmware_files:
    # Firmware files not present, assert
    AddModemAssertion(info)
    return

  # Firmware files present, copy to OTA zip
  CopyBlobs(info.input_zip, info.output_zip)
  # And flash if necessary
  AddFirmwareUpdate(info, firmware_files)
  return

def IncrementalOTA_Assertions(info):
  AddVendorAssertion(info)
  AddModemAssertion(info)
  return

def CopyBlobs(input_zip, output_zip):
  for info in input_zip.infolist():
    f = info.filename
    # Copy files in 'RADIO' to output zip 'firmware-update'
    if f.startswith("RADIO/") and (f.__len__() > len("RADIO/")):
      fn = f[6:]
      common.ZipWriteStr(output_zip, "firmware-update/" + fn, input_zip.read(f))

def AddVendorAssertion(info):
  cmd = 'assert(oneplus.file_exists("/dev/block/bootdevice/by-name/vendor") == "1" || \
    abort("Error: Vendor partition doesn\'t exist!"););'
  info.script.AppendExtra(cmd)
  return

def AddModemAssertion(info):
  android_info = info.input_zip.read("OTA/android-info.txt").decode('utf-8')
  m = re.search(r'require\s+version-modem\s*=\s*(.+)', android_info)
  f = re.search(r'require\s+version-firmware\s*=\s*(.+)', android_info)
  if m and f:
    version_modem = m.group(1).rstrip()
    version_firmware = f.group(1).rstrip()
    if ((len(version_modem) and '*' not in version_modem) and \
        (len(version_firmware) and '*' not in version_firmware)):
      cmd = 'assert(oneplus.verify_modem("' + version_modem + '") == "1" || \
        abort("Error: This package requires firmware version ' + version_firmware + \
        ' or newer. Please upgrade firmware and retry!"););'
      info.script.AppendExtra(cmd)
  return

def AddFirmwareUpdate(info, filemap):
  android_info = info.input_zip.read("OTA/android-info.txt").decode('utf-8')
  m = re.search(r'require\s+version-modem\s*=\s*(.+)', android_info)
  f = re.search(r'require\s+version-firmware\s*=\s*(.+)', android_info)
  if m and f:
    version_modem = m.group(1).rstrip()
    version_firmware = f.group(1).rstrip()
    if ((len(version_modem) and '*' not in version_modem) and \
        (len(version_firmware) and '*' not in version_firmware)):
      info.script.AppendExtra('ifelse(oneplus.verify_modem("' + version_modem + '") != "1",')
      info.script.AppendExtra('(')
      info.script.AppendExtra('  ui_print("Upgrading firmware to ' + version_firmware + '");')
      for file in filemap:
        partname = os.path.splitext(file)[0]
        info.script.AppendExtra('package_extract_file("firmware-update/' + file + '", "/dev/block/bootdevice/by-name/' + partname + '");')
      info.script.AppendExtra('),')
      info.script.AppendExtra('(')
      info.script.AppendExtra('  ui_print("Firmware is up-to-date");')
      info.script.AppendExtra(')')
      info.script.AppendExtra(');')

def AddImage(info, dir, basename, dest):
  path = dir + "/" + basename
  if path not in info.input_zip.namelist():
    return

  data = info.input_zip.read(path)
  common.ZipWriteStr(info.output_zip, basename, data)
  info.script.Print("Patching {} image unconditionally...".format(dest.split('/')[-1]))
  info.script.AppendExtra('package_extract_file("%s", "%s");' % (basename, dest))

def FullOTA_InstallBegin(info):
  AddImage(info, "RADIO", "super_dummy.img", "/tmp/super_dummy.img");
  info.script.AppendExtra('package_extract_file("install/bin/flash_super_dummy.sh", "/tmp/flash_super_dummy.sh");')
  info.script.AppendExtra('set_metadata("/tmp/flash_super_dummy.sh", "uid", 0, "gid", 0, "mode", 0755);')
  info.script.AppendExtra('run_program("/tmp/flash_super_dummy.sh");')
  return
