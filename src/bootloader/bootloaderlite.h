#ifndef BOOTLOADERLITE_H_
#define BOOTLOADERLITE_H_

#include "bootloader/bootloader.h"
#include "libaktualizr/config.h"
#include "rollbacks/rollback.h"

class INvStorage;

class BootloaderLite : public Bootloader {
 public:
  BootloaderLite(BootloaderConfig config, INvStorage& storage);
  virtual ~BootloaderLite() {}
  void setBootOK() const;
  void updateNotify() const;

 private:
  std::unique_ptr<Rollback> rollback_;
};

#endif  // BOOTLOADERLITE_H_
