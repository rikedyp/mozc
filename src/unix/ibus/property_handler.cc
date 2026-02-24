// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "unix/ibus/property_handler.h"

#include <cstdio>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/const.h"
#include "protocol/config.pb.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "client/client.h"  // For client interface
#include "protocol/commands.pb.h"
#include "unix/ibus/ibus_header.h"
#include "unix/ibus/ibus_wrapper.h"
#include "unix/ibus/message_translator.h"
#include "unix/ibus/path_util.h"

namespace mozc {
namespace ibus {

namespace {

// A key for MozcEngineProperty used by g_object_data.
constexpr char kMozcEnginePropertyKey[] = "ibus-mozc-aux-data";

// Icon path for MozcTool
constexpr char kMozcToolIconPath[] = "tool.png";

struct MozcEngineProperty {
  commands::CompositionMode composition_mode;
  const char *key;    // IBus property key for the mode.
  const char *label;  // text for the radio menu (ibus-anthy compatible).
  const char *label_for_panel;  // text for the language panel.
  const char *icon;
};

// The list of properties used in ibus-mozc.
// For the early-access APL release, only Direct and APL modes are exposed.
// Japanese modes remain in the binary but are not shown in the menu.
constexpr MozcEngineProperty kMozcEngineProperties[] = {
    {
        commands::DIRECT,
        "InputMode.Direct",
        "Direct input",
        "A",
        "direct.png",
    },
    {
        commands::APL,
        "InputMode.APL",
        "APL",
        "⍺",
        "alpha_half.png",  // placeholder until a dedicated APL icon is added
    },
};

struct MozcEngineToolProperty {
  const char *key;    // IBus property key for the MozcTool.
  const char *mode;   // command line passed as --mode=
  const char *label;  // text for the menu.
  const char *icon;   // icon
};

constexpr MozcEngineToolProperty kMozcEngineToolProperties[] = {
    {
        "Tool.ConfigDialog",
        "config_dialog",
        "Properties",
        "properties.png",
    },
    {
        "Tool.DictionaryTool",
        "dictionary_tool",
        "Dictionary Tool",
        "dictionary.png",
    },
    {
        "Tool.WordRegisterDialog",
        "word_register_dialog",
        "Add Word",
        "word_register.png",
    },
    {
        "Tool.AboutDialog",
        "about_dialog",
        "About Mozc",
        nullptr,
    },
};

// APL shifting key menu entries — one toggle per configurable modifier.
struct AplShiftingKeyPropEntry {
  const char *key;    // IBus property key, e.g. "AplShift.LeftCtrl"
  const char *label;  // Display text shown in the submenu
};

constexpr AplShiftingKeyPropEntry kAplShiftingKeyProps[] = {
    {"AplShift.LeftCtrl",   "Left Ctrl"},
    {"AplShift.RightCtrl",  "Right Ctrl"},
    {"AplShift.LeftAlt",    "Left Alt"},
    {"AplShift.RightAlt",   "Right Alt (AltGr)"},
    {"AplShift.CapsLock",   "Caps Lock"},
    {"AplShift.LeftSuper",  "Left Super (Win)"},
    {"AplShift.RightSuper", "Right Super (Win)"},
};

// Returns the current enabled state for the given shifting key property key.
bool GetAplShiftKeyValue(const config::Config::AplShiftingKeySet &ks,
                         absl::string_view key) {
  if (key == "AplShift.LeftCtrl")   return ks.left_ctrl();
  if (key == "AplShift.RightCtrl")  return ks.right_ctrl();
  if (key == "AplShift.LeftAlt")    return ks.left_alt();
  if (key == "AplShift.RightAlt")   return ks.right_alt();
  if (key == "AplShift.CapsLock")   return ks.caps_lock();
  if (key == "AplShift.LeftSuper")  return ks.left_super();
  if (key == "AplShift.RightSuper") return ks.right_super();
  return false;
}

// Sets the given shifting key field to |value| in |ks|.
void SetAplShiftKeyValue(config::Config::AplShiftingKeySet *ks,
                         absl::string_view key, bool value) {
  if      (key == "AplShift.LeftCtrl")   ks->set_left_ctrl(value);
  else if (key == "AplShift.RightCtrl")  ks->set_right_ctrl(value);
  else if (key == "AplShift.LeftAlt")    ks->set_left_alt(value);
  else if (key == "AplShift.RightAlt")   ks->set_right_alt(value);
  else if (key == "AplShift.CapsLock")   ks->set_caps_lock(value);
  else if (key == "AplShift.LeftSuper")  ks->set_left_super(value);
  else if (key == "AplShift.RightSuper") ks->set_right_super(value);
}

constexpr commands::CompositionMode kImeOffCompositionMode = commands::DIRECT;

// Returns true if mozc_tool is installed.
bool IsMozcToolAvailable() {
  if (absl::Status s = FileUtil::FileExists(SystemUtil::GetToolPath());
      !s.ok()) {
    LOG(ERROR) << s;
    return false;
  }
  return true;
}

bool GetDisabled(IbusEngineWrapper *engine) {
  bool disabled = false;
  uint purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
  uint hints = IBUS_INPUT_HINT_NONE;
  engine->GetContentType(&purpose, &hints);
  disabled = (purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
              purpose == IBUS_INPUT_PURPOSE_PIN);
  return disabled;
}

}  // namespace

PropertyHandler::PropertyHandler(
    std::unique_ptr<MessageTranslatorInterface> translator,
    bool is_active_on_launch, client::ClientInterface *client)
    : prop_root_(),
      prop_composition_mode_(nullptr),
      prop_mozc_tool_(nullptr),
      prop_apl_shifting_key_(nullptr),
      client_(client),
      translator_(std::move(translator)),
      original_composition_mode_(commands::APL),
      is_activated_(is_active_on_launch),
      is_disabled_(false) {
  commands::SessionCommand command;
  if (is_activated_) {
    command.set_type(commands::SessionCommand::TURN_ON_IME);
  } else {
    command.set_type(commands::SessionCommand::TURN_OFF_IME);
  }
  command.set_composition_mode(original_composition_mode_);
  commands::Output output;
  if (!client->SendCommand(command, &output)) {
    LOG(ERROR) << "SendCommand failed";
  }

  AppendCompositionPropertyToPanel();
  AppendToolPropertyToPanel();
  AppendAplShiftingKeyPropertyToPanel();

  // We have to sink |prop_root_| as well so ibus_engine_register_properties()
  // in FocusIn() does not destruct it.
  prop_root_.RefSink();
}

PropertyHandler::~PropertyHandler() {
  // The ref counter will drop to one.
  prop_composition_mode_.Unref();

  // The ref counter will drop to one.
  prop_mozc_tool_.Unref();
  prop_apl_shifting_key_.Unref();

  // Destroy all objects under the root.
  prop_root_.Unref();
}

void PropertyHandler::Register(IbusEngineWrapper *engine) {
  engine->RegisterProperties(&prop_root_);
  UpdateContentType(engine);
}

// TODO(nona): do not use kMozcEngine*** directory.
void PropertyHandler::AppendCompositionPropertyToPanel() {
  // |sub_prop_list| is a radio menu which is shown when a button in the
  // language panel (i.e. |prop_composition_mode_| below) is clicked.
  IbusPropListWrapper sub_prop_list;

  // Create items for the radio menu.
  const commands::CompositionMode initial_mode =
      is_activated_ ? original_composition_mode_ : kImeOffCompositionMode;

  std::string icon_path_for_panel;
  const char *mode_symbol = nullptr;
  for (const MozcEngineProperty &entry : kMozcEngineProperties) {
    const std::string label = translator_->MaybeTranslate(entry.label);
    IBusPropState state = PROP_STATE_UNCHECKED;
    if (entry.composition_mode == initial_mode) {
      state = PROP_STATE_CHECKED;
      icon_path_for_panel = GetIconPath(entry.icon);
      mode_symbol = entry.label_for_panel;
    }
    IbusPropertyWrapper item(entry.key, PROP_TYPE_RADIO, label, "" /* icon */,
                             state, nullptr /* sub props */);
    item.SetData(kMozcEnginePropertyKey, entry);
    // |sub_prop_list| owns |item|.
    sub_prop_list.Append(&item);
  }
  DCHECK(!icon_path_for_panel.empty());
  DCHECK(mode_symbol != nullptr);

  const std::string mode_label =
      translator_->MaybeTranslate("Input Mode") + " (" + mode_symbol + ")";

  // The label of |prop_composition_mode_| is shown in the language panel.
  // Note that the property name "InputMode" is hard-coded in the Gnome shell.
  // Do not change the name. Otherwise the Gnome shell fails to recognize that
  // this property indicates Mozc's input mode.
  // See /usr/share/gnome-shell/js/ui/status/keyboard.js for details.
  prop_composition_mode_.Initialize("InputMode", PROP_TYPE_MENU, mode_label,
                                    icon_path_for_panel, PROP_STATE_UNCHECKED,
                                    sub_prop_list.GetPropList());

  // Gnome shell uses symbol property for the mode indicator text icon iff the
  // property name is "InputMode".
  prop_composition_mode_.SetSymbol(mode_symbol);

  // Likewise, |prop_composition_mode_| owns |sub_prop_list|. We have to sink
  // |prop_composition_mode_| here so ibus_engine_update_property() call in
  // PropertyActivate() does not destruct the object.
  prop_composition_mode_.RefSink();

  prop_root_.Append(&prop_composition_mode_);
}

void PropertyHandler::AppendAplShiftingKeyPropertyToPanel() {
  IbusPropListWrapper sub_prop_list;

  // Read current config to set initial checked states.
  config::Config config;
  config::Config::AplShiftingKeySet ks;
  if (client_->GetConfig(&config) && config.has_apl_shifting_key_set()) {
    ks = config.apl_shifting_key_set();
  } else {
    // Default: both Ctrl keys active.
    ks.set_left_ctrl(true);
    ks.set_right_ctrl(true);
  }

  for (const AplShiftingKeyPropEntry &entry : kAplShiftingKeyProps) {
    const IBusPropState state = GetAplShiftKeyValue(ks, entry.key)
                                    ? PROP_STATE_CHECKED
                                    : PROP_STATE_UNCHECKED;
    IbusPropertyWrapper item(entry.key, PROP_TYPE_TOGGLE,
                             translator_->MaybeTranslate(entry.label),
                             "" /* icon */, state, nullptr);
    sub_prop_list.Append(&item);
  }

  prop_apl_shifting_key_.Initialize(
      "AplShiftKey", PROP_TYPE_MENU,
      translator_->MaybeTranslate("APL Shifting Key"),
      "" /* icon */, PROP_STATE_UNCHECKED, sub_prop_list.GetPropList());
  prop_apl_shifting_key_.RefSink();
  prop_root_.Append(&prop_apl_shifting_key_);
}

void PropertyHandler::UpdateContentTypeImpl(IbusEngineWrapper *engine,
                                            bool disabled) {
  const bool prev_is_disabled = is_disabled_;
  is_disabled_ = disabled;
  if (prev_is_disabled == is_disabled_) {
    return;
  }
  const auto visible_mode = (prev_is_disabled && !is_disabled_ && IsActivated())
                                ? original_composition_mode_
                                : kImeOffCompositionMode;
  UpdateCompositionModeIcon(engine, visible_mode);
}

void PropertyHandler::ResetContentType(IbusEngineWrapper *engine) {
  UpdateContentTypeImpl(engine, false);
}

void PropertyHandler::UpdateContentType(IbusEngineWrapper *engine) {
  UpdateContentTypeImpl(engine, GetDisabled(engine));
}

// TODO(nona): do not use kMozcEngine*** directory.
void PropertyHandler::AppendToolPropertyToPanel() {
  if (!IsMozcToolAvailable()) {
    return;
  }

  // |sub_prop_list| is a radio menu which is shown when a button in the
  // language panel (i.e. |prop_composition_mode_| below) is clicked.
  IbusPropListWrapper sub_prop_list;

  for (const MozcEngineToolProperty &entry : kMozcEngineToolProperties) {
    const std::string label = translator_->MaybeTranslate(entry.label);
    // TODO(yusukes): It would be better to use entry.icon here?
    IbusPropertyWrapper item(entry.mode, PROP_TYPE_NORMAL, label, "" /* icon */,
                             PROP_STATE_UNCHECKED, nullptr);
    item.SetData(kMozcEnginePropertyKey, entry);
    sub_prop_list.Append(&item);
  }

  const std::string tool_label = translator_->MaybeTranslate("Tools");
  const std::string icon_path = GetIconPath(kMozcToolIconPath);
  prop_mozc_tool_.Initialize("MozcTool", PROP_TYPE_MENU, tool_label, icon_path,
                             PROP_STATE_UNCHECKED, sub_prop_list.GetPropList());

  // Likewise, |prop_mozc_tool_| owns |sub_prop_list|. We have to sink
  // |prop_mozc_tool_| here so ibus_engine_update_property() call in
  // PropertyActivate() does not destruct the object.
  prop_mozc_tool_.RefSink();

  prop_root_.Append(&prop_mozc_tool_);
}

void PropertyHandler::Update(IbusEngineWrapper *engine,
                             const commands::Output &output) {
  if (IsDisabled()) {
    return;
  }

  if (output.has_status()) {
    FILE* f = fopen("/tmp/mozc_lifecycle.txt", "a");
    if (f) { fprintf(f, "PropHandler::Update: status.activated=%d status.mode=%d cur_activated=%d cur_mode=%d\n",
             output.status().activated(), output.status().mode(), is_activated_, original_composition_mode_); fclose(f); }
  }

  if (output.has_status() &&
      (output.status().activated() != is_activated_ ||
       output.status().mode() != original_composition_mode_)) {
    if (output.status().activated()) {
      UpdateCompositionModeIcon(engine, output.status().mode());
    } else {
      UpdateCompositionModeIcon(engine, kImeOffCompositionMode);
    }
    is_activated_ = output.status().activated();
    original_composition_mode_ = output.status().mode();
  }
}

void PropertyHandler::UpdateCompositionModeIcon(
    IbusEngineWrapper *engine, commands::CompositionMode new_composition_mode) {
  if (!prop_composition_mode_.IsInitialized()) {
    return;
  }

  const MozcEngineProperty *entry = nullptr;
  for (const MozcEngineProperty &property : kMozcEngineProperties) {
    if (property.composition_mode == new_composition_mode) {
      entry = &property;
      break;
    }
  }
  DCHECK(entry);

  for (uint prop_index = 0;; ++prop_index) {
    IbusPropertyWrapper prop = prop_composition_mode_.GetSubProp(prop_index);
    if (!prop.IsInitialized()) {
      break;
    }
    if (entry->key == prop.GetKey()) {
      // Update the language panel.
      prop_composition_mode_.SetIcon(GetIconPath(entry->icon));
      // Update the radio menu item.
      prop.SetState(PROP_STATE_CHECKED);
    } else {
      prop.SetState(PROP_STATE_UNCHECKED);
    }
    engine->UpdateProperty(&prop);
    // No need to call unref since GetSubProp (ibus_prop_list_get) does not add
    // ref.
  }

  const char *mode_symbol = entry->label_for_panel;
  // Update the text icon for Gnome shell.
  prop_composition_mode_.SetSymbol(mode_symbol);

  const std::string mode_label =
      translator_->MaybeTranslate("Input Mode") + " (" + mode_symbol + ")";
  prop_composition_mode_.SetLabel(mode_label);

  engine->UpdateProperty(&prop_composition_mode_);
}

void PropertyHandler::SetCompositionMode(
    commands::CompositionMode composition_mode) {
  commands::SessionCommand command;
  commands::Output output;

  // In the case of Mozc, there are two state values of IME, IMEOn/IMEOff and
  // composition_mode. However in IBus we can only control composition mode, not
  // IMEOn/IMEOff. So we use one composition state as IMEOff and the others as
  // IMEOn. This setting can be configured with setting
  // kMozcEnginePropertyIMEOffState. If kMozcEnginePropertyIMEOffState is
  // nullptr, it means current IME should not be off.
  if (is_activated_ && composition_mode == kImeOffCompositionMode) {
    command.set_type(commands::SessionCommand::TURN_OFF_IME);
    command.set_composition_mode(original_composition_mode_);
    client_->SendCommand(command, &output);
  } else {
    command.set_type(commands::SessionCommand::SWITCH_COMPOSITION_MODE);
    command.set_composition_mode(composition_mode);
    client_->SendCommand(command, &output);
  }
  DCHECK(output.has_status());
  original_composition_mode_ = output.status().mode();
  is_activated_ = output.status().activated();
}

void PropertyHandler::ProcessPropertyActivate(IbusEngineWrapper *engine,
                                              const char *property_name,
                                              uint property_state) {
  if (IsDisabled()) {
    return;
  }

  if (prop_mozc_tool_.IsInitialized()) {
    for (uint prop_index = 0;; ++prop_index) {
      IbusPropertyWrapper prop = prop_mozc_tool_.GetSubProp(prop_index);
      if (!prop.IsInitialized()) {
        break;
      }
      if (prop.GetKey() != property_name) {
        continue;
      }

      const MozcEngineToolProperty *entry =
          prop.GetData<MozcEngineToolProperty>(kMozcEnginePropertyKey);
      if (!entry || !entry->mode) {
        continue;
      }
      if (!client_->LaunchTool(entry->mode, "")) {
        LOG(ERROR) << "cannot launch: " << entry->mode;
      }
      return;
    }
  }

  if (prop_apl_shifting_key_.IsInitialized()) {
    for (uint prop_index = 0;; ++prop_index) {
      IbusPropertyWrapper prop = prop_apl_shifting_key_.GetSubProp(prop_index);
      if (!prop.IsInitialized()) break;
      if (prop.GetKey() != property_name) continue;

      // Toggle the field in config and save.
      config::Config config;
      if (!client_->GetConfig(&config)) {
        LOG(ERROR) << "GetConfig failed for APL shifting key toggle";
        return;
      }
      const bool new_value = (property_state == PROP_STATE_CHECKED);
      SetAplShiftKeyValue(config.mutable_apl_shifting_key_set(),
                          property_name, new_value);
      if (!client_->SetConfig(config)) {
        LOG(ERROR) << "SetConfig failed for APL shifting key toggle";
      }
      // Keep the property GObject in sync so RegisterProperties (called on
      // every FocusIn) shows the correct state, and immediately update the
      // panel display.
      prop.SetState(new_value ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
      engine->UpdateProperty(&prop);
      return;
    }
  }

  if (property_state != PROP_STATE_CHECKED) {
    return;
  }

  if (prop_composition_mode_.IsInitialized()) {
    for (uint prop_index = 0;; ++prop_index) {
      IbusPropertyWrapper prop = prop_composition_mode_.GetSubProp(prop_index);
      if (!prop.IsInitialized()) {
        break;
      }
      if (prop.GetKey() != property_name) {
        continue;
      }

      const MozcEngineProperty *entry =
          prop.GetData<MozcEngineProperty>(kMozcEnginePropertyKey);
      if (!entry) {
        continue;
      }
      SetCompositionMode(entry->composition_mode);
      UpdateCompositionModeIcon(engine, entry->composition_mode);
      break;
    }
  }
}

bool PropertyHandler::IsActivated() const { return is_activated_; }

bool PropertyHandler::IsDisabled() const { return is_disabled_; }

commands::CompositionMode PropertyHandler::GetOriginalCompositionMode() const {
  return original_composition_mode_;
}

}  // namespace ibus
}  // namespace mozc
