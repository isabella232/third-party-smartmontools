/*
 * libsmartctl.cpp
 *
 * Home page of code is: http://www.smartmontools.org
 *
 * Copyright (C) 2002-11 Bruce Allen
 * Copyright (C) 2008-17 Christian Franke
 * Copyright (C) 2000 Michael Cornwell <cornwell@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example COPYING); If not, see <http://www.gnu.org/licenses/>.
 *
 * This code was originally developed as a Senior Thesis by Michael Cornwell
 * at the Concurrent Systems Laboratory (now part of the Storage Systems
 * Research Center), Jack Baskin School of Engineering, University of
 * California, Santa Cruz. http://ssrc.soe.ucsc.edu/
 *
 */

#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(__FreeBSD__)
#include <sys/param.h>
#endif

#include "atacmds.h"
#include "ataprint.h"
#include "dev_interface.h"
#include "int64.h"
#include "knowndrives.h"
#include "libsmartctl.h"
#include "nvmeprint.h"
#include "scsicmds.h"
#include "scsiprint.h"
#include "smartctl.h"
#include "utility.h"

const char *libsmartctl_cpp_cvsid = "$Id$" CONFIG_H_CVSID LIBSMARTCTL_H_CVSID;

namespace libsmartctl {

const std::string errStr(ctlerr_t err) {
  static const std::map<ctlerr_t, std::string> errs = {
      {NOERR, "No errors"},
      {POWERMODEBELOWOPTION, "The power mode is below the configured option"},
      {FAILEDDEVICEIDREAD, "Device read failure"},
      {FAILEDSMARTCMD, "Test SMART command failed"},
      {GETDEVICERR, "Could not retrieve device information"},
      {DEVICEOPENERR, "Could not open device"},
      {UNSUPPORTEDDEVICETYPE, "Device type is not supported"},
      {CLIENTINITIALIZTIONFAILURE, "libsmartctl client initialization failure"},
  };

  return errs.at(err);
}

class Client::Impl {

public:
  static Client::Impl &getClient() {
    static Client::Impl c;
    return c;
  }

private:
  // Default constructor
  Impl() {
    check_config();
    // Initialize interface and check registration
    smart_interface::init();
    if (!smi()) {
      up_ = false;
      return;
    }

    // database init has to occur after smart_interface::init();
    if (!init_drive_database(false)) {
      up_ = false;
      return;
    }

    up_ = true;
  }

  ctlerr_t initDevice(smart_device_auto_ptr &device, std::string const &devname,
                      std::string const &type) {
    if (type != "") {
      device = smi()->get_smart_device(devname.c_str(), type.c_str());
    } else {
      device = smi()->get_smart_device(devname.c_str(), nullptr);
    }

    if (!device) {
      return GETDEVICERR;
    }
    // Open with autodetect support, may return 'better' device
    device.replace(device->autodetect_open());

    if (!device->is_open()) {
      return DEVICEOPENERR;
    }

    return NOERR;
  }

public:
  CantIdDevResp cantIdDev(std::string const &devname, std::string const &type) {
    CantIdDevResp resp;
    if (!up_) {
      resp.err = CLIENTINITIALIZTIONFAILURE;
      return resp;
    }

    smart_device_auto_ptr device;
    ctlerr_t err = initDevice(device, devname, type);

    switch (err) {
    case NOERR:
      resp.err = NOERR;
      break;

    case DEVICEOPENERR:
      resp.err = NOERR;
      resp.content = true;
      return resp;

    default:
      resp.err = err;
      return resp;
    }

    if (device->is_ata()) {
      resp.content = cant_id(device->to_ata());
    } else {
      resp.err = UNSUPPORTEDDEVICETYPE;
    }

    return resp;
  }

  DevInfoResp getDevInfo(std::string const &devname, std::string const &type) {
    DevInfoResp resp;
    if (!up_) {
      resp.err = CLIENTINITIALIZTIONFAILURE;
      return resp;
    }

    ata_print_options ataopts;
    scsi_print_options scsiopts;
    nvme_print_options nvmeopts;

    ataopts.drive_info = scsiopts.drive_info = nvmeopts.drive_info = true;

    smart_device_auto_ptr device;
    resp.err = initDevice(device, devname, type);
    if (resp.err != NOERR) {
      return resp;
    }

    if (device->is_ata()) {
      resp.err = get_ata_information(resp.content, device->to_ata(), ataopts);
    } else {
      resp.err = UNSUPPORTEDDEVICETYPE;
    }

    return resp;
  }

  DevVendorAttrsResp getDevVendorAttrs(std::string const &devname,
                                       std::string const &type) {
    DevVendorAttrsResp resp;
    if (!up_) {
      resp.err = CLIENTINITIALIZTIONFAILURE;
      return resp;
    }

    ata_print_options ataopts;
    scsi_print_options scsiopts;
    nvme_print_options nvmeopts;

    ataopts.smart_vendor_attrib = scsiopts.smart_vendor_attrib =
        nvmeopts.smart_vendor_attrib = true;

    smart_device_auto_ptr device;
    resp.err = initDevice(device, devname, type);
    if (resp.err != NOERR) {
      return resp;
    }

    if (device->is_ata()) {
      resp.err = get_ata_vendor_attr(resp.content, device->to_ata(), ataopts);
    } else {
      resp.err = UNSUPPORTEDDEVICETYPE;
    }

    return resp;
  }

private:
  bool up_;
};

Client::Client() : impl_(Impl::getClient()) {}

CantIdDevResp Client::cantIdDev(std::string const &devname,
                                std::string const &type) {
  return impl_.cantIdDev(devname, type);
}

DevInfoResp Client::getDevInfo(std::string const &devname,
                               std::string const &type) {
  return impl_.getDevInfo(devname, type);
}

DevVendorAttrsResp Client::getDevVendorAttrs(std::string const &devname,
                                             std::string const &type) {
  return impl_.getDevVendorAttrs(devname, type);
}

} // namespace libsmartctl
