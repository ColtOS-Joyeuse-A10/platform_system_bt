/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "bt_gd_shim"

#include "gd/att/att_module.h"
#include "gd/common/init_flags.h"
#include "gd/hal/hci_hal.h"
#include "gd/hci/acl_manager.h"
#include "gd/hci/controller.h"
#include "gd/hci/hci_layer.h"
#include "gd/hci/le_advertising_manager.h"
#include "gd/hci/le_scanning_manager.h"
#include "gd/l2cap/classic/l2cap_classic_module.h"
#include "gd/l2cap/le/l2cap_le_module.h"
#include "gd/neighbor/connectability.h"
#include "gd/neighbor/discoverability.h"
#include "gd/neighbor/inquiry.h"
#include "gd/neighbor/name.h"
#include "gd/neighbor/name_db.h"
#include "gd/neighbor/page.h"
#include "gd/neighbor/scan.h"
#include "gd/os/log.h"
#include "gd/security/security_module.h"
#include "gd/shim/dumpsys.h"
#include "gd/shim/l2cap.h"
#include "gd/storage/storage_module.h"

#include "main/shim/hci_layer.h"
#include "main/shim/stack.h"

namespace bluetooth {
namespace shim {

Stack* Stack::GetInstance() {
  static Stack instance;
  return &instance;
}

void Stack::StartIdleMode() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT_LOG(!is_running_, "%s Gd stack already running", __func__);
  LOG_INFO("%s Starting Gd stack", __func__);
  ModuleList modules;
  modules.add<storage::StorageModule>();
  Start(&modules);
  // Make sure the leaf modules are started
  ASSERT(stack_manager_.GetInstance<storage::StorageModule>() != nullptr);
  is_running_ = true;
}

void Stack::StartEverything() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT_LOG(!is_running_, "%s Gd stack already running", __func__);
  LOG_INFO("%s Starting Gd stack", __func__);
  ModuleList modules;
  if (common::InitFlags::GdHciEnabled()) {
    modules.add<hal::HciHal>();
    modules.add<hci::HciLayer>();
    modules.add<storage::StorageModule>();
    modules.add<shim::Dumpsys>();
  }
  if (common::InitFlags::GdControllerEnabled()) {
    modules.add<hci::Controller>();
  }
  if (common::InitFlags::GdAclEnabled()) {
    modules.add<hci::AclManager>();
  }
  if (common::InitFlags::GdSecurityEnabled()) {
    modules.add<security::SecurityModule>();
  }
  if (common::InitFlags::GdCoreEnabled()) {
    modules.add<att::AttModule>();
    modules.add<hci::LeAdvertisingManager>();
    modules.add<hci::LeScanningManager>();
    modules.add<l2cap::classic::L2capClassicModule>();
    modules.add<l2cap::le::L2capLeModule>();
    modules.add<neighbor::ConnectabilityModule>();
    modules.add<neighbor::DiscoverabilityModule>();
    modules.add<neighbor::InquiryModule>();
    modules.add<neighbor::NameModule>();
    modules.add<neighbor::NameDbModule>();
    modules.add<neighbor::PageModule>();
    modules.add<neighbor::ScanModule>();
    modules.add<storage::StorageModule>();
    modules.add<shim::L2cap>();
  }
  Start(&modules);
  // Make sure the leaf modules are started
  ASSERT(stack_manager_.GetInstance<storage::StorageModule>() != nullptr);
  ASSERT(stack_manager_.GetInstance<shim::Dumpsys>() != nullptr);
  if (common::InitFlags::GdCoreEnabled()) {
    ASSERT(stack_manager_.GetInstance<shim::L2cap>() != nullptr);
    btm_ = new Btm(stack_handler_,
                   stack_manager_.GetInstance<neighbor::InquiryModule>());
  }
  if (common::InitFlags::GdAclEnabled()) {
    if (!common::InitFlags::GdCoreEnabled()) {
      acl_ = new legacy::Acl(stack_handler_);
    }
  }
  is_running_ = true;
  if (!common::InitFlags::GdCoreEnabled()) {
    bluetooth::shim::hci_on_reset_complete();
  }
}

void Stack::Start(ModuleList* modules) {
  ASSERT_LOG(!is_running_, "%s Gd stack already running", __func__);
  LOG_DEBUG("%s Starting Gd stack", __func__);

  stack_thread_ =
      new os::Thread("gd_stack_thread", os::Thread::Priority::NORMAL);
  stack_manager_.StartUp(modules, stack_thread_);

  stack_handler_ = new os::Handler(stack_thread_);

  LOG_INFO("%s Successfully toggled Gd stack", __func__);
}

void Stack::Stop() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!common::InitFlags::GdCoreEnabled()) {
    bluetooth::shim::hci_on_shutting_down();
  }
  ASSERT_LOG(is_running_, "%s Gd stack not running", __func__);
  is_running_ = false;

  delete acl_;
  acl_ = nullptr;

  delete btm_;
  btm_ = nullptr;

  stack_handler_->Clear();

  stack_manager_.ShutDown();

  delete stack_handler_;
  stack_handler_ = nullptr;

  stack_thread_->Stop();
  delete stack_thread_;
  stack_thread_ = nullptr;

  LOG_INFO("%s Successfully shut down Gd stack", __func__);
}

bool Stack::IsRunning() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return is_running_;
}

StackManager* Stack::GetStackManager() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT(is_running_);
  return &stack_manager_;
}

legacy::Acl* Stack::GetAcl() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT(is_running_);
  return acl_;
}

Btm* Stack::GetBtm() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT(is_running_);
  return btm_;
}

os::Handler* Stack::GetHandler() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ASSERT(is_running_);
  return stack_handler_;
}

}  // namespace shim
}  // namespace bluetooth